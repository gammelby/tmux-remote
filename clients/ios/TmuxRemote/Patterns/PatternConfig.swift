import Foundation

enum PatternType: String, Codable {
    case yesNo = "yes_no"
    case numberedMenu = "numbered_menu"
    case acceptReject = "accept_reject"
}

struct PatternAction: Codable, Equatable {
    let label: String
    let keys: String
}

struct PatternActionTemplate: Codable, Equatable {
    let keys: String
}

struct PatternDefinition: Codable, Identifiable {
    let id: String
    let type: PatternType
    let promptRegex: String
    let optionRegex: String?
    let actions: [PatternAction]?
    let actionTemplate: PatternActionTemplate?
    let maxScanLines: Int?

    enum CodingKeys: String, CodingKey {
        case id
        case type
        case promptRegex = "prompt_regex"
        case optionRegex = "option_regex"
        case actions
        case actionTemplate = "action_template"
        case maxScanLines = "max_scan_lines"
    }
}

struct AgentConfig: Codable {
    let name: String
    let rules: [PatternDefinition]
}

struct PatternConfig: Codable {
    let version: Int
    let agents: [String: AgentConfig]
}
