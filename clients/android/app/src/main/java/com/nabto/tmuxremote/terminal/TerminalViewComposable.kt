package com.nabto.tmuxremote.terminal

import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.LinearLayout
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import com.termux.terminal.TerminalSession
import com.termux.view.TerminalView
import com.termux.view.TerminalViewClient

/**
 * Compose wrapper for the Termux TerminalView, backed by a NabtoTerminalSession.
 *
 * The Termux TerminalView requires a TerminalSession (which spawns a local process).
 * We work around this by creating a TerminalSession with /system/bin/true (exits immediately),
 * then using reflection to replace both the TerminalView's and TerminalSession's emulator
 * with our Nabto-backed one.
 */
@Composable
fun TerminalViewComposable(
    modifier: Modifier = Modifier,
    onSend: (ByteArray) -> Unit,
    onSizeChanged: (cols: Int, rows: Int) -> Unit,
    onReady: (NabtoTerminalSession) -> Unit
) {
    val context = LocalContext.current

    val sessionClient = remember { NoOpTerminalSessionClient() }

    val viewClient = remember {
        object : TerminalViewClient {
            override fun onEmulatorSet() {}
            override fun onScale(scale: Float): Float = 1.0f
            override fun onSingleTapUp(e: MotionEvent?) {}
            override fun shouldBackButtonBeMappedToEscape(): Boolean = false
            override fun shouldEnforceCharBasedInput(): Boolean = true
            override fun shouldUseCtrlSpaceWorkaround(): Boolean = false
            override fun isTerminalViewSelected(): Boolean = true
            override fun copyModeChanged(copyMode: Boolean) {}
            override fun onKeyDown(keyCode: Int, e: KeyEvent?, session: TerminalSession?): Boolean = false
            override fun onKeyUp(keyCode: Int, e: KeyEvent?): Boolean = false
            override fun onLongPress(event: MotionEvent?): Boolean = false
            override fun readControlKey(): Boolean = false
            override fun readAltKey(): Boolean = false
            override fun readShiftKey(): Boolean = false
            override fun readFnKey(): Boolean = false
            override fun onCodePoint(codePoint: Int, ctrlDown: Boolean, session: TerminalSession?): Boolean = false
            override fun logError(tag: String?, message: String?) {}
            override fun logWarn(tag: String?, message: String?) {}
            override fun logInfo(tag: String?, message: String?) {}
            override fun logDebug(tag: String?, message: String?) {}
            override fun logVerbose(tag: String?, message: String?) {}
            override fun logStackTraceWithMessage(tag: String?, message: String?, e: Exception?) {}
            override fun logStackTrace(tag: String?, e: Exception?) {}
        }
    }

    AndroidView(
        modifier = modifier,
        factory = { ctx ->
            val terminalView = TerminalView(ctx, null)
            terminalView.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.MATCH_PARENT
            )
            terminalView.setBackgroundColor(android.graphics.Color.BLACK)
            terminalView.setTerminalViewClient(viewClient)

            val nabtoSession = NabtoTerminalSession(80, 24, onWrite = onSend)

            // Create a dummy TerminalSession (the /system/bin/true process exits immediately)
            val dummySession = TerminalSession(
                "/system/bin/true",
                "/",
                arrayOf(),
                arrayOf(),
                null,
                sessionClient
            )
            terminalView.attachSession(dummySession)

            // Replace the emulator on both TerminalView and the dummy TerminalSession
            // with our Nabto-backed emulator
            try {
                val viewField = TerminalView::class.java.getDeclaredField("mEmulator")
                viewField.isAccessible = true
                viewField.set(terminalView, nabtoSession.emulator)

                val sessionField = TerminalSession::class.java.getDeclaredField("mEmulator")
                sessionField.isAccessible = true
                sessionField.set(dummySession, nabtoSession.emulator)
            } catch (_: Exception) {
                // Reflection failed; the terminal may not render correctly
            }

            terminalView.post {
                terminalView.requestFocus()
                val cols = nabtoSession.emulator.mColumns
                val rows = nabtoSession.emulator.mRows
                onSizeChanged(cols, rows)
                onReady(nabtoSession)
            }

            terminalView.setOnKeyListener { _, _, _ -> false }

            terminalView
        }
    )
}
