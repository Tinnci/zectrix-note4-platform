package dev.zectrix.note4.companion

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import androidx.compose.ui.platform.LocalContext
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

@Composable
fun CompanionExtras(model: CompanionTransferModel, canSync: Boolean, pendingUpdates: Int) {
    val context = LocalContext.current
    var localNetworkAllowed by remember {
        mutableStateOf(Build.VERSION.SDK_INT < 37 || context.checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) == PackageManager.PERMISSION_GRANTED)
    }
    val networkPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { localNetworkAllowed = it }
    val canTransfer = localNetworkAllowed && !model.busy
    val bookPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) model.uploadBook(uri)
    }
    val picturePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) model.preparePicture(uri)
    }
    var confirmRemove by remember { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(20.dp)) {
        if (model.notice.isNotEmpty() || model.busy) ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(model.notice)
                if (model.busy) {
                    LinearProgressIndicator(progress = { model.progress }, modifier = Modifier.fillMaxWidth())
                    TextButton(onClick = model::cancel) { Text("Cancel") }
                }
            }
        }
        ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Books & sleep picture", style = MaterialTheme.typography.titleMedium)
                Text("Open Wi-Fi Transfer on Note4. Join its hotspot or the same Wi-Fi, then enter the address and access code on its screen.",
                    style = MaterialTheme.typography.bodyMedium)
                if (!localNetworkAllowed) {
                    Button(onClick = { networkPermission.launch(Manifest.permission.ACCESS_LOCAL_NETWORK) }) { Text("Allow local network") }
                }
                TextButton(onClick = { context.startActivity(android.content.Intent(android.provider.Settings.ACTION_WIFI_SETTINGS)) }) {
                    Text("Open Wi-Fi settings")
                }
                OutlinedTextField(model.address, { model.address = it }, enabled = !model.busy,
                    singleLine = true, label = { Text("Note4 address") }, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(model.code, { model.code = it }, enabled = !model.busy,
                    singleLine = true, label = { Text("Access code") }, visualTransformation = PasswordVisualTransformation(),
                    modifier = Modifier.fillMaxWidth())
                FilledTonalButton(onClick = { model.refresh() }, enabled = canTransfer, modifier = Modifier.fillMaxWidth()) {
                    Text("Open library")
                }
                model.library?.let { library ->
                    Text("${library.available / 1024} KiB available", style = MaterialTheme.typography.bodySmall)
                    if (library.books.isEmpty()) Text("Your library is empty")
                    library.books.forEach { Text("${it.name} · ${it.size / 1024} KiB", style = MaterialTheme.typography.bodyMedium) }
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        TextButton(onClick = { model.refresh() }, enabled = canTransfer) { Text("First page") }
                        TextButton(onClick = { model.refresh(true) }, enabled = canTransfer && library.more) { Text("Next page") }
                    }
                }
                Button(onClick = { bookPicker.launch(arrayOf("text/plain", "application/epub+zip", "application/octet-stream")) },
                    enabled = canTransfer, modifier = Modifier.fillMaxWidth()) { Text("Choose & send book") }
                Text("TXT / EPUB, up to 4 MiB. Existing filenames are kept.", style = MaterialTheme.typography.bodySmall)
                if (model.library?.coverSupported != false) {
                    FilledTonalButton(onClick = { picturePicker.launch(arrayOf("image/*")) }, enabled = !model.busy,
                        modifier = Modifier.fillMaxWidth()) { Text("Choose sleep picture") }
                    model.picture?.let { picture ->
                        Image(picture.asImageBitmap(), "Monochrome sleep picture preview",
                            modifier = Modifier.fillMaxWidth().aspectRatio(4f / 3f))
                        Button(onClick = model::uploadPicture, enabled = canTransfer, modifier = Modifier.fillMaxWidth()) { Text("Send picture") }
                    }
                    TextButton(onClick = { confirmRemove = true }, enabled = canTransfer) { Text("Remove saved Note4 picture") }
                }
                FilledTonalButton(onClick = model::finish, enabled = canTransfer, modifier = Modifier.fillMaxWidth()) { Text("Finish transfer") }
            }
        }
        ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Weather & time", style = MaterialTheme.typography.titleMedium)
                Text("Phone time and timezone are sent on each secure connection. Reading positions and weather stay queued while Note4 is offline.",
                    style = MaterialTheme.typography.bodyMedium)
                Text("Saved updates waiting: $pendingUpdates", style = MaterialTheme.typography.bodySmall)
                OutlinedTextField(model.city, { model.city = it }, enabled = !model.busy, singleLine = true,
                    label = { Text("City") }, modifier = Modifier.fillMaxWidth())
                FilledTonalButton(onClick = model::searchWeather, enabled = canSync && !model.busy && model.city.isNotBlank()) { Text("Find city") }
                if (!canSync) Text("Pair and select a Note4 to save weather updates.", style = MaterialTheme.typography.bodySmall)
                model.places.forEach { place ->
                    TextButton(onClick = { model.syncWeather(place) }, enabled = !model.busy && canSync) {
                        Text("${place.name} · ${place.region}")
                    }
                }
                model.weather?.let { weather ->
                    Text("${weather.place} · ${weather.deciCelsius / 10.0} °C · ${WeatherSync.describe(weather.code)}")
                    Text("Observation: ${java.time.Instant.ofEpochSecond(weather.observedAt).atZone(java.time.ZoneId.systemDefault()).toLocalDateTime()}",
                        style = MaterialTheme.typography.bodySmall)
                }
                Text("Weather data: Open-Meteo (CC BY 4.0). Updates are requested only when you choose a city.",
                    style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    if (confirmRemove) AlertDialog(
        onDismissRequest = { confirmRemove = false },
        title = { Text("Remove Note4 picture?") },
        text = { Text("This removes the saved sleep picture from Note4. Books and phone photos are kept.") },
        confirmButton = { TextButton(onClick = { confirmRemove = false; model.removePicture() }) { Text("Remove") } },
        dismissButton = { TextButton(onClick = { confirmRemove = false }) { Text("Cancel") } },
    )
}
