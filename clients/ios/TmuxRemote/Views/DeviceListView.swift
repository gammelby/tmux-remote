import SwiftUI
import NabtoEdgeClient

struct TerminalTarget: Hashable, Identifiable {
    let bookmark: DeviceBookmark
    let session: String
    var id: String { "\(bookmark.deviceId)/\(session)" }
}

struct DeviceListView: View {
    let nabtoService: NabtoService
    let connectionManager: ConnectionManager
    let bookmarkStore: BookmarkStore
    @State private var showPairing = false
    @State private var expandedDevices: Set<String> = []
    @State private var selectedTerminal: TerminalTarget?
    @State private var showNewSessionAlert = false
    @State private var newSessionName = ""
    @State private var newSessionDevice: DeviceBookmark?
    @State private var probeStatuses: [String: ProbeStatus] = [:]

    /// Tracks devices where we fell back to CoAP probing (old agent without control stream).
    private enum ProbeStatus {
        case probing
        case done([SessionInfo])
        case failed
    }

    var body: some View {
        Group {
            if bookmarkStore.devices.isEmpty {
                WelcomeView(showPairing: $showPairing)
            } else {
                deviceList
            }
        }
        .navigationTitle("Devices")
        .toolbar {
            ToolbarItem(placement: .navigationBarTrailing) {
                Button {
                    showPairing = true
                } label: {
                    Image(systemName: "plus")
                }
            }
        }
        .sheet(isPresented: $showPairing) {
            PairingView(nabtoService: nabtoService, bookmarkStore: bookmarkStore)
        }
        .navigationDestination(item: $selectedTerminal) { target in
            TerminalScreen(
                bookmark: target.bookmark,
                sessionName: target.session,
                nabtoService: nabtoService,
                connectionManager: connectionManager,
                bookmarkStore: bookmarkStore,
                onDismiss: { selectedTerminal = nil }
            )
        }
        .alert("New Session", isPresented: $showNewSessionAlert) {
            TextField("Session name", text: $newSessionName)
            Button("Create") {
                let name = newSessionName.isEmpty ? "ns-\(Int.random(in: 1000...9999))" : newSessionName
                if let device = newSessionDevice {
                    Task { await createAndAttach(device: device, name: name) }
                }
                newSessionName = ""
                newSessionDevice = nil
            }
            Button("Cancel", role: .cancel) {
                newSessionName = ""
                newSessionDevice = nil
            }
        }
        .task {
            if let lastId = bookmarkStore.lastDeviceId {
                expandedDevices.insert(lastId)
            }
            await connectAllDevices()
        }
    }

    @ViewBuilder
    private var deviceList: some View {
        List {
            ForEach(bookmarkStore.devices) { device in
                deviceSection(device)
            }
            .onDelete(perform: deleteDevices)
        }
        .refreshable {
            probeStatuses.removeAll()
            for device in bookmarkStore.devices {
                connectionManager.disconnect(deviceId: device.deviceId)
            }
            await connectAllDevices()
        }
    }

    @ViewBuilder
    private func deviceSection(_ device: DeviceBookmark) -> some View {
        let isExpanded = expandedDevices.contains(device.deviceId)

        Button {
            withAnimation {
                if isExpanded {
                    expandedDevices.remove(device.deviceId)
                } else {
                    expandedDevices.insert(device.deviceId)
                }
            }
        } label: {
            deviceRow(device, expanded: isExpanded)
        }
        .accessibilityIdentifier("device-row-\(device.deviceId)")
        .tint(.primary)

        if isExpanded {
            expandedContent(for: device)
        }
    }

    @ViewBuilder
    private func deviceRow(_ device: DeviceBookmark, expanded: Bool) -> some View {
        HStack {
            Circle()
                .fill(statusColor(for: device.deviceId))
                .frame(width: 10, height: 10)

            VStack(alignment: .leading, spacing: 2) {
                Text(device.name)
                    .font(.body)
                    .fontWeight(.medium)

                let status = deviceStatus(for: device)
                switch status {
                case .online(let sessions):
                    let names = sessions.map(\.name).joined(separator: ", ")
                    Text(names.isEmpty ? "No sessions" : names)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("device-status-\(device.deviceId)")
                case .offline:
                    Text("Offline")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("device-status-\(device.deviceId)")
                case .connecting, .unknown:
                    Text("Checking...")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("device-status-\(device.deviceId)")
                }
            }

            Spacer()

            Image(systemName: "chevron.right")
                .font(.caption)
                .foregroundColor(.secondary)
                .rotationEffect(.degrees(expanded ? 90 : 0))
                .animation(.easeInOut(duration: 0.2), value: expanded)
        }
        .padding(.vertical, 4)
    }

