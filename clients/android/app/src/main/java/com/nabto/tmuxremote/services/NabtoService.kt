package com.nabto.tmuxremote.services

import com.nabto.edge.client.Connection
import com.nabto.edge.client.ktx.awaitExecute
import com.nabto.edge.client.ktx.awaitOpen
import com.nabto.edge.client.ktx.awaitReadSome
import com.nabto.edge.client.ktx.awaitWrite
import com.nabto.tmuxremote.models.DeviceBookmark
import com.nabto.tmuxremote.models.PairingInfo
import com.nabto.tmuxremote.models.SessionInfo
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class NabtoService(
    private val connectionManager: ConnectionManager,
    private val bookmarkStore: BookmarkStore
) {
    var currentDeviceId: String? = null
        private set
    var currentSession: String? = null
        private set

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var stream: com.nabto.edge.client.Stream? = null
    private var readJob: Job? = null
    var reconnectJob: Job? = null
        private set
    private var reconnectContext: Pair<String, String>? = null

    /** Called when stream data arrives. Set by TerminalScreen. */
    var onStreamData: ((ByteArray) -> Unit)? = null

    /** Called when the stream ends unexpectedly. */
    var onStreamClosed: (() -> Unit)? = null

    private val reconnectLogic = ReconnectLogic()

    val connectionState: ConnectionState
        get() {
            val deviceId = currentDeviceId ?: return ConnectionState.DISCONNECTED
            return connectionManager.getDeviceState(deviceId)
        }

    // MARK: - Connection

    suspend fun connect(bookmark: DeviceBookmark) {
        currentDeviceId = bookmark.deviceId
        connectionManager.connection(bookmark)
    }

    fun disconnect(keepConnection: Boolean = false) {
        disableReconnectContext()
        onStreamClosed = null
        onStreamData = null

        readJob?.cancel()
        readJob = null

        stream?.let { s ->
            if (keepConnection) {
                try { s.close() } catch (_: Exception) { s.abort() }
            } else {
                s.abort()
            }
        }
        stream = null

        currentDeviceId?.let { deviceId ->
            connectionManager.cancelPendingConnect(deviceId)
            if (!keepConnection) {
                connectionManager.disconnect(deviceId)
            }
        }

        currentSession = null
        currentDeviceId = null
    }

    fun enableReconnectContext(deviceId: String, session: String) {
        reconnectContext = Pair(deviceId, session)
    }

    fun disableReconnectContext() {
        reconnectContext = null
        cancelReconnect()
    }

    fun canAutoReconnect(deviceId: String, session: String): Boolean {
        val ctx = reconnectContext ?: return false
        return ctx.first == deviceId && ctx.second == session
    }

    // MARK: - Pairing

    suspend fun pair(info: PairingInfo): DeviceBookmark {
        val conn = connectionManager.connectForPairing(info)

        try {
            withContext(Dispatchers.IO) {
                conn.passwordAuthenticate(info.username, info.password)
            }

            val paired = coapPairPasswordInvite(conn, info.username)
            if (!paired) {
                throw NabtoError.PairingFailed("The invitation may have already been used.")
            }

            val fingerprint = conn.getDeviceFingerprint()

            val bookmark = DeviceBookmark(
                productId = info.productId,
                deviceId = info.deviceId,
                fingerprint = fingerprint,
                sct = info.sct,
                name = info.deviceId,
                lastSession = null,
                lastConnected = System.currentTimeMillis()
            )

            connectionManager.adoptConnection(conn, info.deviceId)
            return bookmark
        } catch (e: Exception) {
            conn.close()
            connectionManager.setDeviceState(ConnectionState.DISCONNECTED, info.deviceId)
            throw e
        }
    }

    private suspend fun coapPairPasswordInvite(conn: Connection, username: String): Boolean {
        val coap = conn.createCoap("POST", "/iam/pairing/password-invite")
        val payload = CBORHelpers.encodePairingPayload(username)
        coap.setRequestPayload(60, payload)
        coap.awaitExecute()
        return coap.getResponseStatusCode() in 200..299
    }

    // MARK: - CoAP: Sessions

    suspend fun listSessions(bookmark: DeviceBookmark): List<SessionInfo> {
        val conn = connectionManager.connection(bookmark)
        val coap = conn.createCoap("GET", "/terminal/sessions")
        coap.awaitExecute()
        if (coap.getResponseStatusCode() != 205) {
            throw NabtoError.CoapFailed("List sessions", coap.getResponseStatusCode())
        }
        return CBORHelpers.decodeSessions(coap.getResponsePayload())
    }

    suspend fun attach(bookmark: DeviceBookmark, session: String, cols: Int, rows: Int) {
        val conn = connectionManager.connection(bookmark)
        val coap = conn.createCoap("POST", "/terminal/attach")
        val payload = CBORHelpers.encodeAttach(session, cols, rows)
        coap.setRequestPayload(60, payload)
        coap.awaitExecute()

        if (coap.getResponseStatusCode() == 404) {
            throw NabtoError.SessionNotFound(session)
        }
        if (coap.getResponseStatusCode() != 201) {
            throw NabtoError.CoapFailed("Attach", coap.getResponseStatusCode())
        }

        currentDeviceId = bookmark.deviceId
        currentSession = session
    }

    suspend fun createSession(bookmark: DeviceBookmark, name: String, cols: Int, rows: Int, command: String? = null) {
        val conn = connectionManager.connection(bookmark)
        val coap = conn.createCoap("POST", "/terminal/create")
        val payload = CBORHelpers.encodeCreate(name, cols, rows, command)
        coap.setRequestPayload(60, payload)
        coap.awaitExecute()

        if (coap.getResponseStatusCode() != 201) {
            throw NabtoError.CoapFailed("Create session", coap.getResponseStatusCode())
        }
    }

    suspend fun resize(bookmark: DeviceBookmark, cols: Int, rows: Int) {
        try {
            val conn = connectionManager.connection(bookmark)
            val coap = conn.createCoap("POST", "/terminal/resize")
            val payload = CBORHelpers.encodeResize(cols, rows)
            coap.setRequestPayload(60, payload)
            coap.awaitExecute()
            if (coap.getResponseStatusCode() != 204) {
                // Retry once
                val coap2 = conn.createCoap("POST", "/terminal/resize")
                coap2.setRequestPayload(60, payload)
                coap2.awaitExecute()
            }
        } catch (_: Exception) {
            // Resize failures are non-critical
        }
    }

    // MARK: - Stream

    suspend fun openStream(bookmark: DeviceBookmark) {
        val conn = connectionManager.connection(bookmark)
        val s = conn.createStream()
        s.awaitOpen(1)
        this.stream = s
        startReadLoop()
    }

    fun writeToStream(data: ByteArray) {
        val stream = stream ?: return
        scope.launch {
            try { stream.awaitWrite(data) } catch (_: Exception) { }
        }
    }

    fun closeStream() {
        readJob?.cancel()
        readJob = null
        stream?.let { s ->
            s.abort()
            this.stream = null
        }
        currentSession = null
    }

    private fun startReadLoop() {
        readJob?.cancel()
        readJob = scope.launch {
            val s = stream ?: return@launch
            while (isActive) {
                try {
                    val data = s.awaitReadSome()
                    onStreamData?.invoke(data)
                } catch (_: Exception) {
                    if (isActive) {
                        onStreamClosed?.invoke()
                    }
                    break
                }
            }
        }
    }

    // MARK: - Reconnect

    fun attemptReconnect(
        bookmark: DeviceBookmark,
        session: String,
        cols: Int,
        rows: Int,
        onSuccess: (() -> Unit)? = null,
        onGiveUp: (() -> Unit)? = null
    ) {
        if (!canAutoReconnect(bookmark.deviceId, session)) return

        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            val startTime = System.currentTimeMillis()
            var attempt = 0

            while (isActive) {
                if (!canAutoReconnect(bookmark.deviceId, session)) return@launch
                attempt++

                val elapsed = (System.currentTimeMillis() - startTime) / 1000.0
                if (reconnectLogic.shouldGiveUp(elapsed)) {
                    if (!canAutoReconnect(bookmark.deviceId, session)) return@launch
                    connectionManager.setDeviceState(ConnectionState.OFFLINE, bookmark.deviceId)
                    onGiveUp?.invoke()
                    return@launch
                }

                try {
                    connectionManager.disconnect(bookmark.deviceId)
                    connectionManager.setDeviceState(ConnectionState.RECONNECTING, bookmark.deviceId)
                    connect(bookmark)
                    attach(bookmark, session, cols, rows)
                    openStream(bookmark)
                    resize(bookmark, cols, rows)
                    if (!canAutoReconnect(bookmark.deviceId, session)) {
                        connectionManager.disconnect(bookmark.deviceId)
                        return@launch
                    }
                    onSuccess?.invoke()
                    return@launch
                } catch (e: CancellationException) {
                    return@launch
                } catch (e: NabtoError.SessionNotFound) {
                    connectionManager.setDeviceState(ConnectionState.OFFLINE, bookmark.deviceId)
                    onGiveUp?.invoke()
                    return@launch
                } catch (_: Exception) {
                    if (!isActive || !canAutoReconnect(bookmark.deviceId, session)) return@launch
                    connectionManager.setDeviceState(ConnectionState.RECONNECTING, bookmark.deviceId)
                    val delayMs = (reconnectLogic.backoff(attempt) * 1000).toLong()
                    delay(delayMs)
                }
            }
        }
    }

    fun cancelReconnect() {
        reconnectJob?.cancel()
        reconnectJob = null
    }
}
