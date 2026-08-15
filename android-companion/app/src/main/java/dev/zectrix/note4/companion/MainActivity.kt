package dev.zectrix.note4.companion

import android.Manifest
import android.app.Activity
import android.app.PendingIntent
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.content.Intent
import android.content.IntentFilter
import android.nfc.NdefMessage
import android.nfc.NdefRecord
import android.nfc.NfcAdapter
import android.nfc.NfcManager
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import java.util.regex.Pattern

class MainActivity : ComponentActivity() {
    companion object {
        private const val ASSOCIATION_RESULT_OK = Activity.RESULT_OK
    }

    private var snapshot by mutableStateOf(CompanionConnectionManager.snapshot())
    private var nfcAdapter: NfcAdapter? = null
    private var nfcPendingIntent: PendingIntent? = null
    private var nfcTechLists: Array<Array<String>> = arrayOf(arrayOf("android.nfc.tech.Ndef"))
    private var associationCount by mutableIntStateOf(0)
    private val connectionObserver: (GattSnapshot) -> Unit = { latest ->
        runOnUiThread { snapshot = latest }
    }
    private val associationLauncher = registerForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult(),
    ) { result ->
        if (result.resultCode == ASSOCIATION_RESULT_OK) {
            refreshAssociations()
            observeApprovedDevice()
            connectApprovedDevice()
        } else {
            snapshot = GattSnapshot(GattState.IDLE, "Association cancelled")
        }
    }
    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { results ->
        if (results.isNotEmpty() && results.values.all { it }) connectApprovedDevice()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CompanionConnectionManager.initialize(this)
        nfcAdapter = (getSystemService(NFC_SERVICE) as NfcManager).defaultAdapter
        nfcPendingIntent = PendingIntent.getActivity(
            this, 0, Intent(this, javaClass).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_MUTABLE,
        )
        refreshAssociations()
        requestBluetoothPermissionsIfNeeded()
        observeApprovedDevice()
        setContent { ZectrixCompanionScreen() }
    }

    override fun onStart() {
        super.onStart()
        CompanionConnectionManager.observe(connectionObserver)
    }

    override fun onResume() {
        super.onResume()
        val adapter = nfcAdapter
        if (adapter != null) {
            val intent = nfcPendingIntent
            val filters = arrayOf(
                IntentFilter(NfcAdapter.ACTION_NDEF_DISCOVERED).apply {
                    addDataType(NfcEnrollmentParser.MIME_TYPE)
                },
            )
            if (intent != null) {
                adapter.enableForegroundDispatch(this, intent, filters, nfcTechLists)
            }
        }
    }

    override fun onPause() {
        nfcAdapter?.disableForegroundDispatch(this)
        super.onPause()
    }

    override fun onStop() {
        CompanionConnectionManager.removeObserver(connectionObserver)
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleNfcIntent(intent)
    }

    private fun handleNfcIntent(intent: Intent) {
        val messages = intent.getParcelableArrayExtra(NfcAdapter.EXTRA_NDEF_MESSAGES)
            ?: return
        val record = messages
            .filterIsInstance<NdefMessage>()
            .flatMap { it.records.asList() }
            .firstOrNull {
                it.tnf == NdefRecord.TNF_MIME_MEDIA &&
                    it.type.contentEquals(NfcEnrollmentParser.MIME_TYPE.toByteArray(Charsets.US_ASCII))
            } ?: return
        val enrollment = NfcEnrollmentParser.parsePayload(record.payload) ?: run {
            snapshot = GattSnapshot(GattState.FAULT, "Malformed Note4 enrollment record")
            return
        }
        val accepted = CompanionConnectionManager.setEnrollmentProof(
            enrollment.generation, enrollment.token,
        )
        if (accepted) {
            snapshot = GattSnapshot(
                GattState.IDLE,
                "NFC enrollment read. Connect to the Note4 to continue.",
            )
            connectApprovedDevice()
        } else {
            snapshot = GattSnapshot(GattState.FAULT, "Bluetooth transport is not ready")
        }
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
        val callback = object : CompanionDeviceManager.Callback() {
            @Suppress("DEPRECATION")
            @Deprecated("Used on Android 12")
            override fun onDeviceFound(intentSender: android.content.IntentSender) {
                launchAssociationChooser(intentSender)
            }

            override fun onAssociationPending(intentSender: android.content.IntentSender) {
                launchAssociationChooser(intentSender)
            }

            override fun onAssociationCreated(associationInfo: android.companion.AssociationInfo) {
                refreshAssociations()
                observeApprovedDevice()
                connectApprovedDevice()
            }

            override fun onFailure(errorMessage: CharSequence?) {
                snapshot = GattSnapshot(
                    GattState.FAULT,
                    "Association failed: ${errorMessage ?: "unknown error"}",
                )
            }
        }
        if (Build.VERSION.SDK_INT >= 33) {
            manager.associate(request, mainExecutor, callback)
        } else {
            @Suppress("DEPRECATION")
            manager.associate(request, callback, Handler(Looper.getMainLooper()))
        }
    }

    private fun launchAssociationChooser(intentSender: android.content.IntentSender) {
        associationLauncher.launch(IntentSenderRequest.Builder(intentSender).build())
    }

    private fun requestBluetoothPermissionsIfNeeded() {
        if (Build.VERSION.SDK_INT < 31) return
        val required = arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
            .filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (required.isNotEmpty()) permissionLauncher.launch(required.toTypedArray())
    }

    private fun connectApprovedDevice() {
        if (Build.VERSION.SDK_INT >= 31 &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            requestBluetoothPermissionsIfNeeded()
            return
        }
        val address = approvedAddress()
        if (address == null) {
            snapshot = GattSnapshot(GattState.IDLE, "Pair a Note4 to continue")
            return
        }
        CompanionConnectionManager.connectApproved(this, address)
    }

    @Suppress("DEPRECATION")
    private fun observeApprovedDevice() {
        val address = approvedAddress() ?: return
        try {
            getSystemService(CompanionDeviceManager::class.java)
                .startObservingDevicePresence(address)
        } catch (_: Exception) {
            // Presence is an optimization. Explicit connect remains available.
        }
    }

    private fun refreshAssociations() {
        val manager = getSystemService(CompanionDeviceManager::class.java)
        associationCount = if (Build.VERSION.SDK_INT >= 33) {
            manager.myAssociations.size
        } else {
            @Suppress("DEPRECATION")
            manager.associations.size
        }
    }

    private fun approvedAddress(): String? {
        val manager = getSystemService(CompanionDeviceManager::class.java)
        return if (Build.VERSION.SDK_INT >= 33) {
            manager.myAssociations.firstOrNull()?.deviceMacAddress?.toString()
        } else {
            @Suppress("DEPRECATION")
            manager.associations.firstOrNull()
        }
    }

    @OptIn(ExperimentalMaterial3ExpressiveApi::class)
    @androidx.compose.runtime.Composable
    private fun ZectrixCompanionScreen() {
        MaterialExpressiveTheme(
            typography = Typography(),
        ) {
            Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.surface) {
                Column(
                    modifier = Modifier.fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(horizontal = 24.dp, vertical = 28.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Text(
                        "Zectrix Note4",
                        modifier = Modifier.fillMaxWidth(),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Spacer(Modifier.height(44.dp))
                    ConnectionHero(snapshot)
                    Spacer(Modifier.height(32.dp))
                    PrimaryAction()
                    Spacer(Modifier.height(36.dp))
                    Diagnostics(snapshot, associationCount)
                }
            }
        }
    }

    @OptIn(ExperimentalMaterial3ExpressiveApi::class)
    @androidx.compose.runtime.Composable
    private fun ConnectionHero(value: GattSnapshot) {
        val active = value.state in setOf(
            GattState.CONNECTING, GattState.DISCOVERING, GattState.SUBSCRIBING,
            GattState.PAIRING, GattState.VERIFYING_LINK, GattState.NEGOTIATING_PROTOCOL,
        )
        Surface(
            modifier = Modifier.size(104.dp),
            shape = CircleShape,
            color = when (value.state) {
                GattState.READY -> MaterialTheme.colorScheme.primaryContainer
                GattState.FAULT -> MaterialTheme.colorScheme.errorContainer
                else -> MaterialTheme.colorScheme.secondaryContainer
            },
        ) {
            Row(horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                if (active) LoadingIndicator(modifier = Modifier.size(54.dp))
                else Text(
                    when (value.state) {
                        GattState.READY -> "Ready"
                        GattState.FAULT -> "Error"
                        else -> "Note4"
                    },
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                )
            }
        }
        Spacer(Modifier.height(24.dp))
        Text(
            when (value.state) {
                GattState.CONNECTING -> "Connecting…"
                GattState.DISCOVERING, GattState.SUBSCRIBING -> "Setting up the connection…"
                GattState.PAIRING -> "Secure pairing required"
                GattState.VERIFYING_LINK -> "Verifying secure link…"
                GattState.NEGOTIATING_PROTOCOL -> "Starting the Note4 session…"
                GattState.READY -> "Connected securely"
                GattState.FAULT -> "Connection needs attention"
                GattState.DISCONNECTED -> "Note4 is disconnected"
                else -> if (associationCount == 0) "Pair your Note4" else "Ready to connect"
            },
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(8.dp))
        Text(
            value.detail,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyLarge,
            textAlign = TextAlign.Center,
        )
    }

    @androidx.compose.runtime.Composable
    private fun PrimaryAction() {
        val busy = snapshot.state in setOf(
            GattState.CONNECTING, GattState.DISCOVERING, GattState.SUBSCRIBING,
            GattState.PAIRING, GattState.VERIFYING_LINK, GattState.NEGOTIATING_PROTOCOL,
        )
        if (associationCount == 0) {
            Button(onClick = ::beginAssociation, modifier = Modifier.fillMaxWidth().height(56.dp)) {
                Text("Pair a Note4")
            }
        } else if (snapshot.state == GattState.READY) {
            FilledTonalButton(
                onClick = {}, enabled = false,
                modifier = Modifier.fillMaxWidth().height(56.dp),
            ) { Text("Connected") }
        } else {
            Button(
                onClick = ::connectApprovedDevice,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth().height(56.dp),
            ) { Text(if (busy) "Connecting…" else "Connect") }
        }
    }

    @androidx.compose.runtime.Composable
    private fun Diagnostics(value: GattSnapshot, associations: Int) {
        androidx.compose.material3.ElevatedCard(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.elevatedCardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
            ),
        ) {
            Column(Modifier.padding(20.dp)) {
                Text("Connection details", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(14.dp))
                DetailRow("Approved device", if (associations == 1) "Yes" else "$associations")
                HorizontalDivider(Modifier.padding(vertical = 10.dp))
                DetailRow("BLE transport", value.state.name.lowercase().replace('_', ' '))
                HorizontalDivider(Modifier.padding(vertical = 10.dp))
                DetailRow("Protocol", if (value.state == GattState.READY) "Negotiated" else "Not ready")
                HorizontalDivider(Modifier.padding(vertical = 10.dp))
                DetailRow("Received frames", value.receivedFrames.toString())
            }
        }
    }

    @androidx.compose.runtime.Composable
    private fun DetailRow(label: String, value: String) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(value, fontWeight = FontWeight.Medium)
        }
    }
}
