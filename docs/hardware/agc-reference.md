# sceAgc reference

Background for anyone working in `media/src/evo_agc_runtime.c`: where the `libSceAgc`
ABI, the command-buffer layout and the render-target register model came from. EVO did
not invent them. They were read out of two reference projects, both cloned (git-ignored)
under `third_party/`:

- **ProsperoLight** (`blackbearreloaded`): the implementation that has run on a real
  12.70 console. Its `native_agc_present.cpp` and shader headers were the first working
  reference, and its build tail is what `tools/native-app/` is forked from.
- **SharpProspero** (`SvenGDK/SharpProspero`, commit `e24e25e`): the cleaner, fully
  commented ABI and method reference: every `libSceAgc` entry point with a described
  signature, the command-buffer book-keeping layout, the render-target register model and
  a clean-room GPU tiling library. Its own render loop is not documented as
  hardware-verified, so treat its ABI as reference, its sequencing as a strong hint, and
  validate against EVO's own hardware runs.

What EVO has that neither has: shaders compiled from open sources
([shader-compilation.md](shader-compilation.md)) instead of embedded Sony-compiled blobs,
and a runtime that draws the whole UI, video and every post-process through them
([agc-runtime.md](agc-runtime.md)). The symbols EVO imports are listed in
`tools/native-app/stubs/prx/libSceAgc.syms` and `libSceAgcDriver.syms`; the C
declarations are in `media/include/sce/sce_agc.h`.

---

## What SharpProspero documents

| Piece | Where in the clone | Why it matters for EVO |
|---|---|---|
| **Full `libSceAgc` API** — every command builder, every `*GetSize`, shader create/link, register defaults, with described args + packet sizes | `src/SharpProspero/Interop/Agc/SceAgc.cs`, `SceAgcDriver.cs` | The reverse-engineering EVO would otherwise redo by hand. These are plain exported-symbol calls (`[LibraryImport("libSceAgc")]`), so they map 1:1 to C. |
| **DCB book-keeping struct** — the block the flat-C builders read/advance | `Graphics/Agc/DrawCommandBuffer.cs` `State` (offsets 0x00 Bottom / 0x08 Top / 0x10 UpCursor / 0x18 DownCursor / 0x20 Callback / 0x28 UserData / 0x30 ReservedDwords) | Lets EVO build its own DCB wrapper without guessing the struct the builders mutate. |
| **A documented single-draw frame loop** | `Graphics/Renderer3D.cs`, `docs/graphics-gpu.md` | The exact call order: wait-for-display → assemble Cx/Sh/Uc register blocks in direct memory → `sceAgcLinkShaders` → `sceAgcDcbSet{Cx,Sh,Uc}RegistersIndirect` → bind descriptors into shader user-data → draw → `sceAgcDcbSetFlip` (flip on the GPU timeline) → `sceAgcDriverSubmitDcb` → wait-on-screen → rotate N per-frame sets. |
| **Render-target-into-tiled-scanout** | `Graphics/Agc/CxRenderTarget.cs`, `AgcRenderTargetSetup.cs`, `RegisterDefaults.cs`, `DisplayDevice.cs` | The 16-register colour-target block filled from a `RenderTargetSpec`, pointing straight at the `sceVideoOut` back-buffer. This is what removes any CPU swizzle: the GPU writes the tiled layout directly. |
| **Clean-room GPU tiling / swizzle** | `Graphics/Agc/AgcSurfaceTiling.cs`, `AgcTiler.cs`, `AgcSurface.cs`, `AgcTilingTables.cs` | A from-scratch reimplementation of the AMD address library: block dims, mip-tail tables, the `offset = (b_y·W_b + b_x)·S_block + e(x,y)` element swizzle, `Tile`/`Detile`. |
| **Shader-binary container format** | `Graphics/Agc/ShaderBinary.cs`, `AgcShader.cs` | `.sb` = an ELF container with `.shader_header` (magic `0x34333231` = "1234", program-type byte @90, ctx-reg-count @91, sh-reg-count @92) + `.shader_text` microcode. Both copied to GPU-readable direct memory, then `sceAgcCreateShader(&handle, header, code)`. Header carries the register writes the program needs — `AgcShader.ContextRegisters` / `ShaderRegisters` read them straight out. |
| **`DisplayDevice`** — VideoOut open + `SetBufferAttribute2(Bgra8Srgb, tiling)` + `RegisterBuffers2`, tiled vs linear, flip/vblank pacing, the "pitch field must stay 0" gotcha, the size whitelist (1920×1080 the only universally-accepted mode) | `Graphics/DisplayDevice.cs` | Cross-check against `evo_agc_runtime.c`'s VideoOut bring-up; the SRGB-BGRA + `kAlt` channel-order note explains EVO's `0xAABBGGRR` framebuffer rule from the other direction. |
| **Compute dispatch is in the ABI** | `sceAgcCbDispatch` / `sceAgcDcbDispatchIndirect` / `sceAgcAcbDispatchIndirect` in `SceAgc.cs` | A YUV→BGRA **compute** shader (rather than a fullscreen-quad pixel shader) is a viable alternative to a fullscreen-quad pixel shader. |

