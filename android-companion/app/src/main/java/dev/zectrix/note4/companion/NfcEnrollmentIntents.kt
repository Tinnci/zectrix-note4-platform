package dev.zectrix.note4.companion

import android.content.Intent
import android.nfc.NdefMessage
import android.nfc.NdefRecord
import android.nfc.NfcAdapter

object NfcEnrollmentIntents {
    @Suppress("DEPRECATION")
    fun read(intent: Intent): CompanionEnrollmentRecord? {
        if (intent.action != NfcAdapter.ACTION_NDEF_DISCOVERED && intent.action != NfcAdapter.ACTION_TECH_DISCOVERED) return null
        try {
            val messages = intent.getParcelableArrayExtra(NfcAdapter.EXTRA_NDEF_MESSAGES) ?: return null
            require(messages.size in 1..4 && messages.all { it is NdefMessage }) { "Malformed NFC message" }
            val records = messages.filterIsInstance<NdefMessage>().flatMap { it.records.asList() }
            require(records.size <= 8) { "Too many NFC records" }
            val candidates = records.filter { it.tnf == NdefRecord.TNF_MIME_MEDIA &&
                it.type.contentEquals(NfcEnrollmentParser.MIME_TYPE.toByteArray(Charsets.US_ASCII)) }
            if (candidates.isEmpty()) return null
            require(candidates.size == 1) { "Ambiguous Note4 enrollment records; tap again" }
            val payload = candidates.single().payload
            return try {
                requireNotNull(NfcEnrollmentParser.parsePayload(payload)) { "Malformed Note4 enrollment record" }
            } finally { payload.fill(0) }
        } finally {
            // An Activity recreation must not replay the launch token.
            intent.removeExtra(NfcAdapter.EXTRA_NDEF_MESSAGES)
        }
    }
}
