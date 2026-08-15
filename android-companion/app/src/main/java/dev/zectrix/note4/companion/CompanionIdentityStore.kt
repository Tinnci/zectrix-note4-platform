package dev.zectrix.note4.companion

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Stable local companion identity. It is a 128-bit random fingerprint that
 * Android includes in the enrollment proof and then presents on reconnect.
 * It never changes unless the application data is cleared or the identity is
 * explicitly reset.
 *
 * The identity is encrypted at rest with an Android Keystore AES-GCM key.
 * A legacy plaintext value is migrated on first read. If Keystore is not
 * available, the identity remains process-local and the app does not claim
 * durable enrollment.
 */
class CompanionIdentityStore(context: Context) {
    private val preferences = context.applicationContext
        .getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
    private val keyStore: KeyStore? = try {
        KeyStore.getInstance(KEYSTORE).apply { load(null) }
    } catch (_: Exception) {
        null
    }

    fun getOrCreate(): ByteArray {
        val encoded = preferences.getString(KEY_IDENTITY, null)
        if (encoded != null) {
            val stored = decodeBase64(encoded)
            val decoded = when {
                stored == null -> null
                stored.size == IDENTITY_SIZE -> stored
                stored.size > GCM_IV_SIZE -> decrypt(stored)
                else -> null
            }
            if (decoded != null && decoded.size == IDENTITY_SIZE) {
                if (stored?.size == IDENTITY_SIZE && !persistIdentity(decoded)) {
                    clearIdentityEntry()
                    setEnrolled(false)
                }
                return decoded
            }
            // Corrupt or undecryptable identity: clear it before generating a
            // replacement instead of crashing or reusing bad material.
            clearIdentityEntry()
        }
        val generated = ByteArray(IDENTITY_SIZE)
        SecureRandom().nextBytes(generated)
        if (!persistIdentity(generated)) setEnrolled(false)
        return generated
    }

    fun isEnrolled(): Boolean = preferences.getBoolean(KEY_ENROLLED, false)

    fun setEnrolled(enrolled: Boolean) {
        preferences.edit().putBoolean(KEY_ENROLLED, enrolled).apply()
    }

    fun clear() {
        clearIdentityEntry()
        preferences.edit().remove(KEY_ENROLLED).apply()
        deleteKeystoreKey()
    }

    private fun clearIdentityEntry() {
        preferences.edit().remove(KEY_IDENTITY).apply()
    }

    private fun persistIdentity(identity: ByteArray): Boolean {
        val stored = encrypt(identity) ?: return false
        preferences.edit()
            .putString(KEY_IDENTITY, Base64.encodeToString(stored, Base64.NO_WRAP))
            .apply()
        return true
    }

    private fun decodeBase64(encoded: String): ByteArray? {
        return try {
            Base64.decode(encoded, Base64.NO_WRAP)
        } catch (_: IllegalArgumentException) {
            null
        }
    }

    private fun encrypt(identity: ByteArray): ByteArray? {
        val key = getOrCreateKey() ?: return null
        return try {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, key)
            cipher.iv + cipher.doFinal(identity)
        } catch (_: Exception) {
            null
        }
    }

    private fun decrypt(stored: ByteArray): ByteArray? {
        if (stored.size <= GCM_IV_SIZE) return null
        val key = getOrCreateKey() ?: return null
        return try {
            val iv = stored.copyOfRange(0, GCM_IV_SIZE)
            val ciphertext = stored.copyOfRange(GCM_IV_SIZE, stored.size)
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(GCM_TAG_BITS, iv))
            cipher.doFinal(ciphertext)
        } catch (_: Exception) {
            null
        }
    }

    private fun getOrCreateKey(): SecretKey? {
        val ks = keyStore ?: return null
        return try {
            (ks.getKey(KEY_ALIAS, null) as? SecretKey) ?: run {
                val generator = KeyGenerator.getInstance(
                    KeyProperties.KEY_ALGORITHM_AES, KEYSTORE,
                )
                generator.init(
                    KeyGenParameterSpec.Builder(
                        KEY_ALIAS,
                        KeyProperties.PURPOSE_ENCRYPT or
                            KeyProperties.PURPOSE_DECRYPT,
                    )
                        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                        .setEncryptionPaddings(
                            KeyProperties.ENCRYPTION_PADDING_NONE,
                        )
                        .setKeySize(KEY_SIZE_BITS)
                        .build(),
                )
                generator.generateKey()
            }
        } catch (_: Exception) {
            null
        }
    }

    private fun deleteKeystoreKey() {
        try {
            keyStore?.deleteEntry(KEY_ALIAS)
        } catch (_: Exception) {
            // The identity entry is already gone; a stale key is harmless.
        }
    }

    private companion object {
        const val PREFERENCES_NAME = "zectrix_companion"
        const val KEY_IDENTITY = "companion_identity"
        const val KEY_ENROLLED = "companion_enrolled"
        const val KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "zectrix_companion_identity_v1"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val GCM_TAG_BITS = 128
        const val GCM_IV_SIZE = 12
        const val KEY_SIZE_BITS = 256
        const val IDENTITY_SIZE = 16
    }
}
