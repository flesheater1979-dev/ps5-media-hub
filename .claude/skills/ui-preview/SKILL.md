---
name: ui-preview
description: Design or change an EVO list screen (stream picker, audio picker, recent, favorites, provider lists) and look at it on the host before touching the console. Use when the user wants UI suggestions or mockups, asks to see the layout, or when adding pills, a details card or a busy card to a kit list screen. Renders real screens with tools/hui_preview.sh.
---

# Design a list screen and view it on the host

The user wants to SEE options, not read ASCII sketches. Render real mockups with the kit's
own renderer, show the PNG paths, let them pick, then wire the real screen. The console
build cannot take screenshots reliably, so the host render is the layout evidence.

## 1. Where things live

| Part | File |
|---|---|
| Shared list params (row `pills`, details card `panel_*`, busy card `loading`) | `projects/evoplayer/ui_rml/include/evo_rmlui_bridge.h` (`evo_rmlui_list_params_t`) |
| Drawing (rows, pills, card, busy card) | `projects/evoplayer/ui_kit/src/evo_hui_screens.cpp` (`ListScreen`) + `.../include/evo_hui_screens.hpp` |
| Real screens that fill the params | `core/src/screens/ProviderHostScreen.cpp` (`renderStreamPicker`), `core/src/screens/AudioTrackPickerScreen.cpp` (`render`) |
| Host fixtures (fake data) | `tools/hui_preview/hui_preview.cpp`, the `fixtures[]` list: `list_streams`, `list_streams_wide`, `list_audio_pills`, `list_audio_card`, `list_audio_loading` |

The RmlUi fallback ignores pills, panel and loading. Only the kit UI draws them.

## 2. Add or change a mockup

1. Edit the fixture lambda (copy `list_streams`). Strings must be `static` or literals.
2. Anything new you add to the params MUST be reset in `clear_focus` (it resets pills, `row_count`, `panel_count`, `loading`), or it leaks into the next fixture.
3. Render: `timeout 580 ./tools/hui_preview.sh 2>&1 | grep -iE "error|<fixture>"` (1-3 min; renders every fixture).
4. Read `output/uiview/hui_<fixture>.png` and show the user the path, with the variants side by side.
5. Fixture artefacts to ignore: the "1 OF 23" counter, made-up data.

## 3. Pills and cards (what exists)

- `rows[i].pills` = tab-separated chips on the row's second line, replacing `detail`. A leading `+` makes a chip stand out (HDR, playing, resolution).
- `panel_count` > 0 narrows the rows and draws a details card on the right (up to 10 label/value facts, about 9 fit). Facts come from the provider (`evo_stream_choice_t.facts`, "Name\tValue\n" lines) or the screen.
- `loading=1` + `loading_text` draws a dimmed busy card with three pulsing dots. Use it when the screen's work runs on a worker.

## 4. Blocking work: never on the UI thread

A reopen, a network call or a decoder switch on the screen thread freezes the whole loop (the
picker just sits on its last frame, no spinner can draw). Pattern: `AudioTrackPickerScreen`
`activateSelection()` starts a joinable `pthread` (2 MB stack), sets an atomic state
(0 idle, 1 running, 2 done, 3 failed), `handleInput` swallows keys while running, `render`
sets `params.loading`, `update()` joins the thread, toasts the result and navigates back,
`onExit()` joins a live worker. The provider screen does the same for playback start.

## 5. Then verify and ship

Wire the real screen, `docker compose run --rm ps5-dev ./scripts/build-evoplayer.sh` (or the
full package from `ship`), drive it with `ui --line` (focus text) on the console, and say
plainly that pixels were checked on the host only. Commit with `commit-evo`.
