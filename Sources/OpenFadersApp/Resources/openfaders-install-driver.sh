#!/bin/bash
#
# Installs the OpenFaders audio driver that ships inside the app into the system
# Core Audio HAL directory and restarts coreaudiod so the virtual devices appear.
# The app runs this as root through a native admin-authentication prompt; it is
# not meant to be run by hand. It locates the driver relative to its own place
# inside the app bundle, so no paths are passed in.

set -euo pipefail

resources_dir="$(cd "$(dirname "$0")" && pwd)"
contents_dir="$(dirname "$resources_dir")"
driver_source="$contents_dir/SharedSupport/OpenFadersDriver.driver"

hal_directory="/Library/Audio/Plug-Ins/HAL"
driver_destination="$hal_directory/OpenFadersDriver.driver"

if [[ ! -d "$driver_source" ]]; then
    echo "OpenFaders: driver not found in app bundle at $driver_source" >&2
    exit 1
fi

mkdir -p "$hal_directory"
rm -rf "$driver_destination"
/usr/bin/ditto "$driver_source" "$driver_destination"
/usr/sbin/chown -R root:wheel "$driver_destination"
/bin/chmod -R go-w "$driver_destination"

# Restart coreaudiod so it rescans the HAL directory. Prefer launchctl kickstart;
# under System Integrity Protection that is refused, so fall back to signalling the
# exact PID launchd reports for the service (never a process name/pattern kill).
if ! /bin/launchctl kickstart -k system/com.apple.audio.coreaudiod 2>/dev/null; then
    coreaudiod_pid="$(/bin/launchctl print system/com.apple.audio.coreaudiod 2>/dev/null \
        | /usr/bin/awk '/^[[:space:]]*pid[[:space:]]*=/{print $3; exit}')"
    if [[ -n "${coreaudiod_pid:-}" && "$coreaudiod_pid" =~ ^[0-9]+$ ]]; then
        /bin/kill "$coreaudiod_pid" || true
    fi
fi

echo "OpenFaders: driver installed."
