import Foundation

struct SessionInfo: Codable, Identifiable, Equatable {
    var id: String { name }
    var name: String
    var cols: Int
    var rows: Int
    var attached: Int
}
