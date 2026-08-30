package dev.zectrix.note4.companion

import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.net.ConnectException
import java.net.HttpURLConnection
import java.net.NoRouteToHostException
import java.net.SocketTimeoutException
import java.net.URL
import java.net.UnknownHostException
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.util.LinkedHashMap
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.zip.CRC32
import javax.net.ssl.SSLException

sealed class ResourceFetchResult {
    data class Success(val contentType: String?, val body: ByteArray) : ResourceFetchResult()
    data object Offline : ResourceFetchResult()
    data object Timeout : ResourceFetchResult()
    data object ServerError : ResourceFetchResult()
    data object TooLarge : ResourceFetchResult()
    data object Invalid : ResourceFetchResult()
}

fun interface ResourceHttpClient {
    fun fetch(request: ResourceGatewayProtocol.Request): ResourceFetchResult
}

object BoundedBodyReader {
    fun read(input: InputStream, maximumBytes: Int): ByteArray? {
        require(maximumBytes in 1..ResourceGatewayProtocol.MAXIMUM_BODY_SIZE)
        val output = ByteArrayOutputStream(minOf(maximumBytes, 512))
        val chunk = ByteArray(256)
        while (true) {
            val count = input.read(chunk)
            if (count < 0) break
            if (count == 0) continue
            if (output.size() + count > maximumBytes) return null
            output.write(chunk, 0, count)
        }
        return output.toByteArray()
    }
}

class HttpUrlConnectionResourceClient : ResourceHttpClient {
    companion object {
        private const val PUBLIC_TEST_DOCUMENT_ENDPOINT =
            "https://zectrix.com/robots.txt"
    }

    override fun fetch(request: ResourceGatewayProtocol.Request): ResourceFetchResult {
        var connection: HttpURLConnection? = null
        return try {
            connection = URL(PUBLIC_TEST_DOCUMENT_ENDPOINT).openConnection() as HttpURLConnection
            val timeout = request.timeoutMs.toInt()
            connection.requestMethod = "GET"
            connection.instanceFollowRedirects = false
            connection.connectTimeout = timeout
            connection.readTimeout = timeout
            connection.setRequestProperty("Accept", "text/plain")
            connection.setRequestProperty("User-Agent", "ZectrixNote4Companion/1")
            if (request.cacheMaxAgeSeconds == 0L) {
                connection.useCaches = false
                connection.setRequestProperty("Cache-Control", "no-store")
            } else {
                connection.useCaches = true
                connection.setRequestProperty(
                    "Cache-Control", "max-age=${request.cacheMaxAgeSeconds}",
                )
            }
            if (connection.responseCode != HttpURLConnection.HTTP_OK) {
                return ResourceFetchResult.ServerError
            }
            val normalizedType = connection.contentType
                ?.substringBefore(';')?.trim()?.lowercase()
            if (normalizedType != "text/plain") return ResourceFetchResult.Invalid
            val declaredLength = connection.contentLengthLong
            if (declaredLength > request.maximumResponseBytes) {
                return ResourceFetchResult.TooLarge
            }
            val body = connection.inputStream.use {
                BoundedBodyReader.read(it, request.maximumResponseBytes)
            } ?: return ResourceFetchResult.TooLarge
            if (body.isEmpty() || !isValidUtf8(body)) return ResourceFetchResult.Invalid
            ResourceFetchResult.Success(connection.contentType, body)
        } catch (_: SocketTimeoutException) {
            ResourceFetchResult.Timeout
        } catch (_: UnknownHostException) {
            ResourceFetchResult.Offline
        } catch (_: ConnectException) {
            ResourceFetchResult.Offline
        } catch (_: NoRouteToHostException) {
            ResourceFetchResult.Offline
        } catch (_: SSLException) {
            ResourceFetchResult.Invalid
        } catch (_: java.io.IOException) {
            ResourceFetchResult.Offline
        } finally {
            connection?.disconnect()
        }
    }

    private fun isValidUtf8(body: ByteArray): Boolean = try {
        Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(body))
        true
    } catch (_: Exception) {
        false
    }
}

