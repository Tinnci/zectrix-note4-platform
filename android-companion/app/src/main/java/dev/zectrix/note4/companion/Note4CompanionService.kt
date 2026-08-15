package dev.zectrix.note4.companion

import android.annotation.SuppressLint
import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import android.util.Log

@Suppress("OVERRIDE_DEPRECATION")
class Note4CompanionService : CompanionDeviceService() {
    @SuppressLint("MissingPermission")
    @Suppress("DEPRECATION")
    override fun onDeviceAppeared(associationInfo: AssociationInfo) {
        Log.i("ZectrixCompanion", "event=presence state=appeared api=modern")
        CompanionConnectionManager.connectApproved(
            this, associationInfo.deviceMacAddress.toString(), automatic = true,
        )
    }

    @Suppress("DEPRECATION")
    override fun onDeviceAppeared(address: String) {
        Log.i("ZectrixCompanion", "event=presence state=appeared api=legacy")
        CompanionConnectionManager.connectApproved(this, address, automatic = true)
    }

    @Suppress("DEPRECATION")
    override fun onDeviceDisappeared(associationInfo: AssociationInfo) {
        CompanionConnectionManager.stop()
        Log.i("ZectrixCompanion", "event=presence state=disappeared api=modern")
    }

    @Suppress("DEPRECATION")
    override fun onDeviceDisappeared(address: String) {
        CompanionConnectionManager.stop()
        Log.i("ZectrixCompanion", "event=presence state=disappeared api=legacy")
    }
}
