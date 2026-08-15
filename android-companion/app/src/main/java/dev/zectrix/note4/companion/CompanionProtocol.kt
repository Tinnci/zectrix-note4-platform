package dev.zectrix.note4.companion

import java.util.zip.CRC32

object CompanionProtocol {
    const val MAGIC = 0x435a
    const val MAJOR = 1
    const val MINOR = 0
    const val HEADER_SIZE = 24
    const val MAX_PAYLOAD_SIZE = 4096
    const val MAX_TLV_VALUE_SIZE = 2048
    const val REQUIRED_FIELD_BIT = 0x8000
    private const val ALLOWED_FLAGS = 0x07
    const val FLAG_RESPONSE = 1 shl 1
    const val CONTROL_HELLO = 1
    const val CONTROL_HELLO_ACK = 2
    const val HELLO_ENROLLMENT_PROOF_TYPE = 1
    const val HELLO_COMPANION_IDENTITY_TYPE = 2
    const val HELLO_ACK_STATUS_TYPE = 3
    const val HELLO_ENROLLMENT_PROOF_SIZE = 36
    const val HELLO_ACK_STATUS_OK = 0
    const val HELLO_ACK_STATUS_REJECTED = 1
    const val HELLO_ACK_PEER_AUTHORIZED_FLAG = 1

    enum class MessageClass(val wire: Int) {
        CONTROL(0), DURABLE_STATE(1), COMMAND(2), STREAM(3);

        companion object {
            fun fromWire(value: Int): MessageClass? = entries.find { it.wire == value }
        }
    }

    data class Header(
        val major: Int = MAJOR,
        val minor: Int = MINOR,
        val messageClass: MessageClass,
        val flags: Int,
        val messageType: Int,
        val requestId: Long,
        val sequence: Long,
    )

    data class Frame(val header: Header, val payload: ByteArray)
    data class Tlv(val type: Int, val required: Boolean, val value: ByteArray)

    sealed class DecodeResult {
        data class Success(val frame: Frame) : DecodeResult()
        data class Error(val reason: String) : DecodeResult()
    }

    fun encode(header: Header, payload: ByteArray): ByteArray {
        require(header.major in 0..255 && header.minor in 0..255)
        require(header.flags and ALLOWED_FLAGS.inv() == 0)
        require(header.messageType in 0..0xffff)
        require(header.requestId in 0..0xffff_ffffL)
        require(header.sequence in 0..0xffff_ffffL)
        require(payload.size <= MAX_PAYLOAD_SIZE)
        val output = ByteArray(HEADER_SIZE + payload.size)
        put16(output, 0, MAGIC)
        output[2] = header.major.toByte()
        output[3] = header.minor.toByte()
        output[4] = header.messageClass.wire.toByte()
        output[5] = header.flags.toByte()
        put16(output, 6, header.messageType)
        put32(output, 8, header.requestId)
        put32(output, 12, header.sequence)
        put16(output, 16, payload.size)
        put16(output, 18, 0)
        payload.copyInto(output, HEADER_SIZE)
        val crc = CRC32()
        crc.update(output, 0, 20)
        crc.update(payload)
        put32(output, 20, crc.value)
        return output
    }

    fun decode(frame: ByteArray, maximumMinor: Int = MINOR): DecodeResult {
        if (frame.size < HEADER_SIZE) return DecodeResult.Error("truncated")
        if (get16(frame, 0) != MAGIC) return DecodeResult.Error("bad_magic")
        if (u8(frame[2]) != MAJOR || u8(frame[3]) > maximumMinor) {
            return DecodeResult.Error("unsupported_version")
        }
        val messageClass = MessageClass.fromWire(u8(frame[4]))
            ?: return DecodeResult.Error("invalid_class")
        val flags = u8(frame[5])
        if (flags and ALLOWED_FLAGS.inv() != 0) return DecodeResult.Error("invalid_flags")
        if (get16(frame, 18) != 0) return DecodeResult.Error("reserved")
        val payloadSize = get16(frame, 16)
        if (payloadSize > MAX_PAYLOAD_SIZE) return DecodeResult.Error("oversized")
        if (frame.size != HEADER_SIZE + payloadSize) {
            return DecodeResult.Error(if (frame.size < HEADER_SIZE + payloadSize) "truncated" else "oversized")
        }
        val crc = CRC32()
        crc.update(frame, 0, 20)
        crc.update(frame, HEADER_SIZE, payloadSize)
        if (crc.value != get32(frame, 20)) return DecodeResult.Error("crc")
        return DecodeResult.Success(
            Frame(
                Header(
                    u8(frame[2]), u8(frame[3]), messageClass, flags,
                    get16(frame, 6), get32(frame, 8), get32(frame, 12),
                ),
                frame.copyOfRange(HEADER_SIZE, frame.size),
            ),
        )
    }

