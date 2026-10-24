---
name: repro-playback
description: Reproduce a playback problem on the PS5 by driving EVO yourself - start a Jellyfin or other provider stream, switch audio, seek, scrub with the pad, open pickers - and sample status and log around each step. Use after the user reports a freeze, stall or lag and has said you may take over the console.
---

# Reproduce a playback problem on the console

Ask before taking the console if the user might be using it. A rejected tool call means
they are driving: stop. Use `ship` for build and deploy; this skill is the drive-and-watch part.

## 1. Start playback

```bash
./tools/dx.sh ./tools/evo-remote.sh provider jellyfin     # about 10 s to load shelves
# then read each step with `ui --line` instead of guessing; the order of items changes
for k in down down cross; do ./tools/dx.sh ./tools/evo-remote.sh key $k | tail -1 | cut -c1-110; sleep 3; done
```

Series, season and episode are each one `cross`. Wait for `active=1 pos>0` in `status`
before the next step (poll, do not sleep blind):

```bash
for i in 1 2 3 4 5 6 7 8; do s=$(timeout 20 ./tools/dx.sh ./tools/evo-remote.sh status | tail -1)
  echo "$s" | grep -o "t=[0-9]* scr=[0-9]* be=[0-9] pos=[0-9.]*"; case "$s" in *active=1*pos=[1-9]*) break;; esac; sleep 3; done
```

## 2. Keys in the player (screen 2)

| Key | Does |
|---|---|
| `r2` | Audio / quality picker (screen 31). Then `down`/`up`, `cross` to switch. |
| `down` / `up` | Subtitle picker / subtitles on-off |
| `left` / `right` | Pad scrub, +-10 s per press, commits 0.6 s after the last press (waits up to 3 s for the preview frame) |
| `cross` | Pause or resume. It does NOT commit a scrub. |
| `circle` | Stop prompt |
| `seek +N` / `seek -N` / `seek N` | Remote seek. A different code path from the pad scrub: reproduce with the PAD keys when the user used the pad. |
| `l3` | Screenshot to the USB stick (see `ship` section 3, `tools/grab_shot.sh`) |

## 3. The sequences that found bugs

- **Audio switch:** `key r2`, `key down`, `key cross`, then sample `status` every 2 s. A frozen
  `t=` during the switch means the UI thread is blocked. The switch takes 6-20 s (server).
- **Scrub after a switch:** `key right` x4-6, then sample. Healthy = `scrub: preview still
  loading after 3 s - committing the seek anyway` or a quick `SEEK_BEGIN` and `pos` moving.
  Broken = `pos` frozen, `status` shows `scrub`, no `SEEK_BEGIN`.
- Sample both, then read the log with `triage-session` section 3.

## 4. Pacing and safety

- Key presses through the container take 1-3 s each; the log timestamps (`REMOTE_CMD key`) show the real spacing.
- Never stack launches, never deploy over a running EVO. Close with `quit`, wait for `parked=1`,
  then `close`. A soft close after a stall once crashed EVO: check the log for `CRASH` and if
  present treat the console as closed (relaunch directly, deploy with `--force`).
- Bound every call with `timeout`. One cycle at a time; stop at the first anomaly, no auto-retry.
- Leave the console in a sane state (picker closed, or say where it is).
