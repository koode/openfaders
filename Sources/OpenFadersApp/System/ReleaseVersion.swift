import Foundation

// The release manifest published alongside the installers at
// downloads.openfaders.org/latest.json. Scripts/publish-r2.sh writes it.
struct ReleaseManifest: Decodable, Equatable {
    struct Download: Decodable, Equatable {
        let url: String
    }

    let version: String
    let dmg: Download
    let pkg: Download
}

// A dotted release version, compared component by component.
//
// String comparison is wrong here and quietly so: "0.10.0" sorts before "0.9.0",
// which would stop offering updates exactly when the project reaches its tenth
// minor release.
struct ReleaseVersion: Equatable, Comparable {
    let components: [Int]

    // Returns nil for anything that is not a dotted run of digits. A malformed
    // manifest must not be treated as an update, so parsing failure is explicit
    // rather than defaulted to zero.
    init?(_ string: String) {
        let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }

        var parsed: [Int] = []
        for part in trimmed.split(separator: ".", omittingEmptySubsequences: false) {
            guard !part.isEmpty, part.allSatisfy(\.isNumber), let value = Int(part) else {
                return nil
            }
            parsed.append(value)
        }
        components = parsed
    }

    static func < (lhs: ReleaseVersion, rhs: ReleaseVersion) -> Bool {
        // Compare over the longer of the two, treating missing components as zero,
        // so "0.2" and "0.2.0" are the same version rather than an endless update.
        let count = max(lhs.components.count, rhs.components.count)
        for index in 0..<count {
            let left = index < lhs.components.count ? lhs.components[index] : 0
            let right = index < rhs.components.count ? rhs.components[index] : 0
            if left != right { return left < right }
        }
        return false
    }

    // Whether `available` is worth telling the user about. Unparseable input on
    // either side means no, so a broken manifest cannot nag.
    static func isUpdate(available: String, over installed: String) -> Bool {
        guard let available = ReleaseVersion(available),
              let installed = ReleaseVersion(installed) else { return false }
        return installed < available
    }
}
