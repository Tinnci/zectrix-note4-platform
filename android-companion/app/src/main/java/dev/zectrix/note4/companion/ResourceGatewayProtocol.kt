package dev.zectrix.note4.companion

object ResourceGatewayProtocol {
    const val MESSAGE_TYPE = 0x0100
    const val MAXIMUM_BODY_SIZE = 2048
    const val MINIMUM_TIMEOUT_MS = 1_000L
    const val MAXIMUM_TIMEOUT_MS = 15_000L

    private const val REQUEST_CAPABILITY = 1
    private const val REQUEST_MAXIMUM_BYTES = 2
    private const val REQUEST_TIMEOUT_MS = 3
    private const val REQUEST_CACHE_MAX_AGE_SECONDS = 4
    private const val REQUEST_DURABLE = 5

    private const val RESPONSE_STATUS = 1
    private const val RESPONSE_CONTENT_TYPE = 2
    private const val RESPONSE_BODY = 3
    private const val RESPONSE_RETRY_AFTER_MS = 4

    enum class Capability(val wire: Int) {
        PUBLIC_TEST_DOCUMENT_V1(1);

        companion object {
            fun fromWire(value: Int): Capability? = entries.find { it.wire == value }
        }
    }

    enum class Status(val wire: Int) {
        SUCCESS(0),
        PHONE_UNAVAILABLE(1),
        PHONE_OFFLINE(2),
        TIMEOUT(3),
        SERVER_ERROR(4),
        RESPONSE_TOO_LARGE(5),
        INVALID_RESPONSE(6),
        NOT_AUTHORIZED(7),
        UNSUPPORTED_CAPABILITY(8);

        companion object {
            fun fromWire(value: Int): Status? = entries.find { it.wire == value }
        }
    }

    enum class ContentType(val wire: Int) {
        NONE(0),
        TEXT_PLAIN_UTF8(1);

        companion object {
            fun fromWire(value: Int): ContentType? = entries.find { it.wire == value }
        }
    }

    data class Request(
        val capabilityWire: Int,
        val maximumResponseBytes: Int,
        val timeoutMs: Long,
        val cacheMaxAgeSeconds: Long,
        val durable: Boolean,
    ) {
        val capability: Capability? get() = Capability.fromWire(capabilityWire)
    }

    data class Response(
        val status: Status,
        val contentType: ContentType = ContentType.NONE,
        val body: ByteArray = ByteArray(0),
        val retryAfterMs: Long = 0,
    )

    fun encodeRequest(request: Request): ByteArray {
        require(request.capabilityWire in 1..0xffff)
        require(request.maximumResponseBytes in 1..MAXIMUM_BODY_SIZE)
        require(request.timeoutMs in MINIMUM_TIMEOUT_MS..MAXIMUM_TIMEOUT_MS)
        require(request.cacheMaxAgeSeconds in 0..0xffff_ffffL)
        return join(
            CompanionProtocol.encodeTlv(
                REQUEST_CAPABILITY, required = true, value = le16(request.capabilityWire),
            ),
            CompanionProtocol.encodeTlv(
                REQUEST_MAXIMUM_BYTES, required = true,
                value = le16(request.maximumResponseBytes),
            ),
            CompanionProtocol.encodeTlv(
                REQUEST_TIMEOUT_MS, required = true, value = le32(request.timeoutMs),
            ),
            CompanionProtocol.encodeTlv(
                REQUEST_CACHE_MAX_AGE_SECONDS, required = false,
                value = le32(request.cacheMaxAgeSeconds),
            ),
            CompanionProtocol.encodeTlv(
                REQUEST_DURABLE, required = true,
                value = byteArrayOf(if (request.durable) 1 else 0),
            ),
        )
    }

    fun decodeRequest(payload: ByteArray): Request? = try {
        var capability: Int? = null
        var maximumBytes: Int? = null
        var timeoutMs: Long? = null
        var cacheMaxAge = 0L
        var cacheSeen = false
        var durable: Boolean? = null
        for (field in CompanionProtocol.decodeTlvs(payload)) {
            when (field.type) {
                REQUEST_CAPABILITY -> {
                    if (capability != null || field.value.size != 2) return null
                    capability = get16(field.value)
                }
                REQUEST_MAXIMUM_BYTES -> {
                    if (maximumBytes != null || field.value.size != 2) return null
                    maximumBytes = get16(field.value)
                }
                REQUEST_TIMEOUT_MS -> {
                    if (timeoutMs != null || field.value.size != 4) return null
                    timeoutMs = get32(field.value)
                }
                REQUEST_CACHE_MAX_AGE_SECONDS -> {
                    if (cacheSeen || field.value.size != 4) return null
                    cacheSeen = true
                    cacheMaxAge = get32(field.value)
                }
                REQUEST_DURABLE -> {
                    if (durable != null || field.value.size != 1 ||
                        (field.value[0].toInt() and 0xff) > 1) return null
                    durable = field.value[0].toInt() != 0
                }
                else -> if (field.required) return null
            }
        }
        val decoded = Request(
            capability ?: return null,
            maximumBytes ?: return null,
            timeoutMs ?: return null,
            cacheMaxAge,
            durable ?: return null,
        )
        if (decoded.maximumResponseBytes !in 1..MAXIMUM_BODY_SIZE ||
            decoded.timeoutMs !in MINIMUM_TIMEOUT_MS..MAXIMUM_TIMEOUT_MS) null else decoded
    } catch (_: IllegalArgumentException) {
        null
    }

