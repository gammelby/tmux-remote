package com.nabto.tmuxremote.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.nabto.tmuxremote.models.DeviceBookmark
import com.nabto.tmuxremote.models.SessionInfo
import com.nabto.tmuxremote.services.BookmarkStore
import com.nabto.tmuxremote.services.ConnectionManager
import com.nabto.tmuxremote.services.ConnectionState
import com.nabto.tmuxremote.services.NabtoService
import com.nabto.tmuxremote.ui.theme.TmuxAccent
import com.nabto.tmuxremote.ui.theme.TmuxOffline
import com.nabto.tmuxremote.ui.theme.TmuxOnline
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private enum class ProbeStatus {
    PROBING, DONE, FAILED
}

private data class ProbeResult(
    val status: ProbeStatus,
    val sessions: List<SessionInfo> = emptyList()
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceListScreen(
    nabtoService: NabtoService,
    connectionManager: ConnectionManager,
    bookmarkStore: BookmarkStore,
    onNavigateToTerminal: (deviceId: String, session: String) -> Unit,
    onNavigateToPairing: () -> Unit,
    onNavigateToSettings: () -> Unit
) {
    val scope = rememberCoroutineScope()
    val devices by bookmarkStore.devices.collectAsState()
    val deviceStates by connectionManager.deviceStates.collectAsState()
    val deviceSessions by connectionManager.deviceSessions.collectAsState()
    var expandedDevices by remember { mutableStateOf(setOf<String>()) }
    val probeStatuses = remember { mutableStateMapOf<String, ProbeResult>() }
    var isRefreshing by remember { mutableStateOf(false) }

    // New session dialog
    var showNewSessionDialog by remember { mutableStateOf(false) }
    var newSessionName by remember { mutableStateOf("") }
    var newSessionDevice by remember { mutableStateOf<DeviceBookmark?>(null) }

    // Delete confirmation
    var deviceToDelete by remember { mutableStateOf<DeviceBookmark?>(null) }

    fun connectAllDevices() {
        for (device in devices) {
            scope.launch {
                try {
                    connectionManager.connection(device)
                    // Wait for control stream data
                    delay(3000)
                    if (deviceSessions[device.deviceId] == null) {
                        probeStatuses[device.deviceId] = ProbeResult(ProbeStatus.PROBING)
                        try {
                            val conn = connectionManager.connection(device)
                            val sessions = connectionManager.listSessionsCoAP(conn, 5000)
                            probeStatuses[device.deviceId] = ProbeResult(ProbeStatus.DONE, sessions)
                        } catch (_: Exception) {
                            probeStatuses[device.deviceId] = ProbeResult(ProbeStatus.FAILED)
                        }
                    }
                } catch (_: Exception) { }
            }
        }
    }

    LaunchedEffect(Unit) {
        bookmarkStore.lastDeviceId?.let { expandedDevices = expandedDevices + it }
        connectAllDevices()
    }

    if (devices.isEmpty()) {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text("Agents") },
                    actions = {
                        IconButton(onClick = onNavigateToSettings) {
                            Icon(Icons.Default.Settings, contentDescription = "Settings")
                        }
                        IconButton(onClick = onNavigateToPairing) {
                            Icon(Icons.Default.Add, contentDescription = "Add Agent")
                        }
                    }
                )
            }
        ) { padding ->
            Box(modifier = Modifier.padding(padding)) {
                WelcomeScreen(onAddAgent = onNavigateToPairing)
            }
        }
        return
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Agents") },
                navigationIcon = {
                    Row {
                        IconButton(onClick = onNavigateToSettings) {
                            Icon(Icons.Default.Settings, contentDescription = "Settings")
                        }
                        IconButton(
                            onClick = {
                                if (!isRefreshing) {
                                    isRefreshing = true
                                    probeStatuses.clear()
                                    for (device in devices) {
                                        connectionManager.disconnect(device.deviceId)
                                    }
                                    scope.launch {
                                        connectAllDevices()
                                        delay(3000)
                                        isRefreshing = false
                                    }
                                }
                            },
                            enabled = !isRefreshing
                        ) {
                            if (isRefreshing) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(20.dp),
                                    strokeWidth = 2.dp
                                )
                            } else {
                                Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                            }
                        }
                    }
                },
                actions = {
                    IconButton(onClick = onNavigateToPairing) {
                        Icon(Icons.Default.Add, contentDescription = "Add Agent")
                    }
                }
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            items(devices, key = { it.deviceId }) { device ->
                val isExpanded = device.deviceId in expandedDevices

                DeviceRow(
                    device = device,
                    expanded = isExpanded,
                    statusColor = statusColor(device.deviceId, deviceStates, deviceSessions, probeStatuses),
                    statusText = statusText(device, deviceSessions, probeStatuses, deviceStates),
                    onClick = {
                        expandedDevices = if (isExpanded) {
                            expandedDevices - device.deviceId
                        } else {
                            expandedDevices + device.deviceId
                        }
                    },
                    onDelete = { deviceToDelete = device }
                )

                AnimatedVisibility(visible = isExpanded) {
                    ExpandedContent(
                        device = device,
                        sessions = getSessions(device.deviceId, deviceSessions, probeStatuses),
                        isOnline = isOnline(device.deviceId, deviceStates, deviceSessions, probeStatuses),
                        isConnecting = isConnecting(device.deviceId, deviceStates, deviceSessions, probeStatuses),
                        onSessionTap = { session ->
                            onNavigateToTerminal(device.deviceId, session.name)
                        },
                        onNewSession = {
                            newSessionDevice = device
                            showNewSessionDialog = true
                        }
                    )
                }
            }
        }
    }

    // New session dialog
    if (showNewSessionDialog) {
        AlertDialog(
            onDismissRequest = {
                showNewSessionDialog = false
                newSessionName = ""
                newSessionDevice = null
            },
            title = { Text("New Session") },
            text = {
                TextField(
                    value = newSessionName,
                    onValueChange = { newSessionName = it },
                    label = { Text("Session name") },
                    singleLine = true
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    val name = newSessionName.ifEmpty { "ns-${(1000..9999).random()}" }
                    val device = newSessionDevice
                    showNewSessionDialog = false
                    newSessionName = ""
                    newSessionDevice = null
                    if (device != null) {
                        scope.launch {
                            try {
                                nabtoService.createSession(device, name, 80, 24)
                                delay(500)
                                onNavigateToTerminal(device.deviceId, name)
                            } catch (_: Exception) { }
                        }
                    }
                }) { Text("Create") }
            },
            dismissButton = {
                TextButton(onClick = {
                    showNewSessionDialog = false
                    newSessionName = ""
                    newSessionDevice = null
                }) { Text("Cancel") }
            }
        )
    }

    // Delete confirmation
    deviceToDelete?.let { device ->
        AlertDialog(
            onDismissRequest = { deviceToDelete = null },
            title = { Text("Remove Agent") },
            text = { Text("Remove ${device.name} from your device list?") },
            confirmButton = {
                TextButton(onClick = {
                    expandedDevices = expandedDevices - device.deviceId
                    connectionManager.disconnect(device.deviceId)
                    bookmarkStore.removeDevice(device.deviceId)
                    deviceToDelete = null
                }) { Text("Remove", color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { deviceToDelete = null }) { Text("Cancel") }
            }
        )
    }
}

