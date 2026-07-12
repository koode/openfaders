#!/bin/bash
#
# Development reload for the OpenFaders driver. Signs the bundle with a real
# developer certificate (coreaudiod's AMFI check rejects ad-hoc signatures with
# error -423, but does NOT require notarization to load), installs it into the
# system HAL directory with a signature-preserving copy and correct ownership,
# restarts coreaudiod by its launchd PID (which works with SIP on), and reports
# whether the virtual devices loaded. On failure it prints the coreaudiod / AMFI
# log captured live across the restart, which names the reason.
#
# Run with sudo. Restarting coreaudiod briefly interrupts ALL audio on this Mac.
#
# The signing identity defaults to the maintainer's Developer ID and can be
# overridden: OPENFADERS_SIGN_IDENTITY="Apple Development: ..." sudo -E ./reload-driver.sh
#
# Usage: sudo ./Scripts/reload-driver.sh [path-to-OpenFadersDriver.driver]

set -euo pipefail

readonly driver_name="OpenFadersDriver.driver"
readonly hal_directory="/Library/Audio/Plug-Ins/HAL"
readonly service_label="system/com.apple.audio.coreaudiod"
readonly sign_identity="${OPENFADERS_SIGN_IDENTITY:-Developer ID Application: Koode (PXD36584ME)}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_driver="${1:-$repo_root/build/Release/$driver_name}"
readonly log_capture="/tmp/openfaders-load.log"

if [[ $EUID -ne 0 ]]; then
    echo "Run with sudo: writes to $hal_directory and restarts coreaudiod." >&2
    echo "Try: sudo $0" >&2
    exit 1
fi
if [[ ! -d "$source_driver" ]]; then
    echo "Driver bundle not found: $source_driver" >&2
    exit 1
fi

echo "1/6  Stripping extended attributes and signing with a developer certificate..."
echo "     identity: $sign_identity"
xattr -cr "$source_driver"
codesign --force --options runtime --sign "$sign_identity" "$source_driver"
codesign -vvv --strict "$source_driver" 2>&1 | sed 's/^/     /'

echo "2/6  Installing with ditto (preserves the signature and bundle layout)..."
rm -rf "${hal_directory:?}/$driver_name"
ditto "$source_driver" "$hal_directory/$driver_name"
chown -R root:wheel "$hal_directory/$driver_name"
chmod -R go-w "$hal_directory/$driver_name"

echo "3/6  Verifying the installed copy (this is what coreaudiod sees)..."
codesign -vvv --strict "$hal_directory/$driver_name" 2>&1 | sed 's/^/     /'

echo "4/6  Capturing the coreaudiod / AMFI log across the restart..."
log stream \
    --predicate 'process == "coreaudiod" OR senderImagePath CONTAINS "AppleMobileFileIntegrity" OR process == "amfid" OR process == "taskgated"' \
    --info --debug --style compact > "$log_capture" 2>&1 &
log_stream_pid=$!
sleep 1

echo "5/6  Restarting coreaudiod by its launchd PID..."
coreaudiod_pid="$(launchctl print "$service_label" 2>/dev/null \
    | awk '/^[[:space:]]*pid[[:space:]]*=/{print $3; exit}')"
if [[ -n "${coreaudiod_pid:-}" && "$coreaudiod_pid" =~ ^[0-9]+$ ]]; then
    echo "     coreaudiod pid=${coreaudiod_pid}; signalling it (launchd will respawn)."
    kill "$coreaudiod_pid"
else
    echo "     Could not resolve coreaudiod's PID from launchd." >&2
fi
sleep 6
kill "$log_stream_pid" 2>/dev/null || true

echo "6/6  Result:"
if system_profiler SPAudioDataType 2>/dev/null | grep -q 'OpenFaders'; then
    echo "     SUCCESS: OpenFaders devices are loaded:"
    system_profiler SPAudioDataType 2>/dev/null | grep -E 'OpenFaders [1-5]:' | sed 's/^/       /'
else
    echo "     Devices did NOT appear. OpenFaders / AMFI log lines from the restart:"
    grep -iE 'openfaders|app\.openfaders|not valid|code signature|library validation|amfi|Code=-[0-9]' \
        "$log_capture" | grep -iE 'openfaders|amfi|not valid|signature|Code=-' | head -30 | sed 's/^/       /'
    echo "     (full capture saved at $log_capture)"
fi
