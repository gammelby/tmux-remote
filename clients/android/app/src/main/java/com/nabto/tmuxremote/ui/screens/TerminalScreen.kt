package com.nabto.tmuxremote.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.nabto.tmuxremote.models.DeviceBookmark
import com.nabto.tmuxremote.patterns.PatternEngine
import com.nabto.tmuxremote.services.BookmarkStore
import com.nabto.tmuxremote.services.ConnectionManager
import com.nabto.tmuxremote.services.ConnectionState
import com.nabto.tmuxremote.services.ControlStreamEvent
import com.nabto.tmuxremote.services.NabtoError
import com.nabto.tmuxremote.services.NabtoService
import com.nabto.tmuxremote.terminal.KeyboardAccessoryBar
import com.nabto.tmuxremote.terminal.NabtoTerminalSession
import com.nabto.tmuxremote.terminal.TerminalViewComposable
import com.nabto.tmuxremote.ui.components.PatternOverlaySheet
import com.nabto.tmuxremote.ui.theme.TmuxAccent
import com.nabto.tmuxremote.ui.theme.TmuxOffline
import com.nabto.tmuxremote.ui.theme.TmuxOnline
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch

@Composable
fun TerminalScreen(
    bookmark: DeviceBookmark,
    sessionName: String,
    nabtoService: NabtoService,
    connectionManager: ConnectionManager,
    bookmarkStore: BookmarkStore,
    onDismiss: () -> Unit
) {
    val scope = rememberCoroutineScope()
    val patternEngine = remember { PatternEngine() }
    var terminalSession by remember { mutableStateOf<NabtoTerminalSession?>(null) }
    var currentCols by remember { mutableIntStateOf(80) }
    var currentRows by remember { mutableIntStateOf(24) }
    var initialConnectionDone by remember { mutableStateOf(false) }
    var isConnecting by remember { mutableStateOf(false) }
    var isReconnecting by remember { mutableStateOf(false) }
    var isDismissing by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var showError by remember { mutableStateOf(false) }

    val deviceStates by connectionManager.deviceStates.collectAsState()
    val activeMatch by patternEngine.activeMatch.collectAsState()
    val isOverlayHidden by patternEngine.isOverlayHidden.collectAsState()
    val visibleMatch = if (isOverlayHidden) null else activeMatch
    val canRestoreHidden = isOverlayHidden && activeMatch != null

    val connectionState = deviceStates[bookmark.deviceId] ?: ConnectionState.DISCONNECTED

    fun dismissToDevices() {
        isDismissing = true
        connectionManager.onPatternEvent = null
        nabtoService.disableReconnectContext()
        nabtoService.disconnect(keepConnection = true)
        onDismiss()
    }

    fun setupCallbacks() {
        nabtoService.enableReconnectContext(bookmark.deviceId, sessionName)
        nabtoService.onStreamData = { data ->
            terminalSession?.feed(data)
        }
        nabtoService.onStreamClosed = {
            if (nabtoService.canAutoReconnect(bookmark.deviceId, sessionName) && !isReconnecting) {
                patternEngine.reset()
                isReconnecting = true
                nabtoService.attemptReconnect(
                    bookmark, sessionName, currentCols, currentRows,
                    onSuccess = { isReconnecting = false },
                    onGiveUp = { isReconnecting = false; dismissToDevices() }
                )
            }
        }
        connectionManager.onPatternEvent = { deviceId, event ->
            if (deviceId == bookmark.deviceId) {
                when (event) {
                    is ControlStreamEvent.PatternPresent -> patternEngine.applyServerPresent(event.match)
                    is ControlStreamEvent.PatternUpdate -> patternEngine.applyServerUpdate(event.match)
                    is ControlStreamEvent.PatternGone -> patternEngine.applyServerGone(event.instanceId)
                    is ControlStreamEvent.Sessions -> {}
                }
            }
        }
    }

    suspend fun connectAndAttach() {
        if (isDismissing) return
        try {
            nabtoService.connect(bookmark)
            if (isDismissing) throw CancellationException()
            nabtoService.attach(bookmark, sessionName, currentCols, currentRows)
            if (isDismissing) throw CancellationException()
            nabtoService.openStream(bookmark)
            if (isDismissing) throw CancellationException()
            bookmarkStore.updateLastSession(bookmark.deviceId, sessionName)
        } catch (_: CancellationException) {
            return
        } catch (e: NabtoError.SessionNotFound) {
            bookmarkStore.clearLastSession(bookmark.deviceId)
            dismissToDevices()
            return
        } catch (e: Exception) {
            errorMessage = e.message ?: "Connection failed"
            showError = true
        }
    }

    // Terminal ready -> connect
    LaunchedEffect(terminalSession) {
        val session = terminalSession ?: return@LaunchedEffect
        if (isConnecting || initialConnectionDone || isDismissing) return@LaunchedEffect
        isConnecting = true
        setupCallbacks()
        connectAndAttach()
        if (!isDismissing) {
            initialConnectionDone = true
        }
        isConnecting = false
    }

    // Lifecycle handling
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_STOP -> {
                    nabtoService.closeStream()
                }
                Lifecycle.Event.ON_START -> {
                    if (initialConnectionDone &&
                        nabtoService.canAutoReconnect(bookmark.deviceId, sessionName)
                    ) {
                        val state = connectionManager.getDeviceState(bookmark.deviceId)
                        if (state != ConnectionState.CONNECTED || nabtoService.currentSession == null) {
                            patternEngine.reset()
                            isReconnecting = true
                            nabtoService.attemptReconnect(
                                bookmark, sessionName, currentCols, currentRows,
                                onSuccess = { isReconnecting = false },
                                onGiveUp = { isReconnecting = false; dismissToDevices() }
                            )
                        }
                    }
                }
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            isDismissing = true
            connectionManager.onPatternEvent = null
            nabtoService.disableReconnectContext()
            nabtoService.disconnect(keepConnection = true)
        }
    }

    // Main UI
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
    ) {
        // Terminal
        Column(modifier = Modifier.fillMaxSize()) {
            TerminalViewComposable(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                onSend = { data -> nabtoService.writeToStream(data) },
                onSizeChanged = { cols, rows ->
                    currentCols = cols
                    currentRows = rows
                    if (initialConnectionDone) {
                        scope.launch {
                            nabtoService.resize(bookmark, cols, rows)
                        }
                    }
                },
                onReady = { session ->
                    terminalSession = session
                }
            )

            KeyboardAccessoryBar(
                onSend = { data -> nabtoService.writeToStream(data) }
            )
        }

        // Top bar overlay
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Back button
            Text(
                text = "< Agents",
                fontSize = 12.sp,
                color = Color.White,
                modifier = Modifier
                    .background(Color.Gray.copy(alpha = 0.6f), RoundedCornerShape(16.dp))
                    .clickable { dismissToDevices() }
                    .padding(horizontal = 8.dp, vertical = 4.dp)
            )

            Spacer(modifier = Modifier.weight(1f))

            // Show prompt pill
            if (canRestoreHidden) {
                Text(
                    text = "Show prompt",
                    fontSize = 12.sp,
                    color = Color.White,
                    modifier = Modifier
                        .background(Color(0xFFFF9800).copy(alpha = 0.85f), RoundedCornerShape(16.dp))
                        .clickable { patternEngine.restoreOverlay() }
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
            }

            // Connection pill
            ConnectionPill(state = connectionState)
        }

        // Pattern overlay
        AnimatedVisibility(
            visible = visibleMatch != null,
            enter = slideInVertically(initialOffsetY = { it }),
            exit = slideOutVertically(targetOffsetY = { it }),
            modifier = Modifier.align(Alignment.BottomCenter)
        ) {
            visibleMatch?.let { match ->
                PatternOverlaySheet(
                    match = match,
                    onAction = { action ->
                        nabtoService.writeToStream(action.keys.toByteArray())
                        patternEngine.resolveLocally(match.id)
                        connectionManager.sendPatternResolve(
                            bookmark.deviceId, match.id, "action", action.keys
                        )
                    },
                    onDismiss = {
                        patternEngine.dismissLocally(match.id)
                    }
                )
            }
        }

        // Reconnecting overlay
        if (isReconnecting) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.7f)),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator(
                        color = Color.White,
                        modifier = Modifier.size(48.dp)
                    )
                    Text(
                        text = "Reconnecting...",
                        color = Color.White,
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.padding(top = 20.dp)
                    )
                    Text(
                        text = "Back to Agents",
                        fontSize = 14.sp,
                        color = Color.White,
                        modifier = Modifier
                            .padding(top = 16.dp)
                            .background(Color.Gray.copy(alpha = 0.6f), RoundedCornerShape(16.dp))
                            .clickable { dismissToDevices() }
                            .padding(horizontal = 16.dp, vertical = 8.dp)
                    )
                }
            }
        }

        // Error dialog
        if (showError && errorMessage != null) {
            androidx.compose.material3.AlertDialog(
                onDismissRequest = { showError = false },
                title = { Text("Error") },
                text = { Text(errorMessage ?: "Unknown error") },
                confirmButton = {
                    androidx.compose.material3.TextButton(onClick = {
                        showError = false
                        scope.launch { connectAndAttach() }
                    }) { Text("Retry") }
                },
                dismissButton = {
                    androidx.compose.material3.TextButton(onClick = {
                        showError = false
                        dismissToDevices()
                    }) { Text("Back") }
                }
            )
        }
    }
}

@Composable
private fun ConnectionPill(state: ConnectionState) {
    val (text, color) = when (state) {
        ConnectionState.DISCONNECTED -> "Disconnected" to Color.Gray
        ConnectionState.CONNECTING -> "Connecting..." to TmuxAccent
        ConnectionState.CONNECTED -> "Connected" to TmuxOnline
        ConnectionState.RECONNECTING -> "Reconnecting..." to Color(0xFFFF9800)
        ConnectionState.OFFLINE -> "Offline" to TmuxOffline
    }

    Text(
        text = text,
        fontSize = 10.sp,
        color = Color.White,
        modifier = Modifier
            .background(color.copy(alpha = 0.85f), RoundedCornerShape(16.dp))
            .padding(horizontal = 8.dp, vertical = 4.dp)
    )
}