@Composable
private fun DeviceRow(
    device: DeviceBookmark,
    expanded: Boolean,
    statusColor: Color,
    statusText: String,
    onClick: () -> Unit,
    onDelete: () -> Unit
) {
    val rotation by animateFloatAsState(if (expanded) 90f else 0f, label = "chevron")

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(10.dp)
                .clip(CircleShape)
                .background(statusColor)
        )

        Spacer(modifier = Modifier.width(12.dp))

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = device.name,
                style = MaterialTheme.typography.bodyLarge
            )
            Text(
                text = statusText,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        IconButton(onClick = onDelete) {
            Icon(
                Icons.Default.Delete,
                contentDescription = "Remove",
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(18.dp)
            )
        }

        Icon(
            Icons.Default.KeyboardArrowRight,
            contentDescription = null,
            modifier = Modifier.rotate(rotation),
            tint = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun ExpandedContent(
    device: DeviceBookmark,
    sessions: List<SessionInfo>?,
    isOnline: Boolean,
    isConnecting: Boolean,
    onSessionTap: (SessionInfo) -> Unit,
    onNewSession: () -> Unit
) {
    Column(modifier = Modifier.padding(start = 38.dp, end = 16.dp, bottom = 8.dp)) {
        when {
            isOnline && sessions != null -> {
                if (sessions.isEmpty()) {
                    Text(
                        text = "No tmux sessions",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    for (session in sessions) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onSessionTap(session) }
                                .padding(vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = session.name,
                                    style = MaterialTheme.typography.bodyMedium
                                )
                                Text(
                                    text = "${session.cols}x${session.rows}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            if (session.attached > 0) {
                                Text(
                                    text = "attached",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = TmuxAccent,
                                    modifier = Modifier
                                        .background(
                                            TmuxAccent.copy(alpha = 0.15f),
                                            RoundedCornerShape(12.dp)
                                        )
                                        .padding(horizontal = 6.dp, vertical = 2.dp)
                                )
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(4.dp))

                TextButton(onClick = onNewSession) {
                    Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(modifier = Modifier.width(4.dp))
                    Text("New Session", style = MaterialTheme.typography.bodySmall)
                }
            }

            isConnecting -> {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(14.dp),
                        strokeWidth = 2.dp
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Loading sessions...",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            else -> {
                Text(
                    text = "Agent unreachable",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

// Helper functions

private fun getSessions(
    deviceId: String,
    deviceSessions: Map<String, List<SessionInfo>>,
    probeStatuses: Map<String, ProbeResult>
): List<SessionInfo>? {
    deviceSessions[deviceId]?.let { return it }
    val probe = probeStatuses[deviceId]
    if (probe?.status == ProbeStatus.DONE) return probe.sessions
    return null
}

private fun isOnline(
    deviceId: String,
    deviceStates: Map<String, ConnectionState>,
    deviceSessions: Map<String, List<SessionInfo>>,
    probeStatuses: Map<String, ProbeResult>
): Boolean {
    if (deviceSessions.containsKey(deviceId)) return true
    if (probeStatuses[deviceId]?.status == ProbeStatus.DONE) return true
    return deviceStates[deviceId] == ConnectionState.CONNECTED
}

private fun isConnecting(
    deviceId: String,
    deviceStates: Map<String, ConnectionState>,
    deviceSessions: Map<String, List<SessionInfo>>,
    probeStatuses: Map<String, ProbeResult>
): Boolean {
    if (deviceSessions.containsKey(deviceId)) return false
    if (probeStatuses[deviceId]?.status == ProbeStatus.DONE) return false
    if (probeStatuses[deviceId]?.status == ProbeStatus.FAILED) return false
    val state = deviceStates[deviceId]
    return state == ConnectionState.CONNECTING || state == ConnectionState.CONNECTED ||
        state == ConnectionState.RECONNECTING
}

private fun statusColor(
    deviceId: String,
    deviceStates: Map<String, ConnectionState>,
    deviceSessions: Map<String, List<SessionInfo>>,
    probeStatuses: Map<String, ProbeResult>
): Color {
    if (deviceSessions.containsKey(deviceId)) return TmuxOnline
    if (probeStatuses[deviceId]?.status == ProbeStatus.DONE) return TmuxOnline
    return when (deviceStates[deviceId]) {
        ConnectionState.CONNECTED -> TmuxOnline
        ConnectionState.DISCONNECTED, ConnectionState.OFFLINE -> TmuxOffline
        else -> Color.Gray
    }
}

private fun statusText(
    device: DeviceBookmark,
    deviceSessions: Map<String, List<SessionInfo>>,
    probeStatuses: Map<String, ProbeResult>,
    deviceStates: Map<String, ConnectionState>
): String {
    val deviceId = device.deviceId

    deviceSessions[deviceId]?.let { sessions ->
        val names = sessions.joinToString(", ") { it.name }
        return names.ifEmpty { "No sessions" }
    }

    val probe = probeStatuses[deviceId]
    when (probe?.status) {
        ProbeStatus.DONE -> {
            val names = probe.sessions.joinToString(", ") { it.name }
            return names.ifEmpty { "No sessions" }
        }
        ProbeStatus.PROBING -> return "Checking..."
        ProbeStatus.FAILED -> return "Offline"
        null -> {}
    }

    return when (deviceStates[deviceId]) {
        ConnectionState.CONNECTED -> "Checking..."
        ConnectionState.CONNECTING -> "Connecting..."
        ConnectionState.RECONNECTING -> "Reconnecting..."
        ConnectionState.DISCONNECTED, ConnectionState.OFFLINE -> "Offline"
        null -> "Checking..."
    }
}
