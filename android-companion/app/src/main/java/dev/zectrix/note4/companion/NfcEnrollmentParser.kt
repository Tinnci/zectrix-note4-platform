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
        if (flags and FLAG_BLE_ADDRESS_VALID.inv() != 0) return null
        val role = u8(payload[6])
        if (role != BLE_ROLE_PERIPHERAL && role != BLE_ROLE_CENTRAL) return null
        val addressType = u8(payload[7])
        if (addressType != BLE_ADDRESS_PUBLIC &&
            addressType != BLE_ADDRESS_RANDOM_STATIC &&
            addressType != BLE_ADDRESS_UNKNOWN
        ) {
            return null
        }
        val bleAddress = payload.copyOfRange(8, 14)
        if (flags and FLAG_BLE_ADDRESS_VALID != 0 &&
            (addressType == BLE_ADDRESS_UNKNOWN ||
                bleAddress.all { it.toInt() == 0 })
        ) {
            return null
        }
        val generation = get32(payload, 30)
        if (generation == 0L) return null
        val token = payload.copyOfRange(34, 50)
        if (token.all { it.toInt() == 0 }) return null
        return CompanionEnrollmentRecord(
            version = version,
            flags = flags,
            bleRole = role,
            bleAddressType = addressType,
            bleAddress = bleAddress,
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

    private const val FLAG_BLE_ADDRESS_VALID = 1
    private const val BLE_ROLE_PERIPHERAL = 1
    private const val BLE_ROLE_CENTRAL = 2
    private const val BLE_ADDRESS_PUBLIC = 0
    private const val BLE_ADDRESS_RANDOM_STATIC = 1
    private const val BLE_ADDRESS_UNKNOWN = 0xff
}
