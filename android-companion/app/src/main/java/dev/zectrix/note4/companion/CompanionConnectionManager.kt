package dev.zectrix.note4.companion

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import java.util.concurrent.CopyOnWriteArraySet

/** Process-owned transport and offline queue. An Activity only observes this owner. */
object CompanionConnectionManager {
    private val listeners = CopyOnWriteArraySet<(GattSnapshot) -> Unit>()
    private val handler = Handler(Looper.getMainLooper())
    private var client: BleGattClient? = null
    private var application: Context? = null
    @Volatile private var current = GattSnapshot(GattState.IDLE, "Not connected")
    private var selectedAddress: String? = null
    private var tappedAddress: String? = null
    private val present = mutableSetOf<String>()
    private var automaticFailures = 0
    private var nextAutomaticAttemptAt = 0L
    private var reconnect = true
    private val retry = Runnable {
        val context = application
        val address = selectedAddress
        if (context != null && address != null && address in present && reconnect) {
            connectApproved(context, address, automatic = true)
        }
    }

    @Synchronized
    fun initialize(context: Context) {
        if (client != null) return
        application = context.applicationContext
        client = BleGattClient(context.applicationContext) { snapshot ->
            // GATT callbacks never acquire manager state while holding the client's lock.
            handler.post {
                if (client?.isCurrentSnapshot(snapshot) != true) return@post
                publish(snapshot)
                if (snapshot.state == GattState.READY) {
                    tappedAddress = null
                    automaticFailures = 0
                    nextAutomaticAttemptAt = 0
                    handler.removeCallbacks(retry)
                } else if (snapshot.state == GattState.FAULT || snapshot.state == GattState.DISCONNECTED) {
                    if (snapshot.retryable) {
                        automaticFailures++
                        nextAutomaticAttemptAt = SystemClock.elapsedRealtime() + (15_000L shl (automaticFailures - 1).coerceIn(0, 4))
                        scheduleRetry()
                    } else {
                        reconnect = false
                        handler.removeCallbacks(retry)
                    }
                }
            }
        }
        val preferences = context.getSharedPreferences("companion", Context.MODE_PRIVATE)
        selectedAddress = selectApprovedPeer(ApprovedDevices.addresses(context), null, preferences.getString("selected_address", null))
        selectedAddress?.let { client?.prepareDevice(it) }
    }

    private fun publish(snapshot: GattSnapshot) {
        current = snapshot
        listeners.forEach { it(snapshot) }
    }

    fun snapshot(): GattSnapshot = current
    fun preferredAddress(): String? = tappedAddress ?: selectedAddress
    fun pendingTapAddress(): String? = tappedAddress
    fun hasOfflineQueue(): Boolean = selectedAddress != null && (tappedAddress == null || tappedAddress == selectedAddress) && client?.hasOfflineQueue() == true
    fun pendingUpdates(): Int = if (hasOfflineQueue()) client?.pendingUpdates() ?: 0 else 0

    fun observe(listener: (GattSnapshot) -> Unit) {
        listeners += listener
        listener(current)
    }

    fun removeObserver(listener: (GattSnapshot) -> Unit) { listeners -= listener }

    fun selectApproved(context: Context, address: String): Boolean {
        initialize(context)
        val target = normalizeAddress(address)?.takeIf { it in ApprovedDevices.addresses(context) } ?: return false
        if (tappedAddress != null && tappedAddress != target) return false
        val preferences = context.getSharedPreferences("companion", Context.MODE_PRIVATE)
        if (preferences.getString("selected_address", null) != target &&
            !preferences.edit().putString("selected_address", target).commit()) {
            publish(GattSnapshot(GattState.FAULT, "Could not save the selected Note4"))
            return false
        }
        if (!client!!.prepareDevice(target)) return false
        selectedAddress = target
        return true
    }

    @SuppressLint("MissingPermission")
    fun connectApproved(context: Context, address: String, automatic: Boolean = false): Boolean {
        initialize(context)
        val target = normalizeAddress(address) ?: return false
        if (automatic && target != selectedAddress) return false
        if (automatic && SystemClock.elapsedRealtime() < nextAutomaticAttemptAt) {
            scheduleRetry()
            return false
        }
        if (!selectApproved(context, target)) return false
        reconnect = true
        if (!automatic) {
            handler.removeCallbacks(retry)
            automaticFailures = 0
            nextAutomaticAttemptAt = 0
        }
        return try {
            val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter
            if (adapter == null || !adapter.isEnabled) {
                publish(GattSnapshot(GattState.FAULT, "Turn on Bluetooth, then connect again"))
                false
            } else client?.connect(adapter.getRemoteDevice(target)) == true
        } catch (_: SecurityException) {
            publish(GattSnapshot(GattState.FAULT, "Allow Nearby devices to connect to Note4"))
            false
        } catch (_: IllegalArgumentException) {
            publish(GattSnapshot(GattState.FAULT, "The approved device address is invalid"))
            false
        }
    }

    fun setEnrollmentProof(record: CompanionEnrollmentRecord): Boolean {
        if (client?.setEnrollmentProof(record) != true) return false
        tappedAddress = record.targetAddress()
        handler.removeCallbacks(retry)
        automaticFailures = 0
        return true
    }

    fun deviceAppeared(context: Context, address: String) {
        initialize(context)
        val target = normalizeAddress(address) ?: return
        if (target !in ApprovedDevices.addresses(context)) return
        present += target
        if (target == selectedAddress && reconnect) connectApproved(context, target, automatic = true)
    }

    fun deviceDisappeared(address: String) {
        val target = normalizeAddress(address) ?: return
        present -= target
        if (target == selectedAddress) {
            handler.removeCallbacks(retry)
            client?.close()
        }
    }

    private fun scheduleRetry() {
        handler.removeCallbacks(retry)
        if (reconnect && selectedAddress in present && automaticFailures <= 5) {
            handler.postDelayed(retry, (nextAutomaticAttemptAt - SystemClock.elapsedRealtime()).coerceAtLeast(1000))
        }
    }

    fun stop() {
        reconnect = false
        tappedAddress = null
        handler.removeCallbacks(retry)
        client?.close()
    }

    fun putDurableState(entry: DurableEntry): Boolean = client?.putDurableState(entry) == true
    fun readDurableState(key: Int): DurableEntry? = client?.readDurableState(key)
    fun readingProgress(): ReaderProgress? = if (!hasOfflineQueue()) null else readDurableState(ReaderProgressCodec.SYNC_KEY)
        ?.let { ReaderProgressCodec.decode(it.payload) }
    fun sendWeather(weather: WeatherSnapshot): Boolean = client?.sendWeather(weather) == true
    fun sendReadingProgress(progress: ReaderProgress): Boolean = client?.sendReadingProgress(progress) == true
}
