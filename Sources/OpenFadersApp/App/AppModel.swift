import Foundation
import Observation

// The human-readable state shown in the menu-bar item's header, derived from the
// driver presence, the user's on/off intent, and the live engine status.
enum RoutingState: Equatable {
    case driverNotInstalled
    case paused
    case waitingForHardware
    case active(deviceCount: Int)

    var headline: String {
        switch self {
        case .driverNotInstalled: return "Audio driver not installed"
        case .paused: return "Routing paused"
        case .waitingForHardware: return "Waiting for the RODECaster"
        case .active(let deviceCount):
            return "Routing active · \(deviceCount) device\(deviceCount == 1 ? "" : "s")"
        }
    }

    var menuBarSymbolName: String {
        switch self {
        case .active: return "waveform"
        case .paused, .waitingForHardware: return "waveform.slash"
        case .driverNotInstalled: return "exclamationmark.triangle"
        }
    }
}

// The single source of truth the UI observes. Owns the configuration, the engine
// coordinator, and the polled runtime status, and turns user intent into engine
// start/stop calls. Main-actor isolated because it is driven entirely from the UI.
@MainActor
@Observable
final class AppModel {
    var configuration: AppConfiguration {
        didSet { ConfigurationStore.save(configuration) }
    }
    private(set) var routingEnabled: Bool
    private(set) var status: EngineRuntimeStatus = .idle
    private(set) var driverInstalled: Bool = DriverStatus.isInstalled
    private(set) var lastStartError: String?

    @ObservationIgnored private let coordinator = EngineCoordinator()
    @ObservationIgnored private var pollingTask: Task<Void, Never>?

    init() {
        configuration = ConfigurationStore.loadConfiguration()
        routingEnabled = ConfigurationStore.loadRoutingEnabled()

        resolveOutputDeviceIfNeeded()
        if routingEnabled {
            startEngine()
        }
        startPolling()
    }

    var routingState: RoutingState {
        if !driverInstalled {
            return .driverNotInstalled
        }
        if !routingEnabled {
            return .paused
        }
        if !status.running || !status.outputDevicePresent {
            return .waitingForHardware
        }
        return .active(deviceCount: status.activeRouteCount)
    }

    var enabledDeviceCount: Int {
        configuration.devices.filter { $0.enabled }.count
    }

    // MARK: - User intent

    func setRoutingEnabled(_ enabled: Bool) {
        guard enabled != routingEnabled else { return }
        routingEnabled = enabled
        ConfigurationStore.saveRoutingEnabled(enabled)
        if enabled {
            startEngine()
        } else {
            coordinator.stop()
            refreshStatus()
        }
    }

    // Applies a configuration edit made in Settings. Restarting is the simplest
    // correct way to pick up new routes or a new output device; config changes are
    // infrequent, so the brief gap is acceptable.
    func applyConfigurationChange() {
        resolveOutputDeviceIfNeeded()
        if routingEnabled {
            startEngine()
        }
    }

    func availableOutputDevices() -> [OutputDevice] {
        coordinator.enumerateOutputDevices()
    }

    func selectOutputDevice(_ device: OutputDevice) {
        configuration.outputDeviceUID = device.uid
        configuration.outputDeviceName = device.name
        applyConfigurationChange()
    }

    func refreshDriverStatus() {
        driverInstalled = DriverStatus.isInstalled
    }

    // MARK: - Engine control

    private func startEngine() {
        guard driverInstalled else { return }
        do {
            try coordinator.start(configuration: configuration)
            lastStartError = nil
        } catch let error as EngineStartError {
            lastStartError = error.message
        } catch {
            lastStartError = error.localizedDescription
        }
        refreshStatus()
    }

    // If we only know the output device by name (a fresh install), find its UID so
    // reconnects match on the stable identifier and auto-pick the board when seen.
    private func resolveOutputDeviceIfNeeded() {
        let devices = coordinator.enumerateOutputDevices()
        if !configuration.outputDeviceUID.isEmpty,
           devices.contains(where: { $0.uid == configuration.outputDeviceUID }) {
            return
        }
        let match = devices.first(where: { $0.uid == configuration.outputDeviceUID })
            ?? devices.first(where: { $0.name == configuration.outputDeviceName })
            ?? devices.first(where: { $0.looksLikeMultitrackBoard })
        if let match {
            configuration.outputDeviceUID = match.uid
            configuration.outputDeviceName = match.name
        }
    }

    // MARK: - Status polling

    private func startPolling() {
        pollingTask = Task { [weak self] in
            while !Task.isCancelled {
                self?.refreshStatus()
                self?.refreshDriverStatus()
                try? await Task.sleep(for: .seconds(1))
            }
        }
    }

    private func refreshStatus() {
        status = coordinator.status()
    }

    deinit {
        pollingTask?.cancel()
    }
}
