import SwiftUI

// OpenFaders runs as a menu-bar accessory (LSUIElement in Info.plist): no Dock
// icon, no window on launch, just the status item. The engine starts routing in
// the background as soon as the hardware is present.
@main
struct OpenFadersApp: App {
    @State private var model = AppModel()

    var body: some Scene {
        MenuBarExtra {
            MenuContentView()
                .environment(model)
        } label: {
            Image(systemName: model.routingState.menuBarSymbolName)
        }
        .menuBarExtraStyle(.menu)

        Settings {
            SettingsRootView()
                .environment(model)
                .frame(minWidth: 620, minHeight: 460)
        }
    }
}
