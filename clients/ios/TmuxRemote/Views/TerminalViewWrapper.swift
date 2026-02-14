import SwiftUI
import SwiftTerm

/// Bridge between TerminalScreen and the UIKit TerminalView.
/// Shared by value in SwiftUI but holds a reference to the coordinator.
class TerminalBridge {
    weak var coordinator: TerminalViewWrapper.Coordinator?

    func feed(bytes: [UInt8]) {
        coordinator?.feed(bytes: bytes)
    }

    func becomeFirstResponder() {
        coordinator?.becomeFirstResponder()
    }
}

struct TerminalViewWrapper: UIViewRepresentable {
    let bridge: TerminalBridge
    let onSend: (Data) -> Void
    let onSizeChanged: (Int, Int) -> Void
    var onReady: (() -> Void)?

    #if DEBUG
    private static let isStubMode = ProcessInfo.processInfo.arguments.contains("--stub-terminal")
    #endif

    func makeUIView(context: Context) -> UIView {
        #if DEBUG
        if Self.isStubMode {
            // Use a plain UIView in stub mode to avoid SwiftTerm's cursor blink
            // timer, which prevents XCUITest quiescence and causes 120s stalls.
            let stub = UIView(frame: .zero)
            stub.backgroundColor = .black
            bridge.coordinator = context.coordinator
            DispatchQueue.main.async { onReady?() }
            return stub
        }
        #endif

        let tv = TerminalView(frame: .zero)
        tv.terminalDelegate = context.coordinator
        tv.backgroundColor = .black
        tv.nativeBackgroundColor = .black
        tv.nativeForegroundColor = .white

        // Keyboard accessory bar with shared Ctrl state
        let ctrlState = context.coordinator.ctrlState
        let accessory = KeyboardAccessoryView(ctrlState: ctrlState) { data in
            context.coordinator.sendFromAccessory(data)
        }
        tv.inputAccessoryView = accessory

        context.coordinator.terminalView = tv
        bridge.coordinator = context.coordinator

        DispatchQueue.main.async {
            onReady?()
        }
        return tv
    }

    func updateUIView(_ uiView: UIView, context: Context) {}

    func makeCoordinator() -> Coordinator {
        Coordinator(onSend: onSend, onSizeChanged: onSizeChanged)
    }

    class Coordinator: NSObject, TerminalViewDelegate {
        let onSend: (Data) -> Void
        let onSizeChanged: (Int, Int) -> Void
        let ctrlState = CtrlModifierState()
        weak var terminalView: TerminalView?

        init(onSend: @escaping (Data) -> Void, onSizeChanged: @escaping (Int, Int) -> Void) {
            self.onSend = onSend
            self.onSizeChanged = onSizeChanged
        }

        func feed(bytes: [UInt8]) {
            terminalView?.feed(byteArray: ArraySlice(bytes))
        }

        func becomeFirstResponder() {
            terminalView?.becomeFirstResponder()
        }

        func sendFromAccessory(_ data: Data) {
            onSend(data)
        }

        // MARK: - TerminalViewDelegate

        func send(source: TerminalView, data: ArraySlice<UInt8>) {
            let raw = Data(data)
            let modified = ctrlState.apply(to: raw)
            onSend(modified)
        }

        func scrolled(source: TerminalView, position: Double) {}

        func setTerminalTitle(source: TerminalView, title: String) {}

        func sizeChanged(source: TerminalView, newCols: Int, newRows: Int) {
            onSizeChanged(newCols, newRows)
        }

        func clipboardCopy(source: TerminalView, content: Data) {
            if let str = String(data: content, encoding: .utf8) {
                UIPasteboard.general.string = str
            }
        }

        func rangeChanged(source: TerminalView, startY: Int, endY: Int) {}

        func hostCurrentDirectoryUpdate(source: TerminalView, directory: String?) {}

        func requestOpenLink(source: TerminalView, link: String, params: [String: String]) {
            if let url = URL(string: link) {
                UIApplication.shared.open(url)
            }
        }

        func bell(source: TerminalView) {}

        func iTermContent(source: TerminalView, content: ArraySlice<UInt8>) {}
    }
}
