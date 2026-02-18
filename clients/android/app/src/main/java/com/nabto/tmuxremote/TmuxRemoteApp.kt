package com.nabto.tmuxremote

import android.app.Application
import com.nabto.tmuxremote.services.BookmarkStore
import com.nabto.tmuxremote.services.ConnectionManager
import com.nabto.tmuxremote.services.KeyStoreService
import com.nabto.tmuxremote.services.NabtoService

class TmuxRemoteApp : Application() {

    lateinit var keyStoreService: KeyStoreService
        private set
    lateinit var bookmarkStore: BookmarkStore
        private set
    lateinit var connectionManager: ConnectionManager
        private set
    lateinit var nabtoService: NabtoService
        private set

    override fun onCreate() {
        super.onCreate()

        keyStoreService = KeyStoreService(this)
        bookmarkStore = BookmarkStore(this)
        connectionManager = ConnectionManager(this, keyStoreService)
        nabtoService = NabtoService(connectionManager, bookmarkStore)
    }
}
