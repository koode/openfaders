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
        case .active: return "waveform.badge.checkmark"
        case .paused, .waitingForHardware: return "waveform.badge.xmark"
        case .driverNotInstalled: return "waveform.badge.exclamationmark"
        }
    }
}

// The single source of truth the UI observes. Owns the configuration, the engine
// coordinator, and the polled runtime status, and turns user intent into engine
// start/stop calls. Main-actor isolated because it is driven entirely from the UI.
@MainActor
@Observable
final class AppModel {
    // A single shared instance so the SwiftUI scenes, the app delegate, and the
    // onboarding window all drive the same state.
    static let shared = AppModel()

    var configuration: AppConfiguration {
        didSet { ConfigurationStore.save(configuration) }
    }
    private(set) var routingEnabled: Bool
    private(set) var status: EngineRuntimeStatus = .idle
    private(set) var driverInstalled: Bool = DriverStatus.isInstalled
    private(set) var driverInstallState: DriverInstallState = DriverStatus.installState
    private(set) var isInstallingDriver = false
    private(set) var lastStartError: String?
    private(set) var lastInstallError: String?
    private(set) var micState: MicrophonePermission.State = MicrophonePermission.state
    private(set) var multitrackDetected = false
    private(set) var audioDetected = false

    @ObservationIgnored private let coordinator = EngineCoordinator()
    @ObservationIgnored private var pollingTask: Task<Void, Never>?
    @ObservationIgnored private var lastOutputDeviceID: UInt32 = 0
    @ObservationIgnored private var lastMicState: MicrophonePermission.State = MicrophonePermission.state
    @ObservationIgnored private var onboardingOpen = false
    @ObservationIgnored private let signalThreshold: Float = 0.003 // ~ -50 dBFS

    private init() {
        configuration = ConfigurationStore.loadConfiguration()
        routingEnabled = ConfigurationStore.loadRoutingEnabled()

        resolveOutputDeviceIfNeeded()
        if routingEnabled {
            startEngine()
        }
        lastOutputDeviceID = coordinator.resolveOutputDeviceID(configuration)
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
        driverInstallState = DriverStatus.installState
    }

    // MARK: - Driver installation

    // Installs (or repairs/updates) the bundled driver through the system admin
    // prompt, then re-checks status and starts routing if it was enabled.
    func installDriver() async {
        guard !isInstallingDriver else { return }
        isInstallingDriver = true
        lastInstallError = nil
        let outcome = await DriverInstaller.install()
        isInstallingDriver = false
        switch outcome {
        case .installed:
            refreshDriverStatus()
            applyConfigurationChange()
        case .cancelled:
            break
        case .failed(let message):
            lastInstallError = message
        }
    }

    func presentOnboarding() {
        ConfigurationStore.saveHasSeenOnboarding(true)
        onboardingOpen = true
        audioDetected = false
        multitrackDetected = availableOutputDevices().contains { $0.looksLikeMultitrackBoard }
        coordinator.setMetering(true) // meter the captured signal only while onboarding is open
        OnboardingPresenter.shared.show()
    }

    // Called when the onboarding window closes. Stops the signal metering.
    func onboardingDidClose() {
        onboardingOpen = false
        coordinator.setMetering(false)
    }

    // Shows onboarding on first launch, and thereafter whenever the driver or
    // microphone access still needs attention.
    func showOnboardingIfNeeded() {
        let isFirstLaunch = !ConfigurationStore.loadHasSeenOnboarding()
        // Auto-present on first launch, and when the driver is missing (nothing works
        // without it). Don't force the window forward on later launches just because
        // the mic is denied or the driver is merely outdated — that would steal focus
        // at every login; those are surfaced in the menu and shown when setup opens.
        if isFirstLaunch || driverInstallState == .notInstalled {
            presentOnboarding()
        }
    }

    // MARK: - Microphone permission

    func refreshMicState() {
        micState = MicrophonePermission.state
    }

    // While onboarding is visible, surface whether the board is in Multitrack mode
    // (its ten-channel device is present) and whether audio is actually flowing
    // through a virtual device. Gated on onboarding so metering has no cost otherwise.
    private func refreshSignalIndicators() {
        guard onboardingOpen else { return }
        multitrackDetected = availableOutputDevices().contains { $0.looksLikeMultitrackBoard }
        if coordinator.takeSignalPeak() > signalThreshold {
            audioDetected = true // latch: once confirmed it stays for this onboarding session
        }
    }

    // Prompts for microphone access when undecided. When already denied macOS shows
    // no prompt, so the onboarding points the user at System Settings instead.
    func requestMicrophoneAccess() async {
        _ = await MicrophonePermission.request()
        refreshMicState()
        reconcileMicPermission()
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
        lastOutputDeviceID = coordinator.resolveOutputDeviceID(configuration)
        refreshStatus()
    }

    // Notices when Core Audio republishes devices with new identities (coreaudiod
    // restart, unplug/replug, Multitrack toggle) and re-establishes routing without
    // the user rescanning. The output device's object ID changing is the signal:
    // its UID is stable, but the ID is reassigned whenever the audio server rebuilds
    // its device list, which also invalidates the units the engine was running.
    private func reconcileTopology() {
        let currentOutputID = coordinator.resolveOutputDeviceID(configuration)
        guard currentOutputID != lastOutputDeviceID else { return }
        resolveOutputDeviceIfNeeded()
        if routingEnabled {
            startEngine()
        }
        // Record what we resolved AFTER any config change or restart so reconcile
        // converges to a no-op next tick — including when the driver is not installed
        // yet, in which case startEngine returns early and cannot record it itself.
        lastOutputDeviceID = coordinator.resolveOutputDeviceID(configuration)
    }

    // When microphone access flips to granted, the engine must (re)start: it may have
    // started earlier without permission (capturing silence), and a permission change
    // does not republish the audio devices, so reconcileTopology never reacts to it.
    private func reconcileMicPermission() {
        let previous = lastMicState
        lastMicState = micState
        if previous != .granted, micState == .granted, routingEnabled {
            startEngine()
        }
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
                self?.refreshMicState()
                self?.reconcileMicPermission()
                self?.reconcileTopology()
                self?.refreshSignalIndicators()
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
