import Foundation

/// Manages pattern overlay state for a terminal session.
/// In production, the agent pushes match/dismiss events via the control stream.
/// This class tracks the active match, agent selection, and user dismiss debounce.
@Observable
class PatternEngine {
    private(set) var activeMatch: PatternMatch?
    private(set) var lastMatch: PatternMatch?
    /// True when the active match was restored via recall (agent already knows about the dismiss).
    private(set) var isRecalledMatch = false
    var activeAgent: String?
    #if DEBUG
    func testSetUserDismissTime(_ date: Date?) { userDismissTime = date }
    #endif

    private var config: PatternConfig?
    private var userDismissTime: Date?
    private var deviceId: String?

    func loadConfig(_ config: PatternConfig) {
        self.config = config
    }

    /// Set the device ID and restore persisted agent selection.
    func setDeviceId(_ id: String) {
        deviceId = id
        if let saved = UserDefaults.standard.string(forKey: "patternAgent_\(id)") {
            selectAgent(saved)
        }
    }

    func selectAgent(_ agentId: String?) {
        activeAgent = agentId
        activeMatch = nil
        lastMatch = nil
        // Persist selection
        if let deviceId = deviceId {
            UserDefaults.standard.set(agentId, forKey: "patternAgent_\(deviceId)")
        }
    }

    /// Apply a server-pushed pattern match from the agent control stream.
    /// Respects the agent pill and user dismiss debounce.
    func applyServerMatch(_ match: PatternMatch) {
        let debounceAge = userDismissTime.map { Date().timeIntervalSince($0) }
        AppLog.log("applyServerMatch: id=%@, activeAgent=%@, currentMatch=%@, debounceAge=%@",
                   match.id, activeAgent ?? "nil", activeMatch?.id ?? "nil",
                   debounceAge.map { String(format: "%.2fs", $0) } ?? "nil")
        guard activeAgent != nil else {
            AppLog.log("applyServerMatch: IGNORED (no activeAgent)")
            return
        }
        // After user dismiss, suppress server matches for 2 seconds to ride
        // out agent-side oscillation (auto-dismiss then immediate re-match).
        // A genuinely new prompt arrives seconds later.
        if let age = debounceAge, age < 2.0 {
            AppLog.log("applyServerMatch: IGNORED (user dismissed %.1fs ago)", age)
            return
        }
        userDismissTime = nil
        lastMatch = nil
        isRecalledMatch = false
        activeMatch = match
        AppLog.log("applyServerMatch: activeMatch now=%@", activeMatch?.id ?? "nil")
    }

    /// Apply a server-pushed pattern dismiss from the agent control stream.
    /// Does NOT clear lastMatch: the user should still be able to recall
    /// what they dismissed even after the agent confirms the prompt is gone.
    func applyServerDismiss() {
        AppLog.log("applyServerDismiss: currentMatch=%@, lastMatch=%@",
                   activeMatch?.id ?? "nil", lastMatch?.id ?? "nil")
        activeMatch = nil
    }

    /// User tapped "Dismiss" without choosing an action. Save match for recall.
    func dismiss() {
        AppLog.log("dismiss: user dismiss, currentMatch=%@, recalled=%d",
                   activeMatch?.id ?? "nil", isRecalledMatch ? 1 : 0)
        lastMatch = activeMatch
        userDismissTime = Date()
        activeMatch = nil
        isRecalledMatch = false
    }

    /// User chose an action. Clear the match without saving for recall,
    /// and without debouncing future server matches.
    func consume() {
        AppLog.log("consume: user acted, currentMatch=%@", activeMatch?.id ?? "nil")
        activeMatch = nil
        lastMatch = nil
        isRecalledMatch = false
    }

    /// Restore the last dismissed match to the active overlay.
    func recall() {
        guard let match = lastMatch else { return }
        AppLog.log("recall: restoring match=%@", match.id)
        activeMatch = match
        lastMatch = nil
        isRecalledMatch = true
    }

    func reset() {
        activeMatch = nil
        lastMatch = nil
        isRecalledMatch = false
        userDismissTime = nil
    }

    /// Available agent IDs from the loaded config.
    var availableAgents: [(id: String, name: String)] {
        guard let config = config else { return [] }
        return config.agents.map { (id: $0.key, name: $0.value.name) }
            .sorted { $0.name < $1.name }
    }
}
