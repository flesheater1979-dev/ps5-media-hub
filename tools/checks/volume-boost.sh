#!/usr/bin/env bash
# =============================================================================
# tools/checks/volume-boost.sh — Hardware check for audio volume boost (#166).
#
# Sets volume-boost to +100% via dev remote, plays a clip, verifies that
# evo_audio_gain / evo_audio_out logs the active gain boost in evo.log, and restores
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
orig="$(remote set volume-boost 2>/dev/null || true)"
orig_val="${orig#*=}"
if [[ -z "${orig_val}" || "${orig_val}" == ERR* ]]; then
    echo "FAIL volume-boost: unable to query initial setting (${orig:-empty})"
    exit 1
fi
trap '[[ -n "${orig_val}" ]] && remote set volume-boost "${orig_val}" >/dev/null 2>&1 || true' EXIT

# 2. Set volume boost to +100%
set_out="$(remote set volume-boost "+100%" 2>&1 || true)"
if [[ "${set_out}" != *"volume-boost="*"+100%"* ]]; then
    echo "FAIL volume-boost: set volume-boost +100% failed: ${set_out}"
    exit 1
fi

# 3. Start playback of clip
play_out="$(remote play "${CLIP}" 2>&1 || true)"
if [[ "${play_out}" == ERR* ]]; then
    echo "FAIL volume-boost: play ${CLIP} failed: ${play_out}"
    exit 1
fi

# 4. Wait for audio pipeline initialization and block processing
sleep 4

# 5. Read evo.log and inspect the real log line written by evo_audio_out.c:162 or PlaybackController.cpp:1308:
# evo_boot_log("audio gain: +%d%% block peak %d -> %d", evo_audio_gain_get_boost(), peak_in, peak_out)
# evo_boot_log("audio: source %dch -> port %dch (output=%s, pt=%d, boost=%d%%, delay=%dms) handle=%d", ...)
log_out="$(remote log 2>/dev/null || true)"
if [[ -z "${log_out}" && -f "${ROOT}/output/logs/evo.log" ]]; then
    log_out="$(cat "${ROOT}/output/logs/evo.log")"
fi

evidence="$(echo "${log_out}" | grep -E 'audio gain: \+100% block peak|audio: source .*boost=100%' | tail -1 || true)"

if [[ -n "${evidence}" ]]; then
    echo "PASS volume-boost: ${evidence}"
    exit 0
else
    last_audio="$(echo "${log_out}" | grep -E 'audio' | tail -1 || true)"
    echo "FAIL volume-boost: expected 'audio gain: +100%' or 'boost=100%' in evo.log, saw: ${last_audio:-no audio log entry found}"
    exit 1
fi
