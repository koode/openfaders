import SwiftUI

// The Settings window: a native preferences-style TabView. Opening it explicitly
// activates the app, otherwise an accessory app's window can appear behind the
// frontmost application.
struct SettingsRootView: View {
    var body: some View {
        TabView {
            DevicesTab()
                .tabItem { Label("Devices", systemImage: "square.stack.3d.up.fill") }
            RoutingTab()
                .tabItem { Label("Routing", systemImage: "arrow.triangle.branch") }
            AudioTab()
                .tabItem { Label("Audio", systemImage: "dial.medium") }
            StartupTab()
                .tabItem { Label("Startup", systemImage: "power") }
            AboutTab()
                .tabItem { Label("About", systemImage: "info.circle") }
        }
        .onAppear {
            NSApplication.shared.activate()
        }
    }
}

// Devices: enable/disable each virtual device and see which board source it feeds.
struct DevicesTab: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        Form {
            Section {
                ForEach($model.configuration.devices) { $device in
                    Toggle(isOn: $device.enabled) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(device.displayName)
                            Text("Feeds the board's \(device.boardSource) channel · \(device.channelPairLabel)")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            } header: {
                Text("Virtual devices")
            } footer: {
                Text("Each device appears as an output in the macOS Sound menu. Send an app's audio to one to route it onto the matching RODECaster channel. Only the System pair is monitored by default; the others need their source assigned to a fader on the board.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .onChange(of: model.configuration.devices) { _, _ in
            model.applyConfigurationChange()
        }
    }
}

// Routing: pick the hardware output. Channel assignment is fixed to the board
// sources, so it is shown per device rather than made freely editable.
struct RoutingTab: View {
    @Environment(AppModel.self) private var model
    @State private var outputDevices: [OutputDevice] = []

    var body: some View {
        @Bindable var model = model
        Form {
            Section {
                Picker("Output device", selection: outputSelection) {
                    ForEach(outputDevices) { device in
                        Text("\(device.name) (\(device.outputChannelCount) ch)").tag(device.uid)
                    }
                    if !model.configuration.outputDeviceUID.isEmpty
                        && !outputDevices.contains(where: { $0.uid == model.configuration.outputDeviceUID }) {
                        Text("\(model.configuration.outputDeviceName) (not connected)")
                            .tag(model.configuration.outputDeviceUID)
                    }
                }
                Button("Rescan") { outputDevices = model.availableOutputDevices() }
            } header: {
                Text("Hardware output")
            } footer: {
                Text("The RODECaster must be in Multitrack USB mode to expose its ten-channel return. OpenFaders selects it automatically when it appears.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("Channel map") {
                ForEach(model.configuration.devices) { device in
                    LabeledContent(device.displayName) {
                        Text("\(device.boardSource) · \(device.channelPairLabel)")
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { outputDevices = model.availableOutputDevices() }
    }

    private var outputSelection: Binding<String> {
        Binding(
            get: { model.configuration.outputDeviceUID },
            set: { newUID in
                if let device = outputDevices.first(where: { $0.uid == newUID }) {
                    model.selectOutputDevice(device)
                }
            }
        )
    }
}

// Startup: launch-at-login and the driver install status.
struct StartupTab: View {
    @Environment(AppModel.self) private var model
    @State private var launchAtLogin = LoginItem.isEnabled
    @State private var checksForUpdates = UpdateChecker.isEnabled
    @State private var isCheckingNow = false

    var body: some View {
        Form {
            Section("General") {
                Toggle("Open OpenFaders at login", isOn: Binding(
                    get: { launchAtLogin },
                    set: { newValue in
                        LoginItem.setEnabled(newValue)
                        launchAtLogin = LoginItem.isEnabled
                    }
                ))
                if LoginItem.requiresApproval {
                    Button("Approve in Login Items…") { LoginItem.openLoginItemsSettings() }
                }
            }

            Section("Updates") {
                Toggle("Check for updates automatically", isOn: Binding(
                    get: { checksForUpdates },
                    set: { newValue in
                        UpdateChecker.isEnabled = newValue
                        checksForUpdates = newValue
                    }
                ))
                Text("Asks downloads.openfaders.org once a day whether a newer release "
                     + "exists. Nothing about this Mac is sent, and OpenFaders never "
                     + "installs an update on its own.")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                LabeledContent("Status") {
                    if let update = UpdateChecker.shared.availableUpdate {
                        Text("Version \(update.version) available")
                            .foregroundStyle(Color.orange)
                    } else {
                        Text("Version \(UpdateChecker.shared.installedVersion)")
                            .foregroundStyle(.secondary)
                    }
                }

                if UpdateChecker.shared.availableUpdate != nil {
                    Button("Download update…") { UpdateChecker.shared.openDownloadPage() }
                } else {
                    Button(isCheckingNow ? "Checking…" : "Check Now") {
                        isCheckingNow = true
                        Task {
                            await UpdateChecker.shared.check(force: true)
                            isCheckingNow = false
                        }
                    }
                    .disabled(isCheckingNow)
                }
            }

            Section("Audio driver") {
                LabeledContent("Status") {
                    Text(driverStatusText)
                        .foregroundStyle(model.driverInstallState == .installed ? Color.green : Color.orange)
                }
                Button(driverActionText) { model.presentOnboarding() }
                Button("Reveal in Finder") { DriverStatus.revealInFinder() }
            }
        }
        .formStyle(.grouped)
    }

    private var driverStatusText: String {
        switch model.driverInstallState {
        case .installed: return "Installed"
        case .outdated: return "Update available"
        case .notInstalled: return "Not installed"
        }
    }

    private var driverActionText: String {
        switch model.driverInstallState {
        case .installed: return "Reinstall driver…"
        case .outdated: return "Update driver…"
        case .notInstalled: return "Install driver…"
        }
    }
}

// Audio: the clock-sync mode. Hardware-timed is the safe default; adaptive drift
// correction needs the updated driver's clock-slave property.
struct AudioTab: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        Form {
            Section {
                Toggle("Adaptive drift correction", isOn: $model.configuration.adaptiveSync)
            } header: {
                Text("Clock sync")
            } footer: {
                Text("Hardware-timed routing (off) relies on buffer headroom and is the tested default. Adaptive drift correction continuously matches each virtual device's clock to the RODECaster for glitch-free long sessions; it needs the current OpenFaders driver installed.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .onChange(of: model.configuration.adaptiveSync) { _, _ in
            model.applyConfigurationChange()
        }
    }
}

// About: identity, version, license, and the non-affiliation note.
struct AboutTab: View {
    private var version: String {
        let marketing = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "—"
        let build = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "—"
        return "Version \(marketing) (\(build))"
    }

    var body: some View {
        VStack(spacing: 10) {
            Image(systemName: "waveform")
                .font(.system(size: 44))
                .foregroundStyle(.tint)
            Text("OpenFaders").font(.title2).bold()
            Text(version).foregroundStyle(.secondary)
            Text("Open-source virtual audio channels for the RODECaster Pro II in Multitrack mode. Not affiliated with, or endorsed by, RØDE.")
                .font(.caption)
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)
                .padding(.horizontal)
            Text("MIT licensed").font(.caption).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding()
    }
}
