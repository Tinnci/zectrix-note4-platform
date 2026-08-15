package dev.zectrix.note4.companion

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.content.Context
import android.util.Log
import java.util.concurrent.CopyOnWriteArraySet

/** Process-owned BLE transport. Activities observe it but do not own its lifetime. */
object CompanionConnectionManager {
    private val listeners = CopyOnWriteArraySet<(GattSnapshot) -> Unit>()
    private var client: BleGattClient? = null
    @Volatile private var current = GattSnapshot(GattState.IDLE, "Not connected")
    @Volatile private var nextAutomaticAttemptAt = 0L
    private var automaticFailures = 0

    fun initialize(context: Context) {
        if (client != null) return
        synchronized(this) {
            if (client == null) {
                client = BleGattClient(context.applicationContext) { snapshot ->
                    current = snapshot
                    if (snapshot.state == GattState.READY) {
                        automaticFailures = 0
                        nextAutomaticAttemptAt = 0
                    } else if (snapshot.state == GattState.FAULT ||
                        snapshot.state == GattState.DISCONNECTED) {
                        automaticFailures = (automaticFailures + 1).coerceAtMost(5)
                        nextAutomaticAttemptAt = System.currentTimeMillis() +
                            (15_000L shl automaticFailures)
                    }
                    listeners.forEach { it(snapshot) }
                }
            }
        }
    }

    fun snapshot(): GattSnapshot = current

    fun observe(listener: (GattSnapshot) -> Unit) {
        listeners += listener
        listener(current)
    }

    fun removeObserver(listener: (GattSnapshot) -> Unit) {
        listeners -= listener
    }

    @SuppressLint("MissingPermission")
    fun connectApproved(context: Context, address: String, automatic: Boolean = false): Boolean {
        initialize(context)
        if (automatic && System.currentTimeMillis() < nextAutomaticAttemptAt) {
            Log.i("ZectrixCompanion", "event=automatic_connect_deferred reason=backoff")
            return false
        }
        if (!automatic) {
            automaticFailures = 0
            nextAutomaticAttemptAt = 0
        }
        val device = context.getSystemService(BluetoothManager::class.java)
            .adapter.getRemoteDevice(address)
        return client?.connect(device) == true
    }

    fun stop() {
        client?.close()
    }
}
