import Foundation

struct ResolvedAction: Identifiable, Equatable {
    let id = UUID()
    let label: String
    let keys: String
}

struct PatternMatch: Identifiable, Equatable {
    let id: String              // instance_id
    let patternId: String
    let patternType: PatternType
    let prompt: String?
    let actions: [ResolvedAction]
    let revision: Int

    static func == (lhs: PatternMatch, rhs: PatternMatch) -> Bool {
        lhs.id == rhs.id && lhs.revision == rhs.revision
    }
}
