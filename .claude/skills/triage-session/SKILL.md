---
name: triage-session
description: Work out why EVO froze, stalled, buffered, crashed or would not close on the PS5, from its status line and evo.log. Use when the user says it is stuck / frozen / buffering / lagging after a seek, audio switch or stream change, or when evo-remote.sh quit refuses to park. Read-only: it sends no commands to the console.
---

# Triage a stuck or crashed EVO session

Read only. Do not send keys or seeks while the user may be driving the console. A tool
call the user rejected means they are using it: stop and wait for their word.

## 1. Snapshot (two calls)

```bash
./tools/dx.sh ./tools/evo-remote.sh status | tail -1
./tools/dx.sh ./tools/evo-remote.sh log > output/triage.log 2>&1; wc -l output/triage.log
```

Status fields: `t=` is the main loop's own clock (frozen `t` over several samples = the UI
thread is blocked, a blocked screen looks like a hang), `scr=` screen id (2 player, 31 audio
picker, 23 provider browse, 32 closing), `pos=` playback clock, `be=1` a backend is open,
`active=1` playing, `parked=1` safe to `close`.

## 2. Is it dead? (check this before anything else)

`grep -n "CRASH signal" output/triage.log | tail`. A `CRASH` line with `t=` no longer
advancing means EVO crashed. A crashed EVO is already closed: relaunch with
`evo-remote.sh launch`, and deploy with `--force` (memory: crashed-evo-is-closed). If
`quit` says "did not report parked" and status shows `scr=32 active=0`, look for CRASH
before concluding anything. Never `close` remotely without `parked=1`.

## 3. Timeline greps

```bash
grep -nE "REMOTE_CMD|SEEK_BEGIN|SEEK_SETTLE|REBUFFER|PREBUFFER|FIRST_FRAME|scrub:|soft close|CRASH|Closing bitstream|passthrough started" output/triage.log | tail -40
grep -nE "pio:|HTTP error|Stream ends prematurely|read error|reader waiting" output/triage.log | tail
grep -vE "alloc \[play\]|agc health|evo_opendir|pace:" output/triage.log | tail -40
```

## 4. What the evidence means

| Seen | Meaning |
|---|---|
| `pio: reader waiting N s` / `not fetched in 30 s - read error` | Network. The reader gave up on a chunk. |
| None of those, `large=` in the `alloc [play]` lines flat | Not network: the 128 MB read-ahead is full and the reader is idle, so the consumer stopped. |
| `heartbeat decodes=` stops printing (every ~12 s) | The video decoder is no longer being fed. |
| `pace: ... video_frames=0` but `presents` ~60/s | UI alive, video clock stopped. |
| A seek request with no `SEEK_BEGIN` after it | The request never reached the demux thread (a hold, a deadlock, or a scrub that never committed). |
| `status` still shows `scrub` after 40 s | A pad scrub never committed (fixed 2026-10-24 by the 3 s preview cap in `tickScrubAutoCommit`). |
| `P8_03_PREBUFFER_DONE deadline packets=0` | The server delivered nothing for 5 s after a reopen: slow server, not a player bug. |
| `net GET ... in 1100-1700 ms` | The Jellyfin proxy is slow per request; every browse step costs seconds. |
| `http=400` on `Sessions/Playing/Progress` | Jellyfin rejects EVO's progress report after a reopen. Known, harmless so far. |

## 5. Report

Result first, then the next step, short. Say what the log proves and what is a guess. Offer
the reproduction (`repro-playback`) before changing code, and take over the console only
after the user says so.
