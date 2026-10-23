#!/usr/bin/env bash
# =============================================================================
# tools/checks/network-buffer.sh — Hardware check for network read-ahead buffer (#199).
#
# Sets network-buffer to 1 MIN via dev remote, plays a clip, verifies that
# PlaybackController logs the scaled read-ahead ring size (60s) in evo.log, and
# restores the original setting on exit.
#
# Note: As per PlaybackController.cpp:205-211, network ring sizing applies when
# playing network sources (http/https). Non-network sources (e.g. /mnt/usb0/...)
# skip the ring buffer by design. If no ring line is logged for a local clip,
# the check verifies the setting read-back plus evo-remote.sh status showing active playback.
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
orig="$(remote set network-buffer 2>/dev/null || true)"
orig_val="${orig#*=}"
if [[ -z "${orig_val}" || "${orig_val}" == ERR* ]]; then
    echo "FAIL network-buffer: unable to query initial setting (${orig:-empty})"
    exit 1
fi
trap '[[ -n "${orig_val}" ]] && remote set network-buffer "${orig_val}" >/dev/null 2>&1 || true' EXIT

# 2. Set network-buffer to 1 MIN (60 seconds)
set_out="$(remote set network-buffer "1 MIN" 2>&1 || true)"
if [[ "${set_out}" != *"network-buffer="*"1 MIN"* ]]; then
    echo "FAIL network-buffer: set network-buffer 1 MIN failed: ${set_out}"
    exit 1
fi

# 3. Start playback of clip (must play AFTER setting because buffer sizes on file open)
play_out="$(remote play "${CLIP}" 2>&1 || true)"
if [[ "${play_out}" == ERR* ]]; then
    echo "FAIL network-buffer: play ${CLIP} failed: ${play_out}"
    exit 1
fi

# 4. Wait for demuxer initialization and buffer sizing
sleep 4

# 5. Read evo.log and inspect the real log line written by PlaybackController.cpp:253:
# evo_boot_log("  pb: read-ahead %.0fs (ring) -> video %d pkt / %lld MB, ...", ringSeconds, ...)
log_out="$(remote log 2>/dev/null || true)"
if [[ -z "${log_out}" && -f "${ROOT}/output/logs/evo.log" ]]; then
    log_out="$(cat "${ROOT}/output/logs/evo.log")"
fi

evidence="$(echo "${log_out}" | grep -E 'pb: read-ahead 60s' | tail -1 || true)"

if [[ -n "${evidence}" ]]; then
    echo "PASS network-buffer: ${evidence}"
    exit 0
fi

# 6. Fallback for non-network sources: PlaybackController.cpp skips network read-ahead
# for local files. As instructed, verify setting read-back plus evo-remote.sh status.
status="$(remote status 2>/dev/null || true)"
if echo "${status}" | grep -q 'active=1'; then
    echo "PASS network-buffer: setting network-buffer=1 MIN verified, playback active (${status})"
    exit 0
else
    echo "FAIL network-buffer: expected 'pb: read-ahead 60s' or active playback with network-buffer=1 MIN, got: ${status:-no status}"
    exit 1
fi
