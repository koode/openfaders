#!/bin/bash
#
# Installs the OpenFaders virtual audio driver into the system Core Audio HAL
# plug-in directory and asks coreaudiod to rescan it.
#
# Run with sudo. Restarting coreaudiod briefly interrupts ALL audio on this Mac,
# so do not run this during a call or a recording.
#
# Usage: sudo ./Scripts/install-driver.sh [path-to-OpenFadersDriver.driver]

set -euo pipefail

readonly driver_name="OpenFadersDriver.driver"
readonly hal_directory="/Library/Audio/Plug-Ins/HAL"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_driver="${1:-$repo_root/build/Release/$driver_name}"

if [[ $EUID -ne 0 ]]; then
    echo "This script must run with sudo: it writes to $hal_directory and restarts coreaudiod." >&2
    echo "Try: sudo $0" >&2
    exit 1
fi

if [[ ! -d "$source_driver" ]]; then
    echo "Driver bundle not found at: $source_driver" >&2
    echo "Build it first (make build) or pass the bundle path as the first argument." >&2
    exit 1
fi

echo "Installing $source_driver"
echo "      into $hal_directory/"
rm -rf "${hal_directory:?}/$driver_name"
cp -R "$source_driver" "$hal_directory/$driver_name"

# The signature must survive the copy or coreaudiod will refuse to load it.
codesign --verify --strict "$hal_directory/$driver_name"
echo "Signature verified."

"$repo_root/Scripts/reload-coreaudiod.sh"

echo
echo "Done. The OpenFaders output devices should now appear in"
echo "System Settings > Sound and in Audio MIDI Setup."
