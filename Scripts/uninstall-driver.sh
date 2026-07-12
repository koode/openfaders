#!/bin/bash
#
# Removes the OpenFaders virtual audio driver from the system Core Audio HAL
# plug-in directory and asks coreaudiod to rescan.
#
# Run with sudo. Restarting coreaudiod briefly interrupts ALL audio on this Mac.
#
# Usage: sudo ./Scripts/uninstall-driver.sh

set -euo pipefail

readonly driver_name="OpenFadersDriver.driver"
readonly hal_directory="/Library/Audio/Plug-Ins/HAL"

if [[ $EUID -ne 0 ]]; then
    echo "This script must run with sudo: it writes to $hal_directory and restarts coreaudiod." >&2
    echo "Try: sudo $0" >&2
    exit 1
fi

if [[ -d "$hal_directory/$driver_name" ]]; then
    echo "Removing $hal_directory/$driver_name"
    rm -rf "${hal_directory:?}/$driver_name"
else
    echo "Nothing to remove: $hal_directory/$driver_name is not installed."
fi

echo "Restarting coreaudiod (system audio will glitch for a moment)..."
launchctl kickstart -k system/com.apple.audio.coreaudiod
echo "Done."
