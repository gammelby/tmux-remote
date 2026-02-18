package com.nabto.tmuxremote.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Divider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.nabto.tmuxremote.patterns.PatternMatch
import com.nabto.tmuxremote.patterns.PatternType
import com.nabto.tmuxremote.patterns.ResolvedAction
import com.nabto.tmuxremote.ui.theme.TmuxAccent
import com.nabto.tmuxremote.ui.theme.TmuxDestructive
import com.nabto.tmuxremote.ui.theme.TmuxSurface

@Composable
fun PatternOverlaySheet(
    match: PatternMatch,
    isDarkTheme: Boolean = true,
    onAction: (ResolvedAction) -> Unit,
    onDismiss: () -> Unit
) {
    val backgroundColor = if (isDarkTheme) TmuxSurface else MaterialTheme.colorScheme.surface
    val shape = RoundedCornerShape(14.dp)

    Column(
        modifier = Modifier
            .padding(horizontal = 40.dp, vertical = 8.dp)
            .shadow(10.dp, shape)
            .clip(shape)
            .background(backgroundColor),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Drag handle
        Box(
            modifier = Modifier
                .padding(top = 8.dp, bottom = 4.dp)
                .width(36.dp)
                .height(5.dp)
                .clip(RoundedCornerShape(2.5.dp))
                .background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f))
        )

        // Prompt header
        match.prompt?.let { prompt ->
            Text(
                text = prompt,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 20.dp, vertical = 16.dp)
            )
            Divider()
        }

        // Action buttons
        when (match.patternType) {
            PatternType.YES_NO, PatternType.ACCEPT_REJECT -> {
                BinaryActions(actions = match.actions, onAction = onAction)
            }
            PatternType.NUMBERED_MENU -> {
                MenuActions(actions = match.actions, onAction = onAction)
            }
        }

        Divider()

        // Dismiss button
        Text(
            text = "Dismiss",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
            textAlign = TextAlign.Center,
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onDismiss() }
                .padding(vertical = 14.dp)
        )
    }
}

@Composable
private fun BinaryActions(
    actions: List<ResolvedAction>,
    onAction: (ResolvedAction) -> Unit
) {
    Column {
        actions.forEachIndexed { index, action ->
            if (index > 0) Divider()
            Text(
                text = action.label,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = if (index == 0) FontWeight.SemiBold else FontWeight.Normal,
                color = actionColor(action.label, index == 0),
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { onAction(action) }
                    .padding(vertical = 14.dp)
            )
        }
    }
}

@Composable
private fun MenuActions(
    actions: List<ResolvedAction>,
    onAction: (ResolvedAction) -> Unit
) {
    val maxVisible = 5
    if (actions.size > maxVisible) {
        LazyColumn(
            modifier = Modifier.height((maxVisible * 48).dp)
        ) {
            itemsIndexed(actions) { index, action ->
                if (index > 0) Divider()
                MenuItemRow(action = action, onClick = { onAction(action) })
            }
        }
    } else {
        Column {
            actions.forEachIndexed { index, action ->
                if (index > 0) Divider()
                MenuItemRow(action = action, onClick = { onAction(action) })
            }
        }
    }
}

@Composable
private fun MenuItemRow(
    action: ResolvedAction,
    onClick: () -> Unit
) {
    Text(
        text = action.label,
        style = MaterialTheme.typography.bodyLarge,
        color = menuItemColor(action.label),
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 20.dp, vertical = 12.dp)
    )
}

private fun actionColor(label: String, isFirst: Boolean): Color {
    val lower = label.lowercase()
    if (lower == "no" || lower == "deny" || lower == "reject") {
        return TmuxDestructive
    }
    return TmuxAccent
}

private fun menuItemColor(label: String): Color {
    val lower = label.lowercase()
    if (lower == "no" || lower.startsWith("no,")) {
        return TmuxDestructive
    }
    return TmuxAccent
}
