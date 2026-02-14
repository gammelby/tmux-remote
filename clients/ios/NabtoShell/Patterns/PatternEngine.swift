import Foundation

@Observable
class PatternEngine {
    private(set) var activeMatch: PatternMatch?
    private let minimumVisibleDuration: TimeInterval
    private let now: () -> Date
    private var activeSince: Date?

    init(minimumVisibleDuration: TimeInterval = 1.0,
         now: @escaping () -> Date = Date.init)
    {
        self.minimumVisibleDuration = minimumVisibleDuration
        self.now = now
    }

    func applyServerPresent(_ match: PatternMatch) {
        activeMatch = match
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

        activeMatch = nil
        activeSince = nil
    }

    func resolveLocally(instanceId: String) {
        guard activeMatch?.id == instanceId else {
            return
        }
        activeMatch = nil
        activeSince = nil
    }

    func reset() {
        activeMatch = nil
        activeSince = nil
    }
}
