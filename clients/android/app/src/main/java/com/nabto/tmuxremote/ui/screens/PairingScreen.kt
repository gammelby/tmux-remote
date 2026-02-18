package com.nabto.tmuxremote.ui.screens

import android.content.ClipboardManager
import android.content.Context
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.nabto.tmuxremote.models.PairingInfo
import com.nabto.tmuxremote.services.BookmarkStore
import com.nabto.tmuxremote.services.NabtoService
import com.nabto.tmuxremote.ui.theme.TmuxDestructive
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PairingScreen(
    nabtoService: NabtoService,
    bookmarkStore: BookmarkStore,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var pairingString by remember { mutableStateOf("") }
    var isPairing by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var showAdvanced by remember { mutableStateOf(false) }

    // Advanced fields
    var productId by remember { mutableStateOf("") }
    var deviceId by remember { mutableStateOf("") }
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var sct by remember { mutableStateOf("") }

    val canPair = if (showAdvanced) {
        productId.isNotEmpty() && deviceId.isNotEmpty() &&
            username.isNotEmpty() && password.isNotEmpty() && sct.isNotEmpty()
    } else {
        pairingString.trim().isNotEmpty()
    }

    fun doPairing() {
        scope.launch {
            errorMessage = null
            isPairing = true

            val info: PairingInfo? = if (showAdvanced && productId.isNotEmpty()) {
                PairingInfo(productId, deviceId, username, password, sct)
            } else {
                PairingInfo.parse(pairingString)
            }

            if (info == null) {
                errorMessage = "Invalid pairing string. Check the format and try again."
                isPairing = false
                return@launch
            }

            if (bookmarkStore.bookmark(info.deviceId) != null) {
                errorMessage = "Already paired with this agent."
                isPairing = false
                return@launch
            }

            try {
                val bookmark = nabtoService.pair(info)
                bookmarkStore.addDevice(bookmark)
                isPairing = false
                onDismiss()
            } catch (e: Exception) {
                errorMessage = e.message ?: "Pairing failed"
                isPairing = false
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Add Agent") },
                navigationIcon = {
                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Default.Close, contentDescription = "Cancel")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text(
                text = "Paste the pairing string from the agent",
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(modifier = Modifier.height(8.dp))

            Row(
                verticalAlignment = Alignment.Top
            ) {
                OutlinedTextField(
                    value = pairingString,
                    onValueChange = { pairingString = it },
                    modifier = Modifier.weight(1f),
                    label = { Text("Pairing string") },
                    singleLine = false,
                    maxLines = 4
                )
                Spacer(modifier = Modifier.width(8.dp))
                IconButton(onClick = {
                    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                    pairingString = clipboard.primaryClip?.getItemAt(0)?.text?.toString() ?: ""
                }) {
                    Text("Paste")
                }
            }

            Text(
                text = "Format: p=<product>,d=<device>,u=<user>,pwd=<pass>,sct=<token>",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Manual Entry disclosure group
            TextButton(onClick = { showAdvanced = !showAdvanced }) {
                Icon(
                    if (showAdvanced) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null
                )
                Spacer(modifier = Modifier.width(4.dp))
                Text("Manual Entry")
            }

            AnimatedVisibility(visible = showAdvanced) {
                Column {
                    OutlinedTextField(
                        value = productId,
                        onValueChange = { productId = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text("Product ID") },
                        singleLine = true
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedTextField(
                        value = deviceId,
                        onValueChange = { deviceId = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text("Agent ID") },
                        singleLine = true
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedTextField(
                        value = username,
                        onValueChange = { username = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text("Username") },
                        singleLine = true
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedTextField(
                        value = password,
                        onValueChange = { password = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text("Password") },
                        singleLine = true,
                        visualTransformation = PasswordVisualTransformation(),
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password)
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedTextField(
                        value = sct,
                        onValueChange = { sct = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text("Server Connect Token") },
                        singleLine = true
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                }
            }

            errorMessage?.let { error ->
                Text(
                    text = error,
                    color = TmuxDestructive,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
            }

            Spacer(modifier = Modifier.height(16.dp))

            Button(
                onClick = { doPairing() },
                modifier = Modifier.fillMaxWidth(),
                enabled = canPair && !isPairing
            ) {
                if (isPairing) {
                    CircularProgressIndicator(
                        modifier = Modifier.height(20.dp).width(20.dp),
                        strokeWidth = 2.dp,
                        color = MaterialTheme.colorScheme.onPrimary
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Pairing...")
                } else {
                    Text("Pair")
                }
            }

            Spacer(modifier = Modifier.height(32.dp))
        }
    }
}
