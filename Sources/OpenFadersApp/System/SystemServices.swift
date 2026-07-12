import AppKit
import ServiceManagement

// Wraps the login-item registration. SMAppService.mainApp registers the app itself
// as a login item; the app (not the installer) owns this so the user's choice is
// respected on upgrades.
enum LoginItem {
    static var isEnabled: Bool {
        SMAppService.mainApp.status == .enabled
    }

    static var requiresApproval: Bool {
        SMAppService.mainApp.status == .requiresApproval
    }

    static func setEnabled(_ enabled: Bool) {
        do {
            if enabled {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
        } catch {
            NSLog("OpenFaders: could not update login item: \(error.localizedDescription)")
        }
    }

    static func openLoginItemsSettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.LoginItems-Settings.extension") {
            NSWorkspace.shared.open(url)
        }
    }
}

// Reports whether the audio driver is installed and helps the user find it. The
// app cannot install the driver itself (that needs root and a coreaudiod restart);
// installation is the .pkg installer's job.
enum DriverStatus {
    static let halPluginDirectory = "/Library/Audio/Plug-Ins/HAL"
    static let driverBundleName = "OpenFadersDriver.driver"

    static var driverPath: String {
        "\(halPluginDirectory)/\(driverBundleName)"
    }

    static var isInstalled: Bool {
        FileManager.default.fileExists(atPath: driverPath)
    }

    static func revealInFinder() {
        if isInstalled {
            NSWorkspace.shared.selectFile(driverPath, inFileViewerRootedAtPath: halPluginDirectory)
        } else {
            NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: halPluginDirectory)
        }
    }
}
