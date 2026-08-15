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
import android.util.Log
import java.util.ArrayDeque
import java.util.UUID
import java.util.concurrent.atomic.AtomicInteger

enum class GattState {
    IDLE,
    CONNECTING,
    DISCOVERING,
    SUBSCRIBING,
    PAIRING,
    VERIFYING_LINK,
    NEGOTIATING_PROTOCOL,
    READY,
    DISCONNECTED,
    FAULT,
}

data class GattSnapshot(
    val state: GattState,
    val detail: String,
    val receivedFrames: Int = 0,
    val sessionId: Int = 0,
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
        private val sessionSequence = AtomicInteger(0)
    }

    private val applicationContext = context.applicationContext
    private var gatt: BluetoothGatt? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var state = GattState.IDLE
    private var mtu = 23
    private var nextFrameId = 1
    private var receivedFrames = 0
    private var sessionId = 0
    private var writeInFlight = false
    private var helloRequestId = 0L
    private val pendingWrites = ArrayDeque<ByteArray>()
    private val reassembler = CompanionFragments.Reassembler()
    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
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
                BluetoothDevice.BOND_NONE -> if (state == GattState.PAIRING) {
                    report(GattState.FAULT, "Pairing failed. Open pairing on Note4 and try again")
                }
            }
        }
    }

    init {
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= 33) {
            applicationContext.registerReceiver(bondReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            applicationContext.registerReceiver(bondReceiver, filter)
        }
    }

    /** Connect only when no connection attempt or live transport is active. */
    fun connect(device: BluetoothDevice): Boolean {
        if (state == GattState.CONNECTING || state == GattState.DISCOVERING ||
            state == GattState.SUBSCRIBING || state == GattState.VERIFYING_LINK ||
            state == GattState.PAIRING || state == GattState.NEGOTIATING_PROTOCOL ||
            state == GattState.READY) {
            event("connect_ignored", "reason=already_active")
            return false
        }
        closeInternal(report = false)
        sessionId = sessionSequence.updateAndGet { if (it == Int.MAX_VALUE) 1 else it + 1 }
        report(GattState.CONNECTING, "Connecting to approved Note4")
        gatt = device.connectGatt(applicationContext, false, this, BluetoothDevice.TRANSPORT_LE)
        if (gatt == null) {
            report(GattState.FAULT, "Android did not start the GATT connection")
            return false
        }
        return true
    }

    fun close() = closeInternal(report = true)

    /** Queue one complete protocol frame. Android GATT writes are serialized. */
    fun send(frame: ByteArray): Boolean {
        val link = gatt ?: return false
        if (state != GattState.READY || tx == null) return false
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

    override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
        if (gatt !== this.gatt) {
            event("stale_callback", "kind=connection_state")
            return
        }
        if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
            report(GattState.DISCOVERING, "Connected; checking Note4 service")
            if (!gatt.discoverServices()) report(GattState.FAULT, "Service discovery did not start")
            return
        }
        if (newState == BluetoothProfile.STATE_DISCONNECTED) {
            val previousState = state
            resetTransport()
            report(
                GattState.DISCONNECTED,
                if (previousState == GattState.PAIRING) {
                    "Pairing failed. Reopen pairing on Note4; if needed, forget its trusted phone first"
                } else {
                    "Connection ended (code $status)"
                },
            )
            return
        }
        if (status != BluetoothGatt.GATT_SUCCESS) {
            resetTransport()
            report(GattState.FAULT, "Connection failed (code $status)")
        }
    }

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

    override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
        if (gatt !== this.gatt) return
        if (status != BluetoothGatt.GATT_SUCCESS) {
            report(GattState.FAULT, "Service discovery failed (code $status)")
            return
        }
        val service = gatt.getService(SERVICE_UUID)
        tx = service?.getCharacteristic(PHONE_TO_NOTE_UUID)
        val rx = service?.getCharacteristic(NOTE_TO_PHONE_UUID)
        val cccd = rx?.getDescriptor(CCCD_UUID)
        if (tx == null || rx == null || cccd == null) {
            report(GattState.FAULT, "This device does not provide the Note4 service")
            return
        }
        report(GattState.SUBSCRIBING, "Securing link and enabling updates")
        if (!gatt.setCharacteristicNotification(rx, true) || !writeDescriptor(gatt, cccd)) {
            report(GattState.FAULT, "Notification setup did not start")
        }
    }

    override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
        if (gatt !== this.gatt || descriptor.uuid != CCCD_UUID) return
        if (status != BluetoothGatt.GATT_SUCCESS) {
            report(GattState.FAULT, "Secure notification setup failed (code $status)")
            return
        }
        when (gatt.device.bondState) {
            BluetoothDevice.BOND_BONDED -> startVerification(gatt)
            BluetoothDevice.BOND_BONDING ->
                report(GattState.PAIRING, "Confirm the passkey shown on Note4")
            else -> {
                report(GattState.PAIRING, "Open pairing on Note4, then confirm its passkey")
                if (!gatt.device.createBond()) {
                    report(GattState.FAULT, "Android did not start secure pairing")
                }
            }
        }
    }

    private fun startVerification(gatt: BluetoothGatt) {
        if (gatt !== this.gatt) return
        report(GattState.VERIFYING_LINK, "Verifying the encrypted Note4 link")
        if (!gatt.requestMtu(REQUESTED_MTU)) {
            event("mtu_request_not_started", "value=$REQUESTED_MTU")
            sendHello(gatt)
        }
    }

    override fun onCharacteristicWrite(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        status: Int,
    ) {
        if (gatt !== this.gatt || characteristic.uuid != PHONE_TO_NOTE_UUID) return
        writeInFlight = false
        if (status != BluetoothGatt.GATT_SUCCESS) {
            pendingWrites.clear()
            report(GattState.FAULT, "Protocol write failed (code $status)")
            return
        }
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

    private fun receive(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        if (gatt !== this.gatt || characteristic.uuid != NOTE_TO_PHONE_UUID) return
        val frame = reassembler.accept(value) ?: return
        when (val result = CompanionProtocol.decode(frame)) {
            is CompanionProtocol.DecodeResult.Success -> {
                receivedFrames++
                val header = result.frame.header
                val helloAck = header.messageClass == CompanionProtocol.MessageClass.CONTROL &&
                    header.messageType == CompanionProtocol.CONTROL_HELLO_ACK &&
                    header.flags and CompanionProtocol.FLAG_RESPONSE != 0 &&
                    header.requestId == helloRequestId && result.frame.payload.isEmpty()
                if (helloAck) {
                    report(GattState.READY, "Secure link and protocol handshake complete")
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
        if (!writeCharacteristic(gatt, characteristic, packet)) {
            writeInFlight = false
            pendingWrites.clear()
            report(GattState.FAULT, "Protocol write did not start")
        }
    }

    private fun sendHello(gatt: BluetoothGatt) {
        if (state != GattState.VERIFYING_LINK || pendingWrites.isNotEmpty() || writeInFlight) return
        helloRequestId = sessionId.toLong()
        val hello = CompanionProtocol.encode(
            CompanionProtocol.Header(
                messageClass = CompanionProtocol.MessageClass.CONTROL,
                flags = 0,
                messageType = CompanionProtocol.CONTROL_HELLO,
                requestId = helloRequestId,
                sequence = 1,
            ),
            byteArrayOf(),
        )
        val fragments = CompanionFragments.encode(hello, nextFrameId++, mtu - 3)
        fragments.forEach(pendingWrites::addLast)
        event("hello_started", "fragments=${fragments.size}")
        pumpWrite(gatt)
    }

    private fun closeInternal(report: Boolean) {
        val previous = gatt
        gatt = null
        resetTransport()
        previous?.disconnect()
        previous?.close()
        if (report) report(GattState.IDLE, "Not connected") else state = GattState.IDLE
    }

    private fun resetTransport() {
        tx = null
        mtu = 23
        nextFrameId = 1
        writeInFlight = false
        pendingWrites.clear()
        reassembler.reset()
        helloRequestId = 0
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

    private fun report(newState: GattState, detail: String) {
        val previous = state
        state = newState
        event("gatt_state", "from=$previous to=$newState detail=${detail.replace(' ', '_')}")
        listener(GattSnapshot(newState, detail, receivedFrames, sessionId))
    }

    private fun event(name: String, fields: String) {
        Log.i(TAG, "event=$name session=$sessionId $fields")
    }
}
