package dev.zectrix.note4.companion

import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InterruptedIOException
import java.net.HttpURLConnection
import java.net.URI
import java.net.URLEncoder
import java.util.concurrent.atomic.AtomicBoolean

class TransferEndpoint private constructor(val origin: String) {
    companion object {
        fun parse(text: String, allowLoopback: Boolean = false): TransferEndpoint {
            val uri = URI(text.trim())
            require(uri.scheme == "http" && uri.rawUserInfo == null && uri.rawQuery == null &&
                uri.rawFragment == null && uri.rawPath in listOf("", "/")) { "Enter the HTTP address shown on Note4" }
            val host = requireNotNull(uri.host) { "Enter a local IPv4 address" }
            val parts = host.split('.')
            require(parts.size == 4 && parts.all { it.matches(Regex("0|[1-9][0-9]{0,2}")) && it.toInt() <= 255 }) {
                "Use the numeric address shown on Note4"
            }
            val ip = parts.map(String::toInt)
            require(ip[0] == 10 || (ip[0] == 172 && ip[1] in 16..31) ||
                (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254) ||
                (allowLoopback && ip[0] == 127)) { "Note4 transfer requires a local network address" }
            require(uri.port == -1 || uri.port in 1..65535) { "Invalid port" }
            return TransferEndpoint("http://$host${if (uri.port == -1) "" else ":${uri.port}"}")
        }
    }
}

data class TransferBook(val name: String, val size: Long)
data class TransferLibrary(val books: List<TransferBook>, val more: Boolean, val available: Long, val coverSupported: Boolean)
class TransferFailure(val status: Int, message: String) : IOException(message)

/** User-initiated local transfers. Never follows redirects or retries a mutation. */
class Note4TransferClient(private val endpoint: TransferEndpoint, code: String,
                          private val open: (java.net.URL) -> HttpURLConnection = { it.openConnection() as HttpURLConnection }) : AutoCloseable {
    companion object {
        const val BOOK_LIMIT = 4L * 1024 * 1024
        fun validBookName(name: String): Boolean {
            val bytes = name.toByteArray(Charsets.UTF_8)
            return bytes.size in 1..63 && bytes.toString(Charsets.UTF_8) == name &&
                name.none { it.code < 32 || it.code == 127 || it == '/' || it == '\\' } &&
                name.lastIndexOf('.') > 0 && (name.endsWith(".txt", true) || name.endsWith(".epub", true))
        }
        private fun encodeName(name: String) = URLEncoder.encode(name, "UTF-8").replace("+", "%20")
    }
    private val authorization = "Bearer ${code.trim().uppercase()}"
    private val cancelled = AtomicBoolean(false)
    @Volatile private var active: HttpURLConnection? = null

    init { require(authorization.matches(Regex("Bearer [A-Z2-9]{12}"))) { "Enter the 12-character access code" } }

    fun library(after: String? = null): TransferLibrary {
        require(after == null || validBookName(after))
        val body = JSONObject(request("GET", "/api/books" + (after?.let { "?after=${encodeName(it)}" } ?: "")).toString(Charsets.UTF_8))
        val files = body.getJSONArray("files")
        require(files.length() <= 32) { "Note4 returned too many books" }
        val books = (0 until files.length()).map { index ->
            val item = files.getJSONObject(index)
            TransferBook(item.getString("name"), item.getLong("size")).also {
                require(validBookName(it.name) && it.size in 0..0xffff_ffffL)
            }
        }
        val more = body.getBoolean("more")
        require(books.map { it.name }.distinct().size == books.size && (!more || books.isNotEmpty()))
        require(after == null || books.none { it.name == after })
        val available = body.getLong("available")
        require(available in 0..0xffff_ffffL)
        return TransferLibrary(books, more, available, body.optBoolean("cover_supported", false))
    }

    fun uploadBook(file: File, name: String, progress: (Long, Long) -> Unit = { _, _ -> }) {
        require(validBookName(name)) { "Choose a TXT or EPUB with a filename of at most 63 UTF-8 bytes" }
        require(file.isFile && file.length() in 0..BOOK_LIMIT) { "Books must fit within 4 MiB" }
        accepted(request("PUT", "/api/books/${encodeName(name)}", file, progress))
    }

    fun uploadCover(file: File, progress: (Long, Long) -> Unit = { _, _ -> }) {
        require(file.length() == MonochromeCover.FILE_SIZE.toLong()) { "Invalid prepared picture" }
        file.inputStream().use {
            val header = ByteArray(MonochromeCover.HEADER.size)
            java.io.DataInputStream(it).readFully(header)
            require(header.contentEquals(MonochromeCover.HEADER))
        }
        accepted(request("PUT", "/api/cover", file, progress))
    }

    fun removeCover() { accepted(request("DELETE", "/api/cover")) }
    fun finish() { accepted(request("POST", "/api/finish")) }

    private fun accepted(bytes: ByteArray) {
        require(JSONObject(bytes.toString(Charsets.UTF_8)).getBoolean("ok")) { "Note4 did not confirm the operation" }
    }

    private fun checkActive(started: Long) {
        if (cancelled.get() || Thread.currentThread().isInterrupted) throw InterruptedIOException("Transfer cancelled; check the library before retrying")
        if ((System.nanoTime() - started) / 1_000_000 >= 120_000) throw InterruptedIOException("Transfer timed out; check the library before retrying")
    }

    private fun request(method: String, path: String, file: File? = null,
                        progress: (Long, Long) -> Unit = { _, _ -> }): ByteArray {
        val started = System.nanoTime()
        checkActive(started)
        val connection = open(URI(endpoint.origin + path).toURL())
        active = connection
        try {
            checkActive(started)
            connection.instanceFollowRedirects = false
            connection.useCaches = false
            connection.connectTimeout = 8000
            connection.readTimeout = 15_000
            connection.requestMethod = method
            connection.setRequestProperty("Authorization", authorization)
            connection.setRequestProperty("Connection", "close")
            if (file != null || method == "POST") {
                val length = file?.length() ?: 0
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(length)
                connection.setRequestProperty("Content-Type", "application/octet-stream")
                connection.outputStream.use { output ->
                    if (file != null) file.inputStream().use { input ->
                        val buffer = ByteArray(8192)
                        var remaining = length
                        while (remaining > 0) {
                            checkActive(started)
                            val count = input.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                            if (count <= 0) throw IOException("The selected file changed; choose it again")
                            remaining -= count
                            if (remaining == 0L && input.read() != -1) throw IOException("The selected file changed; choose it again")
                            output.write(buffer, 0, count)
                            progress(length - remaining, length)
                        }
                    }
                }
            }
            checkActive(started)
            val status = connection.responseCode
            val response = ByteArrayOutputStream()
            val stream = if (status in 200..299) connection.inputStream else connection.errorStream
            stream?.use { input ->
                val buffer = ByteArray(1024)
                while (true) {
                    checkActive(started)
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (response.size() + count > 16_384) throw IOException("Note4 returned an oversized response")
                    response.write(buffer, 0, count)
                }
            }
            if (status !in 200..299) {
                val detail = runCatching { JSONObject(response.toString("UTF-8")).optString("error") }.getOrNull()
                throw TransferFailure(status, detail?.takeIf { it.isNotBlank() }?.take(240)
                    ?: "Note4 returned HTTP $status; verify its address and access code")
            }
            return response.toByteArray()
        } finally {
            connection.disconnect()
            active = null
        }
    }

    override fun close() {
        cancelled.set(true)
        active?.disconnect()
    }
}