class PhoneResourceGateway(
    private val client: ResourceHttpClient = HttpUrlConnectionResourceClient(),
    private val executor: Executor = Executors.newSingleThreadExecutor { task ->
        Thread(task, "zectrix-resource-gateway").apply { isDaemon = true }
    },
) {
    companion object {
        private const val DEDUPE_CAPACITY = 16
        private const val OFFLINE_RETRY_AFTER_MS = 30_000L
        private const val TIMEOUT_RETRY_AFTER_MS = 10_000L
    }

    private data class DedupeEntry(
        val fingerprint: Long,
        var inFlight: Boolean,
        var cached: ResourceGatewayProtocol.Response? = null,
    )

    private val dedupe = LinkedHashMap<Long, DedupeEntry>(DEDUPE_CAPACITY, 0.75f, true)

    fun handle(
        frame: CompanionProtocol.Frame,
        authorized: Boolean,
        callback: (ByteArray) -> Unit,
    ) {
        if (frame.header.messageClass != CompanionProtocol.MessageClass.COMMAND ||
            frame.header.messageType != ResourceGatewayProtocol.MESSAGE_TYPE ||
            frame.header.flags and CompanionProtocol.FLAG_RESPONSE != 0 ||
            frame.header.requestId == 0L) return

        val request = ResourceGatewayProtocol.decodeRequest(frame.payload)
        if (request == null) {
            callback(responseFrame(frame, ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.INVALID_RESPONSE,
            )))
            return
        }
        if (!authorized) {
            callback(responseFrame(frame, ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.NOT_AUTHORIZED,
            )))
            return
        }
        if (request.capability == null) {
            callback(responseFrame(frame, ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.UNSUPPORTED_CAPABILITY,
            )))
            return
        }

        val fingerprint = CRC32().apply { update(frame.payload) }.value
        var cached: ResourceGatewayProtocol.Response? = null
        var collision = false
        synchronized(dedupe) {
            val existing = dedupe[frame.header.requestId]
            if (existing != null) {
                if (existing.fingerprint != fingerprint) {
                    collision = true
                } else if (existing.cached != null) {
                    cached = existing.cached
                } else if (existing.inFlight) {
                    return
                } else {
                    existing.inFlight = true
                }
            } else {
                evictCompletedEntryIfNeeded()
                dedupe[frame.header.requestId] = DedupeEntry(fingerprint, inFlight = true)
            }
        }
        if (collision) {
            callback(responseFrame(frame, ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.INVALID_RESPONSE,
            )))
            return
        }
        cached?.let {
            callback(responseFrame(frame, it))
            return
        }

        executor.execute {
            val response = mapFetchResult(client.fetch(request))
            synchronized(dedupe) {
                val entry = dedupe[frame.header.requestId]
                if (entry != null && entry.fingerprint == fingerprint) {
                    entry.inFlight = false
                    entry.cached = if (ResourceGatewayProtocol.isTransient(response.status)) {
                        null
                    } else {
                        response.copy(body = response.body.copyOf())
                    }
                }
            }
            callback(responseFrame(frame, response))
        }
    }

    private fun mapFetchResult(result: ResourceFetchResult): ResourceGatewayProtocol.Response =
        when (result) {
            is ResourceFetchResult.Success -> ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.SUCCESS,
                ResourceGatewayProtocol.ContentType.TEXT_PLAIN_UTF8,
                result.body,
            )
            ResourceFetchResult.Offline -> ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.PHONE_OFFLINE,
                retryAfterMs = OFFLINE_RETRY_AFTER_MS,
            )
            ResourceFetchResult.Timeout -> ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.TIMEOUT,
                retryAfterMs = TIMEOUT_RETRY_AFTER_MS,
            )
            ResourceFetchResult.ServerError -> ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.SERVER_ERROR,
            )
            ResourceFetchResult.TooLarge -> ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.RESPONSE_TOO_LARGE,
            )
            ResourceFetchResult.Invalid -> ResourceGatewayProtocol.Response(
                ResourceGatewayProtocol.Status.INVALID_RESPONSE,
            )
        }

    private fun responseFrame(
        request: CompanionProtocol.Frame,
        response: ResourceGatewayProtocol.Response,
    ): ByteArray = CompanionProtocol.encode(
        request.header.copy(flags = CompanionProtocol.FLAG_RESPONSE),
        ResourceGatewayProtocol.encodeResponse(response),
    )

    private fun evictCompletedEntryIfNeeded() {
        if (dedupe.size < DEDUPE_CAPACITY) return
        val candidate = dedupe.entries.firstOrNull { !it.value.inFlight }
        if (candidate != null) dedupe.remove(candidate.key)
    }
}
