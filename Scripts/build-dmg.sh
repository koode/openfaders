#!/bin/bash
#
# Builds a signed OpenFaders disk image: a drag-to-Applications .dmg that holds
# the app and nothing else.
#
# The app installs the audio driver itself on first launch (see docs/INSTALL.md),
# so a disk image is enough for a normal install. The .pkg that build-release.sh
# produces stays the option for people who want an installer to place the driver
# and restart the audio server for them.
#
# It expects the signed Release build that build-release.sh leaves in build/Release,
# so run that first. The image itself is signed with the same Developer ID, which
# lets Gatekeeper check the image as well as the app inside it.
#
# Signing identity (override via environment):
#   OPENFADERS_SIGN_IDENTITY   Developer ID Application: Koode (PXD36584ME)
#
# Notarization (optional): set OPENFADERS_NOTARY_PROFILE to the name of a
# `notarytool store-credentials` keychain profile. It then notarizes and staples
# the app before the image is built, and the image itself afterwards. Both tickets
# matter: the one on the image clears the download, and the one on the app travels
# with the bundle the user drags to /Applications.
#
# Prerequisites: a signed build/Release/OpenFaders.app. Does not require sudo.

set -euo pipefail

readonly project_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_root"

readonly app_identity="${OPENFADERS_SIGN_IDENTITY:-Developer ID Application: Koode (PXD36584ME)}"
readonly notary_profile="${OPENFADERS_NOTARY_PROFILE:-}"

readonly app_bundle="$project_root/build/Release/OpenFaders.app"
readonly dist_dir="$project_root/build/dist"
readonly staging_dir="$project_root/build/dmgroot"

if [[ ! -d "$app_bundle" ]]; then
    echo "error: $app_bundle not found. Run Scripts/build-release.sh first." >&2
    exit 1
fi

# An unsigned app is not shippable: AMFI rejects the driver it installs unless the
# driver carries a Developer ID signature, and build-release.sh is what applies it.
if ! codesign --verify --strict "$app_bundle" >/dev/null 2>&1; then
    echo "error: $app_bundle is not validly signed. Run Scripts/build-release.sh first." >&2
    exit 1
fi

# Read the marketing version from the built app so the file name always matches
# what shipped, the same way build-release.sh names the pkg.
readonly version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$app_bundle/Contents/Info.plist")"
readonly dmg="$dist_dir/OpenFaders-$version.dmg"

if [[ -n "$notary_profile" ]]; then
    echo "==> Notarizing the app with profile '$notary_profile' (this can take a few minutes)"
    # notarytool takes an archive, not a bundle. Staple the ticket to the app
    # before the image is built, so the copy the user drags out carries it and
    # opens on a Mac that is offline.
    readonly app_archive="$project_root/build/OpenFaders-app.zip"
    rm -f "$app_archive"
    ditto -c -k --keepParent "$app_bundle" "$app_archive"
    xcrun notarytool submit "$app_archive" --keychain-profile "$notary_profile" --wait
    rm -f "$app_archive"
    echo "==> Stapling the app"
    xcrun stapler staple "$app_bundle"
    xcrun stapler validate "$app_bundle"
fi

echo "==> Staging the disk image contents"
rm -rf "$staging_dir"
mkdir -p "$staging_dir" "$dist_dir"
ditto "$app_bundle" "$staging_dir/OpenFaders.app"
# The drop target. Finder shows this symlink as the Applications folder, which is
# what makes the image a drag install.
ln -s /Applications "$staging_dir/Applications"

echo "==> Building the disk image"
rm -f "$dmg"
# HFS+ rather than APFS: a read-only compressed HFS+ image mounts on every macOS
# version the app supports, and the image holds one app bundle, so APFS buys nothing.
hdiutil create \
    -volname "OpenFaders $version" \
    -srcfolder "$staging_dir" \
    -fs HFS+ \
    -format UDZO \
    -ov \
    "$dmg" >/dev/null

echo "==> Signing with '$app_identity'"
codesign --force --timestamp --sign "$app_identity" "$dmg"
codesign --verify --strict --verbose=2 "$dmg"

if [[ -n "$notary_profile" ]]; then
    echo "==> Notarizing the disk image (this can take a few minutes)"
    xcrun notarytool submit "$dmg" --keychain-profile "$notary_profile" --wait
    echo "==> Stapling the disk image"
    xcrun stapler staple "$dmg"
    xcrun stapler validate "$dmg"
else
    echo "==> Skipping notarization (set OPENFADERS_NOTARY_PROFILE to enable)"
fi

rm -rf "$staging_dir"

echo ""
echo "Done: $dmg"
