import Foundation
import SwiftCBOR

/// Decoded control stream event from agent.
enum ControlStreamEvent {
    case sessions([SessionInfo])
    case patternPresent(PatternMatch)
    case patternUpdate(PatternMatch)
    case patternGone(String)
}

enum CBORHelpers {

    // MARK: - Encoding

    static func encodeAttach(session: String, cols: Int, rows: Int) -> Data {
        let cbor: CBOR = .map([
            .utf8String("session"): .utf8String(session),
            .utf8String("cols"): .unsignedInt(UInt64(cols)),
            .utf8String("rows"): .unsignedInt(UInt64(rows))
        ])
        return Data(cbor.encode())
    }

    static func encodeCreate(session: String, cols: Int, rows: Int, command: String? = nil) -> Data {
        var map: [CBOR: CBOR] = [
            .utf8String("session"): .utf8String(session),
            .utf8String("cols"): .unsignedInt(UInt64(cols)),
            .utf8String("rows"): .unsignedInt(UInt64(rows))
        ]
        if let command = command {
            map[.utf8String("command")] = .utf8String(command)
        }
        let cbor: CBOR = .map(map)
        return Data(cbor.encode())
    }

    static func encodeResize(cols: Int, rows: Int) -> Data {
        let cbor: CBOR = .map([
            .utf8String("cols"): .unsignedInt(UInt64(cols)),
            .utf8String("rows"): .unsignedInt(UInt64(rows))
        ])
        return Data(cbor.encode())
    }

    static func encodePatternResolve(instanceId: String, decision: String, keys: String?) -> Data {
        var map: [CBOR: CBOR] = [
            .utf8String("type"): .utf8String("pattern_resolve"),
            .utf8String("instance_id"): .utf8String(instanceId),
            .utf8String("decision"): .utf8String(decision)
        ]
        if let keys {
            map[.utf8String("keys")] = .utf8String(keys)
        }

        let payload = Data(CBOR.map(map).encode())
        var lengthPrefix = Data(count: 4)
        let len = UInt32(payload.count).bigEndian
        withUnsafeBytes(of: len) { lengthPrefix.replaceSubrange(0..<4, with: $0) }
        return lengthPrefix + payload
    }

    // MARK: - Decoding

    static func decodeControlMessage(from data: Data) -> [SessionInfo] {
        guard let decoded = try? CBOR.decode([UInt8](data)) else { return [] }
        guard case .map(let outerMap) = decoded else { return [] }
        guard case .utf8String("sessions") = outerMap[.utf8String("type")] else { return [] }
        guard case .array(let items) = outerMap[.utf8String("sessions")] else { return [] }
        return decodeSessionArray(items)
    }

    static func decodeControlStreamEvent(from data: Data) -> ControlStreamEvent? {
        guard let decoded = try? CBOR.decode([UInt8](data)) else { return nil }
        guard case .map(let outerMap) = decoded else { return nil }

        guard case .utf8String(let typeStr) = outerMap[.utf8String("type")] else {
            return nil
        }

        switch typeStr {
        case "sessions":
            if case .array(let items) = outerMap[.utf8String("sessions")] {
                return .sessions(decodeSessionArray(items))
            }
            return nil
        case "pattern_present":
            guard let match = decodePatternInstance(outerMap) else { return nil }
            return .patternPresent(match)
        case "pattern_update":
            guard let match = decodePatternInstance(outerMap) else { return nil }
            return .patternUpdate(match)
        case "pattern_gone":
            guard case .utf8String(let instanceId) = outerMap[.utf8String("instance_id")] else {
                return nil
            }
            return .patternGone(instanceId)
        default:
            return nil
        }
    }

    private static func decodePatternType(_ value: CBOR?) -> PatternType? {
        guard case .utf8String(let typeStr) = value else { return nil }
        switch typeStr {
        case "yes_no": return .yesNo
        case "numbered_menu": return .numberedMenu
        case "accept_reject": return .acceptReject
        default: return nil
        }
    }

    private static func decodePatternInstance(_ map: [CBOR: CBOR]) -> PatternMatch? {
        guard case .utf8String(let instanceId) = map[.utf8String("instance_id")] else { return nil }
        guard case .utf8String(let patternId) = map[.utf8String("pattern_id")] else { return nil }
        guard let patternType = decodePatternType(map[.utf8String("pattern_type")]) else { return nil }

        let prompt: String?
        if case .utf8String(let p) = map[.utf8String("prompt")] {
            prompt = p
        } else {
            prompt = nil
        }

        var actions: [ResolvedAction] = []
        if case .array(let items) = map[.utf8String("actions")] {
            for item in items {
                guard case .map(let actionMap) = item else { continue }
                guard case .utf8String(let label) = actionMap[.utf8String("label")] else { continue }
                guard case .utf8String(let keys) = actionMap[.utf8String("keys")] else { continue }
                actions.append(ResolvedAction(label: label, keys: keys))
            }
        }

        let revision: Int
        if case .unsignedInt(let value) = map[.utf8String("revision")] {
            revision = Int(value)
        } else {
            revision = 1
        }

        guard !actions.isEmpty else { return nil }

        return PatternMatch(
            id: instanceId,
            patternId: patternId,
            patternType: patternType,
            prompt: prompt,
            actions: actions,
            revision: revision
        )
    }

    struct AgentStatus {
        let version: String
        let friendlyName: String?
    }

    static func decodeStatus(from data: Data) -> AgentStatus? {
        guard let decoded = try? CBOR.decode([UInt8](data)) else { return nil }
        guard case .map(let map) = decoded else { return nil }

        let version: String
        if case .utf8String(let v) = map[.utf8String("version")] {
            version = v
        } else {
            return nil
        }

        let friendlyName: String?
        if case .utf8String(let n) = map[.utf8String("friendly_name")] {
            friendlyName = n
        } else {
            friendlyName = nil
        }

        return AgentStatus(version: version, friendlyName: friendlyName)
    }

    static func decodeSessions(from data: Data) -> [SessionInfo] {
        guard let decoded = try? CBOR.decode([UInt8](data)) else { return [] }
        guard case .array(let items) = decoded else { return [] }
        return decodeSessionArray(items)
    }

    private static func decodeSessionArray(_ items: [CBOR]) -> [SessionInfo] {
        var sessions: [SessionInfo] = []
        for item in items {
            guard case .map(let map) = item else { continue }

            let name: String
            if case .utf8String(let s) = map[.utf8String("name")] {
                name = s
            } else {
                continue
            }

            let cols: Int
            if case .unsignedInt(let v) = map[.utf8String("cols")] {
                cols = Int(v)
            } else {
                cols = 0
            }

            let rows: Int
            if case .unsignedInt(let v) = map[.utf8String("rows")] {
                rows = Int(v)
            } else {
                rows = 0
            }

            let attached: Int
            if case .unsignedInt(let v) = map[.utf8String("attached")] {
                attached = Int(v)
            } else {
                attached = 0
            }

            sessions.append(SessionInfo(name: name, cols: cols, rows: rows, attached: attached))
        }
        return sessions
    }
}
