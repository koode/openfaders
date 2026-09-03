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
# Prerequisites: a signed build/Release/OpenFaders.app. Does not require sudo.

set -euo pipefail

readonly project_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_root"

readonly app_identity="${OPENFADERS_SIGN_IDENTITY:-Developer ID Application: Koode (PXD36584ME)}"

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

rm -rf "$staging_dir"

echo ""
echo "Done: $dmg"
