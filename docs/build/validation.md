# Testing and verification

Three kinds of evidence, kept strictly apart. A green check in the first two means
the code **builds and its logic holds**; only the third means it **runs on a
console**. Every VideoOut call once succeeded for a whole session while the screen
stayed black, so "it returned 0" is not evidence of anything on screen.

| Where | Proves | How |
|---|---|---|
| CI and the container | it compiles, packs and links; unit logic holds | `build.yml`, `tests/run_tests.sh` |
| The host | logic that needs no console: parsers, policy, layout | `tools/*_host.sh`, `tools/hui_preview.sh` |
| The console | it works | `evo-remote.sh verify`, `cycle`, `sweep`, a screenshot |

Record a hardware result in the issue or commit, not in a doc: date, build id (from
`evo.log`), what you saw, the evidence line.

---

## Unit tests

```bash
docker compose run --rm ps5-dev ./tests/run_tests.sh
```

Builds `tests/test_runner.c` and `test_settings.c` with the real sources of the
modules under test (settings, direct memory, text reader, playlist sniffing, nav,
focus, layout, data path, readdir, theme, the SDF primitives, the clocks and
`pp_playback`, the Emby/Jellyfin/Plex/Nuvio/Stremio/IPTV/Xtream providers, the
provider bundle path rules, `cJSON`, `evo_net`, audio gain) under `-coverage` and
`-DNO_OPENSSL=1`, runs them, and writes a gcov summary to `output/coverage/`. CI
runs it and feeds SonarCloud (`sonar-project.properties`).

Rules:

- **A setting change extends the save/load round-trip**: every enum or int setting
  in `tests/test_settings.c`, and a load of an old file that lacks the newest line
  (it must keep every earlier value and default the new one).
- A parser or policy fix gets a fixture under `tests/fixtures/` (`Canais.m3u` is the
  malformed-playlist case).
- `main.cpp` and the screens link the whole runtime and cannot be host-linked, so
  their logic is not covered; keep logic in the services and `media/` where it can be.

## Host harnesses

Each compiles the **real** source file the app module builds against a mock or a
host FFmpeg, under the address and undefined-behaviour sanitizers, and prints
PASS/FAIL. The dev image has no FFmpeg development libraries, so the first run of
any harness that needs one builds a small static 7.1.1 into `output/host-ffmpeg/`
via `tools/subsync_host.sh`.

