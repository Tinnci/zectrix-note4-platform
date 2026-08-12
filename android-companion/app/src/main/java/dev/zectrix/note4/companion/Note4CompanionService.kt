package dev.zectrix.note4.companion

import android.annotation.SuppressLint
import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import android.util.Log

@Suppress("OVERRIDE_DEPRECATION")
class Note4CompanionService : CompanionDeviceService() {
    private val lifecycle = ConnectionStateMachine(ConnectionState.ASSOCIATED)

    @SuppressLint("MissingPermission")
    @Suppress("DEPRECATION")
    override fun onDeviceAppeared(associationInfo: AssociationInfo) {
        if (lifecycle.accept(ConnectionEvent.APPEARED)) {
            Log.i("ZectrixCompanion", "Associated Note4 is present")
        }
    }

    @Suppress("DEPRECATION")
    override fun onDeviceDisappeared(associationInfo: AssociationInfo) {
        lifecycle.accept(ConnectionEvent.LOST)
        Log.i("ZectrixCompanion", "Associated Note4 is absent")
    }
}
