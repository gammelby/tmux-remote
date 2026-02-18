package com.nabto.tmuxremote.ui.theme

import android.app.Activity
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

// Theme colors matching iOS
val TmuxAccent = Color(0xFF10B981)
val TmuxDestructive = Color(0xFFEF4444)
val TmuxSurface = Color(0xFF1E293B)
val TmuxOnline = Color(0xFF10B981)
val TmuxOffline = Color(0xFFEF4444)

private val DarkColorScheme = darkColorScheme(
    primary = TmuxAccent,
    secondary = TmuxAccent,
    tertiary = TmuxAccent,
    error = TmuxDestructive,
    surface = TmuxSurface,
    background = Color.Black,
    onPrimary = Color.White,
    onSecondary = Color.White,
    onBackground = Color.White,
    onSurface = Color.White
)

private val LightColorScheme = lightColorScheme(
    primary = TmuxAccent,
    secondary = TmuxAccent,
    tertiary = TmuxAccent,
    error = TmuxDestructive,
    onPrimary = Color.White,
    onSecondary = Color.White
)

enum class AppTheme {
    DARK, LIGHT, SYSTEM;

    val displayName: String
        get() = when (this) {
            DARK -> "Dark"
            LIGHT -> "Light"
            SYSTEM -> "System"
        }
}

@Composable
fun TmuxRemoteTheme(
    appTheme: AppTheme = AppTheme.DARK,
    content: @Composable () -> Unit
) {
    val darkTheme = when (appTheme) {
        AppTheme.DARK -> true
        AppTheme.LIGHT -> false
        AppTheme.SYSTEM -> isSystemInDarkTheme()
    }

    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme

    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            window.statusBarColor = Color.Black.toArgb()
            window.navigationBarColor = Color.Black.toArgb()
            WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = !darkTheme
        }
    }

    MaterialTheme(
        colorScheme = colorScheme,
        content = content
    )
}
