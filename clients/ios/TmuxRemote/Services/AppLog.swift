import Foundation

enum AppLog {
    static func log(_ format: String, _ args: CVarArg...) {
        withVaList(args) { NSLogv("[Pattern] " + format, $0) }
    }
}
