# RmlUi

RmlUi (the retained-mode HTML/RCSS engine) was EVO's whole UI layer from v0.8.0, and
every screen call still goes through its bridge. Today **EVO's own screens are drawn by
the ps5-homebrew-ui kit** ([hui-kit.md](hui-kit.md)); RmlUi remains for:

- **provider screens**: a provider's UI is a set of `.rml`/`.rcss` documents, the
  embedded ones EVO ships (`mediaserver`, `iptv`, `xtream`, the fallback skin) and
  bundles a provider serves over HTTP at run time
  ([../providers/architecture.md](../providers/architecture.md)). For the embedded
  documents RmlUi still does the data binding, focus and layout while the kit
  rasterises; a downloaded bundle is drawn by RmlUi itself;
- **the fallback** for any screen the kit declines (the kit failed to start, or
  `/mnt/usb0/evo_no_hui` exists);
- **the dev tooling**: `evo-remote.sh ui` reads focus from RmlUi's DOM
  ([../console/remote-control.md](../console/remote-control.md)), and `uiview.sh`
  renders the documents on the host.

## Where it lives

| Path | Role |
|---|---|
| `ui_rml/include/evo_rmlui_bridge.h`, `src/evo_rmlui_bridge.cpp` | the C API the screens call: one parameter struct, one `evo_rmlui_update_<screen>()` and one `evo_rmlui_render_<screen>()` per screen. The bridge tries `evo_hui_render_<screen>()` first and falls back to RmlUi if it returns 0 |
| `evo_rmlui_app.h/.cpp` | `EvoRmlApp`: the contexts (main, toast, keyboard, debug; one per provider), document loading, per-screen state updates, theme push |
| `evo_rmlui_render_agc.cpp` | `EvoRenderInterfaceAGC`: turns RmlUi geometry into `sceAgc` command buffers (stencil clip masks, backdrop blur layers) |
| `evo_rmlui_render.cpp` | the CPU coverage rasteriser, used by the host preview only |
| `evo_rmlui_provider.cpp`, `evo_rmlui_provider_art.cpp` | the provider host: data model, focus, events, remote artwork through the `evo:mem/` registry |
| `evo_rmlui_bundle.cpp`, `evo_rmlui_bundle_data.cpp` | the embedded assets. `tools/bundle_rml_assets.py` generates the `_data.cpp` (tracked, and a huge diff each build) because the app's own `/app0` mount cannot be traversed (`fstatfs`/`getdents` fail with EPERM) |
| `evo_rmlui_fileinterface.cpp` | RmlUi file access to the bundle, `evo:mem/` textures and `/assets/...` paths |
| `evo_rmlui_devstate.cpp` | the `evo_ui.json` writer for the dev remote |
| `rmlui_patch/` | RmlUi's corner tessellation with a finer point count, swapped over a build-local copy of `librmlui.a` |
| `../assets/rml/` | `*.rml` + `*.rcss` per screen (`launch`, `browser`, `settings`, `playback`, `dialog`, `keyboard`, `toast`, `mediaserver`, `iptv`, `xtream`, `provider_fallback`, ...) |
| `../assets/fonts/`, `../assets/icons/` | Lato, Roboto, Noto (Latin, Thai, Indic) and DejaVu; PNG icons (Lucide-derived concept icons, controller button glyphs, provider brand marks) |

Screens bind their state by name: the DOM never references C structs, and the bridge
copies the parameter struct into it each frame. Content is never mocked; the documents
show live engine state.

## Rules that cost hardware cycles

Every one of these produced a silent wrong result, not an error.

| Do this | Not this | Why |
|---|---|---|
| `{{ expression }}` for text | `data-value="expr"` | `data-value` is the two-way binding of a form control's `value`; on a div it renders nothing, with no warning |
| `tab-index: auto` and `nav: auto` in RCSS | a `tabindex` attribute | RmlUi has no such attribute. Without the RCSS properties a row is not focusable and spatial navigation does nothing, so the screen reads as hung |
| `display: block` stated | relying on a div being block | RmlUi's `display` defaults to `inline` for every element; there is no HTML default stylesheet |
| `data-attr-rowid="row.id"` and `activate()` | `activate(row.id)` | inside a `data-for`, a `data-event` argument bakes the first iteration's alias, so every row reports row 0's id |
| `#rrggbbaa` colours | `rgba(r,g,b,0.3)` | the alpha in `rgba()` is 0 to 255, so `0.3` is transparent |
| `dp` units on a 1920×1080 canvas | `px` | the context's density-independent ratio scales it to the panel |
| clip with scissor or the stencil path | `overflow: hidden` on a rounded box with solid children | on the AGC path (`clip_masks=0`) the solid children vanish |

- `filter:` and `backdrop-filter:` work on the console (blur) and **not** in the host
  preview (`Could not compile filter`): a bundle using them looks broken in `uiview`
  and correct on hardware, the opposite of the usual risk.
- Provider contexts do not receive EVO's theme push (it stamps inline colours that
  would overwrite what a bundle exists to supply); a bundle sets every colour itself.
- RmlUi scripting is off. A bundle is markup, styles, fonts and images, never code.

## Previewing

`./tools/uiview.sh --all` renders every document (and a `*_stress` fixture per screen
with deliberately over-long strings) to `output/uiview/rml_*.png`; `--only a,b` renders
groups in seconds. The harness is `tools/uiview_playback_rml.cpp` (it also stages a local
server and the real IPTV provider, so provider screens render with real data), and
`uiplay.sh` lays the stills on one page. Add a fixture there for a new document. A
regression pass at the end renders sequences (a screen, then a detour through the resume
dialog, the image viewer and the player, then the screen again, which must be
pixel-identical) because several shipped bugs were one screen poisoning the next.
