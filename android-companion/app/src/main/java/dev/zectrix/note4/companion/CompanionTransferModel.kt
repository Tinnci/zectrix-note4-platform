package dev.zectrix.note4.companion

import android.app.Application
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RectF
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.exifinterface.media.ExifInterface
import androidx.lifecycle.AndroidViewModel
import java.io.File
import java.io.InputStream
import java.util.concurrent.Executors
import java.util.concurrent.Future

class CompanionTransferModel(application: Application) : AndroidViewModel(application) {
    private val preferences = application.getSharedPreferences("companion", Application.MODE_PRIVATE)
    var address by mutableStateOf(preferences.getString("transfer_address", "http://192.168.4.1")!!)
    var code by mutableStateOf("")
    var city by mutableStateOf(preferences.getString("weather_city", "")!!)
    var busy by mutableStateOf(false)
        private set
    var notice by mutableStateOf("")
        private set
    var progress by mutableStateOf(0f)
        private set
    var library by mutableStateOf<TransferLibrary?>(null)
        private set
    var picture by mutableStateOf<Bitmap?>(null)
        private set
    var places by mutableStateOf<List<WeatherPlace>>(emptyList())
        private set
    var weather by mutableStateOf<WeatherSnapshot?>(null)
        private set
    private var preparedPicture: File? = null
    private var libraryOrigin: String? = null
    private val executor = Executors.newSingleThreadExecutor()
    private val handler = Handler(Looper.getMainLooper())
    private var task: Future<*>? = null
    private var generation = 0
    @Volatile private var active: AutoCloseable? = null
    @Volatile private var source: InputStream? = null

    private fun run(message: String, operation: (Int) -> Unit) {
        if (busy) return
        val ticket = ++generation
        busy = true
        progress = 0f
        notice = message
        task = executor.submit {
            try { operation(ticket) }
            catch (failure: Exception) {
                update(ticket) { notice = failure.message?.take(240) ?: "Operation failed; check Note4 before retrying" }
            } finally {
                runCatching { active?.close() }
                active = null
                update(ticket) { busy = false }
            }
        }
    }

    private fun update(ticket: Int, change: () -> Unit) {
        handler.post { if (ticket == generation) change() }
    }

    private fun client(): Note4TransferClient {
        val endpoint = TransferEndpoint.parse(address)
        preferences.edit().putString("transfer_address", endpoint.origin).apply()
        return Note4TransferClient(endpoint, code) { openLocalTransfer(getApplication(), it) }.also { active = it }
    }

    fun refresh(next: Boolean = false) {
        val after = if (next) library?.books?.lastOrNull()?.name else null
        val origin = address
        if (next && libraryOrigin != origin) return
        run("Reading Note4 library…") { ticket ->
            val listing = client().library(after)
            update(ticket) { library = listing; libraryOrigin = origin; notice = "${listing.books.size} books on this page" }
        }
    }

