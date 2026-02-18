package com.nabto.tmuxremote.services

import android.content.Context
import com.nabto.edge.client.Coap
import com.nabto.edge.client.Connection
import com.nabto.edge.client.ConnectionEventsCallback
import com.nabto.edge.client.NabtoClient
import com.nabto.edge.client.ktx.awaitConnect
import com.nabto.edge.client.ktx.awaitExecute
import com.nabto.edge.client.ktx.awaitOpen
import com.nabto.edge.client.ktx.awaitReadSome
import com.nabto.edge.client.ktx.awaitWrite
import com.nabto.tmuxremote.models.DeviceBookmark
import com.nabto.tmuxremote.models.PairingInfo
import com.nabto.tmuxremote.models.SessionInfo
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.nio.ByteBuffer
import java.util.UUID

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    OFFLINE
}

data class ReconnectingState(
    val state: ConnectionState,
    val attempt: Int = 0
)

sealed class NabtoError : Exception() {
    data class ConnectionFailed(override val message: String) : NabtoError()
    data class PairingFailed(override val message: String) : NabtoError()
    data class CoapFailed(override val message: String, val code: Int) : NabtoError()
    data class StreamFailed(override val message: String) : NabtoError()
    data class SessionNotFound(val sessionName: String) : NabtoError() {
        override val message: String get() = "Session '$sessionName' not found"
    }
    data object AlreadyPaired : NabtoError() {
        override val message: String get() = "Already paired with this device"
    }
}

