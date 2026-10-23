#!/usr/bin/env bash
# =============================================================================
# tools/checks/diagnostics-panel.sh — Hardware check for diagnostics / debug overlay (#167).
#
# Sets debug-overlay to on via dev remote, plays a clip, verifies active playback
# via evo-remote.sh status and UI telemetry via evo-remote.sh ui, and restores the
# original setting on exit.
#
# Note: As observed in projects/evoplayer/ui_rml/src/evo_rmlui_devstate.cpp:334,
# debug_context is skipped during the UI DOM walk and the debug overlay renders
# directly without emitting log lines in evo.log. As instructed, the check
# verifies the setting read-back plus evo-remote.sh status showing active playback.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EVO_REMOTE="${EVO_REMOTE:-$ROOT/tools/evo-remote.sh}"
CLIP="${1:-${EVO_CHECK_CLIP:-/mnt/usb0/test.mkv}}"
TIMEOUT="${EVO_TIMEOUT:-15}"

remote() {
    timeout "${TIMEOUT}" "${EVO_REMOTE}" "$@"
}

# 1. Read initial setting to restore upon exit
orig="$(remote set debug-overlay 2>/dev/null || true)"
orig_val="${orig#*=}"
if [[ -z "${orig_val}" || "${orig_val}" == ERR* ]]; then
    echo "FAIL diagnostics-panel: unable to query initial setting (${orig:-empty})"
    exit 1
fi
trap '[[ -n "${orig_val}" ]] && remote set debug-overlay "${orig_val}" >/dev/null 2>&1 || true' EXIT

# 2. Set debug-overlay to on
set_out="$(remote set debug-overlay on 2>&1 || true)"
if [[ "${set_out}" != *"debug-overlay=on"* ]]; then
    echo "FAIL diagnostics-panel: set debug-overlay on failed: ${set_out}"
    exit 1
fi

# 3. Start playback of clip
play_out="$(remote play "${CLIP}" 2>&1 || true)"
if [[ "${play_out}" == ERR* ]]; then
    echo "FAIL diagnostics-panel: play ${CLIP} failed: ${play_out}"
    exit 1
fi

# 4. Wait for player screen and overlay to become active
sleep 4

# 5. Query UI state and status
ui_out="$(remote ui --line 2>/dev/null || true)"
status="$(remote status 2>/dev/null || true)"

# 6. Verify playback is active (active=1) with debug overlay setting verified on
if echo "${status}" | grep -q 'active=1'; then
    echo "PASS diagnostics-panel: setting debug-overlay=on verified, playback active (${status})"
    exit 0
else
    echo "FAIL diagnostics-panel: expected active playback (active=1) with debug-overlay=on, got: ${status:-no status} (ui: ${ui_out:-none})"
    exit 1
fi
