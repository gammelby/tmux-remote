import Foundation
import SwiftCBOR

/// Decoded control stream event from agent.
enum ControlStreamEvent {
    case sessions([SessionInfo])
    case patternMatch(PatternMatch)
    case patternDismiss
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

    /// Encode a pattern dismiss as a length-prefixed CBOR message
    /// for sending back to the agent on the control stream.
    static func encodePatternDismiss() -> Data {
        let cbor: CBOR = .map([
            .utf8String("type"): .utf8String("pattern_dismiss")
        ])
        let payload = Data(cbor.encode())
        var lengthPrefix = Data(count: 4)
        let len = UInt32(payload.count).bigEndian
        withUnsafeBytes(of: len) { lengthPrefix.replaceSubrange(0..<4, with: $0) }
        return lengthPrefix + payload
    }

    // MARK: - Decoding

    /// Decode a control stream message.
    /// Format: {"type":"sessions","sessions":[{name, cols, rows, attached}, ...]}
    static func decodeControlMessage(from data: Data) -> [SessionInfo] {
        guard let decoded = try? CBOR.decode([UInt8](data)) else { return [] }
        guard case .map(let outerMap) = decoded else { return [] }
        guard case .utf8String("sessions") = outerMap[.utf8String("type")] else { return [] }
        guard case .array(let items) = outerMap[.utf8String("sessions")] else { return [] }
        return decodeSessionArray(items)
    }

    /// Decode a control stream event: sessions update, pattern match, or pattern dismiss.
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
        case "pattern_match":
            return decodePatternMatch(outerMap)
        case "pattern_dismiss":
            return .patternDismiss
        default:
            return nil
        }
    }

    private static func decodePatternMatch(_ map: [CBOR: CBOR]) -> ControlStreamEvent? {
        guard case .utf8String(let patternId) = map[.utf8String("pattern_id")] else { return nil }
        guard case .utf8String(let typeStr) = map[.utf8String("pattern_type")] else { return nil }

        let patternType: PatternType
        switch typeStr {
        case "yes_no": patternType = .yesNo
        case "numbered_menu": patternType = .numberedMenu
        case "accept_reject": patternType = .acceptReject
        default: return nil
        }

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

        guard !actions.isEmpty else { return nil }

        let match = PatternMatch(
            id: patternId,
            patternType: patternType,
            prompt: prompt,
            matchedText: "",
            actions: actions,
            matchPosition: 0
        )
        return .patternMatch(match)
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
