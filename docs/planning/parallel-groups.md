# Parallel Task Groups for Open Issues

## Issues
- #214 fix(ui): backing out of playback loses the selection (episode 3 goes back to episode 1) - files: projects/evoplayer/core/src/screens/BrowserScreen.cpp, projects/evoplayer/core/include/evo/screens/BrowserScreen.hpp, projects/evoplayer/core/src/screens/ScreenManager.cpp, projects/evoplayer/core/include/evo/screens/ScreenManager.hpp, projects/evoplayer/core/src/screens/ProviderHostScreen.cpp - agy fit: yes (pure C++ screen focus and selection restore testable via hui_preview)
- #198 feat(diagnostics): startup failures are visible - boot phase and reason written where users can read it - files: projects/evoplayer/src/evo_boot_log.c, projects/evoplayer/include/evo_boot_log.h, projects/evoplayer/main.cpp, projects/evoplayer/core/src/Application.cpp, projects/evoplayer/core/include/evo/Application.hpp, docs/README.md - agy fit: yes (boot logging and status reporting compile in container ps5-dev build)
- #188 feat(video): deinterlacing for 1080i IPTV and broadcast files - files: projects/evoplayer/media/src/evo_vdec_native.c, projects/evoplayer/media/src/evo_vdec_ffmpeg.c, projects/evoplayer/media/src/evo_playback.c, projects/evoplayer/media/include/evo_vdec.h, projects/evoplayer/core/src/screens/SettingsScreen.cpp, projects/evoplayer/core/src/services/SettingsService.cpp - agy fit: partly (filter code compiles on host, but verifying combing/stutter requires 1080i stream on console)
- #180 fix(iptv): a malformed playlist entry must not fail the whole playlist - files: projects/evoplayer/addons/src/provider_iptv.c, projects/evoplayer/media/src/evo_playlist_sniff.c, tests/test_runner.c, tests/fixtures/Canais.m3u - agy fit: yes (pure C parser fix with existing test suite in test_runner.c)
- #178 fix(audio): network stall must not flush the audio queue (find out how much audio EVO drops) - files: projects/evoplayer/media/src/evo_audio_out.c, projects/evoplayer/media/include/evo_audio_out.h, projects/evoplayer/media/src/evo_playback.c, projects/evoplayer/media/src/evo_stream_io.c, projects/evoplayer/media/src/evo_demux.c - agy fit: partly (queue logic compiles in container, but measuring stall drop requires network disruption on console)
- #209 test(hardware): verification pass - sync, video, audio, IPTV, providers, text - files: tools/evo-remote.sh, docs/planning/roadmap.md, docs/planning/project-tracking.md - agy fit: no (hardware-only verification run across console and remote tools, forbidden by agy rules)

## Groups
Independent groups can run in parallel. Overlapping issues are serialized within their group.

- **Group A (UI Navigation & Selection)**:
  1. #214
  Can run in parallel with Groups B, C, D.

- **Group B (Startup Diagnostics)**:
  1. #198
  Can run in parallel with Groups A, C, D.

- **Group C (Playback Pipeline)**:
  1. #188 (video deinterlacing filter & settings)
  2. #178 (audio queue stall recovery)
  *Reason*: #188 and #178 both touch `evo_playback.c`. Run sequentially in priority order (#188 first, then #178).
  Can run in parallel with Groups A, B, D.

- **Group D (IPTV & Addons)**:
  1. #180
  Can run in parallel with Groups A, B, C.

- **Group E (Hardware Verification)**:
  1. #209
  Must run after Groups A through D land. Run by human / reviewer on physical PS5 hardware (not agy).

## Shared Hot Spots
- `projects/evoplayer/media/src/evo_playback.c`: Shared between #188 and #178. Enforced inside Group C with strict sequential ordering (#188 then #178).
- `projects/evoplayer/core/src/Application.cpp`: Touched by Group B (#198 boot diagnostics); central router for screen transitions (Group A) and service setup (Group C). Rule: one group at a time on that file.
- `projects/evoplayer/core/src/services/SettingsService.cpp`: Touched by Group C (#188 settings) and test runner. Rule: serialize settings additions to avoid format string drift.
