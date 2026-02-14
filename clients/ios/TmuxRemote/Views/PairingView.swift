import SwiftUI

struct PairingView: View {
    let nabtoService: NabtoService
    let bookmarkStore: BookmarkStore

    @Environment(\.dismiss) private var dismiss
    @State private var pairingString = ""
    @State private var isPairing = false
    @State private var errorMessage: String?
    @State private var showAdvanced = false

    // Advanced fields
    @State private var productId = ""
    @State private var deviceId = ""
    @State private var username = ""
    @State private var password = ""
    @State private var sct = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    HStack {
                        TextField("Pairing string", text: $pairingString, axis: .vertical)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()

                        Button("Paste") {
                            if let clip = UIPasteboard.general.string {
                                pairingString = clip
                            }
                        }
                        .buttonStyle(.bordered)
                    }
                } header: {
                    Text("Paste the pairing string from the agent")
                } footer: {
                    Text("Format: p=<product>,d=<device>,u=<user>,pwd=<pass>,sct=<token>")
                        .font(.caption2)
                }

                DisclosureGroup("Manual Entry", isExpanded: $showAdvanced) {
                    TextField("Product ID", text: $productId)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    TextField("Device ID", text: $deviceId)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    TextField("Username", text: $username)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    SecureField("Password", text: $password)
                    TextField("Server Connect Token", text: $sct)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                }

                if let errorMessage {
                    Section {
                        Text(errorMessage)
                            .foregroundColor(.tmuxDestructive)
                            .font(.callout)
                    }
                }

                Section {
                    Button {
                        Task { await doPairing() }
                    } label: {
                        if isPairing {
                            HStack {
                                ProgressView()
                                Text("Pairing...")
                            }
                        } else {
                            Text("Pair")
                                .frame(maxWidth: .infinity)
                        }
                    }
                    .disabled(!canPair || isPairing)
                }
            }
            .navigationTitle("Add Device")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
    }

    private var canPair: Bool {
        if showAdvanced {
            return !productId.isEmpty && !deviceId.isEmpty && !username.isEmpty &&
                !password.isEmpty && !sct.isEmpty
        }
        return !pairingString.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    private func doPairing() async {
        errorMessage = nil
        isPairing = true
        defer { isPairing = false }

        let info: PairingInfo

        if showAdvanced && !productId.isEmpty {
            info = PairingInfo(
                productId: productId,
                deviceId: deviceId,
                username: username,
                password: password,
                sct: sct
            )
        } else {
            guard let parsed = PairingInfo.parse(pairingString) else {
                errorMessage = "Invalid pairing string. Check the format and try again."
                return
            }
            info = parsed
        }

        // Check if already paired
        if bookmarkStore.bookmark(for: info.deviceId) != nil {
            errorMessage = "Already paired with this device."
            return
        }

        do {
            let bookmark = try await nabtoService.pair(info: info)
            bookmarkStore.addDevice(bookmark)
            dismiss()
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}
