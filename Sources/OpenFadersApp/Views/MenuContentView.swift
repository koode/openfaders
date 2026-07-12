import SwiftUI

// The contents of the menu-bar dropdown. Kept deliberately small: a status
// header, the one control most people want (enable/disable), a path to Settings,
// and Quit.
struct MenuContentView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        Text(model.routingState.headline)

        if model.routingState == .driverNotInstalled {
            Button("Install audio driver…") {
                DriverStatus.revealInFinder()
            }
        } else {
            Text(hardwareLine)
        }

        Divider()

        Toggle("Enable routing", isOn: Binding(
            get: { model.routingEnabled },
            set: { model.setRoutingEnabled($0) }
        ))
        .keyboardShortcut("r", modifiers: [.control, .option])
        .disabled(!model.driverInstalled)

        Divider()

        SettingsLink {
            Text("Settings…")
        }
        .keyboardShortcut(",", modifiers: .command)

        Button("Quit OpenFaders") {
            NSApplication.shared.terminate(nil)
        }
        .keyboardShortcut("q", modifiers: .command)
    }

    private var hardwareLine: String {
        if model.status.outputDevicePresent {
            return "Connected: \(model.configuration.outputDeviceName)"
        }
        return "RODECaster not detected"
    }
}
