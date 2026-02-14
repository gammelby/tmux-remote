import Foundation

enum LaunchDestination: Equatable {
    case resumeSession(bookmark: DeviceBookmark, session: String)
    case deviceList
}

func resolveLaunchDestination(
    devices: [DeviceBookmark],
    lastDeviceId: String?
) -> LaunchDestination {
    guard let lastDeviceId = lastDeviceId,
          let bookmark = devices.first(where: { $0.deviceId == lastDeviceId }),
          let session = bookmark.lastSession else {
        return .deviceList
    }
    return .resumeSession(bookmark: bookmark, session: session)
}