| Harness | Builds | Checks |
|---|---|---|
| `tools/subsync_host.sh` | `media/src/evo_subsync.c` | subtitle auto-sync (#102): a synthetic 30-minute film plus real clips; in-sync, shifted, rescaled (25→23.976), wrong-film SRT (must come back low-confidence), embedded tracks. Thresholds are calibrated on real audio, not only the synthetic film |
| `tools/dualsub_host.sh` | `media/src/evo_subtitle.c` | the dual-subtitle engine (#110): independent primary and secondary rings and delays, no source as both tracks, selection survives a reopen, seeks, 4000-iteration thread stress. `SAN=thread` needs `vm.mmap_rnd_bits` ≤ 28 (a stock Docker Desktop VM has 32) |
| `tools/dns_host.sh` | `tools/native-app/stubs/evo_dns.c` | name resolution against a mock `libSceNet`: the console resolver is used, multi-record chains, cache, `0.0.0.0` DNS-filter answers skipped, every failure path, 8 threads at once |
| `tools/streamopts_host.sh` | `media/src/evo_stream_io.c` | which URLs are HLS/DASH playlists and which FFmpeg options a network open gets (no `reconnect_at_eof` on a playlist, `extension_picky=0`, bounded retries, the 12 s raw-stream deadline) |
| `tools/hls_host.sh` | `addons/src/evo_hls_variants.c` | the HLS master-playlist parser and relative-URL join (no FFmpeg needed) |
| `tools/netconnect_host.sh` | `addons/src/evo_net.c` | the bounded non-blocking connect: a dead first address costs one timeout, not the kernel's ~75 s SYN retry, and is remembered |
| `tools/iptv_epg_host.sh` | XMLTV parsing | the EPG scanner on fixtures |

The case behind several of these came from a console log, not from reading code;
the comment at the top of each `*_host.c` names it.

## UI on the host

`./tools/hui_preview.sh` renders every kit screen to `output/uiview/hui_*.png`
through Mesa's surfaceless EGL, with the console's own screen code and SDF shader
([../ui/hui-kit.md](../ui/hui-kit.md)); it takes an optional width and height.
`./tools/uiview.sh --all` does the same for the RmlUi documents (provider screens
and fallbacks; `--only a,b` renders groups in seconds), `uiplay.sh` lays those stills
on one page, and
`prof_rmlui.sh` profiles an RmlUi frame. Answer layout questions here; run the
console only for behaviour. What the host cannot show: HarfBuzz scripts (Thai, Indic,
CJK), live video under the OSD, theme repaint timing, D-pad timing, and RCSS
`filter:` (the host logs `Could not compile filter`; the console is right).

## Hardware checks

```bash
./tools/evo-remote.sh verify                         # every script in tools/checks/
./tools/evo-remote.sh verify --check audio-delay --clip "/mnt/usb0/media/x.mkv"
```

`verify` is one bounded cycle that ends in a check instead of a timed wait: it
soft-closes, closes at `parked=1`, packages and deploys, launches, runs
`tools/checks/<name>.sh`, quits and closes, and prints `PASS`/`FAIL` plus an
evidence line. It stops at the first anomaly and never retries. Each check sets its
setting with `evo-remote.sh set`, plays a clip, reads real telemetry back from
`evo.log`, `evo_ui.json` or `evo_status`, and restores the setting on exit. Today:
`audio-delay`, `volume-boost`, `network-buffer`, `diagnostics-panel`. A new
setting or feature should come with one. The surrounding cycle rules are in
[../console/hardware-cycle.md](../console/hardware-cycle.md).

Other measured captures, all driven over the dev remote on a playing video:
`evo-remote.sh upcompare` (the same held frame with the upscaler Off, Sharp, AI
Standard and Large, saved as BMPs), `dbcompare` (Deep Blacks Off/Low/High), and
`tools/seekstutter_run.py`, `tools/subsync_run.py` with their `*_report.py`
readers for seek stutter and subtitle drift.

### The codec sweep

```bash
./tools/evo-remote.sh sweep [--dir /mnt/usb0/clips] [--secs 30] [--max 3] [--skip name]
python3 tools/sweep_report.py output/logs/evo.log -o output/logs/sweep.md
```

Plays every clip in a directory for a window, moving on at the window, the end or a
stall. EVO measures: each file writes one `sweep v=1 ...` line to `evo.log` when
its decoder closes (`media/src/evo_sweep.c`, compiled into every build, so any
playback session leaves the same line), which is why the runner's next `play`
and a trailing `stop` matter: they flush the previous row. The report is host-only,
so a table can be re-rendered from anyone's log.

A pass/fail sweep once called a broken build green twice, so the table carries
more than a checkmark:

| Column | Meaning |
|---|---|
| Backend | `native` (`sceVideodec2`) or `ffmpeg`, the one actually used |
| Decode ms/frame (avg / p95) | wall time in the `evo_vdec` seam, which both backends share, so they compare directly |
| Budget | one frame period; a p95 above it is a throughput failure whatever the cause |
| Dropped (late / seek) | late drops are the pipeline missing its clock; seek discards are expected |
| Colour | a deterministic 16-pixel probe of the composited frame 120 presents in. Clips that are one source pattern in several codecs (`EVO_TEST_colour_*`) must agree within 6/255, which is how a PQ curve applied from the wrong field was caught |
| Verdict | `realtime`, `slow_decode`, `slow_pipeline` (over 5% late), `no_frames` (the decoder opened and produced nothing), `decode_error`, `no_decoder` |

`make_sweep_clips.sh` builds the corpus (a host tool: the container's FFmpeg is the
cross-build) and `push_sweep_clips.sh` copies it to the console. Compare two runs
with `--colour-ref <earlier evo.log>`.

### Before you tell the user it works

- Check the build id in `evo.log` against the build you meant to test. A stale
  `.ffpfsc` cost two debugging rounds.
- `map_fail` in the `alloc` lines means a real out-of-memory; check it before
  blaming a codec ([../hardware/memory-budget.md](../hardware/memory-budget.md)).
- Look at the picture (`key l3`, `tools/shot.sh grab`) for anything about layout,
  colour or rendering, and read the pixels (`shot.sh probe`, `diff`) rather than
  squinting. Use `evo-remote.sh ui` for navigation questions.
