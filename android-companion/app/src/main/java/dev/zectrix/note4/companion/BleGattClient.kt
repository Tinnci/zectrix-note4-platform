package dev.zectrix.note4.companion

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.content.BroadcastReceiver
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.util.ArrayDeque
import java.util.UUID
import java.util.TimeZone
import java.util.concurrent.atomic.AtomicInteger

enum class GattState {
    IDLE,
    CONNECTING,
    DISCOVERING,
    SUBSCRIBING,
    PAIRING,
    VERIFYING_LINK,
    NEGOTIATING_PROTOCOL,
    SYNCHRONIZING,
    READY,
    DISCONNECTED,
    FAULT,
}

data class GattSnapshot(
    val state: GattState,
    val detail: String,
    val receivedFrames: Int = 0,
    val sessionId: Int = 0,
    val retryable: Boolean = false,
)

@SuppressLint("MissingPermission")
@Suppress("DEPRECATION")
class BleGattClient(
    context: Context,
    private val listener: (GattSnapshot) -> Unit,
) : BluetoothGattCallback() {
    companion object {
        private val SERVICE_UUID = UUID.fromString("10c15e3f-15a9-0895-bb47-e24a678f299d")
        private val PHONE_TO_NOTE_UUID = UUID.fromString("11c15e3f-15a9-0895-bb47-e24a678f299d")
        private val NOTE_TO_PHONE_UUID = UUID.fromString("12c15e3f-15a9-0895-bb47-e24a678f299d")
        private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val TAG = "ZectrixCompanion"
        private const val REQUESTED_MTU = 185
        private const val MAX_QUEUED_PACKETS = 32
        private const val HELLO_SEQUENCE = 1L
        private val sessionSequence = AtomicInteger(0)
    }

    private val applicationContext = context.applicationContext
    private val identityStore = CompanionIdentityStore(applicationContext)
    private val companionIdentity = identityStore.getOrCreate()
    private val resourceGateway = PhoneResourceGateway()
    private var gatt: BluetoothGatt? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var latestSnapshot: GattSnapshot? = null
    private var state = GattState.IDLE
    private var mtu = 23
    private var nextFrameId = 1
    private var receivedFrames = 0
    private var sessionId = 0
    private var writeInFlight = false
    private var inFlightPacket: ByteArray? = null
    private var phaseDeadlineAt = 0L
    private var writeDeadlineAt = 0L
    private var helloRequestId = 0L
    private var peerAuthorized = false
    private val enrollment = EnrollmentHandoff()
    private var selectedAddress: String? = null
    private var notificationsEnabled = false
    private var helloStarted = false
    private var durableQueue: DurableQueue? = null
    private var syncSession: CompanionSyncSession? = null
    private val handler = Handler(Looper.getMainLooper())
    private val syncTick = Runnable { synchronized(this) { pollSync() } }
    private val phaseTimeout = Runnable {
        synchronized(this) {
            if (phaseDeadlineAt != 0L && SystemClock.elapsedRealtime() >= phaseDeadlineAt)
                failSession("Connection timed out during ${state.name.lowercase()}; retry when Note4 is nearby")
        }
    }
    private val writeTimeout = Runnable {
        synchronized(this) { if (writeInFlight && SystemClock.elapsedRealtime() >= writeDeadlineAt) failSession("Bluetooth write timed out; saved updates will retry") }
    }
    private val pendingWrites = ArrayDeque<ByteArray>()
    private val reassembler = CompanionFragments.Reassembler()
    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            handleBondChanged(intent)
        }
    }

    @Synchronized
    private fun handleBondChanged(intent: Intent?) {
        if (intent?.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
        val device = if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        } ?: return
        val link = gatt ?: return
        if (device.address != link.device.address) return
        when (device.bondState) {
            BluetoothDevice.BOND_BONDING ->
                report(GattState.PAIRING, "Confirm the passkey shown on Note4")
            BluetoothDevice.BOND_BONDED -> startVerification(link)
            BluetoothDevice.BOND_NONE -> if (state !in setOf(GattState.IDLE, GattState.DISCONNECTED, GattState.FAULT)) {
                failSession("Pairing failed. Open pairing on Note4 and try again", retryable = false)
            }
        }
    }

    init {
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) {
            applicationContext.registerReceiver(bondReceiver, filter, Context.RECEIVER_EXPORTED)
        } else {
            applicationContext.registerReceiver(bondReceiver, filter)
        }
    }

    /** Connect only when no connection attempt or live transport is active. */
    @Synchronized
    fun connect(device: BluetoothDevice): Boolean {
        if (state == GattState.CONNECTING || state == GattState.DISCOVERING ||
            state == GattState.SUBSCRIBING || state == GattState.VERIFYING_LINK ||
            state == GattState.PAIRING || state == GattState.NEGOTIATING_PROTOCOL ||
            state == GattState.SYNCHRONIZING || state == GattState.READY) {
            event("connect_ignored", "reason=already_active")
            return false
        }
        if (!prepareDevice(device.address)) return false
        if (enrollment.address != null && enrollment.address != selectedAddress) {
            failSession("Tap belongs to a different Note4", retryable = false)
            return false
        }
        closeInternal(report = false)
        syncSession = CompanionSyncSession(requireNotNull(durableQueue))
        sessionId = sessionSequence.updateAndGet { if (it == Int.MAX_VALUE) 1 else it + 1 }
        report(GattState.CONNECTING, "Connecting to approved Note4")
        gatt = try {
            device.connectGatt(applicationContext, false, this, BluetoothDevice.TRANSPORT_LE)
        } catch (_: Exception) { null }
        if (gatt == null) {
            failSession("Android did not start the GATT connection")
            return false
        }
        return true
    }

    /** Restore offline state before Bluetooth is available. Switching peers closes the old session. */
    @Synchronized
    fun prepareDevice(address: String): Boolean {
        val normalized = normalizeAddress(address) ?: return false
        if (selectedAddress == normalized && durableQueue != null) return true
        closeInternal(report = false)
        durableQueue = null
        syncSession = null
        selectedAddress = normalized
        val queue = DurableQueue(File(applicationContext.noBackupFilesDir,
            "sync_${normalized.replace(":", "")}.bin"))
        if (!queue.load()) {
            report(GattState.FAULT, "Saved sync state could not be read; recovery is required")
            return false
        }
        durableQueue = queue
        report(GattState.IDLE, "Saved updates ready; connect to Note4")
        return true
    }

    @Synchronized
    fun close() {
        enrollment.clear()
        closeInternal(report = true)
    }

    @Synchronized
    fun setEnrollmentProof(record: CompanionEnrollmentRecord): Boolean {
        if (!identityStore.isDurable || !enrollment.offer(record, SystemClock.elapsedRealtime())) return false
        closeInternal(report = true)
        return true
    }

    /** Queue one complete protocol frame. Android GATT writes are serialized. */
    @Synchronized
    fun send(frame: ByteArray): Boolean {
        if (state != GattState.READY || !peerAuthorized) return false
        return queueFrame(frame)
    }

    private fun queueFrame(frame: ByteArray): Boolean {
        val link = gatt ?: return false
        if (tx == null || (state != GattState.READY && state != GattState.SYNCHRONIZING)) return false
        val fragments = CompanionFragments.encode(frame, nextFrameId, mtu - 3)
        if (pendingWrites.size + fragments.size +
            (if (writeInFlight) 1 else 0) > MAX_QUEUED_PACKETS) {
            event("frame_rejected", "reason=write_queue_full")
            return false
        }
        nextFrameId = if (nextFrameId == 0xffff) 1 else nextFrameId + 1
        fragments.forEach(pendingWrites::addLast)
        pumpWrite(link)
        return true
    }

    @Synchronized
    override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
        if (gatt !== this.gatt) {
            event("stale_callback", "kind=connection_state")
            return
        }
        if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
            report(GattState.DISCOVERING, "Connected; checking Note4 service")
            if (!gatt.discoverServices()) failSession("Service discovery did not start")
            return
        }
        if (newState == BluetoothProfile.STATE_DISCONNECTED) {
            val previousState = state
            closeInternal(report = false)
            report(
                GattState.DISCONNECTED,
                if (previousState == GattState.PAIRING) {
                    "Pairing failed. Reopen pairing on Note4; if needed, forget its trusted phone first"
                } else {
                    "Connection ended (code $status)"
                }, retryable = previousState != GattState.PAIRING,
            )
            return
        }
        if (status != BluetoothGatt.GATT_SUCCESS) {
            failSession("Connection failed (code $status)")
        }
    }

    @Synchronized
    override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
        if (gatt !== this.gatt) return
        if (status == BluetoothGatt.GATT_SUCCESS && mtu >= 23) {
            this.mtu = mtu
            event("mtu_changed", "value=$mtu")
        } else {
            event("mtu_unchanged", "status=$status value=${this.mtu}")
        }
        if (state == GattState.VERIFYING_LINK) sendHello(gatt)
    }

    @Synchronized
    override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
        if (gatt !== this.gatt) return
        if (status != BluetoothGatt.GATT_SUCCESS) {
            failSession("Service discovery failed (code $status)")
            return
        }
        val service = gatt.getService(SERVICE_UUID)
        tx = service?.getCharacteristic(PHONE_TO_NOTE_UUID)
        val rx = service?.getCharacteristic(NOTE_TO_PHONE_UUID)
        val cccd = rx?.getDescriptor(CCCD_UUID)
        if (tx == null || rx == null || cccd == null) {
            failSession("This device does not provide the Note4 service", retryable = false)
            return
        }
        report(GattState.SUBSCRIBING, "Securing link and enabling updates")
        if (!gatt.setCharacteristicNotification(rx, true) || !writeDescriptor(gatt, cccd)) {
            failSession("Notification setup did not start")
        }
    }

    @Synchronized
    override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
        if (gatt !== this.gatt || descriptor.uuid != CCCD_UUID) return
        if (status != BluetoothGatt.GATT_SUCCESS) {
            failSession("Secure notification setup failed (code $status)")
            return
        }
        notificationsEnabled = true
        when (gatt.device.bondState) {
            BluetoothDevice.BOND_BONDED -> startVerification(gatt)
            BluetoothDevice.BOND_BONDING ->
                report(GattState.PAIRING, "Confirm the passkey shown on Note4")
            else -> {
                report(GattState.PAIRING, "Open pairing on Note4, then confirm its passkey")
                if (!gatt.device.createBond()) {
                    failSession("Android did not start secure pairing", retryable = false)
                }
            }
        }
    }

    private fun startVerification(gatt: BluetoothGatt) {
        if (gatt !== this.gatt || !notificationsEnabled || gatt.device.bondState != BluetoothDevice.BOND_BONDED) return
        if (state != GattState.SUBSCRIBING && state != GattState.PAIRING) return
        report(GattState.VERIFYING_LINK, "Verifying the encrypted Note4 link")
        if (!gatt.requestMtu(REQUESTED_MTU)) {
            event("mtu_request_not_started", "value=$REQUESTED_MTU")
            sendHello(gatt)
        }
    }

    @Synchronized
    override fun onCharacteristicWrite(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        status: Int,
    ) {
        if (gatt !== this.gatt || characteristic.uuid != PHONE_TO_NOTE_UUID) return
        handler.removeCallbacks(writeTimeout)
        inFlightPacket?.fill(0)
        inFlightPacket = null
        writeDeadlineAt = 0
        writeInFlight = false
        if (status != BluetoothGatt.GATT_SUCCESS) {
            pendingWrites.clear()
            failSession("Protocol write failed (code $status)")
            return
        }
        if (!writeInFlight && pendingWrites.isEmpty()) pollSync()
        pumpWrite(gatt)
        if (!writeInFlight && pendingWrites.isEmpty() &&
            state == GattState.VERIFYING_LINK) {
            report(GattState.NEGOTIATING_PROTOCOL, "Secure link verified; waiting for Note4")
        }
    }

    @Deprecated("Used on Android 12")
    override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        receive(gatt, characteristic, characteristic.value ?: return)
    }

    override fun onCharacteristicChanged(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
    ) {
        receive(gatt, characteristic, value)
    }

    @Synchronized
    private fun receive(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        if (gatt !== this.gatt || characteristic.uuid != NOTE_TO_PHONE_UUID) return
        val frame = reassembler.accept(value) ?: return
        when (val result = CompanionProtocol.decode(frame)) {
            is CompanionProtocol.DecodeResult.Success -> {
                receivedFrames++
                val helloAck = (state == GattState.VERIFYING_LINK || state == GattState.NEGOTIATING_PROTOCOL) && CompanionProtocol.matchesHelloAck(
                    result.frame, helloRequestId, HELLO_SEQUENCE,
                )
                if (helloAck) {
                    acceptHelloAck(result.frame)
                } else if (peerAuthorized && syncSession?.receive(result.frame) == true) {
                    pollSync()
                } else if (state == GattState.READY && result.frame.header.messageClass ==
                    CompanionProtocol.MessageClass.COMMAND &&
                    result.frame.header.messageType == ResourceGatewayProtocol.MESSAGE_TYPE &&
                    result.frame.header.flags and CompanionProtocol.FLAG_RESPONSE == 0) {
                    val requestSession = sessionId
                    resourceGateway.handle(result.frame, peerAuthorized) { response ->
                        synchronized(this) {
                            if (sessionId != requestSession || !peerAuthorized || !queueFrame(response)) {
                                event("resource_response_deferred", "reason=session_unavailable_or_busy")
                            }
                        }
                    }
                } else {
                    event("protocol_frame_ignored", "reason=unexpected_session_frame")
                }
                event("protocol_frame_received", "type=${result.frame.header.messageType}")
            }
            is CompanionProtocol.DecodeResult.Error ->
                event("protocol_frame_rejected", "reason=${result.reason}")
        }
    }

    private fun pumpWrite(gatt: BluetoothGatt) {
        if (writeInFlight || pendingWrites.isEmpty()) return
        val characteristic = tx ?: return
        val packet = pendingWrites.removeFirst()
        writeInFlight = true
        inFlightPacket = packet
        writeDeadlineAt = SystemClock.elapsedRealtime() + 10_000
        handler.postDelayed(writeTimeout, 10_000)
        if (!writeCharacteristic(gatt, characteristic, packet)) {
            writeInFlight = false
            pendingWrites.clear()
            failSession("Protocol write did not start")
        }
    }

    private fun sendHello(gatt: BluetoothGatt) {
        if (state != GattState.VERIFYING_LINK || helloStarted || pendingWrites.isNotEmpty() || writeInFlight) return
        if (!identityStore.isDurable) {
            failSession("Companion identity could not be saved securely", retryable = false)
            return
        }
        helloRequestId = sessionId.toLong()
        val identityPayload = try {
            enrollment.take(requireNotNull(selectedAddress), companionIdentity, SystemClock.elapsedRealtime())
                ?: CompanionProtocol.encodeTlv(
                    CompanionProtocol.HELLO_COMPANION_IDENTITY_TYPE, required = true,
                    value = CompanionProtocol.encodeCompanionIdentity(companionIdentity),
                )
        } catch (failure: IllegalArgumentException) {
            failSession(failure.message ?: "Tap Note4 again", retryable = false)
            return
        }
        helloStarted = true
        val now = System.currentTimeMillis()
        val payload = identityPayload + CompanionProtocol.encodeTlv(SyncWire.HELLO_CURSORS, true,
            SyncWire.encodeCursors(requireNotNull(durableQueue).cursors())) +
            CompanionProtocol.optionalClockSample(now, TimeZone.getDefault().getOffset(now) / 1000)
        val hello = CompanionProtocol.encode(
            CompanionProtocol.Header(
                messageClass = CompanionProtocol.MessageClass.CONTROL,
                flags = 0,
                messageType = CompanionProtocol.CONTROL_HELLO,
                requestId = helloRequestId,
                sequence = HELLO_SEQUENCE,
            ),
            payload,
        )
        val fragments = CompanionFragments.encode(hello, nextFrameId++, mtu - 3)
        identityPayload.fill(0)
        payload.fill(0)
        hello.fill(0)
        fragments.forEach(pendingWrites::addLast)
        event("hello_started", "fragments=${fragments.size}")
        pumpWrite(gatt)
    }

    private fun closeInternal(report: Boolean) {
        val previous = gatt
        gatt = null
        resetTransport()
        try { previous?.disconnect() } catch (_: Exception) { }
        try { previous?.close() } catch (_: Exception) { }
        if (report) report(GattState.IDLE, "Not connected") else state = GattState.IDLE
    }

    private fun resetTransport() {
        handler.removeCallbacks(syncTick)
        handler.removeCallbacks(phaseTimeout)
        handler.removeCallbacks(writeTimeout)
        phaseDeadlineAt = 0
        writeDeadlineAt = 0
        inFlightPacket?.fill(0)
        inFlightPacket = null
        syncSession?.disconnect()
        notificationsEnabled = false
        helloStarted = false
        tx = null
        mtu = 23
        nextFrameId = 1
        writeInFlight = false
        pendingWrites.forEach { it.fill(0) }
        pendingWrites.clear()
        reassembler.reset()
        helloRequestId = 0
        peerAuthorized = false
    }

    private fun acceptHelloAck(frame: CompanionProtocol.Frame) {
        try {
            val (ack, cursors) = SyncWire.decodeHelloAck(frame.payload)
            if (ack.status != CompanionProtocol.HELLO_ACK_STATUS_OK || !ack.peerAuthorized) {
                identityStore.setEnrolled(false)
                failSession("Enrollment rejected (code ${ack.errorReason})", retryable = false)
                return
            }
            requireNotNull(cursors) { "missing_sync_cursors" }
            val reconciled = requireNotNull(syncSession).start(cursors, SystemClock.elapsedRealtime())
            if (reconciled != DurableResult.OK) {
                failSession("Sync recovery required ($reconciled)", retryable = false)
                return
            }
            if (!identityStore.setEnrolled(true)) {
                failSession("Enrollment could not be saved", retryable = false)
                return
            }
            peerAuthorized = true
            handler.removeCallbacks(phaseTimeout)
            report(GattState.SYNCHRONIZING, "Restoring saved updates")
            pollSync()
        } catch (_: IllegalArgumentException) {
            failSession("Note4 returned an invalid handshake", retryable = false)
        }
    }

    private fun pollSync() {
        handler.removeCallbacks(syncTick)
        if (!peerAuthorized || (state != GattState.READY && state != GattState.SYNCHRONIZING)) return
        val sync = syncSession ?: return
        val now = SystemClock.elapsedRealtime()
        sync.poll(now, ::queueFrame)
        if (sync.status != SyncSessionStatus.ACTIVE) {
            failSession("Sync interrupted (${sync.status}); saved updates will retry on reconnect",
                retryable = sync.status == SyncSessionStatus.TIMEOUT)
            return
        }
        if (sync.converged()) {
            if (state != GattState.READY) report(GattState.READY, "Connected and synchronized")
        } else if (state != GattState.SYNCHRONIZING) {
            report(GattState.SYNCHRONIZING, "Synchronizing saved updates")
        }
        sync.nextWakeMs(now)?.let { handler.postDelayed(syncTick, it) }
    }

    @Synchronized
    fun putDurableState(entry: DurableEntry): Boolean {
        if (durableQueue?.put(entry) != true) return false
        pollSync()
        return true
    }

    @Synchronized
    fun isCurrentSnapshot(snapshot: GattSnapshot): Boolean = latestSnapshot === snapshot

    @Synchronized
    fun sendWeather(weather: WeatherSnapshot): Boolean {
        val payload = runCatching { WeatherSync.encode(weather) }.getOrNull() ?: return false
        if (durableQueue?.enqueue(WeatherSync.KEY, payload) != true) return false
        pollSync()
        return true
    }

    @Synchronized
    fun hasOfflineQueue(): Boolean = durableQueue != null

    @Synchronized
    fun pendingUpdates(): Int = durableQueue?.snapshot()?.size ?: 0

    @Synchronized
    fun readDurableState(key: Int): DurableEntry? = durableQueue?.incoming(key)

    @Synchronized
    fun sendReadingProgress(progress: ReaderProgress): Boolean {
        val queue = durableQueue ?: return false
        if (!ReaderProgressCodec.enqueue(queue, progress)) return false
        pollSync()
        return true
    }

    private fun failSession(detail: String, retryable: Boolean = true) {
        closeInternal(report = false)
        report(GattState.FAULT, detail, retryable)
    }

    private fun writeDescriptor(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor): Boolean =
        if (Build.VERSION.SDK_INT >= 33) {
            gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) ==
                BluetoothGatt.GATT_SUCCESS
        } else {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        }

    private fun writeCharacteristic(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
    ): Boolean = if (Build.VERSION.SDK_INT >= 33) {
        gatt.writeCharacteristic(
            characteristic, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
        ) == BluetoothGatt.GATT_SUCCESS
    } else {
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        characteristic.value = value
        gatt.writeCharacteristic(characteristic)
    }

    private fun report(newState: GattState, detail: String, retryable: Boolean = false) {
        val previous = state
        if (previous != newState) {
            handler.removeCallbacks(phaseTimeout)
            val timeout = when (newState) {
                GattState.CONNECTING, GattState.SUBSCRIBING -> 30_000L
                GattState.PAIRING -> 90_000L
                GattState.DISCOVERING, GattState.VERIFYING_LINK, GattState.NEGOTIATING_PROTOCOL -> 15_000L
                else -> 0L
            }
            phaseDeadlineAt = if (timeout == 0L) 0 else SystemClock.elapsedRealtime() + timeout
            if (timeout > 0) handler.postDelayed(phaseTimeout, timeout)
        }
        state = newState
        event("gatt_state", "from=$previous to=$newState detail=${detail.replace(' ', '_')}")
        val snapshot = GattSnapshot(newState, detail, receivedFrames, sessionId, retryable)
        latestSnapshot = snapshot
        listener(snapshot)
    }

    private fun event(name: String, fields: String) {
        Log.i(TAG, "event=$name session=$sessionId $fields")
    }
}