    fun uploadBook(uri: Uri) = run("Preparing book…") { ticket ->
        val resolver = getApplication<Application>().contentResolver
        val name = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        } ?: throw IllegalArgumentException("The selected document has no filename")
        require(Note4TransferClient.validBookName(name)) { "Choose a TXT or EPUB with a filename of at most 63 UTF-8 bytes" }
        val file = stage(uri, Note4TransferClient.BOOK_LIMIT)
        try {
            val client = client()
            client.uploadBook(file, name, reportProgress(ticket))
            val listing = runCatching { client.library() }.getOrNull()
            update(ticket) {
                if (listing != null) { library = listing; libraryOrigin = address }
                notice = "$name saved. Finish the transfer to open it in Note4 Reader."
            }
        } finally { file.delete() }
    }

    fun preparePicture(uri: Uri) = run("Preparing monochrome picture…") { ticket ->
        val sourceFile = stage(uri, 24L * 1024 * 1024)
        var pbm: File? = null
        try {
            val bitmap = renderPicture(sourceFile)
            val pixels = IntArray(MonochromeCover.WIDTH * MonochromeCover.HEIGHT)
            bitmap.getPixels(pixels, 0, MonochromeCover.WIDTH, 0, 0, MonochromeCover.WIDTH, MonochromeCover.HEIGHT)
            val bytes = MonochromeCover.encode(pixels)
            for (i in pixels.indices) pixels[i] = if (bytes[MonochromeCover.HEADER.size + i / 8].toInt() and (0x80 ushr (i % 8)) != 0) Color.BLACK else Color.WHITE
            bitmap.setPixels(pixels, 0, MonochromeCover.WIDTH, 0, 0, MonochromeCover.WIDTH, MonochromeCover.HEIGHT)
            pbm = File.createTempFile("note4-cover-", ".pbm", getApplication<Application>().cacheDir).also { it.writeBytes(bytes) }
            val ready = pbm
            handler.post {
                if (ticket == generation) {
                    preparedPicture?.delete()
                    preparedPicture = ready
                    picture = bitmap
                    notice = "Picture ready. Note4 retains the image and overlays its wake hint."
                } else { ready?.delete(); bitmap.recycle() }
            }
            pbm = null
        } finally { sourceFile.delete(); pbm?.delete() }
    }

    fun uploadPicture() {
        val file = preparedPicture ?: return
        run("Sending picture…") { ticket ->
            client().uploadCover(file, reportProgress(ticket))
            update(ticket) { notice = "Picture saved. Finish transfer, then choose Sleep Cover → Phone Picture on Note4." }
        }
    }

    fun removePicture() = run("Removing saved Note4 picture…") { ticket ->
        client().removeCover()
        update(ticket) { notice = "Saved Note4 picture removed. You can now send another picture." }
    }

    fun finish() = run("Finishing transfer…") { ticket ->
        client().finish()
        update(ticket) { library = null; code = ""; notice = "Transfer finished. Note4 can use its library again." }
    }

    fun searchWeather() = run("Finding cities…") { ticket ->
        val client = PhoneWeatherClient().also { active = it }
        val found = client.search(city)
        update(ticket) { places = found; notice = if (found.isEmpty()) "No city found" else "Choose your city" }
    }

    fun syncWeather(place: WeatherPlace) {
        val peer = CompanionConnectionManager.preferredAddress() ?: return
        run("Reading weather…") { ticket ->
            val client = PhoneWeatherClient().also { active = it }
            val sample = client.current(place, System.currentTimeMillis() / 1000)
            update(ticket) {
                if (CompanionConnectionManager.preferredAddress() != peer || !CompanionConnectionManager.sendWeather(sample)) {
                    notice = "Weather could not be queued for the selected Note4; try again"
                } else {
                    weather = sample
                    places = emptyList()
                    preferences.edit().putString("weather_city", city).apply()
                    notice = "Weather saved for sync. Note4 shows fresh weather on its daily sleep dashboard."
                }
            }
        }
    }

    private fun reportProgress(ticket: Int): (Long, Long) -> Unit {
        var lastPercent = -1L
        return { sent, total ->
            val percent = if (total == 0L) 100 else sent * 100 / total
            if (percent != lastPercent) {
                lastPercent = percent
                update(ticket) { progress = percent / 100f }
            }
        }
    }

    private fun stage(uri: Uri, limit: Long): File {
        val file = File.createTempFile("note4-send-", ".part", getApplication<Application>().cacheDir)
        try {
            val input = getApplication<Application>().contentResolver.openInputStream(uri)
                ?: throw IllegalArgumentException("Cannot open the selected document")
            source = input
            input.use {
                file.outputStream().use { output ->
                    val buffer = ByteArray(8192)
                    var count = 0L
                    while (true) {
                        check(!Thread.currentThread().isInterrupted) { "Cancelled" }
                        val read = input.read(buffer)
                        if (read < 0) break
                        count += read
                        require(count <= limit) { "Selected file is too large" }
                        output.write(buffer, 0, read)
                    }
                }
            }
            return file
        } catch (failure: Exception) { file.delete(); throw failure }
        finally { source = null }
    }

    private fun renderPicture(file: File): Bitmap {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.path, bounds)
        require(bounds.outWidth in 1..65535 && bounds.outHeight in 1..65535) { "Choose a supported image" }
        var sample = 1
        while ((bounds.outWidth.toLong() / sample + 1) * (bounds.outHeight.toLong() / sample + 1) > 2_000_000) sample *= 2
        val decoded = BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply {
            inSampleSize = sample; inPreferredConfig = Bitmap.Config.ARGB_8888
        }) ?: throw IllegalArgumentException("Could not decode the selected picture")
        val result = Bitmap.createBitmap(MonochromeCover.WIDTH, MonochromeCover.HEIGHT, Bitmap.Config.ARGB_8888)
        try {
            val exif = ExifInterface(file)
            val transform = Matrix().apply {
                if (exif.isFlipped) postScale(-1f, 1f)
                postRotate(exif.rotationDegrees.toFloat())
            }
            val oriented = Bitmap.createBitmap(decoded, 0, 0, decoded.width, decoded.height, transform, true)
            try {
                val scale = minOf(result.width.toFloat() / oriented.width, result.height.toFloat() / oriented.height)
                val width = oriented.width * scale
                val height = oriented.height * scale
                val left = (result.width - width) / 2
                val top = (result.height - height) / 2
                Canvas(result).apply {
                    drawColor(Color.WHITE)
                    drawBitmap(oriented, null, RectF(left, top, left + width, top + height), Paint(Paint.FILTER_BITMAP_FLAG))
                }
            } finally { if (oriented !== decoded) oriented.recycle() }
            return result
        } catch (failure: Exception) { result.recycle(); throw failure }
        finally { decoded.recycle() }
    }

    fun cancel() {
        generation++
        task?.cancel(true)
        runCatching { active?.close() }
        runCatching { source?.close() }
        busy = false
        notice = "Cancelled. If sending had started, check the Note4 library before retrying."
    }

    override fun onCleared() {
        cancel()
        executor.shutdownNow()
        preparedPicture?.delete()
    }
}
