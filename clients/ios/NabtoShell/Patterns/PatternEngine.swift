import Foundation

@Observable
class PatternEngine {
    private(set) var activeMatch: PatternMatch?
    private(set) var isOverlayHidden = false
    private let minimumVisibleDuration: TimeInterval
    private let now: () -> Date
    private var activeSince: Date?

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
         now: @escaping () -> Date = Date.init)
    {
        self.minimumVisibleDuration = minimumVisibleDuration
        self.now = now
    }

    func applyServerPresent(_ match: PatternMatch) {
        activeMatch = match
        isOverlayHidden = false
        activeSince = now()
    }

    func applyServerUpdate(_ match: PatternMatch) {
        guard activeMatch?.id == match.id else {
            return
        }
        activeMatch = match
        if activeSince == nil {
            activeSince = now()
        }
    }

    func applyServerGone(instanceId: String) {
        guard activeMatch?.id == instanceId else {
            return
        }

        if let since = activeSince,
           now().timeIntervalSince(since) < minimumVisibleDuration {
            return
        }

        clearState()
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
        guard activeMatch?.id == instanceId else {
            return
        }
        clearState()
    }

    func reset() {
        clearState()
    }

    private func clearState() {
        activeMatch = nil
        isOverlayHidden = false
        activeSince = nil
    }
}
