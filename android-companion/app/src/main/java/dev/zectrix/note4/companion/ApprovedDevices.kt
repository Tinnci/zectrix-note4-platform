package dev.zectrix.note4.companion

import android.companion.CompanionDeviceManager
import android.content.Context
import android.os.Build

object ApprovedDevices {
    @Suppress("DEPRECATION")
    fun addresses(context: Context): List<String> = try {
        val manager = context.getSystemService(CompanionDeviceManager::class.java)
        val addresses = if (Build.VERSION.SDK_INT >= 33) {
            manager.myAssociations.mapNotNull { it.deviceMacAddress?.toString() }
        } else manager.associations
        addresses.mapNotNull(::normalizeAddress).distinct()
    } catch (_: Exception) {
        emptyList()
    }
}
