#!/bin/bash
#
# Development reload for the OpenFaders driver. Ad-hoc signs the bundle, installs
# it into the system HAL directory with a signature-preserving copy and correct
# ownership, restarts coreaudiod by its launchd PID (which works with SIP on),
# and reports whether the virtual devices loaded. If they do not, it prints the
# coreaudiod / AMFI log lines captured across the restart, which name the reason.
#
# Run with sudo. Restarting coreaudiod briefly interrupts ALL audio on this Mac.
#
# Usage: sudo ./Scripts/reload-driver.sh [path-to-OpenFadersDriver.driver]

set -euo pipefail

readonly driver_name="OpenFadersDriver.driver"
readonly hal_directory="/Library/Audio/Plug-Ins/HAL"
readonly service_label="system/com.apple.audio.coreaudiod"
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

echo "1/6  Stripping extended attributes and ad-hoc re-signing the source..."
xattr -cr "$source_driver"
codesign --force --sign - "$source_driver"
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
sleep 5
kill "$log_stream_pid" 2>/dev/null || true

echo "6/6  Result:"
if system_profiler SPAudioDataType 2>/dev/null | grep -q 'OpenFaders'; then
    echo "     SUCCESS: OpenFaders devices are loaded:"
    system_profiler SPAudioDataType 2>/dev/null | grep -E 'OpenFaders [1-5]:' | sed 's/^/       /'
else
    echo "     Devices did NOT appear. Relevant log lines from the restart:"
    grep -iE 'openfaders|app\.openfaders|Plug-Ins/HAL|plug-?in|code ?sign|signature|library validation|not valid|reject|denied|amfi|not owned|invalid' \
        "$log_capture" | head -40 | sed 's/^/       /'
    echo "     (full capture saved at $log_capture)"
fi