    fun encodeHelloEnrollmentProof(
        generation: Long,
        token: ByteArray,
        companionId: ByteArray,
    ): ByteArray {
        require(token.size == 16)
        require(companionId.size == 16)
        require(generation in 0..0xffff_ffffL)
        return ByteArray(HELLO_ENROLLMENT_PROOF_SIZE).also { value ->
            put32(value, 0, generation)
            token.copyInto(value, 4)
            companionId.copyInto(value, 20)
        }
    }

    fun decodeHelloEnrollmentProof(value: ByteArray): Triple<Long, ByteArray, ByteArray>? {
        if (value.size != HELLO_ENROLLMENT_PROOF_SIZE) return null
        return Triple(
            get32(value, 0),
            value.copyOfRange(4, 20),
            value.copyOfRange(20, value.size),
        )
    }

    fun encodeCompanionIdentity(companionId: ByteArray): ByteArray {
        require(companionId.size == 16)
        return companionId.copyOf()
    }

    fun decodeCompanionIdentity(value: ByteArray): ByteArray? {
        if (value.size != 16) return null
        return value.copyOf()
    }

    fun encodeHelloAckStatus(status: Int, peerAuthorized: Boolean, errorReason: Int): ByteArray {
        require(status in 0..255 && errorReason in 0..0xffff)
        return ByteArray(4).also { value ->
            value[0] = status.toByte()
            value[1] = (if (peerAuthorized) HELLO_ACK_PEER_AUTHORIZED_FLAG else 0).toByte()
            put16(value, 2, errorReason)
        }
    }

    data class HelloAckStatus(val status: Int, val peerAuthorized: Boolean, val errorReason: Int)

    fun decodeHelloAckStatus(value: ByteArray): HelloAckStatus? {
        if (value.size != 4) return null
        return HelloAckStatus(
            u8(value[0]),
            u8(value[1]) and HELLO_ACK_PEER_AUTHORIZED_FLAG != 0,
            get16(value, 2),
        )
    }

    fun matchesHelloAck(frame: Frame, requestId: Long, sequence: Long): Boolean =
        frame.header.messageClass == MessageClass.CONTROL &&
            frame.header.messageType == CONTROL_HELLO_ACK &&
            frame.header.flags and FLAG_RESPONSE != 0 &&
            frame.header.requestId == requestId &&
            frame.header.sequence == sequence

    fun encodeTlv(type: Int, required: Boolean, value: ByteArray): ByteArray {
        require(type in 0 until REQUIRED_FIELD_BIT)
        require(value.size <= MAX_TLV_VALUE_SIZE)
        val output = ByteArray(4 + value.size)
        put16(output, 0, type or if (required) REQUIRED_FIELD_BIT else 0)
        put16(output, 2, value.size)
        value.copyInto(output, 4)
        return output
    }

    fun decodeTlvs(payload: ByteArray): List<Tlv> {
        val fields = mutableListOf<Tlv>()
        var offset = 0
        while (offset < payload.size) {
            require(payload.size - offset >= 4) { "malformed_tlv" }
            val rawType = get16(payload, offset)
            val size = get16(payload, offset + 2)
            require(size <= MAX_TLV_VALUE_SIZE && size <= payload.size - offset - 4) {
                "malformed_tlv"
            }
            fields += Tlv(
                rawType and REQUIRED_FIELD_BIT.inv(),
                rawType and REQUIRED_FIELD_BIT != 0,
                payload.copyOfRange(offset + 4, offset + 4 + size),
            )
            offset += 4 + size
        }
        return fields
    }

    fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }

    fun unhex(value: String): ByteArray {
        require(value.length % 2 == 0)
        return ByteArray(value.length / 2) { index ->
            value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    private fun u8(value: Byte): Int = value.toInt() and 0xff
    private fun put16(output: ByteArray, offset: Int, value: Int) {
        output[offset] = value.toByte()
        output[offset + 1] = (value ushr 8).toByte()
    }
    private fun put32(output: ByteArray, offset: Int, value: Long) {
        repeat(4) { output[offset + it] = (value ushr (it * 8)).toByte() }
    }
    private fun get16(input: ByteArray, offset: Int): Int =
        u8(input[offset]) or (u8(input[offset + 1]) shl 8)
    private fun get32(input: ByteArray, offset: Int): Long =
        (0..3).fold(0L) { result, index ->
            result or (u8(input[offset + index]).toLong() shl (index * 8))
        }
}
