import Foundation

// One virtual device as the user configures it. The UID is fixed (it identifies
// the Core Audio device the driver publishes); the board source and channel pair
// describe which RODECaster return the device feeds and are shown read-only,
// because that alignment is a property of the hardware, not a free choice.
struct DeviceSetting: Codable, Identifiable, Hashable {
    let uid: String
    let displayName: String
    let boardSource: String
    let channelPairStart: Int
    var enabled: Bool

    var id: String { uid }

    // "ch 1–2", "ch 3–4", … in the 1-based form people read on the board.
    var channelPairLabel: String {
        "ch \(channelPairStart + 1)–\(channelPairStart + 2)"
    }
}

// The persisted app configuration: which hardware output to feed and how each
// virtual device maps onto it.
struct AppConfiguration: Codable, Hashable {
    var outputDeviceUID: String
    var outputDeviceName: String
    // Off by default: hardware-timed routing is proven and needs no driver support.
    // Adaptive drift correction requires the updated driver's clock-slave property.
    var adaptiveSync: Bool
    var devices: [DeviceSetting]

    static let defaultOutputName = "RODECaster Pro II Main Multitrack"

    // The five devices the driver publishes, each named after the RODECaster Main
    // Multitrack return it feeds (see the driver's device table).
    static func makeDefault() -> AppConfiguration {
        AppConfiguration(
            outputDeviceUID: "",
            outputDeviceName: defaultOutputName,
            adaptiveSync: false,
            devices: [
                DeviceSetting(uid: "app.openfaders.device.0", displayName: "OpenFaders System",
                              boardSource: "System", channelPairStart: 0, enabled: true),
                DeviceSetting(uid: "app.openfaders.device.1", displayName: "OpenFaders Game",
                              boardSource: "Game", channelPairStart: 2, enabled: true),
                DeviceSetting(uid: "app.openfaders.device.2", displayName: "OpenFaders Music",
                              boardSource: "Music", channelPairStart: 4, enabled: true),
                DeviceSetting(uid: "app.openfaders.device.3", displayName: "OpenFaders Virtual A",
                              boardSource: "Virtual A", channelPairStart: 6, enabled: true),
                DeviceSetting(uid: "app.openfaders.device.4", displayName: "OpenFaders Virtual B",
                              boardSource: "Virtual B", channelPairStart: 8, enabled: true),
            ])
    }
}

// Persists the configuration in UserDefaults. This is throwaway UI-side state
// (the driver has its own device identity), so a plain defaults key is the right
// weight for it.
enum ConfigurationStore {
    private static let configurationKey = "configuration.v1"
    private static let routingEnabledKey = "routingEnabled.v1"

    static func loadConfiguration() -> AppConfiguration {
        guard let data = UserDefaults.standard.data(forKey: configurationKey),
              let configuration = try? JSONDecoder().decode(AppConfiguration.self, from: data)
        else {
            return .makeDefault()
        }
        return configuration
    }

    static func save(_ configuration: AppConfiguration) {
        if let data = try? JSONEncoder().encode(configuration) {
            UserDefaults.standard.set(data, forKey: configurationKey)
        }
    }

    static func loadRoutingEnabled() -> Bool {
        // Default to on so a fresh install starts routing once the hardware is present.
        (UserDefaults.standard.object(forKey: routingEnabledKey) as? Bool) ?? true
    }

    static func saveRoutingEnabled(_ enabled: Bool) {
        UserDefaults.standard.set(enabled, forKey: routingEnabledKey)
    }

    private static let seenOnboardingKey = "seenOnboarding.v1"

    static func loadHasSeenOnboarding() -> Bool {
        UserDefaults.standard.bool(forKey: seenOnboardingKey)
    }

    static func saveHasSeenOnboarding(_ seen: Bool) {
        UserDefaults.standard.set(seen, forKey: seenOnboardingKey)
    }
}
