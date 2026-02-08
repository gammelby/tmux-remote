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

    func makeUIView(context: Context) -> TerminalView {
        let tv = TerminalView(frame: .zero)
        tv.terminalDelegate = context.coordinator
        tv.backgroundColor = .black
        tv.nativeBackgroundColor = .black
        tv.nativeForegroundColor = .white

        // Keyboard accessory bar
        let accessory = KeyboardAccessoryView { data in
            context.coordinator.sendFromAccessory(data)
        }
        tv.inputAccessoryView = accessory

        context.coordinator.terminalView = tv
        bridge.coordinator = context.coordinator
        return tv
    }

    func updateUIView(_ uiView: TerminalView, context: Context) {}

    func makeCoordinator() -> Coordinator {
        Coordinator(onSend: onSend, onSizeChanged: onSizeChanged)
    }

    class Coordinator: NSObject, TerminalViewDelegate {
        let onSend: (Data) -> Void
        let onSizeChanged: (Int, Int) -> Void
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
            onSend(Data(data))
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