class ConnectionManager(
    context: Context,
    private val keyStoreService: KeyStoreService
) {
    private val client: NabtoClient = NabtoClient.create(context)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val connectionMutex = Mutex()

    private val connections = mutableMapOf<String, Connection>()
    private val pendingConnects = mutableMapOf<String, CompletableDeferred<Connection>>()
    private val controlStreams = mutableMapOf<String, com.nabto.edge.client.Stream>()
    private val controlReadJobs = mutableMapOf<String, Job>()
    private val controlGenerations = mutableMapOf<String, String>()

    private val _deviceStates = MutableStateFlow<Map<String, ConnectionState>>(emptyMap())
    val deviceStates: StateFlow<Map<String, ConnectionState>> = _deviceStates

    private val _deviceSessions = MutableStateFlow<Map<String, List<SessionInfo>>>(emptyMap())
    val deviceSessions: StateFlow<Map<String, List<SessionInfo>>> = _deviceSessions

    var onPatternEvent: ((String, ControlStreamEvent) -> Unit)? = null

    fun getDeviceState(deviceId: String): ConnectionState {
        return _deviceStates.value[deviceId] ?: ConnectionState.DISCONNECTED
    }

    fun setDeviceState(state: ConnectionState, deviceId: String) {
        _deviceStates.value = _deviceStates.value + (deviceId to state)
    }

    suspend fun connection(bookmark: DeviceBookmark): Connection {
        val deviceId = bookmark.deviceId

        connectionMutex.withLock {
            connections[deviceId]?.let { return it }
            pendingConnects[deviceId]?.let {
                // Unlock mutex and await
                return@withLock null
            }
        }?.let { return it }

        // Check if there's already a pending connect
        pendingConnects[deviceId]?.let { return it.await() }

        val deferred = CompletableDeferred<Connection>()
        pendingConnects[deviceId] = deferred

        setDeviceState(ConnectionState.CONNECTING, deviceId)

        scope.launch {
            try {
                val privateKey = loadOrCreatePrivateKey()
                val conn = client.createConnection()
                val options = JSONObject().apply {
                    put("ProductId", bookmark.productId)
                    put("DeviceId", bookmark.deviceId)
                    put("PrivateKey", privateKey)
                    put("ServerConnectToken", bookmark.sct)
                }
                conn.updateOptions(options.toString())

                withTimeout(10_000) {
                    conn.awaitConnect()
                }

                conn.addConnectionEventsListener(object : ConnectionEventsCallback() {
                    override fun onEvent(event: Int) {
                        if (event == ConnectionEventsCallback.CLOSED || event == ConnectionEventsCallback.CHANNEL_CHANGED) {
                            scope.launch {
                                handleConnectionClosed(deviceId)
                            }
                        }
                    }
                })

                connectionMutex.withLock {
                    connections[deviceId] = conn
                    pendingConnects.remove(deviceId)
                }
                setDeviceState(ConnectionState.CONNECTED, deviceId)

                openControlStream(deviceId, conn)

                deferred.complete(conn)
            } catch (e: Exception) {
                connectionMutex.withLock {
                    pendingConnects.remove(deviceId)
                }
                if (connections[deviceId] == null) {
                    setDeviceState(ConnectionState.DISCONNECTED, deviceId)
                }
                deferred.completeExceptionally(
                    if (e is CancellationException) e
                    else NabtoError.ConnectionFailed(e.message ?: "Connection failed")
                )
            }
        }

        return deferred.await()
    }

    fun disconnect(deviceId: String) {
        pendingConnects.remove(deviceId)?.cancel()
        closeControlStream(deviceId)
        connections.remove(deviceId)?.close()
        setDeviceState(ConnectionState.DISCONNECTED, deviceId)
    }

    fun disconnectAll() {
        val deviceIds = (connections.keys + pendingConnects.keys).toSet()
        for (deviceId in deviceIds) {
            disconnect(deviceId)
        }
    }

    fun cancelPendingConnect(deviceId: String) {
        pendingConnects.remove(deviceId)?.cancel()
        if (connections[deviceId] == null) {
            setDeviceState(ConnectionState.DISCONNECTED, deviceId)
        }
    }

    fun warmCache(bookmarks: List<DeviceBookmark>) {
        for (bookmark in bookmarks) {
            scope.launch {
                try { connection(bookmark) } catch (_: Exception) { }
            }
        }
    }

    suspend fun connectForPairing(info: PairingInfo): Connection {
        setDeviceState(ConnectionState.CONNECTING, info.deviceId)
        val privateKey = loadOrCreatePrivateKey()
        val conn = client.createConnection()
        val options = JSONObject().apply {
            put("ProductId", info.productId)
            put("DeviceId", info.deviceId)
            put("PrivateKey", privateKey)
            put("ServerConnectToken", info.sct)
        }
        conn.updateOptions(options.toString())

        withTimeout(10_000) {
            conn.awaitConnect()
        }
        setDeviceState(ConnectionState.CONNECTED, info.deviceId)
        return conn
    }

    fun adoptConnection(conn: Connection, deviceId: String) {
        conn.addConnectionEventsListener(object : ConnectionEventsCallback() {
            override fun onEvent(event: Int) {
                if (event == ConnectionEventsCallback.CLOSED || event == ConnectionEventsCallback.CHANNEL_CHANGED) {
                    scope.launch {
                        handleConnectionClosed(deviceId)
                    }
                }
            }
        })
        connections[deviceId] = conn
        setDeviceState(ConnectionState.CONNECTED, deviceId)
        openControlStream(deviceId, conn)
    }

    fun sendPatternResolve(deviceId: String, instanceId: String, decision: String, keys: String? = null) {
        val stream = controlStreams[deviceId] ?: return
        val data = CBORHelpers.encodePatternResolve(instanceId, decision, keys)
        scope.launch {
            try { stream.awaitWrite(data) } catch (_: Exception) { }
        }
    }

    suspend fun listSessionsCoAP(conn: Connection, timeoutMs: Long): List<SessionInfo> {
        return withTimeout(timeoutMs) {
            val coap = conn.createCoap("GET", "/terminal/sessions")
            coap.awaitExecute()
            val statusCode = coap.getResponseStatusCode()
            if (statusCode != 205) {
                throw NabtoError.CoapFailed("List sessions", statusCode)
            }
            val payload = coap.getResponsePayload()
            CBORHelpers.decodeSessions(payload)
        }
    }

    // MARK: - Control Stream

    private fun openControlStream(deviceId: String, connection: Connection) {
        closeControlStream(deviceId)
        val generation = UUID.randomUUID().toString()
        controlGenerations[deviceId] = generation

        controlReadJobs[deviceId] = scope.launch {
            try {
                val stream = connection.createStream()
                stream.awaitOpen(2)
                if (controlGenerations[deviceId] != generation) {
                    stream.abort()
                    return@launch
                }
                controlStreams[deviceId] = stream
                controlReadLoop(deviceId, stream)
            } catch (_: Exception) {
                // Old agent without control stream support
            }
            if (controlGenerations[deviceId] == generation) {
                controlReadJobs.remove(deviceId)
                controlStreams.remove(deviceId)
                _deviceSessions.value = _deviceSessions.value - deviceId
                controlGenerations.remove(deviceId)
            }
        }
    }

    private fun closeControlStream(deviceId: String) {
        controlGenerations.remove(deviceId)
        controlReadJobs.remove(deviceId)?.cancel()
        controlStreams.remove(deviceId)?.abort()
        _deviceSessions.value = _deviceSessions.value - deviceId
    }

    private suspend fun controlReadLoop(deviceId: String, stream: com.nabto.edge.client.Stream) {
        var readBuffer = ByteArray(0)

        while (true) {
            // Read 4-byte big-endian length prefix
            val lengthData = readExactly(stream, readBuffer, 4)
            readBuffer = lengthData.second
            val lengthBytes = lengthData.first
            val length = ByteBuffer.wrap(lengthBytes).int

            if (length <= 0 || length >= 65536) break

            // Read CBOR payload
            val payloadData = readExactly(stream, readBuffer, length)
            readBuffer = payloadData.second
            val payload = payloadData.first

            val event = CBORHelpers.decodeControlStreamEvent(payload) ?: continue

            withContext(Dispatchers.Main) {
                when (event) {
                    is ControlStreamEvent.Sessions -> {
                        _deviceSessions.value = _deviceSessions.value + (deviceId to event.sessions)
                    }
                    is ControlStreamEvent.PatternPresent,
                    is ControlStreamEvent.PatternUpdate,
                    is ControlStreamEvent.PatternGone -> {
                        onPatternEvent?.invoke(deviceId, event)
                    }
                }
            }
        }
    }

    /**
     * Read exactly [count] bytes from a Nabto stream, preserving excess
     * bytes in a buffer for subsequent calls.
     * Returns (result bytes, remaining buffer).
     */
    private suspend fun readExactly(
        stream: com.nabto.edge.client.Stream,
        buffer: ByteArray,
        count: Int
    ): Pair<ByteArray, ByteArray> {
        var buf = buffer
        while (buf.size < count) {
            val chunk = stream.awaitReadSome()
            if (chunk.isEmpty()) throw NabtoError.StreamFailed("Control stream closed")
            val newBuf = ByteArray(buf.size + chunk.size)
            buf.copyInto(newBuf)
            chunk.copyInto(newBuf, buf.size)
            buf = newBuf
        }
        val result = buf.copyOfRange(0, count)
        val remaining = buf.copyOfRange(count, buf.size)
        return Pair(result, remaining)
    }

    private fun handleConnectionClosed(deviceId: String) {
        closeControlStream(deviceId)
        connections.remove(deviceId)
        setDeviceState(ConnectionState.DISCONNECTED, deviceId)
    }

    private fun loadOrCreatePrivateKey(): String {
        keyStoreService.loadPrivateKey()?.let { return it }
        val key = client.createPrivateKey()
        keyStoreService.savePrivateKey(key)
        return key
    }
}