    @ViewBuilder
    private func expandedContent(for device: DeviceBookmark) -> some View {
        let status = deviceStatus(for: device)
        switch status {
        case .online(let sessions):
            if sessions.isEmpty {
                Text("No tmux sessions")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .padding(.leading, 24)
                    .accessibilityIdentifier("sessions-empty-\(device.deviceId)")
            } else {
                ForEach(sessions) { session in
                    Button {
                        selectedTerminal = TerminalTarget(bookmark: device, session: session.name)
                    } label: {
                        sessionRow(session)
                    }
                    .accessibilityIdentifier("session-row-\(session.name)")
                    .tint(.primary)
                    .padding(.leading, 24)
                }
            }

            Button {
                newSessionDevice = device
                showNewSessionAlert = true
            } label: {
                Label("New Session", systemImage: "plus.circle")
                    .font(.subheadline)
                    .foregroundColor(.accentColor)
            }
            .accessibilityIdentifier("new-session-\(device.deviceId)")
            .padding(.leading, 24)

        case .offline:
            Text("Device unreachable")
                .font(.caption)
                .foregroundColor(.secondary)
                .padding(.leading, 24)

        case .connecting, .unknown:
            HStack {
                ProgressView()
                    .scaleEffect(0.8)
                Text("Loading sessions...")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            .padding(.leading, 24)
        }
    }

    @ViewBuilder
    private func sessionRow(_ session: SessionInfo) -> some View {
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

    // MARK: - Status Derivation

    private enum DeviceStatus {
        case unknown
        case connecting
        case online([SessionInfo])
        case offline
    }

    private func deviceStatus(for device: DeviceBookmark) -> DeviceStatus {
        let deviceId = device.deviceId

        // Primary: live control stream data
        if let sessions = connectionManager.deviceSessions[deviceId] {
            return .online(sessions)
        }

        // Fallback: CoAP probe results (for old agents without control stream)
        if let probe = probeStatuses[deviceId] {
            switch probe {
            case .probing: return .connecting
            case .done(let sessions): return .online(sessions)
            case .failed: return .offline
            }
        }

        // Connection state
        switch connectionManager.deviceStates[deviceId] {
        case .connected:
            return .connecting  // connected but no control stream data yet
        case .connecting:
            return .connecting
        case .disconnected, .offline:
            return .offline
        case .reconnecting:
            return .connecting
        case nil:
            return .unknown
        }
    }

    private func statusColor(for deviceId: String) -> Color {
        if connectionManager.deviceSessions[deviceId] != nil {
            return .green
        }
        if case .done = probeStatuses[deviceId] {
            return .green
        }
        switch connectionManager.deviceStates[deviceId] {
        case .connected: return .green
        case .disconnected, .offline: return .red
        default: return .gray
        }
    }

    // MARK: - Actions

    private func deleteDevices(at offsets: IndexSet) {
        for index in offsets {
            let deviceId = bookmarkStore.devices[index].deviceId
            expandedDevices.remove(deviceId)
            connectionManager.disconnect(deviceId: deviceId)
            bookmarkStore.removeDevice(id: deviceId)
        }
    }

    private func connectAllDevices() async {
        for device in bookmarkStore.devices {
            Task {
                do {
                    let conn = try await connectionManager.connection(for: device)

                    // If control stream hasn't delivered data within 3s, fall back to CoAP probe.
                    // This handles old agents without control stream support.
                    try await Task.sleep(nanoseconds: 3_000_000_000)
                    if connectionManager.deviceSessions[device.deviceId] == nil {
                        probeStatuses[device.deviceId] = .probing
                        do {
                            let sessions = try await connectionManager.listSessionsCoAP(
                                on: conn, timeoutNanoseconds: 5_000_000_000)
                            probeStatuses[device.deviceId] = .done(sessions)
                        } catch {
                            probeStatuses[device.deviceId] = .failed
                        }
                    }
                } catch {
                    // Connection failed; deviceStates will reflect .disconnected
                }
            }
        }
    }

    private func createAndAttach(device: DeviceBookmark, name: String) async {
        do {
            try await nabtoService.createSession(bookmark: device, name: name, cols: 80, rows: 24)
            // Control stream will push the updated session list automatically.
            // Wait briefly for it, then navigate.
            try? await Task.sleep(nanoseconds: 500_000_000)
            selectedTerminal = TerminalTarget(bookmark: device, session: name)
        } catch {
            // Session creation failed; control stream or next poll will update status.
        }
    }
}

extension DeviceBookmark: Hashable {
    static func == (lhs: DeviceBookmark, rhs: DeviceBookmark) -> Bool {
        lhs.deviceId == rhs.deviceId
    }

    func hash(into hasher: inout Hasher) {
        hasher.combine(deviceId)
    }
}
