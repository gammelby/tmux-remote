package com.nabto.tmuxremote.services

import android.content.Context
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

/**
 * Stores the Nabto client private key in Android EncryptedSharedPreferences.
 * Equivalent to iOS KeychainService.
 */
class KeyStoreService(context: Context) {

    private val masterKey = MasterKey.Builder(context)
        .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
        .build()

    private val prefs = EncryptedSharedPreferences.create(
        context,
        PREFS_NAME,
        masterKey,
        EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
        EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
    )

    fun loadPrivateKey(): String? {
        return prefs.getString(KEY_PRIVATE_KEY, null)
    }

    fun savePrivateKey(key: String): Boolean {
        return prefs.edit().putString(KEY_PRIVATE_KEY, key).commit()
    }

    companion object {
        private const val PREFS_NAME = "com.nabto.tmux-remote.clientkey"
        private const val KEY_PRIVATE_KEY = "client_private_key"
    }
}
