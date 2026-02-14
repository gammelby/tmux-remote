import Foundation

@Observable
class BookmarkStore {
    private static let devicesKey = "tmuxremote_devices"
    private static let lastDeviceKey = "tmuxremote_last_device"

    private let defaults: UserDefaults

    private(set) var devices: [DeviceBookmark] = []

    var lastDeviceId: String? {
        get { defaults.string(forKey: Self.lastDeviceKey) }
        set { defaults.set(newValue, forKey: Self.lastDeviceKey) }
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        load()
    }

    func load() {
        guard let data = defaults.data(forKey: Self.devicesKey) else {
            devices = []
            return
        }

        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        devices = (try? decoder.decode([DeviceBookmark].self, from: data)) ?? []
    }

    func save() {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        guard let data = try? encoder.encode(devices) else { return }
        defaults.set(data, forKey: Self.devicesKey)
    }

    func addDevice(_ bookmark: DeviceBookmark) {
        if let index = devices.firstIndex(where: { $0.deviceId == bookmark.deviceId }) {
            devices[index] = bookmark
        } else {
            devices.append(bookmark)
        }
        save()
    }

    func removeDevice(id: String) {
        devices.removeAll { $0.deviceId == id }
        if lastDeviceId == id {
            lastDeviceId = nil
        }
        save()
    }

    func updateLastSession(deviceId: String, session: String) {
        guard let index = devices.firstIndex(where: { $0.deviceId == deviceId }) else { return }
        devices[index].lastSession = session
        devices[index].lastConnected = Date()
        lastDeviceId = deviceId
        save()
    }

    func clearLastSession(deviceId: String) {
        guard let index = devices.firstIndex(where: { $0.deviceId == deviceId }) else { return }
        devices[index].lastSession = nil
        if lastDeviceId == deviceId {
            lastDeviceId = nil
        }
        save()
    }

    func bookmark(for deviceId: String) -> DeviceBookmark? {
        devices.first { $0.deviceId == deviceId }
    }
}
