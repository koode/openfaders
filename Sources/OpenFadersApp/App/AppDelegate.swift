import AppKit
import SwiftUI

// The app runs as a menu-bar accessory, so it manages its onboarding window
// through AppKit rather than a SwiftUI scene. On launch it shows onboarding
// whenever the driver is missing or out of date, so a new user is walked through
// installing it without ever opening a terminal.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        AppModel.shared.showOnboardingIfNeeded()
    }
}

// Owns the single onboarding window.
@MainActor
final class OnboardingPresenter {
    static let shared = OnboardingPresenter()

    private var windowController: NSWindowController?

    func show() {
        if windowController == nil {
            windowController = makeWindowController()
        }
        NSApplication.shared.activate(ignoringOtherApps: true)
        windowController?.showWindow(nil)
        windowController?.window?.makeKeyAndOrderFront(nil)
    }

    func close() {
        windowController?.close()
    }

    private func makeWindowController() -> NSWindowController {
        let hostingController = NSHostingController(
            rootView: OnboardingView().environment(AppModel.shared))
        let window = NSWindow(contentViewController: hostingController)
        window.title = "Welcome to OpenFaders"
        window.styleMask = [.titled, .closable]
        window.isReleasedWhenClosed = false
        window.center()
        return NSWindowController(window: window)
    }
}
