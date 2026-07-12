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

// Whether the audio driver is installed and, if so, whether it matches the version
// the app ships.
enum DriverInstallState: Equatable {
    case notInstalled
    case outdated
    case installed
}

// Reports the state of the audio driver and locates the copy bundled inside the
// app. The app cannot load the driver itself (coreaudiod does), but it can install
// the bundled copy into the system HAL directory via DriverInstaller.
enum DriverStatus {
    static let halPluginDirectory = "/Library/Audio/Plug-Ins/HAL"
    static let driverBundleName = "OpenFadersDriver.driver"

    static var installedDriverPath: String {
        "\(halPluginDirectory)/\(driverBundleName)"
    }

    static var isInstalled: Bool {
        FileManager.default.fileExists(atPath: installedDriverPath)
    }

    // The driver that ships inside the app bundle (Contents/SharedSupport).
    static var bundledDriverURL: URL? {
        Bundle.main.sharedSupportURL?.appendingPathComponent(driverBundleName)
    }

    // The privileged install script that ships inside the app bundle.
    static var installScriptURL: URL? {
        Bundle.main.url(forResource: "openfaders-install-driver", withExtension: "sh")
    }

    static var bundledDriverVersion: String? {
        bundledDriverURL.flatMap(version(ofBundleAt:))
    }

    static var installedDriverVersion: String? {
        guard isInstalled else { return nil }
        return version(ofBundleAt: URL(fileURLWithPath: installedDriverPath))
    }

    static var installState: DriverInstallState {
        guard isInstalled else { return .notInstalled }
        if let installed = installedDriverVersion, let bundled = bundledDriverVersion,
           installed != bundled {
            return .outdated
        }
        return .installed
    }

    static func revealInFinder() {
        if isInstalled {
            NSWorkspace.shared.selectFile(installedDriverPath, inFileViewerRootedAtPath: halPluginDirectory)
        } else {
            NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: halPluginDirectory)
        }
    }

    private static func version(ofBundleAt url: URL) -> String? {
        Bundle(url: url)?.infoDictionary?["CFBundleVersion"] as? String
    }
}

// The result of an install attempt.
enum DriverInstallOutcome: Equatable {
    case installed
    case cancelled // the user dismissed the authentication prompt
    case failed(String)
}

// Installs the bundled driver into the system HAL directory. It runs the bundled
// install script as root through the system's native admin-authentication dialog
// (osascript's "with administrator privileges"), so the user authenticates once
// with the standard macOS prompt and never opens a terminal.
enum DriverInstaller {
    static func install() async -> DriverInstallOutcome {
        guard let scriptURL = DriverStatus.installScriptURL else {
            return .failed("The installer script is missing from the app bundle.")
        }
        return await withCheckedContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(returning: runPrivileged(scriptPath: scriptURL.path))
            }
        }
    }

    private static func runPrivileged(scriptPath: String) -> DriverInstallOutcome {
        // Escape the path for an AppleScript string literal, then let AppleScript's
        // `quoted form of` make it shell-safe. The path is from the app bundle, not
        // user input, but quoting keeps spaces and odd characters correct.
        let escapedPath = scriptPath
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
        let appleScript = "do shell script \"/bin/bash \" & quoted form of \"\(escapedPath)\" "
            + "with administrator privileges"

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        process.arguments = ["-e", appleScript]
        let errorPipe = Pipe()
        process.standardError = errorPipe

        do {
            try process.run()
        } catch {
            return .failed(error.localizedDescription)
        }
        process.waitUntilExit()

        if process.terminationStatus == 0 {
            return .installed
        }

        let errorData = errorPipe.fileHandleForReading.readDataToEndOfFile()
        let message = String(data: errorData, encoding: .utf8)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? "Unknown error"
        // osascript reports a cancelled authentication as error -128.
        if message.contains("-128") || message.localizedCaseInsensitiveContains("cancel") {
            return .cancelled
        }
        return .failed(message)
    }
}
