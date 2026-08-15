package dev.zectrix.note4.companion

data class CompanionEnrollmentRecord(
    val version: Int,
    val flags: Int,
    val bleRole: Int,
    val bleAddressType: Int,
    val bleAddress: ByteArray,
    val deviceId: ByteArray,
    val generation: Long,
    val token: ByteArray,
)

object NfcEnrollmentParser {
    const val MIME_TYPE = "application/vnd.zectrix.enroll.v1"
    const val PAYLOAD_SIZE = 50
    private val MAGIC = "ZEN1".toByteArray(Charsets.US_ASCII)

    fun parsePayload(payload: ByteArray): CompanionEnrollmentRecord? {
        if (payload.size != PAYLOAD_SIZE) return null
        if (!payload.copyOfRange(0, 4).contentEquals(MAGIC)) return null
        val version = u8(payload[4])
        if (version != 1) return null
        val flags = u8(payload[5])
        val role = u8(payload[6])
        if (role != 1 && role != 2) return null
        val addressType = u8(payload[7])
        if (flags and 1 != 0 && addressType == 0xff) return null
        val generation = get32(payload, 30)
        if (generation == 0L) return null
        val token = payload.copyOfRange(34, 50)
        if (token.all { it.toInt() == 0 }) return null
        return CompanionEnrollmentRecord(
            version = version,
            flags = flags,
            bleRole = role,
            bleAddressType = addressType,
            bleAddress = payload.copyOfRange(8, 14),
            deviceId = payload.copyOfRange(14, 30),
            generation = generation,
            token = token,
        )
    }

    private fun u8(value: Byte): Int = value.toInt() and 0xff
    private fun get32(input: ByteArray, offset: Int): Long =
        (0..3).fold(0L) { result, index ->
            result or (u8(input[offset + index]).toLong() shl (index * 8))
        }
}
