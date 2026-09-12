package dev.zectrix.note4.companion

import android.Manifest
import android.app.Activity
import android.app.PendingIntent
import android.companion.AssociationRequest
import android.bluetooth.le.ScanFilter
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.content.Intent
import android.content.IntentFilter
import android.nfc.NfcAdapter
import android.nfc.NfcManager
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
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
import androidx.lifecycle.ViewModelProvider

class MainActivity : ComponentActivity() {
    companion object {
        private const val ASSOCIATION_RESULT_OK = Activity.RESULT_OK
    }

    private lateinit var transfers: CompanionTransferModel
    private var approvedDevices by mutableStateOf<List<String>>(emptyList())
    private var associationPending by mutableStateOf(false)
    private var pendingUpdates by mutableIntStateOf(0)
    private var snapshot by mutableStateOf(CompanionConnectionManager.snapshot())
    private var readingProgress by mutableStateOf<ReaderProgress?>(null)
    private var actionNotice by mutableStateOf<String?>(null)
    private var readerNotice by mutableStateOf<String?>(null)
    private val readerHandler = Handler(Looper.getMainLooper())
    private val readerTick = object : Runnable {
        override fun run() {
            readingProgress = CompanionConnectionManager.readingProgress()
            pendingUpdates = CompanionConnectionManager.pendingUpdates()
            readerHandler.postDelayed(this, 1000)
        }
    }
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
        associationPending = false
        if (result.resultCode == ASSOCIATION_RESULT_OK) {
            @Suppress("DEPRECATION")
            val chosen = result.data?.getParcelableExtra<android.os.Parcelable>(CompanionDeviceManager.EXTRA_DEVICE)
            val address = when (chosen) {
                is android.bluetooth.BluetoothDevice -> chosen.address
                is android.bluetooth.le.ScanResult -> chosen.device.address
                else -> null
            }
            address?.let { CompanionConnectionManager.selectApproved(this, it) }
            refreshAssociations()
            observeApprovedDevice()
            connectApprovedDevice()
        } else {
            snapshot = actionSnapshot(GattState.IDLE, "Association cancelled")
        }
    }
    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { results ->
        if (results.isNotEmpty() && results.values.all { it }) {
            if (approvedAddress() == null) beginAssociation() else connectApprovedDevice()
        } else snapshot = actionSnapshot(GattState.FAULT, "Allow Nearby devices, then try again")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.light(android.graphics.Color.TRANSPARENT, android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.light(android.graphics.Color.TRANSPARENT, android.graphics.Color.TRANSPARENT),
        )
        CompanionConnectionManager.initialize(this)
        transfers = ViewModelProvider(this)[CompanionTransferModel::class.java]
        nfcAdapter = (getSystemService(NFC_SERVICE) as? NfcManager)?.defaultAdapter
        nfcPendingIntent = PendingIntent.getActivity(
            this, 0, Intent(this, javaClass).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_MUTABLE,
        )
        refreshAssociations()
        observeApprovedDevice()
        setContent { ZectrixCompanionScreen() }
        if (savedInstanceState == null) handleNfcIntent(intent)
    }

    override fun onStart() {
        super.onStart()
        refreshAssociations()
        CompanionConnectionManager.observe(connectionObserver)
        readerTick.run()
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
            if (intent != null) try {
                adapter.enableForegroundDispatch(this, intent, filters, nfcTechLists)
            } catch (_: IllegalStateException) { }
        }
    }

    override fun onPause() {
        try { nfcAdapter?.disableForegroundDispatch(this) } catch (_: IllegalStateException) { }
        super.onPause()
    }

    override fun onStop() {
        CompanionConnectionManager.removeObserver(connectionObserver)
        readerHandler.removeCallbacks(readerTick)
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleNfcIntent(intent)
    }

    private fun actionSnapshot(state: GattState, detail: String): GattSnapshot {
        actionNotice = detail
        return GattSnapshot(state, detail)
    }

    private fun handleNfcIntent(intent: Intent) {
        val enrollment = try { NfcEnrollmentIntents.read(intent) }
        catch (failure: Exception) {
            snapshot = actionSnapshot(GattState.FAULT, failure.message ?: "Malformed NFC message")
            return
        } ?: return
        try {
            if (enrollment.targetAddress() == null) {
                snapshot = actionSnapshot(GattState.FAULT, "This tag has no supported Note4 peripheral address")
                return
            }
            if (!CompanionConnectionManager.setEnrollmentProof(enrollment)) {
                snapshot = actionSnapshot(GattState.FAULT, "Could not prepare enrollment securely; tap again")
                return
            }
            snapshot = actionSnapshot(GattState.IDLE, "Note4 tap received. Approve this device and confirm its passkey.")
            if (approvedAddress() != null) connectApprovedDevice() else beginAssociation()
        } finally { enrollment.token.fill(0) }
    }

    private fun beginAssociation() {
        actionNotice = null
        if (associationPending || requestBluetoothPermissionsIfNeeded()) return
        val manager = getSystemService(CompanionDeviceManager::class.java)
        val target = CompanionConnectionManager.pendingTapAddress()
        val filter = BluetoothLeDeviceFilter.Builder().apply {
            if (target == null) setNamePattern(Pattern.compile("Zectrix Note4"))
            else setScanFilter(ScanFilter.Builder().setDeviceAddress(target).build())
        }.build()
        val request = AssociationRequest.Builder()
            .addDeviceFilter(filter)
            .setSingleDevice(true)
            .build()
        val callback = object : CompanionDeviceManager.Callback() {
            @Suppress("DEPRECATION")
            @Deprecated("Used on Android 12")
            override fun onDeviceFound(intentSender: android.content.IntentSender) {
                if (Build.VERSION.SDK_INT < 33) launchAssociationChooser(intentSender)
            }

            override fun onAssociationPending(intentSender: android.content.IntentSender) {
                launchAssociationChooser(intentSender)
            }

            override fun onAssociationCreated(associationInfo: android.companion.AssociationInfo) {
                associationPending = false
                associationInfo.deviceMacAddress?.toString()?.let { CompanionConnectionManager.selectApproved(this@MainActivity, it) }
                refreshAssociations()
                observeApprovedDevice()
                connectApprovedDevice()
            }

            override fun onFailure(errorMessage: CharSequence?) {
                associationPending = false
                snapshot = actionSnapshot(
                    GattState.FAULT,
                    "Association failed: ${errorMessage ?: "unknown error"}",
                )
            }
        }
        associationPending = true
        try {
            if (Build.VERSION.SDK_INT >= 33) manager.associate(request, mainExecutor, callback)
            else {
                @Suppress("DEPRECATION")
                manager.associate(request, callback, Handler(Looper.getMainLooper()))
            }
        } catch (_: Exception) {
            associationPending = false
            snapshot = actionSnapshot(GattState.FAULT, "Turn on Bluetooth and retry pairing")
        }
    }

    private fun launchAssociationChooser(intentSender: android.content.IntentSender) {
        associationLauncher.launch(IntentSenderRequest.Builder(intentSender).build())
    }

    private fun requestBluetoothPermissionsIfNeeded(): Boolean {
        if (Build.VERSION.SDK_INT < 31) return false
        val required = arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
            .filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (required.isNotEmpty()) permissionLauncher.launch(required.toTypedArray())
        return required.isNotEmpty()
    }

    private fun connectApprovedDevice() {
        actionNotice = null
        if (Build.VERSION.SDK_INT >= 31 &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            requestBluetoothPermissionsIfNeeded()
            return
        }
        val address = approvedAddress()
        if (address == null) {
            snapshot = actionSnapshot(GattState.IDLE, "Pair a Note4 to continue")
            return
        }
        CompanionConnectionManager.connectApproved(this, address)
    }

    @Suppress("DEPRECATION")
    private fun observeApprovedDevice() {
        if (Build.VERSION.SDK_INT < 31) return
        val address = approvedAddress() ?: return
        try {
            getSystemService(CompanionDeviceManager::class.java)
                .startObservingDevicePresence(address)
        } catch (_: Exception) {
            // Presence is an optimization. Explicit connect remains available.
        }
    }

    private fun refreshAssociations() {
        approvedDevices = ApprovedDevices.addresses(this)
        associationCount = approvedDevices.size
        approvedAddress()?.let { CompanionConnectionManager.selectApproved(this, it) }
    }

    private fun approvedAddress(): String? = selectApprovedPeer(ApprovedDevices.addresses(this),
        CompanionConnectionManager.pendingTapAddress(), CompanionConnectionManager.preferredAddress())

    @OptIn(ExperimentalMaterial3ExpressiveApi::class)
    @androidx.compose.runtime.Composable
    private fun ZectrixCompanionScreen() {
        MaterialExpressiveTheme(
            typography = Typography(),
        ) {
            Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.surface) {
                Column(
                    modifier = Modifier.fillMaxSize()
                        .windowInsetsPadding(WindowInsets.safeDrawing)
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
                    Spacer(Modifier.height(24.dp))
                    ConnectionHero(snapshot)
                    Spacer(Modifier.height(32.dp))
                    PrimaryAction()
                    actionNotice?.let {
                        Spacer(Modifier.height(12.dp))
                        Text(it, style = MaterialTheme.typography.bodyMedium)
                    }
                    if (approvedDevices.size > 1) approvedDevices.forEach { address ->
                        androidx.compose.material3.TextButton(onClick = {
                            CompanionConnectionManager.stop()
                            CompanionConnectionManager.selectApproved(this@MainActivity, address)
                            readingProgress = CompanionConnectionManager.readingProgress()
                            snapshot = actionSnapshot(GattState.IDLE, "Selected $address")
                        }) { Text(address) }
                    }
                    readingProgress?.let { progress ->
                        Spacer(Modifier.height(24.dp))
                        ReadingProgressCard(progress)
                    }
                    Spacer(Modifier.height(24.dp))
                    CompanionExtras(transfers, CompanionConnectionManager.hasOfflineQueue(), pendingUpdates)
                    Spacer(Modifier.height(24.dp))
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
            GattState.SYNCHRONIZING,
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
                GattState.SYNCHRONIZING -> "Restoring saved updates…"
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
            GattState.SYNCHRONIZING,
        )
        if (busy) {
            FilledTonalButton(onClick = { CompanionConnectionManager.stop() }, modifier = Modifier.fillMaxWidth().height(56.dp)) {
                Text("Cancel connection")
            }
        } else if (associationCount == 0 || CompanionConnectionManager.pendingTapAddress() != null && approvedAddress() == null) {
            Button(onClick = ::beginAssociation, enabled = !associationPending, modifier = Modifier.fillMaxWidth().height(56.dp)) {
                Text(if (associationPending) "Approve Note4 pairing…" else "Pair a Note4")
            }
        } else if (snapshot.state == GattState.READY) {
            FilledTonalButton(
                onClick = {
                    CompanionConnectionManager.stop()
                    connectApprovedDevice()
                },
                modifier = Modifier.fillMaxWidth().height(56.dp),
            ) { Text("Reconnect & sync time") }
        } else {
            Button(
                onClick = ::connectApprovedDevice,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth().height(56.dp),
            ) { Text(if (busy) "Connecting…" else "Connect") }
        }
    }

    @androidx.compose.runtime.Composable
    private fun ReadingProgressCard(progress: ReaderProgress) {
        androidx.compose.material3.ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(20.dp)) {
                Text("Reading progress", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                Text(progress.bookId, style = MaterialTheme.typography.bodyLarge)
                Text("${progress.perMille / 10}.${progress.perMille % 10}% · ${if (progress.largeFont) 24 else 16} px")
                Spacer(Modifier.height(12.dp))
                FilledTonalButton(
                    enabled = CompanionConnectionManager.hasOfflineQueue(),
                    onClick = {
                        readerNotice = if (CompanionConnectionManager.sendReadingProgress(progress)) {
                            "Position queued. On Note4, choose Use phone position."
                        } else {
                            "Could not save the position. Please try again."
                        }
                    },
                ) { Text("Resume this position") }
                readerNotice?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
            }
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
