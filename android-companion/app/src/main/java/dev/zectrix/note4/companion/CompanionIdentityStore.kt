package dev.zectrix.note4.companion

import android.content.Context
import android.util.Base64
import java.security.SecureRandom

/**
 * Stable local companion identity. It is a 128-bit random fingerprint that
 * Android includes in the enrollment proof and then presents on reconnect.
 * It never changes unless the application data is cleared or the identity is
 * explicitly reset.
 */
class CompanionIdentityStore(context: Context) {
    private val preferences = context.applicationContext
        .getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

    fun getOrCreate(): ByteArray {
        val encoded = preferences.getString(KEY_IDENTITY, null)
        if (encoded != null) {
            val decoded = Base64.decode(encoded, Base64.NO_WRAP)
            if (decoded.size == IDENTITY_SIZE) return decoded
        }
        val generated = ByteArray(IDENTITY_SIZE)
        SecureRandom().nextBytes(generated)
        preferences.edit()
            .putString(KEY_IDENTITY, Base64.encodeToString(generated, Base64.NO_WRAP))
            .apply()
        return generated
    }

    fun clear() {
        preferences.edit().remove(KEY_IDENTITY).apply()
    }

    private companion object {
        const val PREFERENCES_NAME = "zectrix_companion"
        const val KEY_IDENTITY = "companion_identity"
        const val IDENTITY_SIZE = 16
    }
}
