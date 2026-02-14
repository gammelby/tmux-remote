import SwiftUI

// MARK: - App Theme

enum AppTheme: String, CaseIterable {
    case dark
    case light
    case system

    var colorScheme: ColorScheme? {
        switch self {
        case .dark: return .dark
        case .light: return .light
        case .system: return nil
        }
    }

    var displayName: String {
        switch self {
        case .dark: return "Dark"
        case .light: return "Light"
        case .system: return "System"
        }
    }

    var iconName: String {
        switch self {
        case .dark: return "moon.fill"
        case .light: return "sun.max.fill"
        case .system: return "circle.lefthalf.filled"
        }
    }
}

// MARK: - Color Hex Initializer

extension Color {
    init(hex: UInt, opacity: Double = 1.0) {
        self.init(
            .sRGB,
            red: Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >> 8) & 0xFF) / 255,
            blue: Double(hex & 0xFF) / 255,
            opacity: opacity
        )
    }
}

// MARK: - Theme Colors

extension Color {
    static let tmuxAccent = Color(hex: 0x10B981)
    static let tmuxDestructive = Color(hex: 0xEF4444)
    static let tmuxSurface = Color(hex: 0x1E293B)
    static let tmuxOnline = Color(hex: 0x10B981)
    static let tmuxOffline = Color(hex: 0xEF4444)
}
