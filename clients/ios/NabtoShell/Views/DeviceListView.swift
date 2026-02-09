import SwiftUI
import NabtoEdgeClient

struct DeviceListView: View {
    let nabtoService: NabtoService
    let connectionManager: ConnectionManager
    let bookmarkStore: BookmarkStore
    @State private var showPairing = false
    @State private var deviceStatuses: [String: DeviceStatus] = [:]
    @State private var selectedDevice: DeviceBookmark?

    enum DeviceStatus {
        case unknown
        case probing
        case online([SessionInfo])
        case offline
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
        .navigationDestination(item: $selectedDevice) { device in
            SessionListView(
                bookmark: device,
                nabtoService: nabtoService,
                connectionManager: connectionManager,
                bookmarkStore: bookmarkStore,
                onDismissToDevices: { selectedDevice = nil }
            )
        }
        .task { await probeAllDevices() }
    }

    @ViewBuilder
    private var deviceList: some View {
        List {
            ForEach(bookmarkStore.devices) { device in
                Button {
                    selectedDevice = device
                } label: {
                    deviceRow(device)
                }
                .accessibilityIdentifier("device-row-\(device.deviceId)")
                .tint(.primary)
            }
            .onDelete(perform: deleteDevices)
        }
        .refreshable { await probeAllDevices() }
    }

    @ViewBuilder
    private func deviceRow(_ device: DeviceBookmark) -> some View {
        HStack {
            Circle()
                .fill(statusColor(for: device.deviceId))
                .frame(width: 10, height: 10)

            VStack(alignment: .leading, spacing: 2) {
                Text(device.name)
                    .font(.body)
                    .fontWeight(.medium)

                if case .online(let sessions) = deviceStatuses[device.deviceId] {
                    let names = sessions.map(\.name).joined(separator: ", ")
                    Text(names.isEmpty ? "No sessions" : names)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("device-status-\(device.deviceId)")
                } else if case .offline = deviceStatuses[device.deviceId] {
                    Text("Offline")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("device-status-\(device.deviceId)")
                } else {
                    Text("Checking...")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .accessibilityIdentifier("device-status-\(device.deviceId)")
                }
            }

            Spacer()

            if let lastConnected = device.lastConnected {
                Text(lastConnected, style: .relative)
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.vertical, 4)
    }

    private func statusColor(for deviceId: String) -> Color {
        switch deviceStatuses[deviceId] {
        case .online: return .green
        case .offline: return .red
        case .probing, .unknown, .none: return .gray
        }
    }

    private func deleteDevices(at offsets: IndexSet) {
        for index in offsets {
            let deviceId = bookmarkStore.devices[index].deviceId
            connectionManager.disconnect(deviceId: deviceId)
            bookmarkStore.removeDevice(id: deviceId)
        }
    }

    private func probeAllDevices() async {
        NSLog("[PROBE] probeAllDevices starting with %d devices", bookmarkStore.devices.count)
        for device in bookmarkStore.devices {
            deviceStatuses[device.deviceId] = .probing
        }

        await withTaskGroup(of: (String, DeviceStatus).self) { group in
            for device in bookmarkStore.devices {
                group.addTask {
                    do {
                        NSLog("[PROBE] %@ calling connection(for:)", device.deviceId)
                        let conn = try await connectionManager.connection(for: device)
                        NSLog("[PROBE] %@ got connection, creating CoAP request", device.deviceId)
                        let coap = try conn.createCoapRequest(method: "GET", path: "/terminal/sessions")
                        NSLog("[PROBE] %@ executing CoAP", device.deviceId)
                        let response = try await coap.executeAsync()
                        NSLog("[PROBE] %@ CoAP response status: %d", device.deviceId, response.status)
                        guard response.status == 205, let payload = response.payload else {
                            return (device.deviceId, .online([]))
                        }
                        let sessions = CBORHelpers.decodeSessions(from: payload)
                        NSLog("[PROBE] %@ found %d sessions", device.deviceId, sessions.count)
                        return (device.deviceId, .online(sessions))
                    } catch {
                        NSLog("[PROBE] %@ error: %@", device.deviceId, "\(error)")
                        return (device.deviceId, .offline)
                    }
                }
            }

            for await (deviceId, status) in group {
                NSLog("[PROBE] setting status for %@", deviceId)
                deviceStatuses[deviceId] = status
            }
            NSLog("[PROBE] all probes complete")
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
