#!/bin/bash
#
# Developer-ID-signs the locally built app and the driver embedded inside it, so
# you can test the in-app driver installer without building a full release. The
# ad-hoc signature from a plain build will NOT load a HAL driver (coreaudiod's
# AMFI check requires a real certificate chain); this re-signs with a Developer ID.
#
# Usage: build the app first, then run this, then open the app and use its
# onboarding to install the driver:
#
#   xcodebuild -project OpenFaders.xcodeproj -target OpenFadersApp -configuration Release build
#   Scripts/dev-sign-app.sh
#   open build/Release/OpenFaders.app

set -euo pipefail

readonly project_root="$(cd "$(dirname "$0")/.." && pwd)"
readonly identity="${OPENFADERS_SIGN_IDENTITY:-Developer ID Application: Koode (PXD36584ME)}"
readonly app="$project_root/build/Release/OpenFaders.app"
readonly embedded_driver="$app/Contents/SharedSupport/OpenFadersDriver.driver"
readonly entitlements="$project_root/Sources/OpenFadersApp/Support/OpenFaders.entitlements"

if [[ ! -d "$app" ]]; then
    echo "Build the app first:" >&2
    echo "  xcodebuild -project OpenFaders.xcodeproj -target OpenFadersApp -configuration Release build" >&2
    exit 1
fi

xattr -cr "$app"
# Sign inside-out: the embedded driver first, then the app.
codesign --force --timestamp --sign "$identity" "$embedded_driver"
codesign --force --options runtime --timestamp --entitlements "$entitlements" \
    --sign "$identity" "$app"
codesign --verify --strict --verbose=2 "$app"

echo ""
echo "Signed with '$identity'."
echo "Run:  open '$app'"
echo "Then use its setup window to install the driver."
