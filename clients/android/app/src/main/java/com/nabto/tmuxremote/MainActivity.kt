package com.nabto.tmuxremote

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import com.nabto.tmuxremote.ui.navigation.AppNavigation
import com.nabto.tmuxremote.ui.theme.AppTheme
import com.nabto.tmuxremote.ui.theme.TmuxRemoteTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        val app = application as TmuxRemoteApp

        setContent {
            val prefs = remember {
                getSharedPreferences("tmuxremote_settings", MODE_PRIVATE)
            }
            val theme = remember {
                try {
                    AppTheme.valueOf(prefs.getString("theme", "DARK") ?: "DARK")
                } catch (_: Exception) {
                    AppTheme.DARK
                }
            }

            TmuxRemoteTheme(appTheme = theme) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = Color.Black
                ) {
                    AppNavigation(
                        nabtoService = app.nabtoService,
                        connectionManager = app.connectionManager,
                        bookmarkStore = app.bookmarkStore
                    )
                }
            }
        }
    }
}
