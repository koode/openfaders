#!/bin/bash
#
# Restarts coreaudiod so it rescans the Core Audio HAL plug-ins. Run with sudo.
# Reloading briefly interrupts ALL audio on this Mac.
#
# With System Integrity Protection enabled, `launchctl kickstart -k` on the
# system audio service is not permitted. In that case we resolve the exact PID
# that launchd reports for the service label (never a process name/pattern kill)
# and signal that single PID; launchd then respawns coreaudiod.

set -euo pipefail

readonly service_label="system/com.apple.audio.coreaudiod"

if [[ $EUID -ne 0 ]]; then
    echo "Run with sudo: restarting coreaudiod needs root." >&2
    exit 1
fi

echo "Restarting coreaudiod (system audio will glitch for a moment)..."

if launchctl kickstart -k "$service_label" 2>/dev/null; then
    echo "Restarted coreaudiod via launchctl kickstart."
    exit 0
fi

# kickstart was refused (typically SIP). Fall back to signalling the exact PID
# launchd reports for this service, which is the launcher's own job id, not a
# name match against the process table.
coreaudiod_pid="$(launchctl print "$service_label" 2>/dev/null \
    | awk '/^[[:space:]]*pid[[:space:]]*=/{print $3; exit}')"

if [[ -z "${coreaudiod_pid:-}" || ! "$coreaudiod_pid" =~ ^[0-9]+$ ]]; then
    echo "Could not resolve coreaudiod's PID from launchd. Reboot to load the driver." >&2
    exit 1
fi

echo "kickstart blocked by SIP; restarting coreaudiod by its launchd PID ${coreaudiod_pid}."
kill "$coreaudiod_pid"
echo "Signalled PID ${coreaudiod_pid}; launchd will respawn coreaudiod."
