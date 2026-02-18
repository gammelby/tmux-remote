package com.nabto.tmuxremote.terminal

import com.termux.terminal.TerminalEmulator
import com.termux.terminal.TerminalOutput
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient

/**
 * A TerminalOutput that bridges the Termux terminal emulator to Nabto stream I/O.
 *
 * Instead of spawning a local PTY process (like TerminalSession does),
 * this class sends keyboard input to a Nabto data stream via a callback,
 * and receives PTY output via the feed() method.
 */
class NabtoTerminalSession(
    cols: Int,
    rows: Int,
    private val onWrite: (ByteArray) -> Unit
) : TerminalOutput() {

    private val sessionClient = NoOpTerminalSessionClient()

    val emulator: TerminalEmulator = TerminalEmulator(this, cols, rows, null, sessionClient)

    /** Called by TerminalEmulator when the user types (keyboard input). */
    override fun write(data: ByteArray, offset: Int, count: Int) {
        val bytes = data.copyOfRange(offset, offset + count)
        onWrite(bytes)
    }

    /** Called by TerminalEmulator when the terminal title changes. */
    override fun titleChanged(oldTitle: String?, newTitle: String?) {}

    override fun onCopyTextToClipboard(text: String?) {}

    override fun onPasteTextFromClipboard() {}

    override fun onBell() {}

    override fun onColorsChanged() {}

    /** Feed raw PTY output bytes from the Nabto stream into the emulator. */
    fun feed(data: ByteArray) {
        emulator.append(data, data.size)
    }

    /** Update the emulator's terminal size. */
    fun resize(cols: Int, rows: Int) {
        emulator.resize(cols, rows)
    }

    /** Get the emulator's title (set by terminal escape sequences). */
    val title: String?
        get() = emulator.title
}

/**
 * Minimal TerminalSessionClient that does nothing.
 * Required by TerminalEmulator constructor.
 */
class NoOpTerminalSessionClient : TerminalSessionClient {
    override fun onTextChanged(changedSession: TerminalSession) {}
    override fun onTitleChanged(changedSession: TerminalSession) {}
    override fun onSessionFinished(finishedSession: TerminalSession) {}
    override fun onCopyTextToClipboard(session: TerminalSession, text: String?) {}
    override fun onPasteTextFromClipboard(session: TerminalSession?) {}
    override fun onBell(session: TerminalSession) {}
    override fun onColorsChanged(session: TerminalSession) {}
    override fun onTerminalCursorStateChange(state: Boolean) {}
    override fun getTerminalCursorStyle(): Int? = null
    override fun logError(tag: String?, message: String?) {}
    override fun logWarn(tag: String?, message: String?) {}
    override fun logInfo(tag: String?, message: String?) {}
    override fun logDebug(tag: String?, message: String?) {}
    override fun logVerbose(tag: String?, message: String?) {}
    override fun logStackTraceWithMessage(tag: String?, message: String?, e: Exception?) {}
    override fun logStackTrace(tag: String?, e: Exception?) {}
}
