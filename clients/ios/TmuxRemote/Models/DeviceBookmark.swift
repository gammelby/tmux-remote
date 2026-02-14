import Foundation

struct DeviceBookmark: Codable, Identifiable {
    var id: String { deviceId }
    var productId: String
    var deviceId: String
    var fingerprint: String
    var sct: String
    var name: String
    var lastSession: String?
    var lastConnected: Date?

    enum CodingKeys: String, CodingKey {
        case productId = "product_id"
        case deviceId = "device_id"
        case fingerprint = "device_fingerprint"
        case sct
        case name
        case lastSession = "last_session"
        case lastConnected = "last_connected"
    }
}
