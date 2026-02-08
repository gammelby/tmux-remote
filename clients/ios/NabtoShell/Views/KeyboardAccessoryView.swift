import UIKit

class KeyboardAccessoryView: UIView {
    private var ctrlActive = false
    private var ctrlLocked = false
    private var ctrlButton: UIButton?
    private var lastCtrlTap: Date = .distantPast
    private let onSend: (Data) -> Void

    init(onSend: @escaping (Data) -> Void) {
        self.onSend = onSend
        super.init(frame: CGRect(x: 0, y: 0, width: UIScreen.main.bounds.width, height: 44))
        autoresizingMask = .flexibleWidth
        backgroundColor = UIColor.systemBackground.withAlphaComponent(0.95)
        setupButtons()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var intrinsicContentSize: CGSize {
        CGSize(width: UIView.noIntrinsicMetric, height: 44)
    }

    private func setupButtons() {
        let stack = UIStackView()
        stack.axis = .horizontal
        stack.distribution = .fillEqually
        stack.spacing = 4
        stack.translatesAutoresizingMaskIntoConstraints = false

        let keys: [(String, Selector)] = [
            ("Esc", #selector(tapEsc)),
            ("Tab", #selector(tapTab)),
            ("Ctrl", #selector(tapCtrl)),
            ("\u{25B2}", #selector(tapUp)),
            ("\u{25BC}", #selector(tapDown)),
            ("\u{25C0}", #selector(tapLeft)),
            ("\u{25B6}", #selector(tapRight)),
            ("|", #selector(tapPipe))
        ]

        for (title, action) in keys {
            let button = UIButton(type: .system)
            button.setTitle(title, for: .normal)
            button.titleLabel?.font = UIFont.systemFont(ofSize: 15, weight: .medium)
            button.addTarget(self, action: action, for: .touchUpInside)
            button.layer.cornerRadius = 6
            button.backgroundColor = UIColor.secondarySystemBackground
            stack.addArrangedSubview(button)

            if title == "Ctrl" {
                ctrlButton = button
            }
        }

        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 4),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -4),
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 4),
            stack.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -4)
        ])
    }

    private func sendBytes(_ bytes: [UInt8]) {
        if ctrlActive && !ctrlLocked {
            // Ctrl modifier: send byte & 0x1f for the first byte if it's a printable char
            if let first = bytes.first, first >= 0x40 && first <= 0x7F {
                onSend(Data([first & 0x1F]))
            } else {
                onSend(Data(bytes))
            }
            deactivateCtrl()
        } else if ctrlActive && ctrlLocked {
            if let first = bytes.first, first >= 0x40 && first <= 0x7F {
                onSend(Data([first & 0x1F]))
            } else {
                onSend(Data(bytes))
            }
        } else {
            onSend(Data(bytes))
        }
    }

    private func deactivateCtrl() {
        ctrlActive = false
        ctrlLocked = false
        ctrlButton?.backgroundColor = UIColor.secondarySystemBackground
        ctrlButton?.tintColor = .systemBlue
    }

    private func activateCtrl() {
        ctrlActive = true
        ctrlButton?.backgroundColor = UIColor.systemBlue
        ctrlButton?.tintColor = .white
    }

    private func lockCtrl() {
        ctrlActive = true
        ctrlLocked = true
        ctrlButton?.backgroundColor = UIColor.systemBlue
        ctrlButton?.tintColor = .white
    }

    // MARK: - Key Actions

    @objc private func tapEsc() {
        sendBytes([0x1B])
    }

    @objc private func tapTab() {
        sendBytes([0x09])
    }

    @objc private func tapCtrl() {
        let now = Date()
        let elapsed = now.timeIntervalSince(lastCtrlTap)
        lastCtrlTap = now

        if ctrlActive && !ctrlLocked && elapsed < 0.4 {
            // Double-tap: lock ctrl
            lockCtrl()
        } else if ctrlActive {
            // Already active: deactivate
            deactivateCtrl()
        } else {
            // Activate for one key
            activateCtrl()
        }
    }

    @objc private func tapUp() {
        sendBytes([0x1B, 0x5B, 0x41])  // \x1b[A
    }

    @objc private func tapDown() {
        sendBytes([0x1B, 0x5B, 0x42])  // \x1b[B
    }

    @objc private func tapLeft() {
        sendBytes([0x1B, 0x5B, 0x44])  // \x1b[D
    }

    @objc private func tapRight() {
        sendBytes([0x1B, 0x5B, 0x43])  // \x1b[C
    }

    @objc private func tapPipe() {
        sendBytes([0x7C])  // |
    }
}
