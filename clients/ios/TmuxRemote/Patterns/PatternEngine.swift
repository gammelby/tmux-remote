import Foundation

@Observable
class PatternEngine {
    private(set) var activeMatch: PatternMatch?
    private(set) var isOverlayHidden = false
    private let minimumVisibleDuration: TimeInterval
    private let resolveSuppressionDuration: TimeInterval
    private let goneSuppressionDuration: TimeInterval
    private let now: () -> Date
    private var activeSince: Date?
    private var resolvedPromptSignature: String?
    private var resolvedAt: Date?
    private var goneSuppressedInstanceId: String?
    private var goneSuppressedAt: Date?
    private var pendingGoneWorkItem: DispatchWorkItem?

    var visibleMatch: PatternMatch? {
        if isOverlayHidden {
            return nil
        }
        return activeMatch
    }

    var canRestoreHiddenMatch: Bool {
        return isOverlayHidden && activeMatch != nil
    }

    init(minimumVisibleDuration: TimeInterval = 1.0,
         resolveSuppressionDuration: TimeInterval = 1.5,
         now: @escaping () -> Date = Date.init)
    {
        self.minimumVisibleDuration = minimumVisibleDuration
        self.resolveSuppressionDuration = resolveSuppressionDuration
        self.goneSuppressionDuration = resolveSuppressionDuration
        self.now = now
    }

    func applyServerPresent(_ match: PatternMatch) {
        if shouldSuppressRePresentAfterGone(instanceId: match.id) {
            return
        }
        if shouldSuppressResolvedPrompt(match) {
            return
        }
        cancelPendingGoneClear()
        clearResolveSuppression()
        activeMatch = match
        isOverlayHidden = false
        activeSince = now()
    }

    func applyServerUpdate(_ match: PatternMatch) {
        guard activeMatch?.id == match.id else {
            return
        }
        cancelPendingGoneClear()
        activeMatch = match
        if activeSince == nil {
            activeSince = now()
        }
    }

    func applyServerGone(instanceId: String) {
        guard activeMatch?.id == instanceId else {
            return
        }
        rememberGoneSuppression(instanceId: instanceId)

        let elapsed = activeSince.map { now().timeIntervalSince($0) } ?? minimumVisibleDuration
        let remaining = minimumVisibleDuration - elapsed
        if remaining > 0 {
            scheduleGoneClear(instanceId: instanceId, after: remaining)
            return
        }

        clearActiveState()
    }

    func dismissLocally(instanceId: String) {
        guard activeMatch?.id == instanceId else {
            return
        }
        isOverlayHidden = true
    }

    func restoreOverlay() {
        guard activeMatch != nil else {
            return
        }
        isOverlayHidden = false
    }

    func resolveLocally(instanceId: String) {
        guard let match = activeMatch, match.id == instanceId else {
            return
        }
        resolvedPromptSignature = promptSignature(match)
        resolvedAt = now()
        rememberGoneSuppression(instanceId: instanceId)
        clearActiveState()
    }

    func reset() {
        clearResolveSuppression()
        clearGoneSuppression()
        clearActiveState()
    }

    private func clearActiveState() {
        cancelPendingGoneClear()
        activeMatch = nil
        isOverlayHidden = false
        activeSince = nil
    }

    private func clearResolveSuppression() {
        resolvedPromptSignature = nil
        resolvedAt = nil
    }

    private func clearGoneSuppression() {
        goneSuppressedInstanceId = nil
        goneSuppressedAt = nil
    }

    private func rememberGoneSuppression(instanceId: String) {
        goneSuppressedInstanceId = instanceId
        goneSuppressedAt = now()
    }

    private func shouldSuppressResolvedPrompt(_ match: PatternMatch) -> Bool {
        guard let signature = resolvedPromptSignature, let resolvedAt else {
            return false
        }
        if now().timeIntervalSince(resolvedAt) > resolveSuppressionDuration {
            clearResolveSuppression()
            return false
        }
        return signature == promptSignature(match)
    }

    private func shouldSuppressRePresentAfterGone(instanceId: String) -> Bool {
        guard let suppressed = goneSuppressedInstanceId, let goneAt = goneSuppressedAt else {
            return false
        }
        if now().timeIntervalSince(goneAt) > goneSuppressionDuration {
            clearGoneSuppression()
            return false
        }
        return suppressed == instanceId
    }

    private func scheduleGoneClear(instanceId: String, after delay: TimeInterval) {
        cancelPendingGoneClear()
        let workItem = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.pendingGoneWorkItem = nil
            guard self.activeMatch?.id == instanceId else {
                return
            }
            self.clearActiveState()
        }
        pendingGoneWorkItem = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: workItem)
    }

    private func cancelPendingGoneClear() {
        pendingGoneWorkItem?.cancel()
        pendingGoneWorkItem = nil
    }

    private func promptSignature(_ match: PatternMatch) -> String {
        let prompt = (match.prompt ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .lowercased()
        return "\(match.patternType.rawValue)|\(prompt)"
    }
}
