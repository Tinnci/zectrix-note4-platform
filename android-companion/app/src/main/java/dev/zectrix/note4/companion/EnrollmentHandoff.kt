package dev.zectrix.note4.companion

import java.util.Locale

/** A tap authorizes one Hello to one peripheral, within a monotonic deadline. */
class EnrollmentHandoff {
    companion object { const val LIFETIME_MS = 120_000L }

    var address: String? = null
        private set
    private var record: CompanionEnrollmentRecord? = null
    private var expiresAt = 0L

    fun offer(value: CompanionEnrollmentRecord, now: Long): Boolean {
        val target = value.targetAddress() ?: return false
        if (value.deviceId.size != 16 || value.deviceId.all { it == 0.toByte() } ||
            value.token.size != 16 || value.token.all { it == 0.toByte() } ||
            value.generation !in 1..0xffff_ffffL || now < 0 || now > Long.MAX_VALUE - LIFETIME_MS) return false
        clear()
        record = value.copy(bleAddress = value.bleAddress.copyOf(), deviceId = value.deviceId.copyOf(),
            token = value.token.copyOf())
        address = target
        expiresAt = now + LIFETIME_MS
        return true
    }

    fun take(target: String, identity: ByteArray, now: Long): ByteArray? {
        val pending = record ?: return null
        try {
            require(normalizeAddress(target) == address) { "Tap belongs to a different Note4" }
            require(now >= expiresAt - LIFETIME_MS && now < expiresAt) { "Enrollment expired; tap Note4 again" }
            val proof = CompanionProtocol.encodeHelloEnrollmentProof(pending.generation, pending.token, identity)
            return try { CompanionProtocol.encodeTlv(CompanionProtocol.HELLO_ENROLLMENT_PROOF_TYPE, true, proof) }
                finally { proof.fill(0) }
        } finally {
            // Failed or interrupted sends must never replay a one-use token.
            clear()
        }
    }

    fun clear() {
        record?.token?.fill(0)
        record = null
        address = null
        expiresAt = 0
    }
}

fun normalizeAddress(value: String?): String? = value?.takeIf {
    it.matches(Regex("[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}")) &&
        it != "00:00:00:00:00:00" && !it.equals("FF:FF:FF:FF:FF:FF", true)
}?.uppercase(Locale.ROOT)

fun CompanionEnrollmentRecord.targetAddress(): String? {
    if (version != 1 || flags != 1 || bleRole != 1 || bleAddress.size != 6 ||
        bleAddressType !in 0..1 || (bleAddressType == 1 && bleAddress[0].toInt() and 0xc0 != 0xc0)) return null
    return normalizeAddress(bleAddress.joinToString(":") { "%02X".format(it.toInt() and 0xff) })
}

fun selectApprovedPeer(approved: List<String>, tapped: String?, preferred: String?): String? {
    val addresses = approved.mapNotNull(::normalizeAddress).distinct()
    if (tapped != null) return normalizeAddress(tapped)?.takeIf { it in addresses }
    return normalizeAddress(preferred)?.takeIf { it in addresses } ?: addresses.singleOrNull()
}
