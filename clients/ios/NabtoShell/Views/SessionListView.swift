import SwiftUI

struct SessionListView: View {
    let bookmark: DeviceBookmark
    let nabtoService: NabtoService
    let bookmarkStore: BookmarkStore

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
            } else if let errorMessage {
                VStack(spacing: 16) {
                    Image(systemName: "wifi.slash")
                        .font(.system(size: 48))
                        .foregroundColor(.secondary)
                    Text(errorMessage)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
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
                bookmarkStore: bookmarkStore
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
            .tint(.primary)
        }
    }

    private func loadSessions() async {
        isLoading = true
        errorMessage = nil

        do {
            if nabtoService.connectionState != .connected {
                try await nabtoService.connect(bookmark: bookmark)
            }
            sessions = try await nabtoService.listSessions()
            isLoading = false
        } catch {
            errorMessage = "Device unreachable. Check that the agent is running."
            isLoading = false
        }
    }

    private func loadAndAutoAttach() async {
        await loadSessions()

        // Auto-attach if single session
        if sessions.count == 1, errorMessage == nil {
            selectedSession = sessions[0].name
        }
    }

    private func createAndAttach(name: String) async {
        do {
            if nabtoService.connectionState != .connected {
                try await nabtoService.connect(bookmark: bookmark)
            }
            try await nabtoService.createSession(name: name, cols: 80, rows: 24)
            selectedSession = name
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}

extension String: @retroactive Identifiable {
    public var id: String { self }
}
