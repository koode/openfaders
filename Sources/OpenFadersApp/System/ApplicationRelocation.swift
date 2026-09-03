import AppKit
import ServiceManagement

// Offers to move the app into /Applications on launch.
//
// This matters more here than for a typical app because two things bind to the
// bundle's path. SMAppService registers the login item at whatever path the app
// occupies, and the driver install and update check read the driver out of
// Contents/SharedSupport. Run the app from ~/Downloads or straight off the mounted
// disk image and both end up pointing at a location the user later moves, deletes,
// or ejects.
//
// The disk-image case is the one a first-time user hits: opening the app from the
// image makes Gatekeeper run it from a randomised read-only path that disappears on
// eject, so a login item registered from there silently never launches again.
enum ApplicationRelocator {
    // Set when a move takes the login item's registered path with it, so the copy
    // that relaunches can register itself again from its new home.
    private static let pendingLoginItemKey = "pendingLoginItemReregistration"
    // Set when the user declines, so the offer is made once and not at every launch.
    private static let declinedMoveKey = "declinedMoveToApplications"

    static var currentLocation: ApplicationLocation {
        ApplicationLocation.forBundlePath(Bundle.main.bundleURL.resolvingSymlinksInPath().path)
    }

    static var shouldOfferMove: Bool {
        guard currentLocation != .applications else { return false }
        // A translocated app cannot be trusted to stay put, so always offer for that
        // case even if the user said no to a previous, non-translocated launch.
        if currentLocation == .translocated { return true }
        return !UserDefaults.standard.bool(forKey: declinedMoveKey)
    }

    // Presents the offer and performs the move. Call once, on launch, before any
    // onboarding window so the app is in its final home before the user installs
    // the driver or enables the login item.
    @MainActor
    static func offerMoveIfNeeded() {
        guard shouldOfferMove else { return }

        let alert = NSAlert()
        alert.messageText = "Move OpenFaders to the Applications folder?"
        alert.informativeText = informativeText
        alert.addButton(withTitle: "Move to Applications")
        alert.addButton(withTitle: "Not Now")
        alert.alertStyle = .informational

        NSApplication.shared.activate(ignoringOtherApps: true)
        guard alert.runModal() == .alertFirstButtonReturn else {
            UserDefaults.standard.set(true, forKey: declinedMoveKey)
            return
        }

        do {
            let destination = try moveToApplications()
            relaunch(at: destination)
        } catch {
            let failure = NSAlert()
            failure.messageText = "OpenFaders could not move itself."
            failure.informativeText = """
                \(error.localizedDescription)

                Drag OpenFaders into your Applications folder yourself, then open it \
                from there.
                """
            failure.alertStyle = .warning
            failure.runModal()
        }
    }

    private static var informativeText: String {
        switch currentLocation {
        case .translocated:
            return """
                macOS is running OpenFaders from a temporary read-only copy, which \
                disappears when you eject the disk image. Open at login and driver \
                updates cannot work from there.

                Moving it to your Applications folder fixes this. OpenFaders reopens \
                itself once the move is done.
                """
        case .elsewhere, .applications:
            return """
                OpenFaders installs its audio driver and can open itself at login, and \
                both remember where the app lives. Keeping it in your Applications \
                folder means neither breaks when you move or delete this copy.

                OpenFaders reopens itself once the move is done.
                """
        }
    }

    // Copies the bundle into /Applications and tidies up behind it. Returns the new
    // location. Copy-then-remove rather than a move: the source may sit on a
    // read-only disk image, where a move fails outright.
    private static func moveToApplications() throws -> URL {
        let fileManager = FileManager.default
        let source = Bundle.main.bundleURL
        let destination = URL(fileURLWithPath: ApplicationLocation.applicationsDirectory)
            .appendingPathComponent(source.lastPathComponent)

        if fileManager.fileExists(atPath: destination.path) {
            // Something is already there. Trash it rather than delete it, so a copy the
            // user cared about is recoverable.
            try fileManager.trashItem(at: destination, resultingItemURL: nil)
        }
        try fileManager.copyItem(at: source, to: destination)

        // The login item points at the path being left behind. Drop it here and let
        // the relaunched copy register from its new home, so the user's choice
        // survives the move.
        if LoginItem.isEnabled {
            UserDefaults.standard.set(true, forKey: pendingLoginItemKey)
            LoginItem.setEnabled(false)
        }

        // A translocated bundle lives on a read-only mount owned by Gatekeeper, so
        // leave it alone; ejecting the image takes it away.
        if currentLocation == .elsewhere {
            try? fileManager.trashItem(at: source, resultingItemURL: nil)
        }
        return destination
    }

    // Opens the copy in /Applications and quits this one. Terminating from the
    // completion handler keeps the running instance alive until the new one is
    // actually launched, so a failure leaves the user with a working app.
    @MainActor
    private static func relaunch(at destination: URL) {
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.createsNewApplicationInstance = true
        NSWorkspace.shared.openApplication(at: destination, configuration: configuration) { _, _ in
            DispatchQueue.main.async {
                NSApplication.shared.terminate(nil)
            }
        }
    }

    // Restores the login item after a move took its registered path away. Safe to
    // call on every launch; it does nothing unless a move actually happened.
    static func restoreLoginItemAfterMoveIfNeeded() {
        guard UserDefaults.standard.bool(forKey: pendingLoginItemKey) else { return }
        UserDefaults.standard.set(false, forKey: pendingLoginItemKey)
        guard currentLocation == .applications else { return }
        LoginItem.setEnabled(true)
    }
}
