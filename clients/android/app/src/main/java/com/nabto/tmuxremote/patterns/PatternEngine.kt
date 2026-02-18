package com.nabto.tmuxremote.patterns

import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class PatternEngine(
    private val minimumVisibleDuration: Double = 1.0,
    private val resolveSuppressionDuration: Double = 1.5,
    private val goneSuppressionDuration: Double = 1.5,
    private val now: () -> Long = { System.currentTimeMillis() }
) {
    private val _activeMatch = MutableStateFlow<PatternMatch?>(null)
    val activeMatch: StateFlow<PatternMatch?> = _activeMatch

    private val _isOverlayHidden = MutableStateFlow(false)
    val isOverlayHidden: StateFlow<Boolean> = _isOverlayHidden

    val visibleMatch: PatternMatch?
        get() = if (_isOverlayHidden.value) null else _activeMatch.value

    val canRestoreHiddenMatch: Boolean
        get() = _isOverlayHidden.value && _activeMatch.value != null

    private var activeSince: Long? = null
    private var resolvedPromptSignature: String? = null
    private var resolvedAt: Long? = null
    private var goneSuppressedInstanceId: String? = null
    private var goneSuppressedAt: Long? = null
    private var pendingGoneRunnable: Runnable? = null
    private val handler = Handler(Looper.getMainLooper())

    fun applyServerPresent(match: PatternMatch) {
        if (shouldSuppressRePresentAfterGone(match.id)) return
        if (shouldSuppressResolvedPrompt(match)) return
        cancelPendingGoneClear()
        clearResolveSuppression()
        _activeMatch.value = match
        _isOverlayHidden.value = false
        activeSince = now()
    }

    fun applyServerUpdate(match: PatternMatch) {
        if (_activeMatch.value?.id != match.id) return
        cancelPendingGoneClear()
        _activeMatch.value = match
        if (activeSince == null) {
            activeSince = now()
        }
    }

    fun applyServerGone(instanceId: String) {
        if (_activeMatch.value?.id != instanceId) return
        rememberGoneSuppression(instanceId)

        val since = activeSince
        val elapsed = if (since != null) (now() - since) / 1000.0 else minimumVisibleDuration
        val remaining = minimumVisibleDuration - elapsed
        if (remaining > 0) {
            scheduleGoneClear(instanceId, remaining)
            return
        }
        clearActiveState()
    }

    fun dismissLocally(instanceId: String) {
        if (_activeMatch.value?.id != instanceId) return
        _isOverlayHidden.value = true
    }

    fun restoreOverlay() {
        if (_activeMatch.value == null) return
        _isOverlayHidden.value = false
    }

    fun resolveLocally(instanceId: String) {
        val match = _activeMatch.value ?: return
        if (match.id != instanceId) return
        resolvedPromptSignature = promptSignature(match)
        resolvedAt = now()
        rememberGoneSuppression(instanceId)
        clearActiveState()
    }

    fun reset() {
        clearResolveSuppression()
        clearGoneSuppression()
        clearActiveState()
    }

    private fun clearActiveState() {
        cancelPendingGoneClear()
        _activeMatch.value = null
        _isOverlayHidden.value = false
        activeSince = null
    }

    private fun clearResolveSuppression() {
        resolvedPromptSignature = null
        resolvedAt = null
    }

    private fun clearGoneSuppression() {
        goneSuppressedInstanceId = null
        goneSuppressedAt = null
    }

    private fun rememberGoneSuppression(instanceId: String) {
        goneSuppressedInstanceId = instanceId
        goneSuppressedAt = now()
    }

    private fun shouldSuppressResolvedPrompt(match: PatternMatch): Boolean {
        val signature = resolvedPromptSignature ?: return false
        val resolvedTime = resolvedAt ?: return false
        val elapsed = (now() - resolvedTime) / 1000.0
        if (elapsed > resolveSuppressionDuration) {
            clearResolveSuppression()
            return false
        }
        return signature == promptSignature(match)
    }

    private fun shouldSuppressRePresentAfterGone(instanceId: String): Boolean {
        val suppressed = goneSuppressedInstanceId ?: return false
        val goneTime = goneSuppressedAt ?: return false
        val elapsed = (now() - goneTime) / 1000.0
        if (elapsed > goneSuppressionDuration) {
            clearGoneSuppression()
            return false
        }
        return suppressed == instanceId
    }

    private fun scheduleGoneClear(instanceId: String, delaySeconds: Double) {
        cancelPendingGoneClear()
        val runnable = Runnable {
            pendingGoneRunnable = null
            if (_activeMatch.value?.id != instanceId) return@Runnable
            clearActiveState()
        }
        pendingGoneRunnable = runnable
        handler.postDelayed(runnable, (delaySeconds * 1000).toLong())
    }

    private fun cancelPendingGoneClear() {
        pendingGoneRunnable?.let { handler.removeCallbacks(it) }
        pendingGoneRunnable = null
    }

    private fun promptSignature(match: PatternMatch): String {
        val prompt = (match.prompt ?: "").trim().lowercase()
        return "${match.patternType.value}|$prompt"
    }
}
