import Foundation

struct DeviceBookmark: Codable, Identifiable {
    var id: String { deviceId }
    var productId: String
    var deviceId: String
    var fingerprint: String
    var sct: String
    var name: String
    var agentName: String?
    var lastSession: String?
    var lastConnected: Date?

    /// Display name: user override (if set) takes priority, then agent name, then device ID.
    var displayName: String {
        if name != deviceId { return name }
        return agentName ?? deviceId
    }

    enum CodingKeys: String, CodingKey {
        case productId = "product_id"
        case deviceId = "device_id"
        case fingerprint = "device_fingerprint"
        case sct
        case name
        case agentName = "agent_name"
        case lastSession = "last_session"
        case lastConnected = "last_connected"
    }
}
