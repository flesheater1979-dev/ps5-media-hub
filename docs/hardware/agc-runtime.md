# The AGC runtime

`media/src/evo_agc_runtime.c` is the only owner of `sceAgc` and `sceVideoOut` for
the whole session. It builds the `sceAgc` command buffers itself, with no Mesa, no
GL and no CPU rasteriser in the path, and renders at the panel's own resolution
(`sceVideoOutGetResolutionStatus`, 3840x2160 on a 4K output). It is hardware-verified
since 2026-09-12 and vsync-paced. A second `sceVideoOut` open panics the console, so
nothing else may open one.

Its shaders are compiled offline by amdllpc from `.pipe` sources
([shader-compilation.md](shader-compilation.md)); this page is how the runtime uses
them, the bugs that cost hardware round trips, and how to tell a good run from a
bad one. The register and ABI background is in [agc-reference.md](agc-reference.md).

## Why bare-metal

Two earlier routes were tried and removed. A hand-assembled `sceAgc` present path
worked (4K H.264 at 982 microseconds a frame) but every *textured* pixel shader
failed `sceAgcCreateShader` validation (`0x8a6c001f`), because Sony's compiler
emits an `sl00` resource-metadata trailer that cannot be hand-authored. A Mesa
OpenGL stack (`ps5-opengl`) proved shaders were obtainable, but RmlUi through its
GL3 path ran at about 1.2 s per frame, since that driver's render-to-texture is
synchronous with a full CPU surface copy. The answer kept the shader lesson and
dropped the driver: `.pipe` sources through amdllpc, fed to command buffers built by
hand. The OpenGL submodule, its overlay compose file and the `--gl*` build flags are
gone.

## What runs on the GPU

| Subsystem | Path |
|---|---|
| Kit screens (everything EVO draws itself) | `ui_kit/src/hui_agc_batch.cpp`: the kit's draw lists through the SDF pipeline (`ui_sdf.pipe`) |
| RmlUi screens (provider documents, fallbacks) | `EvoRenderInterfaceAGC` (`ui_rml/src/evo_rmlui_render_agc.cpp`) |
| Video decode | `sceVideodec2` resident decoders (a dedicated block) |
| Video present | YUV to RGB in AGC pixel shaders: NV12, planar, P010 SDR/PQ/HLG, Dolby Vision, and PQ-output twins for an HDR10 scanout |
| Upscaler, Deep Blacks | FSR1 EASU + RCAS, Anime4K S/M/UL convolution passes ([upscaler.md](upscaler.md)) |
| Motion smoothing | pyramid, motion estimate, median, warp ([motion-smoothing.md](motion-smoothing.md)) |
| OSD, subtitles, toasts, keyboard | drawn after the video; on an HDR10 scanout they use the `_PQ` UI pipelines so graphics white sits at 203 nits |

The pipeline enum in `evo_agc_runtime.h` has 80 entries (`EVO_AGC_PIPE_COUNT`). Only
the UI pipeline is mandatory at init: a video pipeline that fails to compile
disables that path and logs it rather than aborting the runtime.
CPU rasterisation (`evo_rmlui_render.cpp`) still exists for the host preview; the
device never uses it.

---

## Runtime bugs fixed along the way

Every one produced a *silent* wrong result, which is why they cost hardware
round-trips. Recorded because the failure signatures are not obvious from code.

| Symptom on screen | Cause | Fix |
|---|---|---|
| GPU wedges, process killed ~55 s later | `VGT_ESGS_RING_ITEMSIZE` = 4 | derive from PAL (= 1) |
| Draws retire, **zero** pixels written | vertex stage compiled with no descriptor binding | `.pipe` `[ResourceMapping]` |
| UI draws once then freezes; navigation dead | `seal(slot, token)` then `begin(slot, 0, 1)` gives `TOKEN_MISMATCH`; the return was ignored, so every slot stayed sealed and all allocations failed | store the seal token, hand it back once the fence proves completion; log begin failures |
| Blank screen, every frame discarded | discarding a frame called `_seal()` (needs a token only a submit can clear) instead of `_abort()` | `evo_agc_transient_ring_abort()` |
| Some glyph atlases garbled, others perfect | textures were 64-byte aligned; a GFX10 image descriptor stores `address >> 8` and needs **256** | over-allocate and align; `build_tsharp_2d_internal` now *rejects* a misaligned base |
| Whole UI colour-swapped (blue theme -> gold) | the scanout is BGRA-ordered; the CB was storing RGBA | `COMP_SWAP = ALT` in `CB_COLOR0_INFO` |
| Black band creeping down the picture while loading | `sceVideoOutSubmitFlip` is async and was never waited on; with 2 buffers the CPU clear wiped the **live** framebuffer | poll `sceVideoOutGetFlipStatus` until `status[3] == flip_arg` |
| Square corners on rounded containers; masked overlays in the wrong place | `EnableClipMask`/`RenderToClipMask` were never implemented, so RmlUi's non-rectangular clipping hit base-class no-ops (rectangular clipping still worked - that goes through the scissor) | stencil buffer + the two overrides |
| UI flashing, alternating with blank | frames with no draws were still cleared and flipped | skip present when `frame_has_draws == 0` |

