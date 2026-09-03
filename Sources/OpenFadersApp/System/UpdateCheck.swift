import AppKit
import Foundation

// Asks downloads.openfaders.org whether a newer release exists.
//
// This is the only network request the app makes, and it is deliberately the
// smallest one that answers the question: an unauthenticated GET of a static JSON
// file, with no query string, no identifier, and no request body. Nothing about the
// installation is sent, and nothing about the response is stored beyond the version
// string and the time of the check.
//
// It notifies and nothing more. It does not download, and it does not install: the
// installers are notarized and the user is the one who decides to replace an app
// that owns a system audio driver.
@MainActor
@Observable
final class UpdateChecker {
    static let shared = UpdateChecker()

    static let manifestURL = URL(string: "https://downloads.openfaders.org/latest.json")!

    // Once a day is enough for a menu bar utility, and it keeps a machine that is
    // rebooted repeatedly from asking repeatedly.
    private static let minimumInterval: TimeInterval = 24 * 60 * 60

    private static let lastCheckedKey = "lastUpdateCheck"
    private static let enabledKey = "automaticallyChecksForUpdates"

    // Default on, and disclosed on openfaders.org/legal. The toggle lives in
    // Settings ▸ Startup.
    static var isEnabled: Bool {
        get {
            UserDefaults.standard.object(forKey: enabledKey) as? Bool ?? true
        }
        set {
            UserDefaults.standard.set(newValue, forKey: enabledKey)
        }
    }

    /// The newer release, once one is found. Nil means up to date or not yet checked.
    private(set) var availableUpdate: ReleaseManifest?

    var installedVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0"
    }

    /// Runs at launch. Does nothing when switched off or when checked recently.
    func checkInBackgroundIfDue() {
        guard Self.isEnabled, isDue else { return }
        Task { await check() }
    }

    private var isDue: Bool {
        let last = UserDefaults.standard.double(forKey: Self.lastCheckedKey)
        guard last > 0 else { return true }
        return Date().timeIntervalSince1970 - last >= Self.minimumInterval
    }

    /// Fetches the manifest and records whether it names a newer version.
    /// `force` skips the interval, for the button in Settings.
    @discardableResult
    func check(force: Bool = false) async -> Bool {
        guard force || isDue else { return availableUpdate != nil }

        var request = URLRequest(url: Self.manifestURL)
        // The manifest is stored no-store in R2, but a proxy in between could still
        // hand back a stale copy and hide a release.
        request.cachePolicy = .reloadIgnoringLocalCacheData
        request.timeoutInterval = 15

        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
                return false
            }
            let manifest = try JSONDecoder().decode(ReleaseManifest.self, from: data)

            // Record the time only on a usable answer, so a failed check is retried
            // on the next launch rather than sat out for a day.
            UserDefaults.standard.set(Date().timeIntervalSince1970, forKey: Self.lastCheckedKey)

            availableUpdate = ReleaseVersion.isUpdate(available: manifest.version, over: installedVersion)
                ? manifest
                : nil
            return availableUpdate != nil
        } catch {
            // Offline is the normal case here, not an error worth showing. The menu
            // simply says nothing about updates.
            NSLog("OpenFaders: update check failed: \(error.localizedDescription)")
            return false
        }
    }

    /// Opens the disk image in the browser. Downloading and replacing the app is
    /// left to the user, deliberately.
    func openDownloadPage() {
        guard let update = availableUpdate, let url = URL(string: update.dmg.url) else { return }
        NSWorkspace.shared.open(url)
    }
}
