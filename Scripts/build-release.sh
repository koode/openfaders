#!/bin/bash
#
# Builds a signed (and optionally notarized) OpenFaders installer .pkg.
#
# It builds the app and driver in Release, re-signs both with a Developer ID
# (the app with the hardened runtime and the audio-input entitlement; the driver
# as a plain Developer-ID bundle), stages them into their install locations, wraps
# them in a component pkg with the coreaudiod-restart postinstall script, and
# builds a signed distribution pkg with productbuild. If a notary profile is
# provided it also notarizes and staples the result.
#
# Signing identities (override via environment):
#   OPENFADERS_SIGN_IDENTITY       Developer ID Application: Koode (PXD36584ME)
#   OPENFADERS_INSTALLER_IDENTITY  Developer ID Installer: Koode (PXD36584ME)
#
# Notarization (optional): set OPENFADERS_NOTARY_PROFILE to the name of a
# `notarytool store-credentials` keychain profile to notarize and staple the pkg.
#
# Prerequisites: Xcode, xcodegen. Does not require sudo (nothing is installed).

set -euo pipefail

readonly project_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_root"

readonly app_identity="${OPENFADERS_SIGN_IDENTITY:-Developer ID Application: Koode (PXD36584ME)}"
readonly installer_identity="${OPENFADERS_INSTALLER_IDENTITY:-Developer ID Installer: Koode (PXD36584ME)}"
readonly notary_profile="${OPENFADERS_NOTARY_PROFILE:-}"

readonly configuration="Release"
readonly build_dir="$project_root/build/$configuration"
readonly app_bundle="$build_dir/OpenFaders.app"
readonly driver_bundle="$build_dir/OpenFadersDriver.driver"
readonly embedded_driver="$app_bundle/Contents/SharedSupport/OpenFadersDriver.driver"
readonly entitlements="$project_root/Sources/OpenFadersApp/Support/OpenFaders.entitlements"

readonly dist_dir="$project_root/build/dist"
readonly staging_root="$project_root/build/pkgroot"
readonly component_pkg="$project_root/build/OpenFadersComponent.pkg"

echo "==> Regenerating the Xcode project"
xcodegen generate >/dev/null

echo "==> Building the driver and app ($configuration)"
xcodebuild -project OpenFaders.xcodeproj -target OpenFadersDriver -configuration "$configuration" \
    build >/dev/null
xcodebuild -project OpenFaders.xcodeproj -target OpenFadersApp -configuration "$configuration" \
    build >/dev/null

# Read the marketing version straight from the built app so the pkg version always
# matches what shipped.
readonly version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$app_bundle/Contents/Info.plist")"
readonly product_pkg="$dist_dir/OpenFaders-$version.pkg"

echo "==> Signing with '$app_identity'"
# The standalone driver the pkg installs into the HAL directory: a passive bundle
# loaded by coreaudiod, Developer-ID signed and timestamped, no hardened runtime
# and no entitlements.
codesign --force --timestamp --sign "$app_identity" "$driver_bundle"
# Sign the app inside-out: first the driver embedded in the app (the copy the
# app's own installer copies out to the HAL directory), then the app itself with
# the hardened runtime and the audio-input entitlement. Signing the nested driver
# first means the app's signature seals a validly signed bundle and notarization
# passes.
codesign --force --timestamp --sign "$app_identity" "$embedded_driver"
codesign --force --options runtime --timestamp --entitlements "$entitlements" \
    --sign "$app_identity" "$app_bundle"
codesign --verify --strict --verbose=2 "$driver_bundle"
codesign --verify --strict --verbose=2 "$app_bundle"

echo "==> Staging the install layout"
rm -rf "$staging_root" "$dist_dir"
mkdir -p "$staging_root/Applications"
mkdir -p "$staging_root/Library/Audio/Plug-Ins/HAL"
mkdir -p "$dist_dir"
ditto "$app_bundle" "$staging_root/Applications/OpenFaders.app"
ditto "$driver_bundle" "$staging_root/Library/Audio/Plug-Ins/HAL/OpenFadersDriver.driver"

echo "==> Building the component package"
chmod +x "$project_root/packaging/scripts/postinstall"
pkgbuild \
    --root "$staging_root" \
    --identifier "app.openfaders.pkg" \
    --version "$version" \
    --scripts "$project_root/packaging/scripts" \
    --ownership recommended \
    --install-location "/" \
    "$component_pkg"

echo "==> Building the signed distribution installer"
productbuild \
    --distribution "$project_root/packaging/distribution.xml" \
    --package-path "$project_root/build" \
    --sign "$installer_identity" \
    "$product_pkg"
rm -f "$component_pkg"

if [[ -n "$notary_profile" ]]; then
    echo "==> Notarizing with profile '$notary_profile' (this can take a few minutes)"
    xcrun notarytool submit "$product_pkg" --keychain-profile "$notary_profile" --wait
    echo "==> Stapling"
    xcrun stapler staple "$product_pkg"
    xcrun stapler validate "$product_pkg"
else
    echo "==> Skipping notarization (set OPENFADERS_NOTARY_PROFILE to enable)"
fi

echo ""
echo "Done: $product_pkg"