Two pieces that were always required and simply missing:

- **End-of-pipe cache protocol.** `FLUSH_AND_INV_CB_DATA_TS` (event 45, GCR 12)
  then `CACHE_FLUSH_AND_INV_TS` (event 40, GCR `0x30c` = GLV/GL1/GL2 invalidate
  **plus GL2 writeback**) via `sceAgcCbReleaseMem`. Without both, the colour
  block and L2 hold the frame while the display — not a coherent client — scans
  out stale DRAM.
- **Init-time cache flush** of `shader_storage` (the ISA the GPU fetches) and
  `gpu_regs` (register arrays the CP DMA-reads, including the shader entry
  address). Written once by the CPU into write-back memory, read by the GPU for
  the life of the process.

---

## Verifying a build

`evo.log` carries everything needed to separate a good run from a bad one:

```
agc pipe ui_screen_2d cx=53 sh=12 uc=3 modifier=0x5 gs_pgm=0286e120:00000000 ...
agc pipe ui_screen_2d user_data vs_n=3 ps_n=2 const=1 vtx=2 tex=1
agc color_target rc=0/0 base0=0x27a8000 info=0x8828 (comp_swap=1) ...
agc health frame=240 dcb=11872/524288 peak=12178 ring_fail=0 tex_fail=0 \
           direct_mem=6249472/67108864 peak=6257664 allocs=347
agc health presents=120 dcb_min_presented=2845 flip_waits=120 timeouts=0
```

- `gs_pgm` / `ps_pgm` **non-zero** — `sceAgcCreateShader` resolved the entries.
- `comp_swap=1` — the CB stores BGRA. Without it the whole UI is R-B swapped.
- `ring_fail` / `tex_fail` **0** — nothing is silently dropped. These counters
  exist because `RenderGeometry` has several early-returns that otherwise make
  content vanish with no error anywhere.
- `flip_waits == presents` — exactly one vblank per present, correctly paced.
  `timeouts` must be 0.
- `direct_mem` / `allocs` stable across windows — no leak.

**Check the build marker before trusting any of it.** Two debugging rounds were
wasted reading a stale `.ffpfsc`; a log line whose format predates the change
under test means the deploy did not land.

`agc_dump_scanout()` reads the framebuffer back once at frame 40 and logs a
luminance thumbnail plus the most common colours. That is what settled the
colour bug: the buffer held `ffedbe00` (correct cyan for `#00cdff`) while the
panel showed gold, proving the swap happened *after* the framebuffer. Reach for
it before theorising about channel order.

---

## Clip masks

RmlUi clips to non-rectangular shapes - rounded containers, masked overlays -
through `EnableClipMask()` / `RenderToClipMask()`, backed here by an 8 MB S8
stencil surface. Depth is deliberately left disabled (`DB_Z_INFO` format 0):
nothing needs a Z test, so no depth buffer is allocated at all. The DB register
layout follows ps5-opengl's `append_depth_target_state()`.

`RenderToClipMask` mirrors RmlUi's own GL3 backend - stencil to write mode with
`CB_TARGET_MASK = 0` so the mask cannot touch colour, draw the geometry through
the normal path, then switch to testing against it.

**One deliberate divergence.** RmlUi's reference clears the stencil buffer before
every `Set`. Here that would be a multi-megabyte fill per mask, many times a
frame. Instead the buffer is zeroed once per frame with a CP-synced DMA fill and
each `Set` claims the next unused value (1, 2, 3...), with the test comparing
against it; an untouched texel is always 0 and so can never collide with a live
mask. `Intersect` still increments, so only texels already carrying the previous
value reach `ref + 1`. `SetInverse` stamps the same way but tests NOTEQUAL.

This is the one piece with no reference implementation to copy the state machine
from - the registers come from ps5-opengl, the mask-value scheme does not. If
corners come back square the DB binding did not take (`agc stencil base=...
s_info=0x20000181` in the log says whether it did); if clipping is inverted or
content vanishes entirely, the compare function or the `ref` bookkeeping is wrong.

## Notes

- Scanout resolution is the panel's mode, not a constant. A sub-4K video on a 4K panel
  is scaled by the video pass itself (bilinear, or the upscaler).
- The GPU-side `SetFlip` packet in the DCB is unused; the CPU flip is the proven path.
  Revisiting it would save a CPU round trip per frame.
- Shutdown order: `agc_wait_gpu_idle(500)` waits on every frame slot's fence before
  anything is unregistered or released, bounded so a wedged GPU is a hang rather than a
  panic. It logs `agc shutdown: slot=N idle` or `STILL BUSY - releasing anyway`.