    fun encodeResponse(response: Response): ByteArray {
        val success = response.status == Status.SUCCESS
        require(response.retryAfterMs in 0..0xffff_ffffL)
        require(response.body.size <= MAXIMUM_BODY_SIZE)
        require(
            if (success) {
                response.contentType != ContentType.NONE && response.body.isNotEmpty()
            } else {
                response.contentType == ContentType.NONE && response.body.isEmpty()
            },
        )
        val fields = mutableListOf(
            CompanionProtocol.encodeTlv(
                RESPONSE_STATUS, required = true, value = byteArrayOf(response.status.wire.toByte()),
            ),
        )
        if (success) {
            fields += CompanionProtocol.encodeTlv(
                RESPONSE_CONTENT_TYPE, required = true,
                value = byteArrayOf(response.contentType.wire.toByte()),
            )
            fields += CompanionProtocol.encodeTlv(
                RESPONSE_BODY, required = true, value = response.body,
            )
        }
        if (response.retryAfterMs != 0L) {
            fields += CompanionProtocol.encodeTlv(
                RESPONSE_RETRY_AFTER_MS, required = false,
                value = le32(response.retryAfterMs),
            )
        }
        return join(*fields.toTypedArray())
    }

    fun decodeResponse(payload: ByteArray): Response? = try {
        var status: Status? = null
        var contentType: ContentType? = null
        var body: ByteArray? = null
        var retryAfter = 0L
        var retrySeen = false
        for (field in CompanionProtocol.decodeTlvs(payload)) {
            when (field.type) {
                RESPONSE_STATUS -> {
                    if (status != null || field.value.size != 1) return null
                    status = Status.fromWire(field.value[0].toInt() and 0xff) ?: return null
                }
                RESPONSE_CONTENT_TYPE -> {
                    if (contentType != null || field.value.size != 1) return null
                    contentType = ContentType.fromWire(field.value[0].toInt() and 0xff) ?: return null
                }
                RESPONSE_BODY -> {
                    if (body != null || field.value.isEmpty() ||
                        field.value.size > MAXIMUM_BODY_SIZE) return null
                    body = field.value
                }
                RESPONSE_RETRY_AFTER_MS -> {
                    if (retrySeen || field.value.size != 4) return null
                    retrySeen = true
                    retryAfter = get32(field.value)
                }
                else -> if (field.required) return null
            }
        }
        val decodedStatus = status ?: return null
        if (decodedStatus == Status.SUCCESS) {
            Response(
                decodedStatus,
                contentType?.takeIf { it != ContentType.NONE } ?: return null,
                body ?: return null,
                retryAfter,
            )
        } else {
            if (contentType != null || body != null) return null
            Response(decodedStatus, retryAfterMs = retryAfter)
        }
    } catch (_: IllegalArgumentException) {
        null
    }

    fun isTransient(status: Status): Boolean =
        status == Status.PHONE_UNAVAILABLE || status == Status.PHONE_OFFLINE ||
            status == Status.TIMEOUT

    private fun le16(value: Int) = byteArrayOf(value.toByte(), (value ushr 8).toByte())

    private fun le32(value: Long) = ByteArray(4) { (value ushr (it * 8)).toByte() }

    private fun get16(value: ByteArray): Int =
        (value[0].toInt() and 0xff) or ((value[1].toInt() and 0xff) shl 8)

    private fun get32(value: ByteArray): Long =
        (0..3).fold(0L) { result, index ->
            result or ((value[index].toInt() and 0xff).toLong() shl (index * 8))
        }

    private fun join(vararg fields: ByteArray): ByteArray {
        val output = ByteArray(fields.sumOf(ByteArray::size))
        var offset = 0
        for (field in fields) {
            field.copyInto(output, offset)
            offset += field.size
        }
        return output
    }
}
