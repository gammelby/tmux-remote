import Foundation

struct PairingInfo {
    var productId: String
    var deviceId: String
    var username: String
    var password: String
    var sct: String

    /// Parse a pairing string: p=<product>,d=<device>,u=<user>,pwd=<pass>,sct=<token>
    static func parse(_ string: String) -> PairingInfo? {
        let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }

        var fields: [String: String] = [:]
        for part in trimmed.split(separator: ",") {
            let kv = part.split(separator: "=", maxSplits: 1)
            guard kv.count == 2 else { continue }
            fields[String(kv[0])] = String(kv[1])
        }

        guard let productId = fields["p"],
              let deviceId = fields["d"],
              let password = fields["pwd"],
              let sct = fields["sct"] else {
            return nil
        }
        let username = fields["u"] ?? "owner"
        guard !username.isEmpty else { return nil }

        return PairingInfo(
            productId: productId,
            deviceId: deviceId,
            username: username,
            password: password,
            sct: sct
        )
    }
}
