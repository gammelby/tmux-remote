#if DEBUG
import Foundation

/// Script that defines a sequence of pattern events for stub testing.
/// Each event is delivered to ConnectionManager.onPatternEvent, matching
/// what the agent control stream would push in production.
struct StubScript: Codable {
    struct PatternEvent: Codable {
        let type: String           // pattern_present, pattern_update, pattern_gone
        let delay: Double          // seconds before delivering this event
        let instanceId: String?
        let patternId: String?
        let patternType: String?
        let prompt: String?
        let revision: Int?
        let actions: [Action]?

        struct Action: Codable {
            let label: String
            let keys: String
        }

        enum CodingKeys: String, CodingKey {
            case type, delay, prompt, revision, actions
            case instanceId = "instance_id"
            case patternId = "pattern_id"
            case patternType = "pattern_type"
        }
    }

    let events: [PatternEvent]
}
#endif
