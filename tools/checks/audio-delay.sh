#!/usr/bin/env bash
# =============================================================================
# tools/checks/audio-delay.sh — Hardware check for audio delay / lip-sync (#170).
#
# Sets audio-delay to +100 ms via dev remote, plays a clip, verifies that
# PlaybackController logs the applied delay (delay=100ms) in evo.log, and restores
# the original setting on exit.
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
orig="$(remote set audio-delay 2>/dev/null || true)"
orig_val="${orig#*=}"
if [[ -z "${orig_val}" || "${orig_val}" == ERR* ]]; then
    echo "FAIL audio-delay: unable to query initial setting (${orig:-empty})"
    exit 1
fi
trap '[[ -n "${orig_val}" ]] && remote set audio-delay "${orig_val}" >/dev/null 2>&1 || true' EXIT

# 2. Set audio delay to +100 ms
set_out="$(remote set audio-delay +100 2>&1 || true)"
if [[ "${set_out}" != *"audio-delay="*"+100"* ]]; then
    echo "FAIL audio-delay: set audio-delay +100 failed: ${set_out}"
    exit 1
fi

# 3. Start playback of clip
play_out="$(remote play "${CLIP}" 2>&1 || true)"
if [[ "${play_out}" == ERR* ]]; then
    echo "FAIL audio-delay: play ${CLIP} failed: ${play_out}"
    exit 1
fi

# 4. Wait for audio pipeline initialization
sleep 4

# 5. Read evo.log and inspect the real log line written by PlaybackController.cpp:1308:
# evo_boot_log("audio: source %dch -> port %dch (output=%s, pt=%d, boost=%d%%, delay=%dms) handle=%d", ...)
log_out="$(remote log 2>/dev/null || true)"
if [[ -z "${log_out}" && -f "${ROOT}/output/logs/evo.log" ]]; then
    log_out="$(cat "${ROOT}/output/logs/evo.log")"
fi

evidence="$(echo "${log_out}" | grep -E 'audio: source .*delay=100ms' | tail -1 || true)"

if [[ -n "${evidence}" ]]; then
    echo "PASS audio-delay: ${evidence}"
    exit 0
else
    last_audio="$(echo "${log_out}" | grep -E 'audio:' | tail -1 || true)"
    echo "FAIL audio-delay: expected 'delay=100ms' in evo.log, saw: ${last_audio:-no audio log entry found}"
    exit 1
fi
