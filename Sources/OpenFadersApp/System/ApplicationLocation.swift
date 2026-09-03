import Foundation

// Where the running app bundle sits, which decides whether it should offer to move
// itself into /Applications.
enum ApplicationLocation: Equatable {
    case applications   // already installed where it belongs
    case translocated   // Gatekeeper is running a randomised read-only copy
    case elsewhere      // a real location outside /Applications, e.g. ~/Downloads

    static let applicationsDirectory = "/Applications"

    // Kept free of AppKit and of Bundle.main so the path rules can be exercised
    // directly, without a real bundle in each location.
    static func forBundlePath(_ path: String) -> ApplicationLocation {
        // Checked first: Gatekeeper runs a translocated copy from a randomised path
        // even when the original sits in /Applications, and the randomised path is the
        // one that decides whether a login item survives.
        if path.contains("/AppTranslocation/") { return .translocated }
        // Trailing slashes and "/Applications" itself are not an installed app.
        let normalised = path.hasSuffix("/") ? String(path.dropLast()) : path
        guard normalised.hasPrefix(applicationsDirectory + "/") else { return .elsewhere }
        // A nested path such as /Applications/Utilities/OpenFaders.app still counts:
        // the app is inside the Applications folder and will not move under the user.
        return .applications
    }
}
