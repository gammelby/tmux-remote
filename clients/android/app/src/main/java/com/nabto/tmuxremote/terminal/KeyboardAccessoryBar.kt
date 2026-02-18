package com.nabto.tmuxremote.terminal

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Shared Ctrl modifier state between the accessory bar and terminal input.
 */
class CtrlModifierState {
    var isActive = false
        private set
    var isLocked = false
        private set
    var onStateChanged: ((Boolean) -> Unit)? = null

    fun activate() {
        isActive = true
        isLocked = false
        onStateChanged?.invoke(true)
    }

    fun lock() {
        isActive = true
        isLocked = true
        onStateChanged?.invoke(true)
    }

    fun deactivate() {
        isActive = false
        isLocked = false
        onStateChanged?.invoke(false)
    }

    /** Apply Ctrl modifier to a byte if active. Returns the (possibly modified) data. */
    fun apply(data: ByteArray): ByteArray {
        if (!isActive || data.isEmpty()) return data
        val first = data[0].toInt() and 0xFF
        if (first < 0x40 || first > 0x7F) return data
        val modified = byteArrayOf((first and 0x1F).toByte())
        if (!isLocked) {
            deactivate()
        }
        return modified
    }
}

@Composable
fun KeyboardAccessoryBar(
    modifier: Modifier = Modifier,
    onSend: (ByteArray) -> Unit
) {
    val ctrlState = remember { CtrlModifierState() }
    var ctrlActive by remember { mutableStateOf(false) }
    var lastCtrlTap by remember { mutableStateOf(0L) }

    ctrlState.onStateChanged = { active ->
        ctrlActive = active
    }

    fun sendBytes(bytes: ByteArray) {
        val data = ctrlState.apply(bytes)
        onSend(data)
    }

    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(44.dp)
            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.95f))
            .padding(horizontal = 4.dp, vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        val buttonModifier = Modifier.weight(1f)
        val buttonColors = ButtonDefaults.buttonColors(
            containerColor = MaterialTheme.colorScheme.secondaryContainer,
            contentColor = MaterialTheme.colorScheme.onSecondaryContainer
        )
        val ctrlColors = if (ctrlActive) {
            ButtonDefaults.buttonColors(
                containerColor = Color(0xFF10B981),
                contentColor = Color.White
            )
        } else {
            buttonColors
        }
        val shape = RoundedCornerShape(6.dp)

        Button(
            onClick = { sendBytes(byteArrayOf(0x1B)) },
            modifier = buttonModifier,
            colors = buttonColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("Esc", fontSize = 13.sp) }

        Button(
            onClick = { sendBytes(byteArrayOf(0x09)) },
            modifier = buttonModifier,
            colors = buttonColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("Tab", fontSize = 13.sp) }

        Button(
            onClick = {
                val now = System.currentTimeMillis()
                val elapsed = now - lastCtrlTap
                lastCtrlTap = now
                when {
                    ctrlState.isActive && !ctrlState.isLocked && elapsed < 400 -> ctrlState.lock()
                    ctrlState.isActive -> ctrlState.deactivate()
                    else -> ctrlState.activate()
                }
            },
            modifier = buttonModifier,
            colors = ctrlColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("Ctrl", fontSize = 13.sp) }

        // Arrow keys
        Button(
            onClick = { sendBytes(byteArrayOf(0x1B, 0x5B, 0x41)) },
            modifier = buttonModifier,
            colors = buttonColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("\u25B2", fontSize = 13.sp) }

        Button(
            onClick = { sendBytes(byteArrayOf(0x1B, 0x5B, 0x42)) },
            modifier = buttonModifier,
            colors = buttonColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("\u25BC", fontSize = 13.sp) }

        Button(
            onClick = { sendBytes(byteArrayOf(0x1B, 0x5B, 0x44)) },
            modifier = buttonModifier,
            colors = buttonColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("\u25C0", fontSize = 13.sp) }

        Button(
            onClick = { sendBytes(byteArrayOf(0x1B, 0x5B, 0x43)) },
            modifier = buttonModifier,
            colors = buttonColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("\u25B6", fontSize = 13.sp) }

        Button(
            onClick = { sendBytes(byteArrayOf(0x7C)) },
            modifier = buttonModifier,
            colors = buttonColors,
            shape = shape,
            contentPadding = ButtonDefaults.TextButtonContentPadding
        ) { Text("|", fontSize = 13.sp) }
    }
}
