import Foundation

@Observable
class PatternEngine {
    private(set) var activeMatch: PatternMatch?

    func applyServerPresent(_ match: PatternMatch) {
        activeMatch = match
    }

    func applyServerUpdate(_ match: PatternMatch) {
        guard activeMatch?.id == match.id else {
            return
        }
        activeMatch = match
    }

    func applyServerGone(instanceId: String) {
        guard activeMatch?.id == instanceId else {
            return
        }
        activeMatch = nil
    }

    func resolveLocally(instanceId: String) {
        guard activeMatch?.id == instanceId else {
            return
        }
        activeMatch = nil
    }

    func reset() {
        activeMatch = nil
    }
}
