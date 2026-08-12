package dev.zectrix.note4.companion

import android.app.Activity
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.os.Build
import android.os.Bundle
import android.text.InputType
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.util.regex.Pattern

class MainActivity : Activity() {
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        status = TextView(this).apply {
            text = "Not associated\nBLE: idle\nSync: idle"
            textSize = 18f
            setPadding(24, 24, 24, 24)
            inputType = InputType.TYPE_CLASS_TEXT
        }
        val associate = Button(this).apply {
            text = "Associate Note4"
            setOnClickListener { beginAssociation() }
        }
        val diagnostics = Button(this).apply {
            text = "Refresh diagnostics"
            setOnClickListener { showDiagnostics() }
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 48, 32, 32)
            addView(status)
            addView(associate)
            addView(diagnostics)
        })
        showDiagnostics()
    }

    private fun beginAssociation() {
        val manager = getSystemService(CompanionDeviceManager::class.java)
        val filter = BluetoothLeDeviceFilter.Builder()
            .setNamePattern(Pattern.compile("Zectrix Note4"))
            .build()
        val request = AssociationRequest.Builder()
            .addDeviceFilter(filter)
            .setSingleDevice(true)
            .build()
        status.text = "Association chooser requested"
        manager.associate(request, mainExecutor,
            object : CompanionDeviceManager.Callback() {
                override fun onAssociationPending(intentSender: android.content.IntentSender) {
                    @Suppress("DEPRECATION")
                    startIntentSenderForResult(intentSender, 100, null, 0, 0, 0)
                }
                override fun onAssociationCreated(associationInfo: android.companion.AssociationInfo) {
                    status.text = "Associated\nID: ${associationInfo.id}\nBLE: waiting for presence"
                }
                override fun onFailure(errorMessage: CharSequence?) {
                    status.text = "Association failed: ${errorMessage ?: "unknown"}"
                }
            })
    }

    private fun showDiagnostics() {
        val manager = getSystemService(CompanionDeviceManager::class.java)
        val count = if (Build.VERSION.SDK_INT >= 33) manager.myAssociations.size else 0
        status.text = "Associations: $count\nBLE: transport not connected\nSync: queue available\nProtocol: 1.0"
    }
}
