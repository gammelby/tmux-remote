import SwiftUI

struct SessionListView: View {
    let bookmark: DeviceBookmark
    let nabtoService: NabtoService
    let connectionManager: ConnectionManager
    let bookmarkStore: BookmarkStore
    let onDismissToDevices: () -> Void

    @State private var sessions: [SessionInfo] = []
    @State private var isLoading = true
    @State private var errorMessage: String?
    @State private var selectedSession: String?
    @State private var showNewSessionAlert = false
    @State private var newSessionName = ""

    var body: some View {
        Group {
            if isLoading {
                ProgressView("Loading sessions...")
                    .accessibilityIdentifier("sessions-loading")
            } else if let errorMessage {
                VStack(spacing: 16) {
                    Image(systemName: "wifi.slash")
                        .font(.system(size: 48))
                        .foregroundColor(.secondary)
                    Text(errorMessage)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                        .accessibilityIdentifier("sessions-error")
                    Button("Retry") {
                        Task { await loadSessions() }
                    }
                    .buttonStyle(.bordered)
                }
                .padding()
            } else if sessions.isEmpty {
                VStack(spacing: 16) {
                    Image(systemName: "rectangle.on.rectangle.slash")
                        .font(.system(size: 48))
                        .foregroundColor(.secondary)
                    Text("No tmux sessions running on this device.")
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                        .accessibilityIdentifier("sessions-empty")
                }
                .padding()
            } else {
                sessionList
            }
        }
        .navigationTitle(bookmark.name)
        .toolbar {
            ToolbarItem(placement: .navigationBarTrailing) {
                Button {
                    showNewSessionAlert = true
                } label: {
                    Label("New Session", systemImage: "plus")
                }
            }
        }
        .navigationDestination(item: $selectedSession) { session in
            TerminalScreen(
                bookmark: bookmark,
                sessionName: session,
                nabtoService: nabtoService,
                connectionManager: connectionManager,
                bookmarkStore: bookmarkStore,
                onDismiss: { onDismissToDevices() }
            )
        }
        .alert("New Session", isPresented: $showNewSessionAlert) {
            TextField("Session name", text: $newSessionName)
            Button("Create") {
                let name = newSessionName.isEmpty ? "ns-\(Int.random(in: 1000...9999))" : newSessionName
                Task { await createAndAttach(name: name) }
                newSessionName = ""
            }
            Button("Cancel", role: .cancel) {
                newSessionName = ""
            }
        }
        .task { await loadAndAutoAttach() }
        .refreshable { await loadSessions() }
    }

    @ViewBuilder
    private var sessionList: some View {
        List(sessions) { session in
            Button {
                selectedSession = session.name
            } label: {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(session.name)
                            .font(.body)
                            .fontWeight(.medium)
                        Text("\(session.cols)x\(session.rows)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    if session.attached > 0 {
                        Text("attached")
                            .font(.caption2)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .background(Color.blue.opacity(0.15))
                            .foregroundColor(.blue)
                            .clipShape(Capsule())
                    }
                }
                .padding(.vertical, 4)
            }
            .accessibilityIdentifier("session-row-\(session.name)")
            .tint(.primary)
        }
    }

    private func loadSessions() async {
        isLoading = true
        errorMessage = nil

        do {
            sessions = try await nabtoService.listSessions(bookmark: bookmark)
            isLoading = false
        } catch {
            errorMessage = "Device unreachable. Check that the agent is running."
            isLoading = false
        }
    }

    private func loadAndAutoAttach() async {
        await loadSessions()

        if sessions.count == 1, errorMessage == nil {
            selectedSession = sessions[0].name
        }
    }

    private func createAndAttach(name: String) async {
        do {
            try await nabtoService.createSession(bookmark: bookmark, name: name, cols: 80, rows: 24)
            selectedSession = name
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}

extension String: @retroactive Identifiable {
    public var id: String { self }
}