## The frame loop, distilled (from `Renderer3D.cs` + `graphics-gpu.md`)

```
once:
  sceAgcInit(state, defaultsRevision = 8)
  open DisplayDevice: sceVideoOutOpen → SetFlipRate(0)
    → AgcSurface.Compute(desc) for the tiled RenderTarget layout
    → allocate N framebuffers from 2 MiB-aligned direct memory
    → sceVideoOutSetBufferAttribute2(Bgra8Srgb, tiling=Tiled)   // row pitch MUST be left 0
    → sceVideoOutRegisterBuffers2
  prepare shaders: ShaderBinary.Load(.sb) → copy header+code to direct memory
    → sceAgcCreateShader(&h, header, code)   // once per shader
  allocate, per frame-in-flight (= framebuffer count, min 2):
    DrawCommandBuffer (GPU-readable direct memory, ~256 KB)
    Cx / Sh / Uc register-state regions + any constant/descriptor buffers

per frame (slot rotates with the framebuffer):
  constants (matrices / convert coefficients) written into this slot's region
  build Cx block in direct memory:
     CxRenderTarget.Init(RegisterDefaults.RenderTargetBlock())
       → AgcRenderTargetSetup.Initialize(rt, RenderTargetSpec(
             k8_8_8_8, kUNorm, kAlt,               // kAlt = framebuffer carries blue first
             width, height, backBufferGpuAddr,
             tiled ? kRenderTarget : kLinear))
     AgcViewport.SetViewport(0,0,w,h) → WriteTo(block)
     CB_TARGET_MASK (offset 0x008E) = 0xF
     reserve 34 regs, then sceAgcLinkShaders(linkageSlot, ucPrimStateRegion,
             null, vsHandle, psHandle, PRIM_TRIANGLE_LIST=4)
     append vs.ContextRegisters, ps.ContextRegisters
  build Sh block: vs.ShaderRegisters, ps.ShaderRegisters
  dcb.Reset()
  dcb.WaitUntilSafeForDisplay(outHandle, currentBufferIndex)
  sceAgcDcbSetCxRegistersIndirect(dcb, cxRegion, cxCount)
  sceAgcDcbSetShRegistersIndirect(dcb, shRegion, shCount)
  sceAgcDcbSetUcRegistersIndirect(dcb, ucRegion, 3)
  for each shader resource: write 4-dword descriptor at
     GsUserDataBaseOffset(0x008C) + slotDwordOffset   via sceAgcCbSetShRegisterRangeDirect
  dcb.SetIndexSize / SetIndexBuffer / SetIndexCount / DrawIndex     // or DrawIndexAuto(3) for a fullscreen triangle
  sceAgcDcbSetFlip(dcb, outHandle, bufferIndex, VSyncMode=1, frameIndex)
  sceAgcDriverSubmitDcb(&{words, wordCount, flag})                  // wordCount ≤ 0xFFFFF
  sceAgcSuspendPoint()
  wait until sceVideoOutGetFlipStatus.FlipArg ≥ frameIndex (sceVideoOutWaitVblank between polls)
  advance buffer index + frame counter + register-set slot
```

Register-block rhythm (every typed block): `block.Init(driver defaults from
RegisterDefaults)` → typed setters → copy `block.Registers` into the combined
Cx region → one indirect set packet loads them all. Offsets and reset values
come from the driver at runtime, never baked in.

## Files worth reading in the clone

```
third_party/SharpProspero/
  src/SharpProspero/Interop/Agc/SceAgc.cs         every libSceAgc call, described
  src/SharpProspero/Interop/Agc/SceAgcDriver.cs   submit / queue / flip / wait
  src/SharpProspero/Graphics/Agc/DrawCommandBuffer.cs   DCB State struct + record calls
  src/SharpProspero/Graphics/Agc/AgcShader.cs           shader header field offsets, user-data slots
  src/SharpProspero/Graphics/Agc/ShaderBinary.cs        .sb container parser
  src/SharpProspero/Graphics/Agc/CxRenderTarget.cs      16-reg colour-target block
  src/SharpProspero/Graphics/Agc/AgcRenderTargetSetup.cs  fill it from a spec
  src/SharpProspero/Graphics/Agc/RegisterDefaults.cs    driver reset values
  src/SharpProspero/Graphics/Agc/CxBlend.cs             alpha-blend register block (for UI-over-video)
  src/SharpProspero/Graphics/Agc/AgcSurfaceTiling.cs    the swizzle address library
  src/SharpProspero/Graphics/Agc/Shaders/mesh_{vs,ps}.pssl + .sb   worked example
  src/SharpProspero/Graphics/Renderer3D.cs              the whole frame loop, one draw
  src/SharpProspero/Graphics/DisplayDevice.cs           VideoOut open/register/flip
  docs/graphics-gpu.md                                  prose for all of the above
  samples/prospero-3d/Program.cs                        Renderer3D in use
```

Also relevant: `samples/prospero-payload-unjail/` and `docs/app-promotion.md` describe the
promotion a registered module cannot do for itself ([../architecture/app-module.md](../architecture/app-module.md#sandbox-and-the-self-unjail)).
