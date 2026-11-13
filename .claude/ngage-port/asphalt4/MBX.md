# Asphalt 4 -- the MBX / PowerVR (N95-class) edition

The general edition shaves the **software** rasterizer's per-pixel cost. This
edition aims higher: move the 3D onto the **PowerVR MBX** the N95 and its class
carry, so the CPU stops rasterizing at all. This file is the recon that must
precede any code (port rule 4 and the source ranking), and the staged plan.

## What the binaries say (source rank 1: the images on this machine)

Read with `toolchain/port/e32imports.py` after inflating each DLL with the
compiled `e32inflate` (all NGI DLLs are deflate-compressed, UID `0x101f7afc`).

- **The game rasterizes in software, in its own code.** `asphalt4_ngi.exe`
  imports only `euser, runtime, libc, libm, libz, libstdcpp, libpthread,
  ngiutil, ArenaEnv, dfpaeabi, drtaeabi, drtrvct2_2` -- **no GL, no EGL, no
  ngiOpenGLControl, no ngibackbuffer**. The textured/transparent/fill span
  fillers the general edition rewrote (`+0x2ABAA, +0x3357A, +0x33B16`) are in
  the game's own `.text`. So the game owns its rasterizer; it does not delegate
  rendering to the platform.
- **The platform has a full GL path the game declined.** `ngiOpenGLControl.dll`
  (UID `0x2000f7cb`) imports `libdl` and dynamically loads **`libGLES_CM.dll`**;
  on S60 that one ROM library exports both GL ES 1.1 *and* EGL. `ngiSGI.dll`
  sits beside it. `ngibackbuffer.dll` is the software 2D path (imports
  `FBSCLI, BITGDI, SCDV, WS32`). The game uses none of these -- it chose the
  portable software route that runs on every S60v3, GPU or not. That choice is
  why it is the slowest Asphalt.

So there is **no dormant GPU path to flip on**. The MBX edition means routing
the game's own 3D primitive stream to GL ES 1.1 ourselves.

## What the emulator gives us (source rank 5, for what the *bench* will do)

- **EKA2L1 emulates GL ES 1.1 for real**, not as stubs: `dispatch/.../gles1/
  gles1.cpp` is 108 KB, ~157 dispatched entrypoints, with a fixed-function ->
  host-shader translator (`shadergen.cpp`, `shaderman.cpp`) and PVRTC decode
  (`pvrt-dec.cpp`). It advertises itself to the guest as renderer
  `"POWERVR EKA2L1"`, version `"1.1.0 POWERVR (EKA2L1 IMPLEMENTATION)"`
  (`gles1/consts.h`). EGL is dispatched too (`dispatch/.../egl/egl.cpp`).
- So a `libGLES_CM` call from injected game code runs on the host GPU on the
  bench, and the identical call hits the real MBX on an N95. **The path is
  bench-testable** before it ever reaches hardware. Host FPS is not phone FPS
  (handoff caveat), but correctness and "does it reach the GPU" are.

## The shape of the work

Intercept the game's 3D **above** the span fillers -- at the triangle, where
screen-space vertices + UV + texture + light still exist -- and re-emit as GL
ES 1.1 textured triangles, composited into the frame the game presents. From
the span-filler contract already reverse-engineered we know the per-primitive
state (affine u/v with ustep/vstep, umask/vmask/vshift, a texture base, a light
value, an alpha threshold), which is most of a GL textured-triangle setup.

The hard parts, named up front (rule 4): finding the triangle submission point
upstream of the fillers and its vertex format; converting the game's textures
(paletted; cf. `renderer/a4_sprite_lut.c`, `palconv_*`) to GL textures;
attaching an EGL surface to the game's existing window (its window comes from
`runtime.dll`/CONE); and compositing GPU 3D with the software 2D HUD the game
still draws. `libGLES_CM` is not in the game's import table, so the injected
code either adds it as an import or `dlopen`s it as `ngiOpenGLControl` does.

## Staged plan (each stage bench-verified before the next)

0. **Recon (this file).** Done: architecture, emulator capability, dependencies.
1. **GL bring-up spike.** Injected code opens a GL ES 1.1 context via EGL on the
   game's window and draws one known textured triangle / clears to a colour.
   Success = EKA2L1's `POWERVR` renderer executes it and it reaches the frame.
   Proves we can call `libGLES_CM`, attach a surface, and present -- the whole
   risky foundation -- before touching the engine.
2. **Find the primitive stream.** Disassemble up from the span fillers to the
   triangle setup; pin the vertex/UV/texture/light format. Log it.
3. **One textured triangle from the game's data.** Hook the submission point,
   draw a single real primitive on the GPU while the software path still runs
   the rest; diff on the bench.
4. **Full 3D on the GPU**, software 2D composited over it; texture upload +
   format conversion; state tracking.
5. **Tune for the real MBX** (texture formats incl. PVRTC, batching, the N95's
   FP1 quirks) and bake for hardware.

Gate: stop and reassess after stage 1. If attaching an EGL surface to the
game's window on the bench proves intractable, the edition reduces to
ARM11-specific CPU tuning (STM new-base, scheduling), which is not the GPU win
the owner asked for -- so stage 1 is the go/no-go.

## Stage 1 findings -- feasibility is positive on every point checked

- **EKA2L1's EGL is complete**, not stubs: `egl_get_display`, `egl_initialize`,
  `egl_choose_config`, `egl_create_window_surface` (takes the guest's WS32
  window), `egl_create_pbuffer_surface`, `egl_create_context`,
  `egl_make_current`, `egl_swap_buffers` all in `dispatch/.../egl/egl.cpp`.
- **The `libGLES_CM` calling surface is mapped.** EKA2L1 patches the ROM
  `libgles_cm.dll` export at ordinal N to HLE dispatch `0x1100+(N-1)`
  (`gles1/register.h` patch table) and binds each dispatch number to a named
  Khronos symbol (`register.cpp`), so register.cpp in dispatch order *is* the
  DLL's ordinal table. The EGL block is the standard alphabetical DEF freeze,
  confirming the numbering matches the real ROM DLL. Ordinals a spike needs:

  | call | ord | call | ord |
  |---|---|---|---|
  | eglChooseConfig | 1 | eglCreateContext | 3 |
  | eglCreatePbufferSurface | 4 | eglCreateWindowSurface | 6 |
  | eglGetDisplay | 14 | eglInitialize | 17 |
  | eglMakeCurrent | 18 | eglSwapBuffers | 22 |
  | glClear | 31 | glClearColorx | 33 |
  | glDrawArrays | 54 | glEnableClientState | 57 |
  | glVertexPointer | 131 | glViewport | 132 |

- **Linking `libGLES_CM` into the game is mechanical.** Import section is EABI
  format at `+0xB9BA0`: a size word, then `dll_ref_count` (12) entries of
  `{name_off, count, count x code-offset}`, then the name block. Each code-offset
  holds `ord|(adj<<16)` and the loader overwrites it with the resolved address --
  the IAT, which sits in the code section (euser's at `+0x50CC0`, runtime's at
  `+0x50CF8`; the fps detour already resolves these pc-relatively). To add GL:
  append a 13th entry (name `libgles_cm.dll`, the ordinals above), point its
  import-blocks at fresh IAT words placed in the appended blob, bump
  `dll_ref_count`, fix the trailing section offsets + E32 CRC.
- **The loader matches on base filename.** `libmanager.cpp get_real_dll_name`
  strips the `{version}` / `[uid]` decoration, so a plain `libgles_cm.dll`
  import entry resolves to the patched ROM library. No UID guess needed.

**One open dependency:** `eglCreateWindowSurface` (ord 6) needs the game's
`RWindow`. For the first spike that is sidestepped with
`eglCreatePbufferSurface` (ord 4) -- render offscreen, read back / MEMSNAP the
result -- which proves the whole call path (link -> context -> draw -> GL output
on the bench's POWERVR renderer) without touching the game's windowing. Binding
to the real window and compositing is stage 1b.

## Build plan for the spike (next)

1. Injector gains an **add-import** step (new `libgles_cm.dll` entry + IAT slots
   in the blob); verify the rebuilt image's imports parse and it still boots.
2. `renderer/a4_gl.c` + trampoline: resolve the GL IAT slots pc-relatively,
   `eglGetDisplay/Initialize/ChooseConfig/CreatePbufferSurface/CreateContext/
   MakeCurrent`, then `glClearColorx` + `glClear` + one `glDrawArrays` triangle,
   `glFinish`; MEMSNAP/read-back the pbuffer to confirm pixels.
3. Bench: confirm EKA2L1's `POWERVR` renderer executes the calls and the pbuffer
   holds the cleared colour + triangle. That is the go/no-go result.

## Stage 1 result: GO -- GL rendering proven on the bench from injected code

The pbuffer spike (`renderer/a4_gl.c` + `a4_gl.s`, injector `--gl`) works end to
end. `a4_fps_tick` calls `a4_gl_spike` once per frame; it brings up EGL ES and
clears a pbuffer to green, recording progress in scratch `0x401000` a MEMSNAP
reads. Built `--measure-only --gl` from the stock image, baked, run on the bench:

- **The import injection and the guard both accept it.** The loader logs
  `libgles_cm.dll (UID3=0x101FCABD) runtime code: 0x8062e1c8` -- a new static
  import, resolved, booting clean under the patched factory DLL + a regenerated
  loader hash. Adding an imported DLL does **not** trip the module guard.
- **Full EGL ES bring-up succeeds:** `eglGetDisplay`->1, `eglInitialize`->1
  (EGL 1.4), `eglCreatePbufferSurface`->1, `eglCreateContext`->1,
  `eglMakeCurrent`->1. Scratch latched `magic`, `step` 11, `attempts` 1.
- **Real pixels render.** `glClearColorx(0,1,0,1)` + `glClear` + `glReadPixels`
  read back `0xff00ff00` (RGBA bytes 00,ff,00,ff = opaque green) across all
  16x16 -- injected game code drove the bench's POWERVR renderer to actual
  output.

Two findings worth keeping (rule 4):
- **The blob must be fully position independent.** A plain C `extern a4_gl_iat[]`
  compiled to an absolute literal-pool address (link space `0xC2xxx`) that
  nothing relocates -- it faulted reading `0xc2284` at runtime. Fixed by
  resolving `&a4_gl_iat` pc-relatively in asm (`a4_gl_iat_ptr`) and passing the
  pointer to C. Any future blob data-symbol reference needs the same.
- **EKA2L1's `glReadPixels` is ES2-only** (`get_es2_active_context` rejects a
  non-ES2 context). An ES1.1 context renders fine but cannot read back on the
  bench; the green proof was taken through an ES2 context
  (`A4_GL_READBACK_ES2`, a diagnostic toggle). The shipped MBX path stays
  ES1.1 -- that is the fixed-function hardware the N95's MBX actually is -- and
  its visual proof comes in stage 1b via a window surface, where output is
  presented to the screen rather than read back.

## Stage 1b result: GL output reaches the presented frame

The original plan was an EGL *window* surface bound to the game's `RWindow`.
Two bench facts redirected it (and neither was a dead end):
- EKA2L1's `eglCreatePixmapSurface` is commented out and ES1 `glReadPixels` was
  ES2-only, so neither zero-copy nor readback into the game's backbuffer worked
  out of the box; and
- the game owns no `RWindow` of its own -- windowing lives inside `runtime.dll`,
  so a window surface would mean digging the window object out of the runtime.

The game already presents its **software backbuffer** (base `0x911088`, cached
by `a4_cachebb_tramp` at `0x400700`), and the fps gauge proves that buffer
reaches the screen. So the route taken is GL -> backbuffer -> the game's own
present:

1. Made EKA2L1's `glReadPixels` work from an ES1.1 context (a one-call-site
   emulator fix; `EMU_PATCHES.md`). ES1.1 is the real MBX API, and a device's
   libGLES_CM reads ES1 pixels too, so this is bench/device parity.
2. `a4_gl.c` clears the pbuffer to **blue** (distinct from the gauge's green),
   `glReadPixels` into guest memory, then copies it as a **contiguous run**
   into the backbuffer -- contiguous because the display path stripes 2D writes
   (the same reason the gauge is a contiguous bar; a first 2D blit landed
   scattered).

Bench, ES1.1 build, baked, booted: no fault, GL scratch latched
(`magic`, EGL 1.4, pbuffer/context/makeCurrent all 1, `pixel0` blue), and a
MEMSNAP of the backbuffer at the GL run read **128/128 pixels = 0x001F (blue)**.
GL-rendered pixels are in the frame the game presents.

**The foundation is complete.** Injected code links libGLES_CM past the guard,
brings up an ES1.1 context, renders on the GPU, and lands the result in the
presented frame -- all bench-verified, all device-shaped.

Shipping note: the final edition will not read back + copy every frame (slow);
it will render the 3D into the backbuffer directly -- a pixmap surface
(zero-copy) on a real device, which EKA2L1 would need to implement to bench 1:1,
or an FBO-to-bitmap blit. The readback path proven here is the portable
fallback and the plumbing proof.

## Stage 2 result: the triangle-submission point and vertex format

Disassembled up from the span fillers (static, capstone; the stock image).

**Call chain (bottom-up):**
`textured span filler +0x333F4`  <-  `scanline rasterizer +0x2F6FC`  <-
`per-primitive dispatcher +0x300AE`  <-  submission loops `+0x34798`,
`+0x347EA`, `+0x35D3A`.

**The hook point is `+0x300AE`:** `draw_primitive(r0=ctx, r1=desc, r2=flags)`.
It copies `[desc+0x30]` (the texture) into `ctx+0x54`, calls the setup
`+0x2FF60` (back-face / off-screen cull + sort the 3 vertices by Y, writing the
3 sorted vertex pointers to a 3-word scratch), then dispatches by `flags` bits
to one of ~8 rasterizers: **bit 0 = textured (`+0x2F6FC`)**, bit 1 = `+0x2FBDC`,
and others (`+0x2F358, +0x2EFB4, +0x2ECB4, +0x2E910, +0x2E56C, +0x2E1C8`) for the
other shade/blend modes. So every rendered triangle of every mode passes through
`+0x300AE` with its geometry and texture in hand -- the single place to divert to
GL.

**Primitive descriptor `desc`:**
`v0* @+0x0C, v1* @+0x18, v2* @+0x24` (three vertex pointers, 0x0C apart),
`texture* @+0x30`, and `flags` chooses the rasterizer.

**Vertex wrapper** (what each `vN*` points at):
`position* @+0x00, U @+0x04, V @+0x08`.
**Position** `P` (what `vN[0]` points at): `X @+0x00, Y @+0x04, Z/W @+0x08`
(screen-space, post-projection). `+0x2F6FC` reads `[vN]`,`[vN+4]`,`[vN+8]` for
P/U/V and multiplies U,V by a per-vertex perspective factor
`0x522C8(ctx, P.ZW)` (a 1/w reciprocal) -- i.e. **perspective-correct textured
triangles**, which is exactly what GL ES 1.1 does in hardware. Lighting is the
per-pixel `light`/`lightmask`/`thresh` the filler already consumes (a shade
ramp); whether its source is per-vertex or per-primitive is a stage-3 detail.

**What this means for GL:** at `+0x300AE`, feed GL the 3 `(X,Y)` screen
positions (`glVertexPointer`), the 3 `(U,V)` (`glTexCoordPointer`), the depth
`Z/W`, and bind the texture from `desc+0x30`; `glDrawArrays(GL_TRIANGLES,0,3)`
and let the GPU do the perspective-correct mapping and depth test. Because the
positions are already in screen space, GL runs under an ortho projection -- no
need to recover the game's model/view/projection matrices for a first cut.

Open for stage 3: the texture format at `desc+0x30` (paletted; cf.
`renderer/a4_sprite_lut.c`, `palconv_*`) and its GL upload/conversion, and the
exact light source. Batching at the submission loops (vs. per-triangle at
`+0x300AE`) is a later optimization.

## Stage 3a: the format, measured live -- and it corrects the stage-2 guess

A capture detour at `+0x300B2` (`renderer/a4_prim.c`, injector `--capture`)
recorded raw memory of real gameplay primitives (after 100k textured prims, i.e.
past the menu). Reading the actual bytes corrected the stage-2 disassembly read
of which pointer holds what (rule 4: measure, don't assume):

- **`desc`** = `{ vtable@0, flags@4, ?@8, then three {vptr, U, V} triples at
  +0x0C / +0x18 / +0x24, texture* @0x30 }`. The setup `+0x2FF60` passes the
  *addresses of the triples* (`&desc[+0x0C]`, ...) to the rasterizer, not the
  vertex pointers -- so the rasterizer's `[r5]` is `vptr`, `[r5+4]`=U, `[r5+8]`=V.
  **U and V live in the desc triple, not in the vertex** (the stage-2 note had
  them in the vertex -- wrong).
- **vertex (`vptr`)** = `{ X@0, Y@4, Z/W@8, ... }`: screen-space X,Y inline, and
  Z/W (`vptr[8]`) is the perspective input to `0x522C8`.
- Verified on a live triangle: v0=(256,86), v1=(263,86), v2=(263,73), UVs
  (1008,688)/(1536,688)/(1536,1360), one texture `0x022BEF70` for the primitive.
  X,Y are plainly screen coordinates; U,V are texel-scale (to be divided by the
  texture dimensions for GL's [0,1]).

So the per-triangle GL draw at `+0x300AE` reads, for each of the three triples:
`vptr[0]`=X, `vptr[1]`=Y, `vptr[2]`=Z/W, and the triple's U,V; plus the one
texture at `desc+0x30`. Still open for stage 3b: the texture format at
`desc+0x30` (dimensions + paletted decode) and the exact X/Y coordinate scale
(viewport), both needed to emit the GL triangle in the right place with the
right texels.

## Stage 3b result: a real Asphalt 4 textured triangle on the GPU

Done. `a4_prim.c` (the +0x300B2 capture) picks the first gameplay textured
primitive whose texture is <= 64x64, deep-copies its geometry + RGB565 texels
into a shared struct; `a4_gl.c`, once per frame via the fps detour, brings up a
GL ES 1.1 context on a 64x64 pbuffer, uploads that texture
(`glTexImage2D` RGB565), and draws the triangle under an ortho matching its
screen bounding box (`glVertexPointer` GL_SHORT positions, `glTexCoordPointer`
GL_FIXED UVs, `glDrawArrays(GL_TRIANGLES)`), then reads the pbuffer back and
blits it into the game's back buffer (the 1b path -- a corner, not over the
software scene; that composite is stage 4).

Bench, baked, booted: no fault, `glGetError` == 0, texture uploaded (id 1), drew
252 frames. The back-buffer blit held **12 distinct colours** -- ~2080 px of the
magenta clear background and ~2016 px of the texture (dominant `0xFFDF` plus ten
detail colours), the triangle covering ~half its bounding box. The game's own
geometry and texture, rendered by the bench's POWERVR renderer, in the presented
frame.

Review catch (rule 4): the first draw came back a single uniform colour -- the
UV scale was 2x too large (`U*8192` made U=512 on a 32-wide texture land at
exactly 2.0, which `GL_REPEAT` wraps to texel 0, collapsing all three texcoords
to one texel). The raw U/V are `texel<<4`, so the scale is `U*4096 >> log2(w)`
(U=512 on a 32-wide tex -> 1.0); with that, the texture renders.

## Stage 4 (started): the full-frame renderer

Stage 4 turns the one-triangle proof into the actual edition: every textured
primitive of the frame drawn on the GPU into the game's real surface, the
software rasterizer skipped, the 2D HUD composited over it, measured against the
software path. It is the largest stage; this is the first recon piece.

Captured the render context and its draw surface at `+0x300B2`:
- The 3D **surface is `ctx[0x4c]` = `0x911088`, raw RGB565 pixels, no header**
  (dumped all `0x75BF`, the current sky colour) -- the same buffer the gauge/
  stage-1b blit already reach and the game presents. So GL output must land here.
- Width/height are **not** in the surface; they are in `ctx` (candidates
  `ctx+0x70`=600, `ctx+0x78`=0xA0=160, to be confirmed against the cull bounds
  `+0x2FF60` reads). `ctx+0x10..0x40` is a 1/n reciprocal table (the perspective
  divide `0x522C8` uses it); `ctx+0x54` holds the current texture.

Architecture decided (device-accurate, bench-testable where EKA2L1 allows):
1. Per textured primitive at `+0x300AE`: look the texture up in a cache keyed on
   `desc+0x30` (upload RGB565 once, reuse), append the triangle to a GL batch.
2. Frame boundary (the present detour `+0x1E6`): flush the batch to a GL surface
   the size of the game's screen, then present.
3. On device, the GL surface is a pixmap bound to `0x911088` (zero-copy); on the
   bench (no pixmap surface in EKA2L1) a pbuffer + one full-frame blit stands in
   for correctness, and the real CPU win is measured on the phone (or by adding
   pixmap-surface support to EKA2L1).
4. Skip the software rasterizer for prims drawn on the GPU; leave non-textured
   prims and the 2D HUD on the software path, composited.

Open before the renderer: confirm the viewport W/H, and the per-frame batch +
texture-cache sizing. This is the next build.

### Result: the full textured 3D scene renders on the GPU

Done (correctness). The renderer brings up a GL ES 1.1 context on a 320x240
pbuffer, caches each texture by `desc+0x30` (RGB565 upload once), draws every
textured primitive, and at the present detour reads the pbuffer back (in strips)
and blits it into the game's surface. Bench run: `up` latched, **41 textures
cached, ~741 prims/frame, 13 frames flushed, 0 faults**; a mid-screen read of
the surface afterward held **179 distinct colours** -- a varied textured scene,
not the uniform software sky. Asphalt 4's whole textured 3D scene is being drawn
by the bench's POWERVR renderer and landed in the frame the game presents.

Two bugs fixed along the way (rule 4, both caught by the fault address, not
guessed):
- **A large image `.bss` corrupts the game.** Growing `.bss` to ~372 KB for a
  full-frame readback made the game fault ~806x/frame at load (a load-time layout
  clash, not an address my code writes). Shrinking the `.bss` and reading the
  pbuffer back in strips through a small buffer fixed it (806 -> 0).
- **The surface stride is 320, not 720.** The gauge's 720 was the scaled display
  path; the 3D render surface is 320x240, stride 320. Blitting at 720 overran the
  surface (`0x911088`+..) into the next object at `0x936B60` -- exactly the fault
  address -- clobbering its vtable. Stride 320 fixed it (frames 0 -> 13).

Remaining to make it the shipping edition (perf, not correctness): skip the
software rasterizer for GPU-drawn prims (the CPU win -- currently both run);
composite the 2D HUD over the GPU 3D instead of the full-frame overwrite; and on
device use a pixmap surface bound to `0x911088` (zero-copy) instead of readback.
Then measure against the software path.

### Viewport confirmed, full-frame renderer written (earlier WIP)

- **Viewport = 320x240** (measured: `+0x2FF60` culls against `*[(ctx+0x40100)+0x10]`
  = 320 and `+0x14` = 240). So GL renders at 320x240 under `glOrthox(0,320,240,0)`,
  matching the game's screen space.
- Wrote the renderer: `a4_gl.c` keeps a GL ES 1.1 context on a 320x240 pbuffer,
  a texture cache keyed on `desc+0x30` (upload RGB565 once), draws each textured
  primitive, and at the present detour reads the pbuffer back and blits it (Y
  flipped, stride 720) into the surface at `ctx[0x4c]`. `a4_prim.c` forwards every
  primitive; `a4_fps_tick` calls `a4_gl_frame`. Gated to start after the intro
  video + menu (`seen > 100000`) -- drawing during the intro video segfaulted the
  emulator (the video's own graphics), which the gate fixes.
- **Not working yet.** The first gated run throws a per-frame prefetch abort:
  the game's present does a vtable call through `r3`, and `r3` comes back
  `0x474009E5` -- the object at `r4+0x20` has a clobbered vtable
  (`0x47400xxx`). It fires ~806x and slows the game so it never even reaches the
  `seen>100000` gate (stopped at 98789). New to this build (the capture-only run
  had zero such faults), so the renderer corrupts guest state somewhere on the
  per-primitive or per-frame path. Root-causing this is the next step; the
  one-triangle stage-3b path (which shares most of this code) did NOT fault, so
  the regression is in the per-prim forwarding / frame-flush added for stage 4.

The shippable build remains clean v5; this renderer is committed as WIP behind
`--gl --capture` (and fully stripped by `--clean`).

## Earlier (3b part 1): texture format decoded

Captured the full per-triangle memory (geometry + the texture struct at
`desc+0x30`) for live gameplay primitives and decoded it:

- **Texel format is RGB565** (from the verified `renderer/a4_span_textured.c`:
  `const uint16_t *tex`, `tex[(tv<<vshift)|tu]`, no palette) -- which GL ES 1.1
  uploads natively as `GL_RGB` / `GL_UNSIGNED_SHORT_5_6_5`, no conversion.
- **Texture struct** (`ctx+0x54` = `desc+0x30`): `[0]=(h<<16)|w`,
  `[1]=umask=w-1`, `[2]=vmask=h-1`, `[3]=RGB565 pixel pointer`, `[5]=vshift=
  log2(w)`. Seen live: a 128x128 texture (`umask/vmask=0x7F`, vshift 7) and a
  32x16 (`0x1F/0x0F`, vshift 5). So `w=texs[1]+1, h=texs[2]+1, pixels=texs[3]`.
- **Geometry** (measured): a primitive is a quad as two tris sharing one
  texture; e.g. tri0 v=(215,50),(215,77),(226,77), screen-space, UVs spanning
  one tile. Per-vertex `vtx[2]` = Z/W (the 1/w perspective input), so GL gets
  the q/w for free.

Two small items remain before the first GL triangle: the exact UV fixed-point
scale (the raw U/V look like `texel<<3`; GL needs `U/(8*w)` with GL_REPEAT --
to be confirmed by the rendered result) and the viewport (back-buffer W/H for
the GL ortho; base is `0x911088`, stride 720). Then stage 3b emits the draw:
upload the RGB565 texture, `glVertexPointer`(screen X,Y)/`glTexCoordPointer`
(U,V)/`glDrawArrays`, and show the game's own textured triangle rendered by the
GPU (to a pbuffer + blit, the stage-1b path, so it does not yet fight the
software scene -- that composite is stage 4).

## Stage 4 perf: software-rasterizer skip + the measurement problem

The dispatcher hook now returns a flag. `a4_gl_draw_prim` returns 1 when the
GPU drew the primitive and 0 to fall through; `a4_prim_capture_tramp` (in
`renderer/a4_tramp.s`) branches on it -- 0 re-executes the two displaced Thumb
moves and continues into the software rasterizer, 1 returns straight out of
`draw_primitive` via its own epilogue (`+0x300E0`, r0=0 as the cull path
returns), so the software span-fill never runs for a GPU-drawn triangle. The
injector patches the `a4_skip_ret` placeholder to `(code+0x300E0)|1` (Thumb).
This is the CPU win: the triangle rasterization moves off the ARM core.

**Why the bench cannot measure that win, and what does.** Two bench-only
distortions sit between the bench number and the device:

1. **HLE GL is host-side.** EKA2L1 runs `libGLES_CM` as host dispatch, so the
   guest instructions a `glDrawArrays` costs on the bench are near zero, while
   on the device each call drives the real MBX/PowerVR driver. FRAMESTAT counts
   guest instructions, so it *understates* the device GL cost and *overstates*
   the net saving.
2. **No zero-copy surface on the bench.** The device target is a pixmap surface
   bound to the game's own `0x911088` buffer (no copy back). EKA2L1's
   `eglCreatePixmapSurface` is stubbed out, so the bench has to render to a
   pbuffer and copy the pixels back by hand -- a ~76800-pixel guest blit every
   frame that the device path does not have. That blit alone roughly cancels
   the span-fill saving in the bench FRAMESTAT, which is why the skip build
   measured ~unchanged instr/frame (~11.2M vs stock ~10.9M) in the stage-4-core
   run.

So the instrument for the real number is a build that renders + skips but does
*not* blit: `A4_EXTRA_CFLAGS="-DA4_NO_BLIT"` compiles `a4_gl_frame` to drop the
readback+blit (early return after `glFinish`), modelling the device's zero-copy
path. Built as `a4_glnoblit.exe` (`--gl --capture`, 769216 bytes, builds
clean). The honest reading of it is still a *lower bound* on the device saving
because of distortion (1): the device pays GL-call cost the bench does not.
**The decisive measurement is on the phone** -- stock vs. the GPU-skip build,
frame rate in a crowded scene -- per the project's "a picture is not a
measurement; give the instrument to the person holding the phone" rule.

Not logged as a bench row: the Asphalt 4 FRAMESTAT launch (N-Gage title via the
`ngiplay` launcher `0x20003B78`, which stops at the Arena showroom UI and needs
a keypress to start the game) was not reproducible from state saved to disk this
session -- the autodrive navigation ran inline in an earlier turn. Re-deriving
it is bench rounds spent on a confounded proxy, so it is deferred to the device
run instead.

Still open after this: composite the 2D HUD over the GPU 3D (today the readback
path overwrites the whole surface), and the device zero-copy pixmap
(`eglCreatePixmapSurface`, which would need implementing in EKA2L1 or is
device-only).

## Stage 4 perf: 2D HUD composite

The stage-4-core blit overwrote the whole surface with the GPU 3D, erasing the
sky, the player car and the entire HUD (position, lap, WANTED, minimap, boost
bar, speedometer). The composite fixes that so the HUD renders correctly over
the GPU 3D. Two pieces, both in `renderer/a4_gl.c`:

- **Sentinel colour-key mask.** The pbuffer is cleared to magenta
  (`glClearColorx(1,0,1,1)`) each frame; `a4_gl_flush` reads it back and blits
  only the non-magenta pixels. Pixels the GPU never drew (the sky, and anything
  the software layer owns) are left untouched, so the software sky and the
  player car survive instead of being painted over with a black scene.
- **Composite at the 3D->2D boundary, not at present.** The present detour runs
  after the whole frame (3D + HUD) is built, so a blit there lands on top of the
  HUD. Instead `a4_gl_flush` is triggered from `a4_gl_fill_hook`, called at the
  frame's first `a4_fill_alpha` (`patch/fill.c`) -- the first 2D translucent
  fill, which marks the end of the 3D pass -- so the software HUD, drawn
  afterwards, paints over the composited GPU 3D. `a4_gl_frame` (present) still
  flushes as a fallback for a HUD-less frame. A per-frame `flushed` flag makes it
  idempotent. The blit also skips the top `CLIP_TOP` rows (the HUD banner, which
  the game's scene fill at `x0 y11 w320 h229` shows is outside the 3D viewport),
  so GPU scene pixels do not leak into the position/lap/WANTED strip.

On the device this is a zero-copy pixmap and there is no readback at all; the
compositing ordering (software sky under, GPU 3D, software HUD over) is the same.

The top-of-screen green bar was our own fps gauge, not a render fault. `a4_fps.c`
paints a contiguous bar from the back buffer's first pixel (bright green = fps,
red tick at 30, white at 15/45), which lands on the top rows of the screen -- the
HUD banner strip. It is in every non-`--clean` build, so it showed up the moment
the GL build drew a real HUD under it. The GL build renders for real and does not
want the measurement overlay, so `a4_fps.c` now suppresses the gauge under
`-DA4_GL` (passed by the injector for `--gl` builds) while still calling
`a4_gl_frame()` to flush the GPU frame and still keeping the fps ring. The
measurement (`--measure-only`) build keeps the gauge.

(A long mis-diagnosis preceded this: the gauge is present in every build used to
test, so isolating "capture detour on/off" never removed it, and it was wrongly
read as a game integrity response to the `+0x300B2` code change. The tell was
that `--clean` -- the one build without the gauge -- was also the one without the
bar. Lesson: when an artifact is in every test build, suspect the thing every
test build carries before the thing under test.)

Verified on the bench (stock vs. composite, same race): sky, player car, the 3D
scene, minimap, speedometer, boost bar and position/lap/WANTED all render
correctly, no top bar, 0 faults.

## CPU win: bench preview, and the on-device measurement

The win is moving the textured-3D rasterization off the ARM core onto the MBX.
The honest measurement is on the phone, but a bench preview is defensible here
because FRAMESTAT counts *guest* instructions: the guest-side cost of issuing a
GL call is counted, the host GL work is not -- and on the device the GL work is
the MBX (native), not guest instructions either. With `-DA4_NO_BLIT` there is
also no readback to confound it, and no glFinish stall (dropped on that path).

Measured (FRAMESTAT, dyncom, instr/frame):

| scene                         | stock (sw) | GPU-skip (no-blit) | reduction |
|-------------------------------|-----------:|-------------------:|----------:|
| idle start line (controlled)  |      9.57M |              4.83M |    49.5%  |
| LA race, held accelerate      |      9.53M |              4.65M |    51.2%  |

Two independent scenes both land at ~50%. Stock sits at a near-constant ~9.5M
regardless of scene (the software rasterizer fills the whole 320x240 every
frame -- a roughly fixed cost); the GPU path removes about half of it. The GPU
path is confirmed genuinely active, not a fallback (glr live: up=MAGIC,
~500 prims/frame drawn on GL, 23 textures, past the START_AFTER threshold,
0 faults).

Caveats carried to the device: the ~50% is guest CPU; realised fps also needs
the MBX to keep up (trivially, for ~500 tris) and needs zero-copy (pixmap) to
avoid the readback -- the no-blit build models that but is not the playable
build. A playable fast build still needs `eglCreatePixmapSurface` implemented.

On-device instrument: `a4_fps.c`'s on-screen gauge, forced on in a GL build with
`-DA4_GAUGE`. Two gauge builds shipped to the owner -- stock+gauge and
GPU-skip+gauge -- to compare the bright-green bar (fps) in the same scene on the
phone. If the GPU build's bar is clearly longer, the ~50% CPU is translating to
real fps and zero-copy is worth building; if GL fails to start on the phone the
build falls back to software and the bars match.

## Stage 6: run-length batching (no depth) -- floor fixed, transparency exposed

The depth-sorted batch (stage 5) rendered the depth-ordered scene with wrong
occlusion on the real MBX (the floor/road dropped out), a divergence the bench's
HLE GL never showed. Stage 6 drops the depth buffer entirely: it accumulates the
captured triangles in arrival order and draws them back in that order, merging
only *consecutive* same-texture triangles into one glDrawArrays. This is
byte-identical to the per-triangle path that rendered correctly, just ~2.7x fewer
GL calls -- it cannot occlude wrongly.

Bench (RM-409, LA race, `--gl --capture`): the **road and ground render
correctly** (the floor is back), up=MAGIC, bstep 6, ~536 prims/frame, 0 faults,
errc 0. So the depth/occlusion glitch is solved.

It exposes a separate, pre-existing bug: the **trees and sprites draw as solid
black boxes**. The GPU path renders every captured prim opaque (GL_REPLACE, no
alpha), but the game's transparent prims use a colorkey -- the software
transparent filler (`+0x33B16`) *skips texel value 0* (black = transparent). So
the captured transparent sprites show their black background instead of keying it
out. Two fixes: (a) only GPU-render the opaque prims and leave the transparent
ones to software (needs the opaque/transparent flag bit at the +0x300B2 capture,
not yet decoded -- the dispatch at +0x300C6 tests flag bits 1/0/3 to pick
rasterizers sub_2fbdc / sub_2f6fc / ...), or (b) upload with a 1-bit alpha keyed
on texel==0 and enable GL_ALPHA_TEST. (a) is simpler and keeps the expensive
opaque road on the GPU.

## The overlooked cost: our own composite hands back the whole offload saving

Measured deterministically (FRAMESTAT instr/frame, fixed idle start-line scene,
same renderer):

| build | instr/frame |
|---|---:|
| stock software (no offload)            | 8.11M |
| composite: offload + readback + blit   | **8.17M** |
| readback only (glReadPixels, no blit)  | 8.49M |
| no-blit: offload, no readback          | **4.96M** |

The offload saves **3.15M** (39% of the frame) -- the prize. The composite costs
**3.2-3.5M** -- it gives the entire saving back; net ~= stock. That is the
no-gain, deterministically, and it is *our* code, not the MBX or a cap.

Attribution: readback-only ~= composite, so the per-pixel sentinel blit loop
(~17 instr/drawn px, ~9/sentinel px) is **negligible** and the whole cost is
the **`glReadPixels` readback** -- ~3.2M for only **15 calls** (one per 16-row
STRIP), ~215K per call, which points at **per-call overhead**, not per-pixel.
On the N95 that is worse still: each glReadPixels forces a full tile-flush and
pipeline sync on the tile-based MBX, and `STRIP=16` (chosen to keep the readback
buffer tiny in .bss) makes that 15 syncs a frame.

**Mechanism, pinned by two more deterministic runs.** Row-range readback (only
the rows the GPU drew) changed nothing (8.21M) -- but a `glr` memsnap shows the
drawn extent at that scene is rows 17..239, **92% of the screen**, so no strips
were saved; the run was the wrong scene, not a refutation. The clean isolation
is more calls over the same rows: `STRIP=8` (30 calls) cost **8.80M** vs ~8.2M
for 15 -- only ~0.6M more, ~40K per call. So per-call overhead is real but
small, and the dominant **~2.6M is a fixed per-frame cost**: a **per-frame sync**.
Reading back the frame we *just* drew forces the GPU to finish it (the forced
render on the bench; the tile-flush + pipeline stall on the MBX). Trimming
strips, rows or bytes cannot touch it.

**The fix: pipelined (one-frame-deferred) readback.** At each flush, read back
and composite the *previous* frame -- it had a whole frame of CPU work to finish
on the GPU, so the read does not stall -- then clear and draw this frame, which
completes in the background. One pbuffer, no `glFinish`, one frame of 3D
latency (~38 ms at 26 fps, imperceptible). Implemented in `a4_gl_flush`
(`pminy/pmaxy` carry the previous frame's extent; the sentinel clear moves to
after the readback, with one clear at bring-up so the first read is
all-sentinel). Expected on the bench: composite falls from ~8.2M toward no-blit
(4.96M) + the ~0.6M per-call term -- the offload's 3.15M saving realised rather
than handed back. Caveat: if the bench's software GL renders lazily *on* the
readback's flush, the bench may still pay the render there even though a real
GPU would not; the device A/B is the ground truth either way.

**Result: pipelined = 8.03M (n=115), composite 8.17M.** The bench does not
move, and the emulator's source says why it cannot: `gl_read_pixels_emu`
(`gles2.cpp:2817`) calls `flush_to_driver` and then `read_bitmap`, a
`send_sync_command` that blocks the guest's host thread on a condvar until the
driver thread has run *every queued command* (`itc.cpp:31`). The draws are
queued, not executed, until that flush; llvmpipe rasterises at the flush. So on
the bench the previous frame's draws are still executed inside the readback,
whichever frame is read -- there is no GPU running concurrently with the guest
to pipeline against. The ~2.6M "per-frame" cost is host wall time inside that
blocking call, seen by the guest as extra timer/wserv work per frame. The
device has a real asynchronous GPU; only the device A/B
(`a4_opaque` vs `a4_opaque_noblit`, and `a4_pipelined` vs `a4_opaque`) can say
whether the readback is its cost too. Pipelined build renders correctly on the
bench (`idle_cmp_pipelined.png`: road, grass, trees, opponents).

## Re-analysis of the engine plan: what was overlooked

1. **The ceiling.** The race frame is ~9.5M instr: ~64% rendering, the rest
   (~3.3M) logic, physics, AI, sound and the long tail. The phone runs ~168M
   instr/s at the measured 17.7 fps. With **all** rendering offloaded for
   free and a zero-cost composite, that is 168/3.3 = ~50 fps. With *any*
   readback composite (3.2M here) it is ~25 fps. **45+ fps is reachable only
   with no readback at all**, and only with the whole rendering pass gone,
   not one mode of eight.
2. **The composite's direction is wrong for a tile-based GPU.** GPU -> CPU is
   the one transfer the MBX is worst at (tile flush + pipeline sync + a copy
   out of the render target through the driver), and it is the only direction
   that needs the GPU to be *finished*. CPU -> GPU (a `glTexSubImage2D` of a
   320x240 layer, ~150 KB) is the cheap direction and stalls nothing. The
   design should be inverted: **the GPU is the presenter** -- window surface,
   `eglSwapBuffers` -- and whatever stays in software is uploaded as a texture
   and drawn as a full-screen quad. No `glReadPixels`, no sentinel blit, and
   the game's own back-buffer-to-panel copy (`ngibackbuffer.dll +CCB8`, 7%)
   becomes dead.
3. **Partial offload breaks painter's order; full offload keeps it for free.**
   Lib3D has no z-buffer: it sorts faces and paints in order. Offloading one
   mode and merging the GPU layer wholesale onto software pixels *is* the
   floor-glitch and transparency class of bugs (stages 5-6a). GL draws in
   submission order, which is painter's order, so offloading **every** 3D mode
   preserves correctness with no depth test at all, and leaves only the 2D/HUD
   pass in software -- drawn after the flush point, so it is a clean top layer.
4. **The window.** The pixmap probe was red, so a window surface is the only
   zero-copy exit, and the game owns no `RWindow` (it lives in `runtime.dll` /
   `ngibackbuffer.dll`, which imports WS32). But the platform's own GL path,
   `ngiOpenGLControl.dll`, exists precisely to hand an N-Gage title an EGL
   window -- the supported route, which the game declined. Research item:
   how `ngibackbuffer` presents (DSA vs window blit) and where its `RWindow`
   is, or whether `ngiOpenGLControl` can be brought up beside it.
5. **The HUD layer.** With the 3D pass fully on the GPU the software buffer
   holds only what the 2D pass draws. Clear it to a key colour at frame start
   (~40-80K instr), convert 565 -> 5551 with alpha 0 on the key (~300K for
   76.8K px, ES 1.1 has no colour key), upload, draw over the scene. ~0.4M a
   frame against the 3.2M readback.

Estimate for the inverted design: 3.3M logic + ~0.4M HUD layer + ~0.3M GL
submission (~40-60 batched draw calls) = ~4.0M a frame, ~2.4x stock, **~40-42
fps at phone speed** on the current numbers -- near the 45 target, not over
it, unless the long tail of the "logic" share has rendering in it still (it
has not been itemised; `EKA2L1_PCPROF` can). What the current numbers cannot
settle: the MBX driver's per-draw-call cost on the N95, and the device's
readback cost; the A/B pair in the user's hands answers the second.

## The N95's MBX, from the datasheets (why the GPU route is right)

The N95 is a TI OMAP 2420: ARM11 + **PowerVR MBX with the VGP vertex
co-processor** (hardware transform/lighting -- the full MBX, not the cut-down
MBX Lite). OpenGL ES 1.x fixed-function, tile-based deferred renderer.
Throughput is quoted 1.7-3.7M tris/s and 135-300M px/s **at 100 MHz**, and the
OMAP 2420 runs its core at ~50-66 MHz -- so roughly half: ~67-150M px/s,
~0.85-1.85M tris/s. This game needs ~77k px x 60 fps = **4.6M px/s** and ~490
tris x 60 = **30k tris/s**. The MBX has ~15-30x the fill and ~30-60x the
geometry headroom needed for 60 fps. **It is nowhere near the bottleneck -- it
sits idle while the ARM does everything in software.** This is the whole case for
the GPU route: the hardware can clearly do it (as the iPhone's MBX did), the port
just never used it.

## Pixmap probe: does the N95 support zero-copy render-to-backbuffer?

The no-readback path needs an EGL **pixmap** surface (render straight into the
game's backbuffer bitmap). Symbian's EGL *can* bind GL to a `CFbsBitmap` but it
"is not guaranteed to be supported on all EGL implementations" -- so the N95 must
be asked directly. (EKA2L1 itself lacks pixmap: `eglCreatePixmapSurface` is
commented out; upstream PR #865 adds it and can be ported to the fork to bench
the path 1:1.)

The probe (`a4_gl_pixmapcap`, built `--gl --capture -DA4_NO_BLIT -DA4_GAUGE`):
bring-up calls `eglChooseConfig` with `EGL_SURFACE_TYPE = EGL_PIXMAP_BIT`; if it
returns a config, the device offers pixmap surfaces. Reported on gauge band 2:
GREEN (3 rows) = supported, RED = not, magenta = GL error. Band 1 stays the GL
state. Bench-verified: band 1 green (GL up), band 2 **red** -- correct, EKA2L1
offers no pixmap config, so the probe reads true. The N95 reading is the answer:
green -> zero-copy viable, build it (port PR #865 + render into the backbuffer);
red -> zero-copy dead on the N95, refocus on widening the offload instead.

## Stage 6a: transparency fixed by colorkey texture scan

Fix (a) from the previous section, the simpler one, works. On upload each texture
is scanned for the colorkey (texel value 0, what the software transparent filler
skips); a texture that is >=1/8 colorkey is a transparent sprite, and
`a4_gl_draw_prim` returns 0 for prims using it -- those stay in software, where
the colorkey is handled. Only opaque textures (the road, buildings) go to the
GPU. No flag decoding needed, and it is robust: an opaque texture with a few
black texels just misses the GPU and renders in software (correct, only not
offloaded). Bench (LA race): the road renders on the GPU, the palm trees and
sprites render in software with their transparency intact -- no black boxes, no
missing floor, scene correct. This is the clean build.

## Is the game fps-capped? No -- it is compute-bound (measured)

The obvious worry, after a 51% guest-instruction cut gave no device fps: the
game paces to a fixed frame rate, so freeing the CPU just idles it. Checked, and
it is not so.

- **The game imports no timer.** Of euser it pulls only clock *readers*
  (`TickCount` 674, `UniversalTime` 855, `HomeTime` 859) -- no `RTimer`, no
  `User::After`, no `CPeriodic`. The loop is driven from the NGI runtime
  (`runtime.dll`), which does own a `CPeriodic` -- but at a **10 ms (100 fps)**
  interval (the only frame-shaped constant in it), a ceiling far above the
  achieved rate, not a binding cap.
- **The game's own UI exceeds 30 fps when the work is light** -- 32-33 fps on the
  tip screens, 61 fps on the intro video (seen in bench screenshots). A hard
  25/30 fps cap could not produce 61 fps. So the loop is compute-bound, not
  timer-paced.

That the loop is compute-bound means freeing CPU *should* raise fps. Whether it
does on the device is a separate, still-open question (see the correction below).

**Measurement correction (important).** An earlier version of this section put a
"stock 12.8 fps vs GPU-skip 24.1 fps = 1.88x" table here and concluded the CPU
headroom converts to fps. That fps came from counting posts over a window while
**autodrive** drove -- and autodrive takes a different route each run, so one run
sat in open road and another in dense downtown. The 2x was **scene variance, not
the skip**. Retracted. Frame *rate* measured over an uncontrolled autodrive is
not a measurement here; only instructions-per-frame at a **fixed** scene is.

Controlled instead (idle start-line, no input, same scene both runs, FRAMESTAT
instr/frame median):

| build | instr/frame |
|---|---:|
| no-skip (software, with the ARM span rewrites) | **8.11M** |
| force-skip (skip the captured prims, no GL)     | **3.92M** |

So the primitive skip is real: it cuts ~52% of guest instructions at that scene
(matching the independent 9.57M->4.83M start-line number recorded earlier), and
the skipped prims leave the picture looking the same (force-skip vs no-skip
frames are indistinguishable by eye). What is NOT established is that this CPU
saving turns into fps on the phone -- the clean test is the `A4_FORCE_SKIP`
build (skip in software, no GL at all): if it is faster on the device it
converts; if not, it does not.

Also retracted: the earlier read that the phone's no-blit probe "showed live 3D,
so software is not being skipped." A no-blit build shows live 3D on the **bench**
too (the non-captured prims still render); it does not indicate the skip failed.

## On-device probe gauge (crack the black box)

The bench cannot see device GL, so the gauge was turned into a telemetry channel.
`a4_gl.c` now keeps, per frame, the GL error (`glGetError`, IAT ord 68, sticky
first non-zero), the triangles the GPU path actually took (`last_prims`, captured
in `a4_gl_flush` -- once a frame, when `prims` is complete, not in the
present detour where a double-fire clobbered it to 0), and whether
`eglChooseConfig` granted a depth config (`depthcfg`). Accessors
`a4_gl_prims/errc/depthcfg`. Under `-DA4_GAUGE` (`a4_fps.c`) the gauge draws two
contiguous bands instead of the fps bar:

- **Band 1 (from pixel 0):** red stub = libGLES not linked; yellow (len=step) =
  bring-up stalled; **green, len = prims taken** (ticks at 100/300/500). A long
  green bar = the scene is being offloaded; an (almost) empty green bar = GL is
  up but the GPU is taking nothing (software still draws it) -- the central
  unknown from the phone videos.
- **Band 2 (lower):** magenta (rows = `(errc&7)+1`) = a GL error, enum readable;
  green short = no error + depth config; orange = no error but **no depth config**
  on this device (explains a depth-batched composite glitch).

Bench-verified (RM-409, LA race, `--gl --capture -DA4_NO_BLIT -DA4_GAUGE`): the
probe reads up=MAGIC, bstep=6, **last_prims 591-612**, errc=0, depthcfg=1 -- the
"offload healthy" baseline. On the phone a departure from that (empty band 1,
magenta band 2, orange band 2) names the fault in one round.

## Device result: no gain, and why (per-triangle driver overhead)

Two gauge builds (stock+gauge, GPU-skip no-blit+gauge) went to the phone. The
owner's reading: **the bars match, no felt gain.** GL did start on device (the
owner saw the green "up" state), so this is not a silent software fallback -- it
is a real wash. The video the owner sent corroborates ~26 fps (14% duplicate
frames by frame-count), against stock's ~27.

The cause is the shape of the GL traffic, not whether the GPU is used. Stage 4
issued **one draw per triangle**: bindtex + glVertexPointer + glTexCoordPointer +
glDrawArrays(3), ~490 times a frame, ~2000 GL calls. On the bench those calls are
HLE (host-side, free); on the N95 the **MBX driver runs every one of them on the
ARM CPU** -- command validation, state flushes (the MBX is a tile-based deferred
renderer, so a texture bind between draws flushes the tile buffer), and submit
overhead. That per-command ARM cost replaces, roughly one-for-one, the
rasteriser work the skip removed. Net ARM load ~unchanged -> fps ~unchanged. The
bench could never show this (its GL is free); only the phone could, and did.

Measured texture churn in the LA race (capture): ~490 tris/frame, 23 distinct
textures, **179 texture changes (37%)** in arrival order. The fix is to stop
issuing per-triangle commands.

## Stage 5: the batched renderer

`renderer/a4_gl.c`, rewritten. `a4_gl_draw_prim` no longer draws; it only
**accumulates** each triangle (pos x,y,z + texcoord u,v) into frame buffers in
the grown .bss and records its texture-cache index. At the 3D->2D boundary
(`a4_gl_flush`) the frame's triangles are **counting-sorted by texture** and
issued as **one `glDrawElements` per texture** -- ~23 draws and ~23 binds a frame
instead of ~490 of each. Grouping reorders triangles, so painter's order is lost;
a **depth buffer** restores it, keyed on the game's own per-vertex Z/W (`vtx[2]`,
larger = closer).

Details, and the guesses named (rule 4):
- **Depth direction / ortho z sign.** Not derivable cleanly from the ES1.1 ortho
  convention, so isolated behind `DEPTH_FUNC` (`GL_GREATER`), `CLEAR_DEPTH` (0)
  and a symmetric `Z_RANGE` (`+/-32767*FX`, wide enough that z in [1,32766] is
  never clipped whichever sign the ortho uses) -- then **verified by eye on the
  bench**: occlusion is correct first try (road to horizon, palm in front of the
  hills, houses behind roadside trees, player car in front, no z-fighting). If a
  future scene renders inside-out, flip `DEPTH_FUNC` to `GL_LESS` and
  `CLEAR_DEPTH` to `FX`.
- **Depth config.** The pbuffer now asks `eglChooseConfig` for a config with
  `EGL_DEPTH_SIZE>=16` (every EKA2L1 config is D24S8, but the device's config 6
  may have no depth), falling back to config 6. New IAT ordinals (verified
  against the ROM: ord = dispatch - 0x1100 + 1): eglChooseConfig 1, glDrawElements
  55, glDepthFunc 48, glClearDepthx 35 (`a4_gl.s` [26]-[29]).
- **Buffers.** pos int16[3], uv int32[2] (GL_FIXED), uint16 index, uint16 per-tri
  texidx, from 0x408000; `MAXT`=640 (an overflow triangle draws immediately, still
  depth-correct). `GL_SCRATCH_SIZE` grown 0x8000 -> 0xF000 (bss top 0x410000). The
  bigger .bss did **not** corrupt the load layout -- boots clean.

Bench-verified (RM-409, LA race, composite build 770672 bytes): renders
correctly, **0 faults**, and the GPU path confirmed live by a `glr` memory
snapshot mid-race -- up=MAGIC, bstep=6 (full context), seen=57326 (past the
START_AFTER threshold), **prims=572 grouped into ntex=31 texture draws** (~18x
fewer draw calls/binds than stage 4), flushed=1.

The proof that the *batched* draws render correctly, not that software is
quietly covering for them: once GL is up, `a4_gl_draw_prim` returns 1 for every
textured triangle, so the game skips its own software rasteriser for the 3D --
yet in the seen=57326 frame the full textured scene (road, trees, cars, houses)
is present and correctly occluded. Had the grouped `glDrawElements` failed, that
3D would be *absent* (only the software sky and HUD), masked back to the
software layer by the magenta sentinel. It is there and right, so the batch
drew it. (`errc` in `glr` is a dead field -- declared, never written -- so it is
not evidence either way; the scene is.)

The bench cannot measure the win: batching changes the count of *native MBX*
commands, which is free on the bench's HLE GL and not counted in FRAMESTAT's
guest instructions either. The decisive test is on the phone -- batched
no-blit+gauge vs stock+gauge, directly comparable to the per-triangle wash above.
If the batched bar is now longer, the ~18x fewer driver commands translated to
ARM headroom and the zero-copy pixmap is worth building; if it is still a wash,
the readback (not the draw count) is the wall and `eglCreatePixmapSurface` is the
only lever left.

## Status

Stages 0, 1, 1b, 2, 3a, 3b complete; stage 4 core done -- the full textured 3D
scene renders on the GPU into the game's surface (41 textures, ~741 prims/frame,
0 faults). Stage 5 done on the bench: the per-triangle draws are batched by
texture with a depth buffer (~490 draws/binds -> ~23), renders correctly, 0
faults, GPU path confirmed live. The device wash (stage 4, per-triangle) is
diagnosed as MBX per-command ARM overhead; the batched build is the fix to
re-test on the phone. The software-rasterizer skip is implemented and renders;
its CPU win is a device measurement (see above), with the `-DA4_NO_BLIT` build as
the instrument. The 2D HUD composites correctly over the GPU 3D with no artifacts
(the former top bar was our own fps gauge, now suppressed in GL builds -- see
above). Remaining on this track: device re-test of the batched build, then the
device pixmap (zero-copy) if the readback proves to be the wall.
Foundation proven end to end (inject -> link
libGLES_CM past the guard -> ES1.1 context -> GPU render -> presented frame), and
the game's 3D pipeline is mapped down to the per-triangle hook `+0x300AE`, with
one real textured triangle now drawn by the GPU. Stage 4: drive ALL the frame's
textured primitives through GL into the game's actual render surface (not a
corner) -- a texture cache keyed on `desc+0x30`, batched draws, the correct
viewport from the render context, and the software 2D HUD composited over the
GPU 3D -- then measure the win against the software renderer.

## The plan from here (written after the pipelined result)

**Round 0 -- the device A/B, three builds, same renderer (stage 6a), same
track, same spot.** `a4_opaque` (readback + composite, floor visible),
`a4_opaque_noblit` (no readback, floor blank), `a4_pipelined` (readback of the
previous frame). The one measurement the bench cannot make:

| noblit vs opaque | pipelined vs opaque | meaning | next |
|---|---|---|---|
| noblit clearly smoother | pipelined ~ noblit | readback is the cost, and it is the sync | pipelined is a shippable interim; proceed to phase 1 |
| noblit clearly smoother | pipelined ~ opaque | readback is the cost: the transfer/per-call, not the sync | only the inverted design (phase 1-3) removes it |
| all three the same | -- | readback is not the phone's cost; the driver's per-draw overhead is | the GPU route's economics must be measured on the device (a draw-count A/B) before any more stages |

**Phase 1 -- the zero-copy exit (window surface).** From the binaries: how
`ngibackbuffer.dll` presents (DSA or window blit) and where its `RWindow` is;
whether `ngiOpenGLControl.dll` can be brought up in the process. Deliverable: a
build that draws a GL quad to the panel through `eglSwapBuffers` while the
game runs. Gain: none yet; it proves the exit. Bench-provable for the window
surface mechanics (the emulator implements it); 1-2 device rounds for the N95
driver and the runtime's screen ownership.

**Phase 2 -- the whole 3D pass on the GPU, in submission order.** Capture every
mode at `+0x300AE` (textured opaque, transparent/colourkey as 5551 textures
with alpha test, flat fills, the rest of the ~8), draw them in order, no depth
test. Bench-provable per mode by pixel comparison against the software
rasterizer. Gain: the ~64% rendering share becomes ~3% of GL submission; the
floor/transparency class of glitches is gone at the root because painter's
order is kept.

**Phase 3 -- the HUD as a layer, the game's own copy stubbed.** Clear the
software buffer to a key colour, convert the 2D pass to 5551 with alpha,
upload, draw over the scene, swap; `ngibackbuffer +CCB8` (7%) is dead. ~0.4M
a frame. Bench-provable.

**Phase 4 -- itemise the rest.** `EKA2L1_PCPROF` on the remaining ~3.3M: what
is logic, what is still drawing, what is sound. Anything found is the margin
between ~40 and 45+.

**Expected:** 9.5M -> ~4.0M instr/frame, ~2.4x: **~40-42 fps at phone speed
from 17.7**, with 45+ depending on phase 4. Device rounds: 1 (round 0) + 1-2
(phase 1) + 1 (phase 2) + 1 (phase 3), about five. Risks, named: the window
handle (phase 1 may find the runtime owns the panel through DSA and fights a
GL window); the MBX driver's per-draw cost (round 0's third row); the logic
floor (phase 4 may find nothing to trim, leaving ~40). If round 0 lands on
its third row the GPU route is capped by the driver and the goal of 45 is
not reachable on this title; the CPU-side rewrites are worth ~10-13%.

## Phase 4 done early: the "remaining 3.3M" itemised (exact PCPROF, no-blit build)

`EKA2L1_PCPROF` on `a4_opaque_noblit` (the textured-opaque mode on the GPU,
nothing read back), 0.5G instructions after SKIP 6.0G, the idle start-line
scene, FRAMESTAT median 4.57M/frame in the window (~109 frames). The
navigation had to be driven off the log: profiling halves the boot rate, and a
first run pressed its Returns before the menu was up and profiled the menu
(kept as `prof_menu_noblit.txt`: +298F4/+298D2/+29A9C sprite-painter blocks,
as round A1 said).

| what | where | share | per frame |
|---|---|---:|---:|
| **565 -> 32-bit convert of the whole back buffer for the panel** | `ngibackbuffer.dll +CCB8` (ARM, 10 instr per 2 px, `and 0xf800/0x7e0/0x1f`, 32-bit stores) | 26.2% | **1.20M** |
| 2D sprite painter (HUD, car/tree sprites) | `+29736` | 14.0% | 0.64M |
| transparent (colourkey) span filler, still software | `+33964` | 8.6% | 0.39M |
| 3D pipeline: `+2E1C8` (5.0), `+2ECB4` (1.7), `+2FF60` (1.0), `+2F6FC`, `+300AE` | | 8.7% | 0.40M |
| another span mode still in software | `+33F8C` | 5.0% | 0.23M |
| our injected code (capture, texture scan, GL submission, span rewrites) | `+B9F50..+BA358` | 7.1% | 0.32M |
| textured filler on the colourkey prims we hand back | `+333F4` | 3.1% | 0.14M |
| 565 blend / translucent fill family | `+522A4`, `+51E44`, `+52E44`, `+52FAA` | 6.5% | 0.30M |
| `+345E4`, `+349F8`, `+349B0`, `+34B1A` (mesh/transform?) | | 5.5% | 0.25M |
| `+35D3A`, `+35A28`, `+3598C`, `+35FDA` (physics?) | | 3.0% | 0.14M |
| `+CE8C`, `+CF6E` | | 1.7% | 0.08M |
| `+3115C`, `+031246`, `+026E28`, `+050060` | | 3.7% | 0.17M |
| euser, drtaeabi (division) | | 1.4% | 0.06M |
| tail | | ~5% | ~0.25M |

**The non-rendering floor at this scene is ~1.0M, not 3.3M.** The "logic"
share was 100 minus the rendering blocks named in round A1; the rest was
rendering too, just not in those blocks. The biggest single item, 1.2M, is the
runtime converting the game's 565 buffer to 32-bit for the panel, every
present (`+CCB8` is 0.78M a frame in the stock profile, 7%; here it runs ~1.5
presents a frame, so it is on the runtime's cadence, not the game's). In the
inverted design the game's buffer is never presented, so that conversion and
the window server's copy of it both go.

Inverted-design estimate at this scene: keep the ~1.0M floor, the sprite
painter 0.64M (if the HUD is what it paints; billboards can move to GL), the
blend fills 0.30M, the 3D transform/sort 0.40M, our submission ~0.3M, the HUD
upload ~0.4M: **~3.0M a frame** -> 168M/3.0M = **~55 fps**, and ~62 if the
sprite painter's race work is billboards. The 60 is back on the table, on this
scene's numbers; a driving profile (AI, physics under load) is next, and the
device's instr/s figure is the other unknown.

**Driving profile** (same build, accelerate held from 6 s after the start,
SKIP 6.0G, 0.5G credited, window median **5.03M/frame**, `prof_noblit_drive.txt`):
`+CCB8` 18.3% (0.92M: fewer presents per longer frame -- the runtime's cadence
again), sprite painter 12.2% (0.61M), transparent filler 6.1% (0.31M),
`+3015E` 5.3% + `+33EA8` 5.1% + `+33F8C` 4.7% + `+333F4` 3.4% (span modes and
per-primitive setup still in software, 0.93M), our code 8.1% (0.41M), blend
fills 5.5% (0.28M), `+2E1C8`/`+2ECB4`/`+2FF60` 6.1% (0.31M). New under load:
`+2C4A4` 2.1%, `+2CE90` 0.6% (collision/physics, by position). Everything that
is not drawing: **~1.2M** (the `+345E4` family 0.24, `+2C4A4` 0.14, the
`+35xxx` family 0.16, `+CE8C` 0.08, `+3115C`/`+31246` 0.12, euser/drtaeabi
0.09, tail ~0.3).

Inverted-design estimate mid-race: 1.2M floor + 0.61 sprite painter + 0.28
blend fills + ~0.3 3D transform/sort + ~0.4 submission + ~0.4 HUD upload =
**~3.2M -> ~52 fps**, ~60 if the sprite painter's race work is billboards that
move to GL. Next bench question, binary and cheap: what `+29736` paints during
a race (HUD only, or the trees and cars) -- count its calls and their sizes.

## What the sprite painter draws in a race: the HUD only (register trace)

`EKA2L1_PCTRACE_REGS` at `+29736` through 40 s of driving on the no-blit build
(`trace_sprites.regs`, 19,276 entries; the runtime-present marker was mistyped
as `0x70909B70` for `0x7090CB70` and never fired, so frames are counted by the
one call per frame at (1,5)). The tail 40% of the run, 391 frames:

- **18 calls a frame** (min 18, median 18, max 35), every one at a **fixed
  HUD position**: the position counter (1,0); lap text (34,5) (137,5) (213,5)
  (141,25) (148,25) (155,25); the speed gauge, one call per segment at
  (17..36, 220); the bottom items (209,210) (241,226) (263,183); the speed
  digits (236..257, 213). `+29DF1` and `+29BB1` are the occasional text
  banners at y=43..66 (0.8 a frame together).
- Four callers (`+29B1D` 12.6/frame, `+2A077` 6.2, `+29DF1` 0.7, `+29BB1` 0.1),
  seven sprite/font objects in r0 (r7, the one whose `[r7+0x34]` is the effect
  mode), one destination surface in r1 for all calls (`[r1+4]` pixels,
  `[r1+8]`/`[r1+0xA]` size, `[r1+0x38..0x3E]` the clip rect; I had the two
  arguments the other way round when reading the prologue).
- **No billboards.** Nothing varies with the camera; the trees and the cars go
  through the 3D span fillers (the transparent filler `+33964` is where they
  show in the profile).

So the painter's 0.61M a frame is ~32K instructions per small HUD blit -- the
generic per-pixel path plus a setup cost paid twenty times, once per gauge
segment. It does not move to GL by itself, but it **can be made to**: 18 quads
a frame from seven small atlas textures, which is the cheapest thing a GPU
does, and then the game touches the software buffer not at all -- no HUD
layer to clear, convert and upload (the ~0.4M of phase 3 goes too), and the
translucent HUD fills (`+2ABAA` family, 0.28M) become alpha quads.

Estimate with the HUD on the GPU as well: ~1.2M floor + ~0.3M 3D transform/sort
+ ~0.5M submission (3D runs + 18 HUD quads + fills) = **~2.0M a frame**,
~4.7x stock, ~80 fps on the bench's instr/s figure for the phone -- 60 with
margin for the figure being optimistic. Phase 3 is redefined: HUD painter and
translucent fills to GL, software buffer never presented.

## Round 0 result, and the composite's real bug (bench-reproduced)

The user ran `a4_opaque`, `a4_opaque_noblit` and `a4_pipelined` on the N95,
same track: **no difference in speed by eye** between the three (row 3 of the
decision table), and the two composite builds showed the same glitch a few
seconds into the race: the player's car vanishes, the floor near it goes
wobbly/dark, the trees get black backgrounds; the no-blit floor is blank out to
a distance (by design). Frames from the recordings at 2/8/14/17 s: at 8 s the
car, minimap, nitro bar and speedometer are all present; at 14 s and after, the
car and every bottom HUD element are gone while the top HUD (position, lap,
WANTED) survives.

**The bench reproduces it, and had been showing it all along.** `shots.sh`
(a4_opaque, start-line scene, twelve screenshots over 36 s): no car, no minimap,
no bottom HUD in any of them -- the same state the phone reaches after a few
seconds. My "renders correctly (road, grass, trees, opponents)" read of the
pipelined screenshot missed exactly the things that are missing. Rule 4: a
picture is not a measurement, and a reading of it is only as good as the
checklist it was read against -- the player's car was not on mine.

The signature names the cause: everything software-drawn **over GPU-drawn
ground** (the car, the bottom HUD, the minimap over the grass) is lost, and
everything over the **sky** (sentinel, not GPU-drawn) survives. So the
composite lands *after* the car and the HUD have been painted -- the
frame-end fallback in `a4_gl_frame`, not the 3D->2D boundary. The boundary
hook is the frame's first translucent fill through `fill.c`; on the phone some
early-race fill (an overlay that exists only for the first seconds) put the
flush in the right place and then went away. The fix is a boundary that is
not a side effect of the HUD's content: the draw-order trace (`regs_order.py`)
is to say where the 3D pass ends and what the car's mode is.

**The draw order of a race frame** (`EKA2L1_PCTRACE_REGS` at `+300AE` with
r2 = flags, `+29736`, `+2ABAA`, `ngibackbuffer +CB70`; `trace_order.regs`,
`regs_order.py`; a4_opaque at the start line, ~1,390 draws a frame):

```
P1x99  (P1 | P2 | P0 interleaved, ~600)  P1x155  (P32 | P0 interleaved, ~800)  Sx17
```

- `P1x99` first: the sky/background, textured. Then the world: textured
  (`1`), the bit-1 mode (`2`) and flat (`0`) **interleaved in painter's order**.
- `P1x155`: a block of 155 textured triangles drawn after the world -- the
  player's car (nearest, so last). It is flags 1, so the GPU path *takes* it
  unless its texture fails the colourkey scan; it does not show in the
  composite, so its textures (black tyres, windows: texel 0 is black in 565)
  are read as colourkey and handed back to software -- which is why it was
  overwritten rather than drawn late.
- `P32 | P0`, ~800: a mode with bit 5 that no earlier note lists, interleaved
  with flat -- by position (after the car) and count, the trees and roadside
  billboards with their shadows; all software.
- `Sx17`: the HUD. **No `F` at all**: a race frame has no translucent fill
  through `fill.c`, so the old boundary hook never fired and the flush fell to
  the frame end, after the car and the HUD.
- Flags over the tail: 0 x22352, 1 x18620, 2 x1824, 32 x12466. The textured
  mode is 34% of the primitives; the opaque-textured share the GPU takes is
  less than that after the colourkey hand-back.

**Fix (`composite_prev`): the composite moves to the frame's first primitive.**
The GPU frame drawn at the previous frame's end is read back and blitted
before any of this frame's software draws, so the car, trees and HUD land on
top of it in painter's order; `a4_gl_flush` at the frame end only clears and
draws. One frame of latency for the GPU world, as the pipelined build already
had; the readback follows the draw by the present plus the game logic (~2.4M
instructions), less than a full frame of GPU time, but the MBX needs a
fraction of that for ~300 triangles. The fill hook is a no-op. Builds:
`a4_gl` (clean), `a4_fpsbar_gl`, `a4_fpsbar_noblit` (`-DA4_FPSBAR`: the plain
fps bar kept in a GL build, over the top HUD banner). Bench check: `shots.sh`
on `a4_fpsbar_gl`.
Bench check passed (`shots_fix_*.png`, twelve shots over 36 s at the start
line): the car, the minimap over the grass, the trees and the HUD are all
present on top of the GPU-drawn road and hills, in every shot; the fps bar
reads at the top. The device's "trees with black backgrounds" did not show on
the bench and stays open (suspect: a texture cache slot reused after the
colourkey scan, late in a race when more textures are live than `NCACHE`).
Round 0 goes out again as `a4_fpsbar_stock`, `a4_fpsbar_noblit`,
`a4_fpsbar_gl`, each with the fps bar: full green rows x4 + the partial row,
red tick = 30, white = 15 and 45.

## Round 0, with the fps bar: the offload pays, the readback is the wall

Three recordings (sped up to fit), whole races: **stock** sits below the red
30 line almost the whole race and gets heavier when the screen is busy;
**noblit** (offload, nothing read back) is smoother and crosses 30 repeatedly;
**gl** (offload + the fixed composite) is the worst of the three -- down to
~8 fps while drifting -- and shows the wobbly floor and the black-backed trees
again, with the car and the HUD now present (the composite point fix held).

What this settles:
- **The GPU route is real on the N95.** With the textured-opaque mode on the
  MBX and no readback the game is faster than software by eye and by the bar.
  Row 1/2 of the decision table, not row 3: the driver's per-draw cost is not
  the wall.
- **The readback is the wall, and worse than the bench could show.** The
  composite costs more on the phone than the offload saves, by a wide margin;
  pipelining did nothing for it. On a tile-based GPU a `glReadPixels` is a
  forced render + flush + a copy out through the driver, fifteen times a frame.
  The "wobbly floor" is consistent with the read landing on a frame the GPU has
  not finished (tiles half-written), which pipelining by one frame does not
  prevent if the driver renders at the read.
- **The black-backed trees** come with the composite builds only and late in a
  race: a texture-cache entry reused across a texture reload (a stale
  `cache_tr`/image under a recycled address) is the suspect; not reproduced in
  36 s on the bench.

Decision: **no more work on the readback composite.** The inverted design
(phase 1: GPU presents through a window surface; phase 2: the whole 3D pass
on the GPU in painter's order; phase 3: the HUD as GL quads) is the only path
the data supports. The no-blit build is the proof it is worth it.

## Phase 1 on the bench: window up, swap up, present skipped -- and why the bench cannot show it

`-DA4_WINDOW` (a4_gl.c, a4_gl.s, injector): a `ws32` import table beside the GL
one; at bring-up our own `RWsSession`, a window group that declines focus at
ordinal priority 1000, a full-screen 565 `RWindow`, `eglChooseConfig` for a
window-type 16-bit config, `eglCreateWindowSurface`; at the frame end the
batch is drawn, the fps bar as GL quads, `eglSwapBuffers`. The present detour
returns 1 while our window is up and the trampoline steps over the game's
present call (`+1EA/+1EC`, the `[this+0x80]->vtable[5]` call). The window
hides after 30 present hooks without a 3D primitive (a menu) and shows again
on the next.

Bench (`glr` snapshots, `trace_skip.regs`): bring-up `winstep 6`, `bstep 6`,
`disp 1 surf 1`, 7,147 GPU frames swapped, `errc 0`; `+1EA` never entered in
the race (the skip holds), `+1CE` (the game's frame function) 3x per post. But
the panel shows the game's software frame, static. The reason is in the
runtime and the emulator:

- **The runtime posts on its own.** `ngibackbuffer +CB70` (the 565 -> 32
  convert into the hardware bitmap) is reached from `+3608`, a virtual call
  `[obj+0xC]->vtable[2](region, buffer)` taken when `+6690(window)` says the
  DSA region is the whole window; otherwise the code at `+360C` walks the
  region's rects (`[window+0x58]`, `RRegion`, euser 1833) and copies each
  through `vtable[4]` -- **the runtime clips to its DSA region**, and with an
  empty region copies nothing. The game's `+1EC` call is not what posts.
- **The emulator never shrinks that region.** EKA2L1 aborts a DSA only on a
  screen-mode change (`screen.cpp:562`) and draws the whole `dsa_texture`
  over the screen on every post (`dispatch/src/screen.cpp`); a window placed
  in front of the DSA owner changes nothing. A device does the opposite
  (SYMBIAN.md: `PossibleVisibilityChangedEvent -> IsAbortRequired`, abort,
  restart with the visible region, empty while covered). So on the phone the
  runtime's copy stops the moment our window is in front, and on the bench it
  cannot. The bench has proven the mechanics that are ours (window, surface,
  swap, skip); the region behaviour is the platform's, documented, and only
  the phone can show it.

Device test `a4_window` (built with `-DA4_FPSBAR`): in a race, expected the
GPU world (road, hills, sky) with the GL fps bar at the top and **no car, no
HUD, no trees** (software layers are not presented -- that is phase 2/3), and
the fps number is the true "GPU presents" rate with the runtime's convert
gone; in the menus, the software screen as before. Outcomes: (a) as expected
-> phase 1 proven, on to phase 2; (b) the software frame shows through or
flickers -> the runtime does not clip or the DSB flips regardless; the
copy must be stopped another way; (c) nothing GL at all -> the N95's
`eglCreateWindowSurface` wants something else of the window (the runtime's
own `ngiOpenGLControl` path is the fallback to read).

## Phase 1 device round: WSERV 6 (invalid window handle), and what it rules in

The device panicked **WSERV 6** (`EWservPanicWindow`, invalid window handle,
`windowserver/SERVER/w32cmd.h`) shortly into a race. Verified against the
hardware before the next step (rule 4):

- **Every ws32 ordinal used is in the RM-409 ROM and matches its def name**
  (`romimg` over `z:\sys\bin\ws32.dll`, 580 exports): 76/70/69 session,
  158/157/148 group, 211/212 ordinal-position, 278/275/277 window,
  107/81/99/268. 157 is `Construct(TUint32,TBool)` (the focusable overload),
  not a screen-device one. So the calls are right; the ordinals are not it.
- **The object sizes fit.** RWsSession (MWsClientClass+RSessionBase+iPimpl
  ~24 b), RWindowGroup (8 b), RWindow (RDrawableWindow = MWsClientClass + TRect
  = 24 b) all inside the 0x20 slots. Not a layout overlap.
- **The bench cannot catch it.** EKA2L1's window server indexes its object
  vector and never panics an unknown handle (`get_object`); the "parent ID
  0x0 -> root" and "handle invalid 0" log lines fire for the game's own
  top-level groups too, so they are benign. The window built on the bench
  (winstep 6, surface made, 7k swaps). WSERV 6 is device-only, as the DSA gap
  is (CLAUDE.md).

**What WSERV 6 points at:** the platform's EGL takes a *native window* and
drives the window server with it; a raw `RWindow` we built in a second
`RWsSession` is not what it expects. The platform's own GL path proves the
shape: `ngiOpenGLControl.dll` never constructs a window -- it receives one
(`RWindow::SetExtent` on a window passed in) from a `CCoeControl`, and
`ngidisplay.dll` makes its window through `CCoeControl::CreateWindowL`
(cone 25) on the app's own window group, not a parallel session. So the
device-safe native window is the runtime's existing one, not ours.

**Next step is bench-side, no hardware round:** find the runtime's live
`RWindow` (ngidisplay/runtime own it; a window object is MWsClientClass, so
its second word is the session's RWsBuffer -- SYMBIAN.md's test for "is this a
window") by tracing/memsnapping ngidisplay around its `CreateWindowL`, and
bind the EGL window surface to that handle instead of a window of our own.
That removes the second session, the focus juggling and the front-priority
group -- all of which the device had to accept and one of which it refused.

## Phase 1, take 2: the window in the app's own session (WSERV 6 resolved)

The device WSERV 6 was a window built in a SECOND window-server session.
Symbian's `libGLES_CM` resolves the session internally via
`CCoeEnv::Static()->WsSession()`, so a window in any other session is an
invalid handle to it. Fix: create the EGL window in the app's OWN session.

Found by tracing (bench, stock build): the app window the game's DSA uses is
`CDirectScreenAccess::NewL`'s 3rd arg (r2) -- a real `RWindow` (iBuffer ==
session iBuffer, the SYMBIAN.md test). But the only pointer to it is the DSA
object's `iWindow`, with no stable root reachable from the game exe. And the
game exe has no cone/ws32/EGL imports -- the window flows framework -> runtime
-> ngibackbuffer. So we take the app's session and root group from `CCoeEnv`
instead.

`CCoeEnv::Static()` (cone 182) resolves on the bench; `RootWin` (717) does
**not** (slot 0, though 717 is in the ROM cone's 729 exports) -- an emulator
resolver gap at that ordinal. So we read `CCoeEnv`'s `iWsSession` and
`iRootWin` by **content, not offset**: the bench ROM is RM-409 (Nokia 5320,
S60 **3.2**), a different edition from the N95 (**3.1**), and private offsets
differ across editions (SYMBIAN.md), so a measured offset would be wrong on
the phone. The scan (bringup, `a4_gl.c`): the RWsSession is the member with a
heap `iBuffer` at word+1 and a kernel handle (>=0x40000000) at word+2; the
RWindowGroup is the member with a window-server handle (nonzero, <0x80000) at
word+0 and the same `iBuffer`. Our `RWindow` is built as a child of that group,
sharing the app's `RWsBuffer`; the object is ours (scratch), the session and
buffer the app's.

Bench (RM-409): `winstep 7`, EGL window surface created (`surf=1`), `errc 0`,
7481 frames, no crash; env/session/group/buffer all sane
(0x900358/0x900384/0x900390/0x900a40). As far as the bench can go -- it draws
the DSA framebuffer regardless of our window (the CLAUDE.md gap), so it cannot
show the window itself.

**Device test (`a4_window`, `-DA4_FPSBAR`):** a winstep bar is drawn on the
bottom row of the SOFTWARE buffer every frame -- occluded by the GPU window
when it presents, visible when it does not. Read it:
- **GPU world on screen (road/hills/sky) + fps bar, no car/HUD/trees** -> phase
  1 works; the scan ported to 3.1. On to phase 2 (whole 3D pass) and 3 (HUD).
- **software game + GREEN bottom bar** -> window up (winstep 7) but not
  presenting: EGL/DSA interaction, not the handle.
- **software game + YELLOW bar** -> a bring-up step mid-way (bar length =
  winstep*8 px: 7 steps = 56 px).
- **software game + RED bar** -> the scan bailed on 3.1 (0xC0 cone Static
  missing, 0xC2 no session matched the heuristic, 0xC3 no group); the length
  encodes which, and the handle/heap heuristics need the N95's encodings.

## Device load error -5, and the fix: do not import a 3.2-only ordinal

The first app-session build would not start on the N95: "Unable to start game
(error -5)" = `KErrNotSupported` at load. Cause: the cone import included
`RootWin` ordinal **717**. The bench ROM (RM-409, 5320, S60 3.2) has 717 and
its loader tolerated it (left the slot 0 when it would not resolve); the N95
(S60 3.1) is an older edition whose cone does not export 717, and a real
loader REJECTS an image whose import cannot be resolved -> the image never
loads. The second-session WSERV-6 build imported only ws32 (all present on
3.1) and loaded; adding a 3.2-only cone ordinal is what broke it.

Fix: import `cone` **182 (Static) only** -- a universal export across S60 3.x
-- and leave the window group to the content scan, which never needed RootWin.
Rule for this project: an import ordinal is a per-edition claim; importing one
the target edition lacks is a load failure, not a runtime one, so it cannot be
caught by a bench on a different edition. Prefer the lowest, oldest ordinal
that does the job, and resolve the rest by measurement at run time.


## The N95 ROM installed, and why phase 1 is not benchable

The user supplied the N95 8GB ROM; installed as **RM-320** (Nokia N95,
epoc93fp1 = **S60 3.1 FP1**), alongside RM-409 (5320, 3.2). Findings:

- **The emulated N95 cannot boot the game.** Stock `asphalt4_ngi.exe` on
  RM-320: **KERN-EXEC 3** at boot, 0 frames (2048 access-violation reads, PC
  -> 0). The whole bench (190+ rounds) was built on RM-409; EKA2L1's 3.1
  support (or the N-Gage platform setup for it) does not run this title. So
  the one edition I can bench (3.2) is not the target, and the target (3.1)
  does not run on the bench. **The 3.1 window path is not benchable; the phone
  is the only oracle for it.**
- **cone ordinals are stable across editions** (182 is byte-identical on both
  ROMs and worked as `Static()` on the bench), so that is not the problem.
- **Device `a4_window` (cone 182 only) result: CONE 46** =
  `ECoePanicInvalidHandle` (`cone/src/coepanic.h`), ~15 s into a race, after
  the race ran in software. So bring-up ran, the content scan found the WRONG
  objects under 3.1's different CCoeEnv layout, and the window built on them
  carried a handle the framework later rejected. (The fps bar showed, software
  rendered -> the GPU window was not presenting.)
- **cone 717 (RootWin) must not be imported:** it loads on the bench (3.2) but
  gives device load error **-5** (`KErrNotSupported`) on the N95 -- a 3.2-only
  / placeholder slot on 3.1. Import `Static` (182) only.

**Consequence.** The content scan (`a4_gl.c` bringup) is tuned to RM-409's 3.2
CCoeEnv (session = heap iBuffer + kernel handle at word+2; group = ws handle +
same iBuffer). On 3.1 those word positions / handle encodings differ, so it
mis-identifies. Fixing it blind costs one hardware round per guess, which the
project's rules forbid. Next step is a **measurement, not a guess**: a safe
telemetry build that reads CCoeEnv on the phone and reports its layout (writes
the first words to a file on C:, or encodes them on screen) WITHOUT creating a
window, so it cannot panic -- one round yields the 3.1 layout, then the window
code is written correctly for it. Pending the user's call on spending that
round vs. banking the bench-safe CPU-loop rewrites (~10-13%) first.

## Why the game will not launch on the N95 ROM: OpenC is missing

Traced the RM-320 boot crash. Stock `asphalt4_ngi.exe` faults at its first
instruction after `$HEAP` is created: **access violation at 0x0** (KERN-EXEC
3), PC=0, LR=0x7004b998. The call is `+0x4b994 bl +0x50f64` -- an import
veneer whose slot holds a raw ordinal, i.e. unresolved. The slot is
**libc.dll ordinal 2** (the game imports `libc{...}[10207369]`, the Open C
standard library), and the log says **"Can't find libc.dll"**.

Cause: **the N95 ROM contains no Open C / PIPS libraries** (libc, libm, libz,
libpthread all absent from `z:\rm-320\sys\bin`); the 5320 ROM (RM-409) has
them built in, which is the only reason the whole bench ever ran. Open C is a
separate runtime that a real N95 has installed (the game needs it,
UID 0x10207369, ordinals up to 349+). So the game runs on the user's phone
(Open C installed) and on RM-409 (Open C in ROM), but not on a bare emulated
N95.

Tried supplying the PIPS libs the N-Gage package bundles
(`ngapp/x_inner/libc.dll`): the crash advanced from KERN-EXEC 3 to
**USER 7** (`ETDes16BadDescriptorType`) -- because the bundled `libc.dll` is a
different library (UID **0x20002339**, only **178 exports**), not the Open C
libc the game links against (0x10207369, 349+ exports). It resolves by name
but has the wrong ordinals. Removed it again (it was on the shared E: drive
and would shadow RM-409's ROM libc and break the working bench).

**Status:** the launch failure is diagnosed. The fix is to install the Open C
3.1 runtime into the emulated N95, which is not present in any extracted files
here (only Open C *stub* SIS in the 5320 ROM, and the wrong bundled libc). To
proceed, either the Open C `stdlibs` SIS for S60 3.1 is needed, or the 3.1
framework layout is extracted statically from the N95's own `cone.dll`
(no boot) to fix the window scan directly. Pending the user's call.

## Open C supplied -> game loads, blocked on one file: backend.dll (0x102828d5)

The user supplied the N95's Open C runtime (libc 0x10207369/596 exports, libm,
libz, libpthread, libstdcpp, libssl, libcrypto, libcrypt, libdl). Placed on
E:\sys\bin (the loader searches the game's own drive first, not C:). Result:
the game loads its whole dependency tree -- 50 modules -- and gets deep into
Open C init before faulting. Each layer resolved revealed the next:
  KERN-EXEC 3 (no libc) -> USER 7 (wrong bundled libc 0x20002339) ->
  libc loads -> crash in libc init (missing backend) -> add bundle backend ->
  crash INSIDE backend.

The remaining block: **Open C libc imports `backend{...}[102828d5].dll`** --
the Open C POSIX backend. It is NOT in the supplied zip. The game package
bundles a *different* `backend.dll` (UID **0x20001074**, the NAF Bluetooth/SDP
backend: imports bluetooth/sdpagent/bteng), which matches by name and crashes
(wrong ordinals) when libc calls into it. The correct backend
(UID 0x102828d5) exists only XIP-baked in the 5320 ROM, which cannot be
relocated to RM-320. So one file is needed from the user's N95 Open C install:
**backend.dll, UID 0x102828d5** (part of openc_stdlibs). With it the chain
should complete.

## N95 bench: launch FIXED (Open C installed); boots but stalls at the launcher

Root cause of "won't launch on the N95 ROM" = **the N95 ROM has no Open C /
PIPS runtime** (the 5320 ROM has it built in, the only reason the bench worked).
The game imports `libc[10207369]` + the Open C stack and dies at the first
libc call without them. Fix, with the user's N95 Open C libs:

**Install (reproducible):** copy to `e:\sys\bin` (the loader searches the
game's own drive first, not C:):
- From the user's `OpenC_RM320.zip`: libc (0x10207369, 596 exp), libm, libz,
  libpthread, libstdcpp, libssl, libcrypto, libcrypt, libdl, libcipcserver.
- `backend.dll` **UID 0x102828d5** (the Open C POSIX backend; user supplied
  separately -- not in the zip, and the game bundle's `backend.dll` is a
  different lib, UID 0x20001074 = NAF Bluetooth, which crashes libc).
- `rpipe.dll` + `pipelib.ldd` from the game bundle (`ngapp/x_inner`).
Same UIDs as the 5320 ROM's Open C, so RM-409 is unaffected (verified: it
still renders with these on E:).

**Result:** the game now boots on RM-320 -- 328 modules load, no crash, the
window server and app UI come up (**CCoeEnv exists**), the NAF/Arena online
init runs and its -17004 leaves are trapped and bounded (11, same as RM-409).
Progression over the fix: KERN-EXEC 3 (no libc) -> USER 7 (wrong bundled
libc) -> libc init crash (no backend) -> backend crash (wrong backend) ->
boots with the right backend.

**Still stalls at the N-Gage launcher (black), before the game's render loop.**
EKA2L1 stubs the launcher's window-GC drawing on RM-320 ("Store draw command
stubbed") -> black; the Main thread then idles in ROM (euser wait,
ping-ponging with a NAF worker) and never enters Asphalt 4's own render loop,
so FRAMESTAT stays 0 and the injected hooks / a PC-filtered memsnap never fire
(the game is in ROM/NAF code, not its own). RM-409 gets past this; the
difference is a device-specific N-Gage-platform/window-server behaviour in
EKA2L1's RM-320 config, not the Open C fix. So the N95 edition now BOOTS on
the bench but is not yet a rendering oracle; getting it to the render loop is
a separate EKA2L1 RM-320 investigation.

## Why RM-320 boots but will not render: a two-thread livelock in init

Pushed RM-320 toward rendering. The game boots (OpenC fix), runs its full
startup, creates the FIRST screen device (the launcher's), loads the entire
NAF/HTTP transition stack -- then DEADLOCKS, never creating the SECOND screen
device (Asphalt 4's own 3D surface) that immediately precedes rendering.

Established by a 210 s run: it is not slow and not polling -- the property
polls FREEZE at 154 and FRAMESTAT stays 0 for 180 s. The steady state is a
tight two-thread ping-pong that never progresses:
  Main [asphalt4_ngi] pc=0x8039fa9c  <->  worker 01d0...  pc=0x8039f840
(both in ROM euser). The last forward progress before the freeze: spawn
`baksrvs` (!BackupServer), rendezvous `!ecomserver`, then "Sending a blind
sync message" -- and silence. RM-320 did ~203 ecomserver ops and 2 blind
syncs before freezing; RM-409 does ~503 and 5, and renders. Same EKA2L1
binary, same libraries, same screen/property/NAF activity on both -- the
divergence is a subtle service/scheduler interaction that completes on the
RM-409 (5320/3.2) config and livelocks on the RM-320 (N95/3.1) config.

**Assessment.** The N95 *launch* is fixed (it boots, 328 modules, window
server + CCoeEnv up) -- the original question is answered. But making it
*render* is a deep EKA2L1 scheduler/platform-init bug (a two-thread euser
livelock), not a data/DLL gap, and the RM-320 graphics path has its own holes
("Corrupted graphics command list", stubbed window-GC draws). As a rendering
oracle for the 3.1 window path it is not paying off. The remaining way to the
3.1 framework layout without this is the phone telemetry round (a safe build
that dumps CCoeEnv and cannot panic), or banking the bench-safe CPU rewrites.

**The livelock, named:** resolving the ping-pong PCs against the N95 euser --
the worker thread is in **`RFastLock::Wait`** (euser ord 1416, a fast
mutex/semaphore); Main is in euser a little above it. Two threads contend on a
fast lock and make no progress. Open C (libc/libpthread), which the game now
uses heavily, is built on these locks, so this is most likely an Open C
pthread-mutex contention that EKA2L1's fast-lock scheduling does not resolve
fairly on the RM-320 run (it does on RM-409). That is an emulator
scheduler/sync issue, deep and uncertain to fix, not a missing file.

## RM-320 RENDERS: the livelock root-caused and fixed (EKA2L1_EXCLFAULT)

The N95 bench now runs Asphalt 4's race loop. Root cause fully traced, fix in
the emulator, verified by screenshot on RM-320 and no regression on RM-409.

**The livelock, traced end to end.** With a scheduler probe
(`EKA2L1_SCHEDDIAG`, new in `scheduler.cpp`: logs the outgoing thread's state,
wait-object and PC on every reschedule), the steady state was:
`ngilightserver`'s **Main** thread spinning in euser (state=run, no wait
object), while `asphalt4_ngi`'s worker sat blocked (state=wait_fast_sema) on a
request semaphore the light server would complete. Resolving the spin PC
against the N95 euser export table (romimg: 2229 exports, so a real ordinal,
not a def guess) gave **ordinal 1416 = `RFastLock::Wait()`**, and the N95 (3.1)
euser is the **SWP/1 ms-backoff variant** of it (arch-5 style, implemented with
ldrex/strex) -- the 5320 (3.2) euser is the clean ldrex-and-block variant, so
this spin shape only exists on 3.1.

Capturing r0/r2 at the spin: the lock pointer was **0x34** and the value read
was **0** every time -- i.e. `RFastLock::Wait` on a lock at address ~0x30, a
**null object**. The caller (saved LR) was euser **ordinal 696 =
`RHeap::Reset()`** (`RHeap::Reset` does `add r5, this, #0x30` -> `RFastLock::Wait`
on `&iLock`, so iCount at this+0x34 = 0x34 means **the RHeap `this` is NULL**).
Reading the real stack (in the process's own address space) gave the caller of
`RHeap::Reset`: **`ngilightserver.exe +0x324`**, a `blx r2` where
`r2 = (*this->[0x1c])->vtable[5]` -- a virtual `Reset()` on a null member
(`this->[0x1c] == 0`). The function is a **destructor** (sets the vtable,
destroys sub-objects, calls `Reset`), running during a **leave unwind**: the
sequence right before it is `ngilightserver` querying the HWRM Light service
(EKA2L1 stubs "get supported targets" with -1), attaching a property, then
`User::LeaveIfError` leaving with **-6 (KErrArgument)** / **-12
(KErrPathNotFound)** (chain through efsrv + ecomserver). So an init step fails
on the emulated N95, a half-built object's destructor runs, and it makes a
virtual call through a null allocator pointer.

**Why it hung the whole system (the emulator half).** That null dereference is
an `ldrex [0x34]`. On an ARMv6 device this is a data abort -> KERN-EXEC 3, and
the light server thread dies. EKA2L1's exclusive monitor instead read the
unmapped address back as **0** and let the following `strex` spin-fail forever
(`exclusive_monitor::exclusive_read*` ignored the read's success flag). So a
guest null-deref that kills a thread on hardware became an infinite busy-spin
here, and the light server never completed the game worker's request. This is
the same family as the other known gaps (EKA2L1 declines quietly where a device
kills the process).

**The fix: `EKA2L1_EXCLFAULT=1`** (`src/emu/cpu/src/12l1r/exclusive_monitor.cpp`,
used by the dyncom core). An exclusive load whose target is unmapped now raises
the guest access violation, which the kernel turns into KERN-EXEC 3 on the
faulting thread -- as a device does. Off by default (the rest of the bench
relies on the lenient reads), opt-in for the N95 bench. Indexed in
`EMU_PATCHES.md`.

**Result (round-equivalent, both verified this session):**
- RM-320, `EKA2L1_EXCLFAULT=1`: `ngilightserver` Main dies cleanly
  ("Access violation reading address 0x34 in thread Main" -> "KERN-EXEC exit
  code 3"), the worker unblocks, the game reaches its render loop. FRAMESTAT
  0 -> 813 posts; drove into a race, **520 race frames, median ~9.4M
  instr/frame**; screenshot shows the in-race 3D view + speedo HUD (rotated,
  = N95 landscape orientation -- cosmetic, irrelevant to instruction counts).
- RM-409 regression (default, EXCLFAULT off, same new binary): still renders
  upright, **541 race frames, median 9.31M** -- unchanged. The switch is a
  no-op on any access that succeeds, so it cannot affect the 5320 bench.

**Bench status:** RM-320 is now a working rendering oracle for the N95 (3.1)
edition. Run it with `EKA2L1_EXCLFAULT=1`. Open C install (libc 0x10207369 +
backend 0x102828d5 + rpipe) remains the prerequisite. Next: use it to read the
real 3.1 CCoeEnv layout for the window-path (phase 1) GPU work, and to take
edition-accurate perf numbers.

## Race bench on RM-320 (first time it renders) -- and RM-409 parity

Controlled run (`benchrace.sh`): boot -> menu -> enter race (6x Return) ->
**hold accelerate** -> sample `FRAMESTAT` instr/post over ~40 s. RM-320 with
`EKA2L1_EXCLFAULT=1`. instr/frame is host-independent; N95 fps from the
NOTES.md calibration (115 instructions/us).

| device (edition)        | race frames | median | mean  | p10   | p90    | N95 fps @115/us |
|-------------------------|-------------|--------|-------|-------|--------|-----------------|
| RM-320 (N95, 3.1)       | 459         | 9.88M  | 9.55M | 8.07M | 12.07M | 11.6 (9.5-14.3) |
| RM-409 (5320, 3.2)      | 463         | 9.89M  | 9.72M | 8.25M | 11.96M | 11.6 (9.6-13.9) |

**The two editions render the race at the same per-frame cost (median 9.88M vs
9.89M, within 0.1%).** So RM-320 is now a faithful rendering oracle, matching
the long-validated RM-409 bench. The figure agrees with the earlier calibration
(~9.5M/frame), the user's by-eye 10-15 fps on the real N95, and the emulator's
own on-screen counter (it showed "12 FPS" mid-race, RM-320). Verified in a
screenshot that the sample was an actual race (countdown + HUD + traffic).

**Baseline for the GPU work.** The software race costs ~9.9M instr/frame =
~11.6 fps on the N95. Reaching 45-60 fps means ~2.0-2.6M instr/frame, a 4-5x
cut -- in line with the inverted-design estimate (~2-3.2M/frame -> ~52-80 fps).
That is the number to beat.

## Phase 1 wired on RM-320: the window-surface path brings up and survives a race (CONE 46 fixed)

With RM-320 now rendering (EKA2L1_EXCLFAULT), the 3.1 window path is benchable at
last -- the "measurement, not a guess" the record was waiting on. Built the
A4_WINDOW variant (`A4_EXTRA_CFLAGS=-DA4_WINDOW injector.py ... --gl --capture`;
the GPU draw-prim hook at +0x300B2 needs `--capture`), ran it on RM-320, and read
the state by memsnap of the renderer scratch (0x401000) rather than the on-screen
winstep bar -- a measurement.

**First run reproduced the device CONE 46 on the bench** (`Main panicked CONE 46`,
~mid-race), with `winstep 7, bstep 6, surf 1, errc 0`: bring-up "succeeded" then
cone panicked. So the bench is now a faithful oracle for this bug.

**The record's hypothesis was wrong: the CCoeEnv scan is correct on 3.1.** Dumped
the real CCoeEnv (memsnap at `env`) and decoded it against the Symbian class
layout (`W32STD.H`): `RWsSession : MWsClientClass {iWsHandle@0, iBuffer@4},
RSessionBase {iHandle@8}` (12 bytes, no own data), immediately followed by
`RWindowGroup : RWindowTreeNode : MWsClientClass {iWsHandle@0, iBuffer@4}`. In the
dump (env=...2e0): env+0x2c = {iWsHandle 0x124, iBuffer=appbuf, iHandle 0x400a0003
= kernel handle} = **iWsSession**; env+0x38 = {iWsHandle 0x20002, iBuffer=appbuf}
= **iRootWin**. These are EXACTLY what the content scan picked. The 3.1/3.2
layouts agree here; the scan was never the problem.

**The real cause of CONE 46 (from cone source, not a guess):** our window shares
the app's window-server session, so its redraw events pass through the app's
`CCoeRedrawer::RunL` (`COEPRIV.CPP`), which does
`__ASSERT_ALWAYS(IsHandleValid(handle), Panic(ECoePanicInvalidHandle))` then
`CCoeControl* w = (CCoeControl*)handle; if (w) w->HandleRedrawEvent(...)`.
`IsHandleValid(h) = (h & 3) == 0` -- a bare 4-byte-alignment check. Our client
handle was `A4_GL_MAGIC = 0x6C900DE6`; `0x...E6 & 3 == 2` -> unaligned -> the
assert fires **CONE 46** the first time the server sends our window a redraw (a
focus/exposure change mid-race -- which is why it survived ~15 s, not instantly).
The scan, the session, the group were all fine; one odd-valued handle did it.

**Fix (one line, `a4_gl.c` bringup):** construct our RWindow with client handle
**0**, not A4_GL_MAGIC. 0 is aligned (`IsHandleValid(0)` true -> no panic) AND
makes `w` NULL, so `if (w)` skips `HandleRedrawEvent` -- the redraw is consumed
harmlessly. We present every frame via `eglSwapBuffers`, so the server's redraw
protocol is never needed. (An aligned *nonzero* handle would pass the assert then
deref a wild CCoeControl* -- 0 is the only safe choice.)

**Result (RM-320, EKA2L1_EXCLFAULT, full 60 s race, memsnap + log):**
`winstep 7, bstep 6, surf 1, errc 0`, 2127 present-frames, `egl_swap` firing on
every 3D frame, **no CONE/WSERV/panic** -- the game runs the whole race with the
window-surface present path live. Screenshot: the game races; our GL window's
green fps bar shows at the top, the DSA software frame over the rest (EKA2L1 draws
the DSA framebuffer regardless -- the CLAUDE.md gap -- so present-OVER-software is
still only provable on the phone; bring-up and survival are proven here).

**Status.** Phase 1 (GPU presents through a window surface in the app's own
session) is wired and stable on the N95 (3.1) bench. ws32 ordinals, cone 182,
the CCoeEnv scan and the EGL window surface are all confirmed on 3.1. Remaining
phase-1 question (does the window present over the DSA frame) is the phone's to
answer; then phase 2 (whole 3D pass) and 3 (HUD).

## Phase 1 PROVEN on the bench: the GPU window presents the 3D world (EKA2L1_NODSA)

The one thing the bench "could not show" -- the GPU window presenting over the
software -- it can now, by removing the emulator gap that hid it. EKA2L1 draws
the DSA framebuffer over the whole screen on every post regardless of windows in
front (CLAUDE.md gap: a device aborts/clips the DSA to the visible region). Added
`EKA2L1_NODSA=1` (`dispatch/src/screen.cpp`): skip drawing the DSA framebuffer, so
only the window-server composite shows -- which includes our GPU present window.

**Result (RM-320, A4_WINDOW + EXCLFAULT + NODSA, in a race):** the screen shows
**our window's GPU content** -- the 3D track rendered by the GPU (road, kerbs,
grass, buildings, trees, lampposts, all the textured triangles a4_gl_draw_prim
captured), live and moving frame to frame, drawn upright. The sky and other
undrawn areas are **magenta** = the pbuffer/window SENTINEL clear (pixels the GPU
never drew). **No car, no HUD, no speedo, no minimap** -- those are the software
2D layer, which our window does not contain. This is exactly the record's
phase-1 success picture ("GPU world on screen + no car/HUD/sprites -> phase 1
works"), now seen on the bench instead of needing the phone.

So, measured and shown on the N95 edition: the EGL window surface is created in
the app's session (winstep 7), the GPU draws the captured 3D into it, and
eglSwapBuffers presents it to a real window the window server composites to the
screen. The GPU-present pipe works end to end.

**What this leaves for the phone and for phases 2-3:**
- On a device our window is opaque and fullscreen, so it fully covers the DSA --
  the device shows this same picture (GPU 3D + magenta sky + no HUD). NODSA on the
  bench faithfully previews that. The phone round is now a confirmation, not a
  discovery.
- Magenta sky / missing sprites+HUD is phase 1 BY DESIGN (GPU does the textured
  3D only). Phase 2 = move the whole 3D pass (incl. sky/background) onto the GPU;
  phase 3 = the HUD/sprites, or make the window colorkey-transparent on the
  sentinel so the software layer composites through. Either removes the magenta.

`EKA2L1_NODSA` is a diagnostic (env-gated, off by default); the principled fix
(clip the DSA to the DSA-owner window's visible region, as a device does) is a
larger emulator change left for later. Indexed in EMU_PATCHES.md.

## Phase 2, step 1: perspective-correct texturing (the wobble fix)

The phase-1 GPU road wobbled because it was textured AFFINELY. Confirmed from the
capture code: phase 1 read the already-projected screen (X,Y) and raw (U,V) and
drew them under a 2D ortho projection with 2-component texcoords, so GL
interpolated U,V linearly in screen space. The game itself textures
perspective-correctly (it multiplies U,V by a per-vertex 1/w). The big near-field
road triangles span the most depth, so affine mapping warps/swims there -- the
wobble.

**Measured the perspective input.** vptr[2] (the vertex's Z/W) captured for road
triangles: near vertex (screen y=241) ZW~202, far (y=122) ZW~1009 -- ZW grows
with distance, so **ZW is w** (depth), and 1/w is proportional to 1/ZW (also ~
linear in screen y, as a ground plane should be).

**The fix is two halves that only work together:**
1. **Renderer (`A4_PERSP`, `a4_gl.c`):** per vertex compute iw = 2^22/ZW with a
   self-contained shift-subtract divide (`a4_udiv` -- the renderer is freestanding,
   no libgcc, so no hardware divide or float), and emit 4-component projective
   texcoords (U_norm*iw, V_norm*iw, 0, iw) = (s,t,0,q) with q = 1/w. UV batch
   buffer widened to 4 (GL_SCRATCH_SIZE bumped to 0x12000).
2. **Emulator (`EKA2L1_TEXPROJ`, `gles1/shadergen.cpp`):** EKA2L1's GL ES 1.1
   fragment shader sampled `texture(tex, mTexCoord.xy)`, ignoring the q
   coordinate -- a GL conformance gap (GL samples at (s/q, t/q)). Added the
   divide, gated by the env var, default off. q defaults to 1 for a 2-wide
   texcoord, so it is a no-op for ordinary content. With it, GL interpolates
   (s,t,q) across the triangle (affinely, since our ortho w=1) and divides per
   fragment: Sum(U_norm/w)/Sum(1/w) = the perspective-correct U_norm.

**Result (RM-320, A4_PERSP build + EKA2L1_TEXPROJ + NODSA, in a race):** the GPU
road is textured perspective-correctly -- lane markings and asphalt foreshorten
to the vanishing point, grass/kerb/buildings all correct, no wobble. Without
TEXPROJ the same build showed flat untextured solids (q ignored -> wrong sample
coords), proving the two halves are both needed. Indexed in EMU_PATCHES.md.

**Remaining phase 2:** the magenta sky / sprites are still the software layer --
the GPU pass does not yet take the sky/background and the non-textured primitive
modes. Taking the whole 3D pass (all `flags` rasterizer modes + the background)
and adding a depth buffer is step 2; that removes the magenta.

## Phase 2, step 2 attempt: the whole pass needs more than flags 1 (findings)

Tried to extend the GPU pass beyond the perspective-correct textured mode. Two
things stand in the way, both characterized on the bench (NODSA screenshots +
memsnap captures), neither a quick add:

1. **The rest of the geometry is a different render mode.** A flags histogram of
   every draw_primitive (before the textured-only filter) over a race:
   - low-nibble 1 (bit0, the +0x2F6FC perspective-textured rasterizer): ~51k prims
     -- this is what the GPU path takes now.
   - low-nibble 0: ~116k prims (~2.3x), also carrying a texture. BUT capturing
     their descriptors showed the full flags is **0x4000** (bit0=0): a DIFFERENT
     rasterizer path, not +0x2F6FC. Geometry/UVs are the same desc layout (sane
     v0 x,y, ZW~1600, sane U,V, one texture 0x01f46240 for a tight screen
     cluster), but bit0=0 means the game does not texture them through the
     perspective path -- likely flat/gouraud-shaded or alpha-blended. Drawing
     them as plain textured (what the current path does) overdraws the screen
     with a flat fill. Taking them correctly needs disassembling the dispatcher
     (+0x300AE) to see how flags->rasterizer maps and what 0x4000 shades (vertex
     colour? blend?), then emitting the matching GL state. Also needs a **depth
     buffer** (they interleave with the flags-1 geometry in Z): scaffolded it
     (A4_DEPTH -- EGL EGL_DEPTH_SIZE, GL_DEPTH_TEST, z = ZW>>3 into an ortho
     [0,4096] range, depth clear bit; no new GL IAT funcs needed, defaults
     GL_LESS/clear 1.0 suffice) and it is correct, but reverted with the flags-0
     attempt since the mode is wrong until its shading is known.
2. **The sky is not draw_primitive geometry.** No primitive of any flags mode
   has a vertex in the top ~sky band (all minY >= 77 except a few flags-1 at the
   horizon). The magenta sky is simply where nothing draws -- the game fills the
   sky by another path (a framebuffer fill / gradient, not a triangle), so it
   can't be captured at the +0x300B2 hook. Removing the magenta means either
   finding that fill, drawing our own sky, or making the window colorkey the
   sentinel so the software sky composites through (phase-3 territory).

**Status.** Phase 2's substantive step is done and shipped: the
perspective-correct textured 3D pass (flags 1) on the GPU, wobble fixed. The
"whole pass" completion (the 0x4000 shaded mode + depth, and the sky) is a
larger RE task, scoped above. Good checkpoint to confirm the perspective pass on
the phone (the MBX GPU) and/or decide whether the 0x4000 mode is worth the RE for
the perf it would offload, before sinking rounds into it.

## Phase 1 device round #2: WSERV 4, and the window-handle dilemma (cone vs wserv)

Phone round of the perspective build (handle 0): the game rendered correctly in
software up to a race, then **WSERV 4** ("Application closed / Main / WSERV 4",
video) the moment the GPU path engaged -- and NO green fps bar, so bringup/present
never completed. WSERV 4 = **EWservPanicNullHandle** ("null handle given as a
handle value", `w32cmd.h`): the real window server rejects a **0 client handle at
RWindow::Construct**. The bench's window server is lenient and accepted 0 (which
is why the bench was green) -- a bench/device gap that hid this.

So the window's client handle is pinned between two validators, both of which the
bench under-enforces:
- **cone** (shared app session): `CCoeRedrawer::RunL` and the WS-event handler
  reinterpret the client handle as `CCoeControl*`. Redraw asserts `handle&3==0`
  (unaligned -> CONE 46) then, if non-zero, calls `HandleRedrawEvent`; the event
  path calls `HandleWsEventL(ev, (CCoeControl*)handle)`. So the handle must be 0
  (skipped) or a real control.
- **window server**: the client handle must be **non-zero** (WSERV 4).

0 fails wserv; a non-zero fake fails cone. Tried a **crafted minimal CCoeControl**
as the handle (iFlags@0x1C=0 so IsReadyToDraw() is false -> no draw; iWin@0x20=our
RWindow so HandleRedrawEvent's Window().BeginRedraw/EndRedraw hit a valid window;
offsets from CCoeControl = CBase-vptr, MObjectProvider-vptr, iContext, iPosition,
iSize, iFlags, iWin). On the bench it **cleared CONE 46 (redraw path safe, CONE=0)**
-- real progress -- but then crashed `Access violation 0xFFFFFFFF` in ws32 (called
from euser, NOT cone's redraw) right after an orientation change. Comparison: the
handle-0 builds survived 45-95 orientation events with no such crash; the crafted
control crashed after 2. So the non-zero handle also goes through cone's **event**
path (`HandleWsEventL`), and a minimal fake control is not safe there. Reverted.

**Conclusion.** An injected RWindow in the app's cone session needs a client
handle that is a **real, fully-constructed CCoeControl** (safe for cone's redraw
AND event paths) and non-zero (wserv). A fake won't do. Options for the next step,
in rough order of robustness:
1. Find the app's own top CCoeControl (via CCoeEnv->iAppUi->control stack) and use
   its address as the handle -- safe for all cone paths. Needs the N-Gage app to
   have a conventional control (uncertain for a DSA title) and a scan to find it.
2. Make our window a **backed-up window** (RBackedUpWindow) or otherwise event-
   free, so it never reaches cone's redraw/event paths at all -- then any non-zero
   handle is fine. Needs confirming EGL accepts a backed-up window as its native
   window.
3. A different present mechanism that avoids the app's window/cone entirely.

The perspective-correct GPU 3D render itself is proven correct (bench, NODSA); the
blocker is purely the window-present plumbing fighting the framework. Good point to
decide how far to push the plumbing vs. bank the render and move on.

## Option 1 (backed-up window): dissolves the cone dilemma, but doesn't present

Pursued the backed-up-window idea to escape the client-handle dilemma. Added ws32
RBackedUpWindow::Construct (ord 197, from WS322U.DEF -- same def whose ord 81 =
SetVisible matches our IAT, so the N95 ordinals line up) and built our window as
window_type backed_up (A4_BACKUP), with a deliberate POISON client handle
(0x7FFFFFFC, unmapped/aligned/non-zero) to detect any cone deref.

**Result (bench): the cone dilemma is solved.** Full race, **109 orientation
events** (the crafted control died after 2), poison handle **never faulted**
(no "reading 0x7FFFFFFC"), no CONE, no WSERV -- so a backed-up window gets no
redraw AND no event callbacks: cone never touches our handle, and the window
server takes the non-zero handle. The whole WSERV-4-vs-CONE-46 bind disappears.

**But it doesn't present.** With the DSA suppressed (NODSA) the GPU content is
black. Cause: EKA2L1 composites an EGL window surface only for the REDRAW canvas
(`redraw_msg_canvas::draw_surface_window`, winuser.cpp); the `bitmap_backed_canvas`
has no equivalent, so the EGL surface never reaches the screen. And whether the
real N95 PowerVR EGL presents a backed-up window at all is unknown (EGL window
surfaces are normally RWindow/redraw).

**So the two window types trade off exactly oppositely:**
- redraw window: **presents** (proven, NODSA shows the 3D) but is cone-fragile
  (handle must be 0-or-real-control; 0 -> device WSERV 4; fake -> event-path crash).
- backed-up window: **cone-safe** (no callbacks at all) but doesn't present (EKA2L1
  only composites EGL for redraw canvases; device support unknown).

**Recommended path: redraw window + the app's own real CCoeControl as the handle.**
A real, fully-constructed control is safe for every cone path (redraw and event),
and the redraw window presents. It is fully bench-validatable (the bench reproduces
both the CONE 46 and the event-path crash) and uses only proven mechanisms, so it
is the most likely to work on the device. The open question is whether this N-Gage
(DSA) title has a findable top CCoeControl (via CCoeEnv->iAppUi->control stack);
if it does not, fall back to fixing EKA2L1's backed-up present + a phone round to
test device backed-up EGL. A4_BACKUP code kept (gated off) as the cone-safe
building block; ws32 ord 197 added.

## The app has NO CCoeControl: the stack is structurally empty

Went ahead with the redraw+real-control plan: resolve the app's top CCoeControl
and use its address as the window's client handle. Taught a4_gl.c the measured
chain (CCoeEnv+0x24 = iAppUi; CCoeAppUi+0x0C = iStack; CArrayFixBase iCount@0x04,
iBase@0x14; CBufFlat iPtr@0x10; SStackedControl 16B, iControl first) and had it walk
the stack, log it to the diag struct, and pick the first window-owning control
(iWin@+0x20 live) -- else bail (winstep 0xC4). Benched RM-320, NODSA, memsnap 0x401000.

**Result: stkcount = 0.** The chain resolves perfectly and deterministically
(env=0xD002E0, iAppUi=0xD028A0, iStack=0xD009C8 -- iAppUi/env identical to a prior
run's live dump), but the stack is empty. Confirmed by dumping the objects directly
(not via my chain):
- iStack @0xD009C8: vptr=0x8078C650 (real ROM vtable), **iGranularity=2**,
  **iLength=0x10** -- both exactly what `CCoeControlStack()` sets, so this IS the
  cone control stack -- **iCount=0, iBase=NULL**. Empty, no buffer ever allocated.
- AppUi @0xD028A0: vptr=**0x701031CC (the GAME's code, 0x70xxxxxx)** -- the gate6
  shim supplies its OWN CCoeAppUi subclass; iCoeEnv@+0x04=0xD002E0 (=env, confirms
  +0x04), iStack@+0x0C=0xD009C8 (confirms +0x0C). A DSA title: it draws direct and
  never pushes a control.

So there is no real CCoeControl to use as the client handle. The approved
real-control path has nothing to point at. (The game process ran the full race;
the only KERN-EXEC 3 is the expected ngilightserver EXCLFAULT kill -- `ldrex [0x34]`,
same as every handle-0 run -- the RM-320 render fix, not a regression.)

## But a CRAFTED control can now be done right (the earlier one's root cause)

CCoeRedrawer::RunL does `((CCoeControl*)handle)->HandleRedrawEvent(rect)`, and
HandleRedrawEvent (COECNTRL.CPP:1388) is `Window().BeginRedraw(rect); if
(IsReadyToDraw()){...Draw()...} Window().EndRedraw();`. IsReadyToDraw() (line 313) is
`((iFlags&(EInvisible|EActivated))==EActivated) || (iFlags&EBackedUpWindow)` -- so
**iFlags=0 makes it false**, and the whole draw block (ActivateGc, virtual Draw,
DrawComponents) is skipped. What's left is BeginRedraw/EndRedraw on the control's
iWin. Point iWin (+0x20) at OUR OWN window and cone's redraw of our window becomes
a no-op redraw that VALIDATES our window (killing the redraw storm) and touches
nothing else. HandleWsEventL derefs aDestination only for pointer events (none on
the keypad-only N95); orientation (EEventScreenDeviceChanged) does not. So a
minimal craft -- vtable stub, iFlags@+0x1C=0, iWin@+0x20=A4_WS_OBJ, rest zero --
satisfies every cone path. The round-? crafted control crashed precisely because
its iFlags/iWin were wrong (it hit the Draw block on a redraw-during-orientation);
this craft removes that root cause. Bench-validatable (redraw window presents).
Open risk: does cone's BeginRedraw/EndRedraw blank the EGL surface? -- bench shows it.

## Crafted control: dissolves BOTH handle panics, GPU presents -- but redraw
## servicing faults in ws32

Built the crafted control done right: a zeroed 0x28-byte CCoeControl at 0x402840
with iFlags=0 (so IsReadyToDraw() is false and HandleRedrawEvent skips its whole
draw block) and iWin(+0x20) -> our own window, used as the redraw window's client
handle. Benched RM-320, NODSA, memsnap.

**Result -- the two hard blockers are gone.** winstep=7 (EGL window surface + context
up), ctrlh=0x402840, **frames=147, prims=71, 24 textures** -- the GPU 3D pass ran
and presented. **CONE=0** (the aligned crafted control passes cone's IsHandleValid
and HandleRedrawEvent runs without panicking), **WSERV=0** (the non-zero handle
passes the device null-handle check). stkcount=0 reconfirmed. This is the first
build to clear both the WSERV-4 and the CONE-46 at once.

**But it crashes after ~147 frames**, uniquely (kernel AV 0xFFFFFFFF = 1 here, 0 in
every handle-0 / backed-up run): the GAME's own Main thread, executing **ws32.dll**
(pc 0x806c8cd4, ws32 base 0x806c8658; lr in euser's active-scheduler/IPC path),
reads 0xFFFFFFFF -> KERN-EXEC 3 -> Domino cascade -> host halt. The one behavioural
difference from the working handle-0 build is that a non-zero handle makes cone's
CCoeRedrawer actually SERVICE our window's redraws -- HandleRedrawEvent ->
Window().BeginRedraw/EndRedraw on our injected window -- and that servicing faults
in ws32. (The window object is big enough: a full RWindow is only 0x18 bytes --
iWsHandle@0, iBuffer@4, iDrawRect@8..0x18 -- all inside our zeroed 0x20, so
SetDrawRect does not overflow. The fault is elsewhere in the ws32 redraw path, not
pinned to an instruction; the ROM disasm at the run address is not linear.)

**Reading:** the redraw window's very nature -- it gets redraw events that cone
services through ws32 -- is the recurring fight (first the handle, now the servicing
of an injected window). A BACKED-UP window takes NO redraw events at all (proven:
poison handle, 109 orientations, zero faults), so this entire class is absent; its
only gap is that EKA2L1 does not composite an EGL surface for bitmap_backed_canvas.
Fixing that (mirroring redraw_msg_canvas::draw_surface_window) turns the
already-cone-safe backed-up path into a bench-validated present, then one phone
round. That is the cleaner foundation than RE-ing the ws32 redraw fault and then
still not knowing whether an empty redraw blanks the EGL surface on the device.

## The crafted-control crash is the ORIENTATION redraw (not frame 1)

Pinned the cc1 crash from the log: it fires immediately after `[SCDV-HLE]
Orientation set to 2`, and the "last instruction" is 0x46696820 = the ASCII
"Fih " -- i.e. the game's Main thread JUMPED INTO A DATA STRING (a corrupted
return/pointer), not a clean field read. So frames 1-147 (incl. our window's
initial redraw) were fine; the fault is the FIRST orientation change.

Mechanism: our window is a redraw window sharing the app's RWsSession/RWsBuffer
(required -- libGLES_CM resolves the session via CCoeEnv::Static()->WsSession, and
a 2nd session can't back the EGL surface the app uses). With a non-zero handle,
cone's CCoeRedrawer SERVICES our window's redraws -- HandleRedrawEvent ->
Window().BeginRedraw/EndRedraw, writing + flushing the shared buffer. At
orientation, EKA2L1 resizes our fixed-size (320x240) injected window and
re-invalidates it (handle_extent_changed -> invalidate -> queue_redraw), and
servicing that rotated-screen redraw corrupts guest state. handle-0 never hit this
(cone skips a 0 handle); the backed-up window never hit it across 109 orientations
(no redraw events at all). ws32's MWsClientClass::Write derefs iBuffer with no null
guard, so a null-buffer dummy window can't make the servicing a no-op either.

On a real device, cone servicing redraws and flushing the shared buffer is the
normal path every app uses, so this specific corruption is most likely an EKA2L1
bench artifact (its rotate/resize handling of a surface-backed redraw window) -- but
the bench cannot validate through orientation as-is.

**State of the redraw window:** it is the first build to clear BOTH handle panics
and present the GPU pass (147 frames), but it dies at the first orientation, and the
remaining blocker is now EKA2L1 bench behaviour, not the renderer. The backed-up
window is proven cone-safe through 109 orientations and needs only an EKA2L1
compositing path (bitmap_backed_canvas EGL, mirroring draw_surface_window) to
present on the bench -- a smaller, self-contained emulator change that also matches
the device's real "no redraw events" backed-up semantics.

## SOLVED: the backed-up window presents (EKA2L1 compositing fix)

Took the recommended path. Fixed EKA2L1 so a bitmap_backed_canvas composites its
attached EGL surface, then built the backed-up renderer (A4_WINDOW + A4_PERSP +
A4_BACKUP, the poison-handle backed-up window) and benched RM-320 against the
fixed emulator.

The fix, in two parts (`src/emu/services/src/window/classes/winuser.cpp`,
`bitmap_backed_canvas::draw`):
1. The EGL surface is attached as `background_surface_` for ANY canvas
   (canvas_base::attach_surface), and eglSwapBuffers publishes each GPU frame into
   it, but only redraw_msg_canvas::draw composited it. Added
   `draw_surface(builder, background_surface_)` to bitmap_backed_canvas::draw.
2. First bench (bk2) was still black: draw() bailed at the top with
   `if (!driver_win_id ...) return false`, and our backed-up window carries NO
   backing bitmap (we never send a bitmap handle -- it exists only for EGL), so
   driver_win_id stays 0. Reworked draw() to proceed when EITHER a backing bitmap
   or an attached surface exists, draw the bitmap only if present, then composite
   the surface. (`on_activate` is empty for backed-up; driver_win_id is only set
   when the client sends a bitmap, which we never do.)

**Result (bk3, RM-320, NODSA): the GPU race PRESENTS through the backed-up
window.** winstep=7, last_prims ~480-566, 2000+ frames, ntex 75. Screenshot: the
Asphalt 4 road, buildings, trees and houses all from the GL 3D pass, perspective
-correct (A4_PERSP), with the sky as the GL magenta sentinel (NODSA suppresses the
software sky-fill that covers it on a real composite). black% fell from 87 (bk2)
to ~3; 900-1457 unique colours.

**And it is cone-safe through the whole race including orientations:** CONE=0,
WSERV=0, no crash -- the backed-up window takes no redraw events, so cone's
redrawer never touches our handle, dissolving the dilemma that killed the
redraw+crafted-control window at the first orientation. Only KERN-EXEC is the
expected ngilightserver EXCLFAULT.

So the GPU window-surface present path is bench-validated: cone-safe AND presents.
Remaining for a phone round: the device's PowerVR/MBX EGL must present a backed-up
window surface (the bench now does; device untested), and the sky/HUD software
layer must still composite with the GPU 3D on a real (non-NODSA) screen.
Documented in EMU_PATCHES.md ("A backed-up window composites its attached (EGL)
surface").

## Phone build handed out: backed-up GPU present (present-diagnostic round)

Built the phone artifact from committed source: `asphalt4_ngi.exe`, A4_WINDOW +
A4_PERSP + A4_BACKUP, --gl --capture. sha256 bf605bc49f1c6b370b3d62d75032e4a898
17f5fc857e1fd895f5a826da24971c -- byte-identical to the bench-validated bk3 image
(boot + full race + orientations, CONE=0/WSERV=0, GPU presents on the bench with
the backed-up compositing fix). The EKA2L1 fix is bench-only; the phone runs the
real PowerVR/MBX EGL + real window server, which is the untested question.

Owner bakes (owns the bypass): `make_loader_hashes.py asphalt4_ngi.exe
ngifilemanagerfactory_2001092b.dll <out>`, install the four-file set
(E:\sys\bin\{exe,dll} + C:\sys\hash\{exe,dll}).

What the round establishes that the bench cannot: whether the real N95 EGL
presents a BACKED-UP window surface. No NODSA on the phone, so expected outcomes
in a race:
- GPU 3D with a MAGENTA sky (the GL sentinel; no software sky composite yet) =>
  backed-up EGL PRESENTS on device (success).
- Black where the game was => our window is up/occluding the DSA but EGL is not
  presenting into it.
- Unchanged software race => our window never came up (unlikely; bench winstep=7).
Menus render normally (software); the GPU window only appears on the first in-race
3D frame.

Validation on record for this exact image: boot, full race, orientation changes
(bench). NOT run: the hold-C picture-mode / background-foreground / quit battery.

## Phone round 1 (backed-up GPU present): runs, GPU activates, but screen-door glitch

Owner ran the backed-up build on the N95 (video). Result:
- Boots, menus and the first ~16s of the race render NORMALLY (software; the GPU
  window only comes up on the first in-race 3D frame). No crash through the round.
- A few seconds in (~16-17s, when the GPU window activates) the whole screen turns
  into a fine regular CHECKERBOARD / screen-door mesh -- the software HUD (WANTED,
  lap, position) is meshed too, so it is a present/compositing-level artifact, not
  just the GPU surface. The scene stays recognisable (blue sky, buildings, road)
  under the mesh.
- **No magenta anywhere** (0% across all glitched frames), though the bench GPU
  present always shows a magenta sentinel sky. Measured from the video frames.

So backed-up EGL does light up on the device (the glitch onset tracks the GPU
window activating), but the on-device present is wrong. A regular two-shade
checkerboard = the N95 window server **screen-door (dither) alpha-compositing** our
window. The bench composites opaque (draw_surface blend off), so it never showed
this.

Leading cause: our EGL window config asked only `EGL_BUFFER_SIZE 16`, so the device
was free to hand us a 16-bit config WITH alpha (RGBA4444/5551), which the N95 then
screen-doors. Fix: request an explicit OPAQUE 565 config (EGL_RED 5, GREEN 6,
BLUE 5, **EGL_ALPHA_SIZE 0**). Bench re-run (o565): no regression -- winstep 7,
presents, magenta sentinel, CONE=0/WSERV=0. The checkerboard itself is not
bench-reproducible (bench composites opaque), so the fix goes to a phone round.

**This build is self-diagnosing** on the next round, because the magenta sentinel is
kept:
- clean GPU 3D + a MAGENTA sky  => the alpha-config was the cause, FIXED (then the
  magenta sky is the known phase-1 cosmetic: software sky/HUD compositing is next).
- still a blue software mesh, no magenta => our window is NOT presenting its GPU
  content, only corrupting the display (a different bug); investigate the surface
  present, not the config.

Build: a4_backup565.exe, A4_WINDOW+A4_PERSP+A4_BACKUP, sha256
38717eea9ded7ef170b1d6c2153851cd712a0375d69c83be64c17c2ac967d5a7.

## Thorough review of phone round 1: the window's GPU content is NOT shown on device

Re-examined the round-1 video frame-by-frame at 10 fps.
- The glitch alternates at ~1.2 s between (A) a recognisable scene with a fine
  checkerboard mesh and (B) a washed-out, vertically-smeared frame. The owner's
  "every second or so it tries a normal scene then goes back glitching" is this A/B
  cycle.
- **No magenta sentinel anywhere, including in the clear (A) frames.** Measured the
  sky region: software frame (t=10) blue 82% / magenta 0%; glitch "clear" frames
  (t=17.5, 26.5) blue 78-88% / magenta 0%. On the BENCH the GPU sky is always
  magenta (a huge region). Its total absence on device is decisive.

Conclusion: the backed-up window's EGL content is **not being presented on the
device**. What is on screen is the game's own software race (blue sky), and our
window only corrupts it -- a screen-door mesh plus a ~1.2 s fight. The game draws
via **CDirectScreenAccess (direct to the screen framebuffer, bypassing the window
server)**; the bench hid this because EKA2L1 does not abort/clip DSA under a
covering window (CLAUDE.md gap) and our NODSA switch suppressed the DSA entirely, so
our window had the screen to itself. On hardware the live DSA overwrites/fights our
window, and the EGL window-surface content never wins. So "does the N95 present a
backed-up EGL window surface?" is, in the presence of the live DSA, effectively NO.

The opaque-565 fix (v2) addresses the mesh's alpha but not this: it cannot make the
GPU content win against the DSA. v2 is still worth one run as a clean confirm (its
kept magenta sentinel: magenta on screen => content IS presented; blue mesh =>
confirmed not).

Implication: a separate window competing with the game's DSA is the wrong
architecture on device. The device-correct present is the **pbuffer + readback +
sentinel-masked blit into the game's OWN framebuffer** (composite_prev, the
pre-window path): the GPU 3D replaces the software fill in the buffer the game
already posts via DSA, so there is no second surface to fight. It needs no NODSA to
validate -- the bench shows the real composite through the game's DSA, matching the
device. Open cost: the per-frame glReadPixels (the readback the window path existed
to avoid) -- correctness first, then measure whether MBX readback meets the speed
goal.

## Blit path bench-validated as the device-correct present (no NODSA)

Built the pre-window pbuffer path (-DA4_PERSP, no A4_WINDOW/BACKUP) and benched RM-320
WITHOUT NODSA (blit1, sha f64d9f69...). Result: a CLEAN composite -- GPU 3D (road,
car, traffic, scenery, perspective-correct) with the software HUD preserved (WANTED,
lap, position, minimap, speedometer, nitro), no magenta, no mesh, no cone/wserv. The
GPU 3D replaces the software fill in the game's OWN framebuffer (composite_prev
blits non-sentinel pixels into A4_GL_BB), which the game posts via its own DSA -- so
it is visible on the bench with the normal DSA draw (no NODSA), exactly as the device
posts. GPU and software are always aligned because the blit writes in the game's
framebuffer coords (the bench happened to be in the 180 orientation; GPU followed it,
confirming alignment rather than a flip bug).

This is the device-correct architecture the round-1 review pointed to: no second
surface competing with the live DSA, so nothing for the DSA to overwrite. Tradeoff:
it reintroduces the per-frame glReadPixels the window path was built to avoid. The
software fillers it removes were ~81 instr/pixel (PATCHING.md), so it is plausibly a
net win, but only a device fps measurement decides. (Readback could later be halved
by reading 565 directly instead of RGBA+convert.)

Next: phone-round a4_blit.exe to (1) confirm the clean composite on real hardware
and (2) measure fps vs software -- the readback cost on the MBX is the open question.

## On-device telemetry/capture harness (A4_TEL) -- bench-validated, phone build out

To stop debugging from shaky videos, the build now writes files the owner retrieves
(same loop as the gate6 logs). a4_tel.c + an added efsrv import (a4_gl.s a4_efs_iat:
Connect 68, Replace 108, Write@pos 101, Flush 96; all <= the N95's 351 efsrv exports)
write a ring of 4 files C:\a4tel0..3.dat, each = [128B header: magic a4TL, geometry,
the glr counters] + [the game framebuffer A4_GL_BB, 320x240x2]. Ticked every frame
from a4_gl_frame so capture is continuous; the ring holds the last 4 moments.

Bench (RM-320, blit build): all efsrv calls return 0, 4 files materialise, headers
parse, and the framebuffers decode to the exact clean composite (GPU 3D + software
HUD) -- pixel-exact, from a file. (First attempt wrote files but misnamed by an
off-by-one in the ring digit; fixed. Captures first plateaued because the tick was
prim-gated in a4_gl_flush and bench prims stop after the ngilightserver EXCLFAULT
kill; moved to a4_gl_frame, now continuous.)

Phone build out: blit path + A4_TEL, A4_PERSP, TEL_EVERY=8, sha256
ccae0befc0a37428c7acd76c375f3628a5c09b41825443f005f2ce5e1fffd337. The owner plays a
race (into the glitch), retrieves C:\a4tel0..3.dat, sends them; a4tel_decode.py reads
the counters and decodes each framebuffer (tries the recorded 320 stride and 720).
This settles both open questions at once: the exact on-device composite (clean vs the
stride/stripe artifact PATCHING saw at 720) AND the counters (prims, bb, last_prims).

## Phone round 2 (telemetry): the checkerboard is FIXED; harness works on hardware

Owner ran the blit+A4_TEL build and sent C:\a4tel0..3.dat. The on-device telemetry
loop works end to end -- first pixel-exact data off the phone, no video.

Headers (all 4, the ring's last captures, capidx 146-149, fcount ~1200): magic a4TL,
**errc=0** (no GL error), bstep=6 (context current), **last_prims=283, ntex=106**
(GPU rendering a full scene), stride=320 and the framebuffer decodes CLEANLY at 320
(so the 720 stride PATCHING saw for the gauge does not apply to A4_GL_BB), bb=0x70C0E0.
winstep=0/wvis=0 as expected for the blit path (no window).

**The opaque-565 fix worked: no checkerboard/screen-door.** The composite is clean --
GPU 3D + software HUD + the "ARE YOU SURE?" quit dialog (the 4 files are the paused
quit screen, where the owner stopped). Scene renders (palms, buildings, mountains,
billboard all present); it is a dusk track, dim (mean brightness ~53/255), so the
distant tree line reads as dark silhouette.

Remaining per owner: "wobbly floor, black background trees, choppy, esp. drifting."
These are temporal/perf and invisible in paused frames. So v2 telemetry adds the
a4_fps_state block (fps = last-1s window count, min/max frame dt, the 128-frame delta
ring) to every file -- choppiness is then readable from one file even if its
framebuffer is paused. TEL_EVERY=30 so the 4-file ring spans ~10s and keeps active
race frames (the wobble needs an active frame to see). v2 bench-validated (ver=3, fps
block decodes). sha256 ef93e32d1f1be2f29265791ed2c0a68de58c6d886d82d8f1bc98f794bcd90f48.

Open question for the owner: is this track this dark in the STOCK game too? GPU uses
GL_REPLACE (no light modulation), which would make it brighter than software if
anything, so the darkness is likely the track's own dusk art, not a GPU bug -- but a
stock-vs-GPU look on the same track settles it.

## Correction: the "dark scene" was the pause dimming, not the track

Owner clarified the game dims the screen while paused. All 4 round-2 files were the
quit/pause screen, so the dimness and dark tree-line were the pause overlay, not dusk
art and not a GPU issue. The brightness question is closed. Only wobble + choppiness
remain, and both need active (unpaused) frames + the v2 fps/timing block.

## Phone round 3 (telemetry v2): fps signal in, but pictures still the pause menu

Owner sent 4 files; 3 fresh (capidx 10-12, the stale a4tel1 was a leftover v1 file).
All framebuffers were the PAUSE/quit menu again -- exiting goes through the menu,
which fills the 10s ring faster than it spans. But the fps block gave real signal:
errc=0, last_prims up to 485 (busy scene), and fps reads ~17 in the active window vs
85 on the paused menu (85 = trivial menu rendering; so the real race fps is ~17 --
low, matching "choppy"). The frame-delta ring shows uneven pacing (alternating
fast/slow frames, occasional spikes). bb=0x70C0E0, stride 320 clean.

So fps/choppiness is now measurable, but the WOBBLE still needs an active framebuffer.
v3 telemetry fixes the capture: gate on an active 3D scene (last_prims>100, skips
menu/boot) and STOP after the first TEL_RING captures (do not cycle), so early driving
is kept and the later exit-menu never overwrites it. Bench-validated: captures are
clean active race frames (daylight race, no menu), stops after the ring fills. sha256
d673c7644f24748ccfdc2bbd58c7828d9cc2062aa107386a9b62799c1213a33a.

## Phone round 4 (v3 gate): first ACTIVE driving frame -- diagnosed all 3 issues

v3's active-gate+freeze worked: a4tel3.dat is a real active driving capture (capidx 3,
bright 148, last_prims 554, fps 25; the other 3 slots were stale leftovers the user
didn't overwrite). The frame is a clean GPU composite (car, road, traffic, HUD,
daylight) -- so:
- CHECKERBOARD stays fixed, GPU healthy (errc=0, ntex 46, bstep 6).
- "Black background trees" = COLORKEY: tree/foliage sprites and the distant tree-line
  render with BLACK backgrounds (clear black blobs/jagged band vs the sky). The game's
  software treats texel 0 as transparent; the GPU drew it opaque. The old >=1/8 scan
  missed these (small transparent fraction), and lowering it would push opaque
  building textures to the slow software filler (worse choppy).
- "Choppy" = measured: fps ~17-25, and a regular frame-delta pattern 1 1 5 1 1 5
  (two fast frames then a ~5x stall), i.e. a periodic hitch. Likely the per-frame
  glReadPixels sync + sprites falling to software.
- "Wobbly floor" = not visible in a static frame; the road shows no gross affine
  distortion. May be temporal, or the choppy hitch during fast camera motion.

## Fix: GPU alpha-test colorkey (black trees) -- bench-validated, phone build out

Instead of a threshold, handle colorkey ON the GPU. tex_index now uploads any texture
with a zero texel as RGBA 5551 (alpha=0 on the zero texels, converted+strip-uploaded
through the readback scratch via glTexSubImage2D), and bring-up enables the alpha test
(glAlphaFuncx GREATER 0) to discard them -- matching the software "texel 0 =
transparent" exactly and keeping the sprite on the GPU (no software fallback, which
also helps choppy). Opaque 565 textures get alpha 1 (GL_REPLACE on RGB) and pass the
test unchanged. Two GL imports added, resolved via EKA2L1's dispatch table (matches
the ROM): glAlphaFuncx=28 (0x111B), glTexSubImage2D=128 (0x117F); cross-checked the
scheme (glTexImage2D 0x117C=125, glClearColorx 0x1120=33, glViewport 0x1183=132).

Bench: errc=0 (5551 + alpha test + glTexSubImage2D all accepted), scene renders,
colorkey sprites (a palm) show the sky through their gaps, not black. sha256
3f2e57f13a6fda717e7b9ff20a7513030668e1f227ead3ceaae73da2d597088f (blit + A4_PERSP +
A4_TEL, TEL_EVERY=30). Phone round confirms the trees + gives active frames for the
wobble and fps for the choppy.

## Phone round 6 (colorkey build 3f2e57f1): BLACK TREES FIXED on hardware

User re-downloaded/reinstalled the colorkey build, redid everything. 4 files; a4tel1
(capidx 1, fcount 60, ntex 43, last_prims 640, errc 0) and a4tel3 (capidx 3, fcount
120, ntex 46, last_prims 554, errc 0, bb 7391072) are fresh active driving frames from
THIS build -- NOT repeats of round-4/5: those were stale a4tel0/a4tel2 (capidx 10/12,
ntex 67). a4tel0/a4tel2 this round are again the stale leftovers.
- COLORKEY FIXED: zoomed the palm-tree row (a4tel3) and the right palm (a4tel1) -- the
  foliage now shows sky/buildings through its gaps, green leaves on a clean edge, NO
  black boxes. The GPU alpha-test colorkey holds on real MBX hardware. errc=0 both
  frames: the 5551 upload + glAlphaFuncx + glTexSubImage2D are all accepted on-device.
- CHECKERBOARD stays fixed; scenes are correct GPU composites (car, road, traffic, HUD,
  daylight), no screen-door.
- CHOPPY still present and now measured on the fixed build: fps 24-25 (a4_fps disp),
  frame-delta ring a hard periodic cadence `1 1 5` (and `1 2 9` spikes) -- two fast
  frames then a ~5-tick stall, every third frame. min_dt 0-1, max_dt 5. This is the
  per-frame glReadPixels GPU->CPU sync in the blit path: the game's DSA still owns the
  panel, so every GPU frame is read back into A4_GL_BB and the game posts it. The
  readback is the hitch. The window-surface present path (eglCreateWindowSurface +
  RBackedUpWindow + the winuser.cpp compositing fix, already built & bench-proven)
  removes the readback entirely -- that is the choppy fix, pending a phone round.
- WOBBLE: still not visible statically; both frames show straight road edges, no gross
  affine. Likely the choppy hitch under fast camera motion.

## Bench diagnosis D1: choppy+wobble = over-present; black band = software-kept region

Ran the colorkey build's renderer on the bench (RM-320) with three instruments, no phone
round. Results measured, not eyeballed.

CHOPPY + WOBBLE -- same root cause, MEASURED:
- 8 consecutive frames captured (a4_tel.c TEL_CYCLE + TEL_EVERY=1, ring of 8): the
  320x240x16 framebuffer is BYTE-IDENTICAL for runs of 2-3 frames, then jumps. Each
  group has its own last_prims (282 / 277 / 272), i.e. a distinct real render duplicated.
  The bb pointer is constant (0xD0E9C0, single buffer) -> not a stale-buffer capture
  artifact.
- Present-diag counters (a4_gl_frame: had>0 vs had==0, read via MEMSNAP of g9/g10):
  42 EMPTY presents vs 16 REAL at post 400 -> only 27.6% of presents carry new geometry.
  The game re-presents each world-state ~3-4x; new frames arrive at ~28% of the present
  rate.
- So CHOPPY = effective motion ~1/3-1/4 of the fps number; WOBBLE = the road leaps ~3
  frames of motion per update (big steps, read as "wobbly floor"). Both are the GAME
  producing new frames at ~28% of the present rate -- NOT our readback (duplicate
  presents issue no draw calls and no glReadPixels). This matches the phone's 1-1-5
  frame-delta cadence exactly. The present hook (a4_gl_frame, planted just before the
  game's present call at +0x1E6) runs on every present; 72% have no draws.

BLACK BAND -- location identified, H2 (software-kept), MEASURED:
- The short bench race (~58 active frames, a different track spot) shows no black-on-sky
  (16 zero px in the sky band) -- the phone band is the specific "TAKE HIM OUT" takedown
  scene the bench doesn't reach.
- Sentinel-vis build (A4_SENTINEL_VIS: paint GPU-didn't-draw pixels bright green): the
  GPU NEVER draws the sky / upper background -- that whole region is software-kept. The
  phone's black band sits at the horizon = exactly the software/GPU boundary. So it is
  H2 (a software-kept region left black), NOT H1 (a misclassified opaque GPU texture).
- Likely mechanism (inferred, needs the takedown scene to confirm): the colorkey fix
  makes distant hill/tree sprites' sky-parts transparent (alpha 0 -> discarded ->
  sentinel -> software-kept). Where software already holds sky there (most scenes, the
  palms) it shows through -> fixed. In the takedown scene the backdrop behind the hills
  is black (not pre-filled), so the now-transparent parts reveal black. Definitive H1/H2
  split for that exact scene needs the sentinel-vis build on one phone round.

Rule 4: over-present ratio + byte-identical duplicates are measured (counters,
array-equality); the black-band mechanism's "why black in that scene" is inferred.
Verified the #ifdef'd instruments are inert -- ship build sha stays 3f2e57f1 -- after
catching and fixing an unconditional actframes++ that had shifted it (+8 bytes).

## Bench diagnosis D2: choppy is NOT tick-gated (tick-variation test)

Swept the emulated guest clock on the present-diag build (a4_gl_frame real-vs-empty
counter), reading the real-present fraction at post 400. Default dyncom (wall clock)
gave 27.6% real. EKA2L1_CYCLETICK (instruction-based guest tick, feeds fast_counter /
ntick_count / tick_count):
  CYCLETICK=50  -> 33.3% real
  CYCLETICK=200 -> 33.3% real
  CYCLETICK=800 -> 34.7% real
A 16x change in tick rate moves the real fraction by ~1 point. The game produces new
geometry on ~1/3 of presents REGARDLESS of how fast guest-visible time advances, so the
3:1 present:render cadence is structural, not gated on User::TickCount / elapsed time.

Conclusion: choppy/wobble cannot be fixed by tick tuning (CYCLETICK/DETTICK). The game
re-presents each rendered world-state ~3x by its own loop structure (the present path at
+0x1E6 runs ~3x per draw_primitive batch), and that cadence is independent of the clock.
GPU offload speeds each render but cannot raise the game's new-frame rate. The phone's
1-1-5 dt cadence is the same 3:1 (two quick re-presents, one slow render).

Open question for the goal (smoothness): is our effective motion rate (~1/3 of present
fps) better or worse than STOCK software? Next cheap bench test: count real renders/sec
for the stock build vs the GPU build over the same race window -- that decides whether
GPU offload helped motion or merely raised the (duplicated) present count.

## Bench diagnosis D3: stock-vs-GPU present rate + the present architecture

Present rate over a 40s race window (RM-320, FRAMESTAT posts / wall-time):
  STOCK (a4_clean_final, software raster) : 11.2 presents/sec
  GPU   (a4_persp, blit+persp)            :  7.1 presents/sec
CAVEAT: the bench forces LIBGL_ALWAYS_SOFTWARE=1 (llvmpipe), so "GPU" here is SOFTWARE
GL -- slower than the game's own raster. This is a bench artifact; it does NOT predict the
phone, which has a real MBX. What it DOES establish: the rate tracks render cost (11.2 vs
7.1), so the loop is render-bound -- there is no hard frame cap limiting it on the bench.
So on the phone, if MBX is faster than the native software raster, present rate (and thus
motion, at the fixed ratio below) rises. Comparing GPU-vs-stock for real must be done on
the phone (stock-phone present rate not yet measured).

Present/render architecture (static RE of the baseline image, a4code.bin):
- The present is a wrapper at code-offset 0x1CE: DoPresent(this,arg1,arg2) sets
  this->0x6c=1, calls this->0x20->vtable[0x14] (the real present), clears the flag. The
  a4_gl_frame hook sits at its inner call 0x1E6.
- DoPresent has 17 call sites. It is REGION-BASED: callers pass arg1 = a region/mode id
  (e.g. 0x14 at 0x39888, 0x1e at 0x12DE6, and -1 for "full" which reads *(this+0x80)).
  So the game presents partial regions from many places, not one frame-loop present.
- The 3D world render (draw_primitive at 0x300B2, the a4_gl_draw_prim hook) runs on only
  ~28% of DoPresent calls (present-diag: 16 real vs 42 empty). Captured frames are
  byte-identical across the ~3 presents between renders, so the whole frame (3D + HUD) is
  frozen, not just the 3D.

So the "fixed new-frame rate" the owner wants raised is: the 3D scene is re-rendered on
~1 of every ~3-4 presents, and this is tick-independent (D2) and render-bound (D3), i.e.
a structural gate in the game's own update/render path -- not a timer and not our shim.
Finding that gate means tracing from draw_primitive (0x300B2) up to the conditional that
decides "render the scene this iteration or not", in a region-based present engine.
Next decisive measurement: capture the caller (lr at DoPresent entry) for real vs empty
presents -- same caller => a render/skip conditional inside one loop; different callers
=> the 3D render has its own cadence separate from the HUD/region presents.

## RE map toward the render gate (static), and the vtable wall

Traced the 3D render path statically in a4code.bin (baseline), bottom-up from the hook:
- draw_primitive  = function 0x300AE (hook at +0x300B2). (this, desc, flags); desc[0x30]
  is the texture addr a4_gl reads.
- Its 4 callers live in 3 mesh-draw funcs: 0x34798, 0x347EA, 0x35D3A.
- All three are called from ONE per-object render dispatcher, 0x31246, which switches on
  the object's flags (obj->4 bits) to pick the mesh path and writes obj->0x64 (a render
  mode) around the call.
- 0x31246 has ZERO direct BL callers: the scene traversal invokes it VIRTUALLY (node
  vtable). The static bl-climb ends here.

So the "render scene or not" gate sits above a virtual call and cannot be reached by
bl-climbing. It needs a dynamic trace: capture the DoPresent (0x1CE) caller's return
address (on the stack at the 0x1E6 hook) for real-render vs empty presents. Same caller
=> a render/skip conditional inside one race loop (patch that condition). Different
callers => the 3D render is driven on its own cadence (e.g. a redraw/idle active object)
separate from the HUD/region presents. That caller is statically analysable once named.

Risk to weigh before changing anything (rule 4): the ~1/3 cadence may be an intentional
"render 3D every Nth frame" optimisation the physics/timing assumes. If physics is
time-stepped (dt-based) raising the render rate is safe and just adds frames; if it is
per-render-step, raising it speeds the game up or destabilises it. Must confirm the
update/step is decoupled from the render before shipping a rate change.

## RE breakthrough: the race loop and its time-paced render gate (lr-capture)

Added an lr-capture instrument (a4_tramp.s stashes DoPresent's caller return addr at
0x400840 under -DA4_LRCAP; a4_gl.c histograms it split by whether 3D was drawn). Bench
run, MEMSNAP of the histogram: during a race, DoPresent (0x1CE) is called from exactly
THREE sites, each 15x over 15 cycles:
  0x950  present(arg1=-1,  arg2=1)  -> 0 real, 15 empty
  0x97E  present(arg1=0xa, arg2=0)  -> 0 real, 15 empty
  0x9A6  present(arg1=0xa, arg2=0)  -> 15 real, 0 empty   (the one after the scene render)
All three are in ONE function, the race loop at 0x808. So the 3:1 is literal: the race
loop presents three times per cycle and renders the 3D scene once (only 0x9A6 carries new
geometry). The guard at 0x954 (pause / game-state 0x21,0x22 / r4[0x1c] / presenting flag
r4[0x6c]) passes every racing frame, so it is not the gate.

The loop is TIME-PACED via euser imports -- a dedicated euser IAT veneer block at
0x50cf0..: HomeTime=ord 859 (confirmed, same slot a4_tramp.s uses), plus ords 10 and 11
and 413. The loop head reads a 64-bit value from the euser import at veneer 0x50cfc (def
ord 11) into [sp]/[sp+4] and [r4+0x7c] (0x838-0x840); 0x1916 (called right before the
real present at 0x994) does a time-delta compare: (t[0x2c] - r5[0x34]) >> 8 vs 0x7d<<5 =
4000, i.e. a frame-interval threshold.

KEY: this clock is euser imports called DIRECTLY, NOT the tick_count/ntick_count SVCs.
That is exactly why the CYCLETICK sweep (D2) was flat -- CYCLETICK only rewrites those
SVCs, not these euser calls. So the "fixed new-frame rate" is a time-delta gate in the
race loop driven by a euser clock import, with a ~4000-unit interval threshold (0x1916).

Rule 4 (def-vs-ROM trap): ord 674=TickCount and 859=HomeTime are confirmed (romimg +
a4_tramp.s measurement), so def and ROM agree at least up to 674; ords 10/11/413 are
below that so likely CHeartbeat::NewL/RunL and TSglQueIterBase per the def -- BUT ord 11
is called returning a 64-bit value used as time, which RunL would not, so the ROM
function at ord 11 (romimg: euser+0x803a6da5) must be disassembled to name it before any
patch. NOT yet resolved against the ROM by disassembly; do not trust the def name here.

Next: (a) disassemble the ord 10/11/413 ROM functions to name the clock; (b) decode the
0x1916 threshold and whether [r4+0x7c]/r5[0x34] dt also drives physics (dt-scaled => safe
to raise the render rate; frame-locked => raising it speeds the game up). Only then craft
the patch.

## RE conclusion: the engine is RENDER-BOUND, not rate-capped (the "fixed rate" isn't one)

Followed the chain to the end. ROM-confirmed names: euser ord 9=CHeartbeat::New,
10=CHeartbeat::NewL, 11=CHeartbeat::RunL, 12=CHeartbeat::Start, 413=TSglQueIterBase.
Correction to the earlier read: 0x1916 is a ~1-second TIMEOUT ((now-last)>>8 > 4000 clears
a flag), NOT the frame gate.

Structure:
- Frame driver 0x1afc creates the game object (alloc 0x9c, ctor 0x112) and calls the race
  function 0x808 ONCE. So 0x808 contains the internal race loop.
- The race loop renders the 3D scene EVERY iteration (present-diag: 0x9A6 is a real-render
  present on all 15/15 iterations; draws always land between the 0x97E and 0x9A6 presents).
- Each rendered frame is PRESENTED THREE TIMES per iteration (sites 0x950 full, 0x97E, and
  0x9A6). That is the whole "3:1": the two earlier presents re-post the PREVIOUS frame
  (bb not yet updated this iteration), the third posts the new one. The consecutive-frame
  capture saw each frame ~3x because a4_tel_tick fires per present (3x/iter), not because
  the game froze.
- CRITICAL: the loop contains NO wait/after/timer import (only CHeartbeat::RunL, pumped,
  and a list iterator). CHeartbeat::RunL does not block. So the loop runs FREE -- the
  iteration rate is RENDER-BOUND, set by how long each frame takes, with no fixed cap.
  This matches D3 (bench rate tracked render cost: stock 11.2 vs llvmpipe 7.1 presents/s).

So there is NO fixed new-frame-rate constant in the engine to raise -- the motion rate IS
the render rate; the fps number is just inflated 3x by redundant presents. The real and
only lever for smoother motion is making each frame cheaper to render. In the current blit
build that per-frame cost is dominated by glReadPixels (GPU->CPU), which is slow on MBX.
=> The window-surface present build (eglSwapBuffers, no readback -- already built and
bench-proven) is the confirmed fix: it removes the per-frame readback so iterations run
faster => higher render/motion rate. It also lets us drop the 2 redundant game presents
(our hook already skips the game's present under A4_WINDOW). No risky engine rate-patch is
needed, and the physics-coupling worry is moot (we are not changing any rate, just the
per-frame render cost).

## Window-surface build bench test: fast, but compositing gap (NOT yet shippable)

Built a4_winsurf.exe (-DA4_WINDOW -DA4_BACKUP -DA4_PERSP -DA4_TEL, sha 52faa8ba). Bench
(RM-320): framestat 897 over the same race where the blit build did ~464 -- ~2x the frame
rate, with the a4_fps ring now mostly 0/1-tick deltas and fps 37->87 (the hard 1-1-5
cadence gone), errc=0. So removing the per-frame glReadPixels roughly DOUBLES the rate even
on slow llvmpipe; on real MBX the win is larger. This confirms the readback was the
bottleneck (the whole point).

BUT it is not visually shippable as-is:
- The GPU window shows MAGENTA (the sentinel clear) through the sky and has NO HUD. The GPU
  only draws the 3D geometry; the game draws the sky and HUD in SOFTWARE into bb, and the
  window path (unlike the blit path's readback composite) never puts that software layer on
  the GPU surface. So the sky/HUD are lost.
- Some bench frames show the whole image vertically flipped / fighting the software frame.
  That is the documented bench gap (EKA2L1 does not abort the game's DSA under our window,
  CLAUDE.md), so the GPU window and the game's full-screen DSA post alternate. Bench noise;
  on the device the DSA aborts under the window. But it means the BENCH CANNOT fully
  validate the window present -- only the device can.

So the window path's real, device-relevant problem is compositing the software sky+HUD onto
the GPU surface WITHOUT the readback. The fix is upload-composite: upload bb (sky+HUD) as a
background texture (CPU->GPU upload is cheaper than the GPU->CPU readback on MBX), draw it,
then the 3D on top, then swap. Open issues: POT texture size (320x240 -> 512x256 + clamp)
and HUD-over-3D ordering (bb-as-background puts HUD under the 3D; the HUD sits at screen
edges so the overlap may be minor, or draw the HUD strip again on top). Real work, not a
bake. Did NOT ship a4_winsurf to the phone -- it would show a magenta sky.

## FIX: perspective distortion (the "wobble") -- inverted 1/w in A4_PERSP

The owner identified the real artifact (not temporal wobble): straight road markings (a
zebra crossing) stay straight far away but BOW into a smooth "U"/horseshoe as they near the
car -- worst at the bottom of the screen (most foreshortened). That is perspective-incorrect
texture mapping on the ground plane, and the smooth curve (not a triangle-edge "V") pointed
at A4_PERSP, not affine.

Root cause, MEASURED (A4_VTXDUMP: dump each triangle's (screen-y, vp[2]) via MEMSNAP):
vp[2] is RECIPROCAL depth (1/z ~= 1/w), not z/w. It is LARGER nearer the bottom of the
screen (nearer): mean vp2 2305 at y[0,60) (far) rising to 3941 at y[60,120) (near), and
11298->14602 across y 84->97 on buildings. A4_PERSP was INVERTING it (iw = 2^22/vp[2] ~ z),
so the GL perspective weight q came out proportional to w instead of 1/w -- inverted
perspective, which GL interpolates smoothly into the U-bow.

Fix (a4_gl.c, A4_PERSP path): use vp[2] DIRECTLY as q=1/w (iw = vp[2]), no inversion.
s=(un*iw)>>16, q=iw, so s/q = U_norm with correct perspective-correct interpolation.
a4_udiv is now unused (kept, marked unused). Bench: errc=0, scene renders clean, road
straight. The decisive before/after is the zebra crossing, which the short bench race does
not reach -- confirm on the phone.

This fix is in the texcoord path, so it applies to BOTH the blit and the window-surface
builds. The shippable build for confirming it is blit+persp(fixed)+colorkey+TEL
(a4_perspfix.exe) -- correct compositing (sky/HUD), still choppy (readback), but the
distortion gone. The window-surface choppy fix still needs upload-composite work.

## Phone round 7 (a4_perspfix ae183781): NOT fixed -- my 1/z change was wrong; popping found

Owner: distortion still there (zebra crossing still bows near the car); NEW "road pops out
and in"; choppiness still comes, later than before (before the tunnel, not after drifts).
Two telemetry sets (7a, 7b), 8 fresh captures, all errc=0. Not repeats of round 6 (round 6
was the colorkey build with the original A4_PERSP; these are the perspfix build).

1. MY ERROR (rules 2 and 4). The "vp[2] is 1/z" conclusion came from a vertex sample of
   sky and building triangles only (screen y <= 111), where screen-y does not track depth,
   and I did not check this file first: "Phase 2, step 1" had already MEASURED vp[2] on
   ROAD triangles (near y=241 ZW~202, far y=122 ZW~1009) -- ZW grows with distance, ZW IS
   w, and the original q = 2^22/ZW was correct. The perspfix build inverted a correct
   formula. Reverted.

2. Why the original still distorted on the phone. A4_PERSP feeds projective texcoords
   (s,t,0,q) and relies on the GL dividing s/q PER FRAGMENT. The bench only does that with
   the EKA2L1_TEXPROJ patch (without it: flat solids). On the phone the distortion looked
   the same with q = 1/w (round 6) and q = w (round 7), and the road is textured (not flat
   solids) -- so the MBX driver uses q but evidently divides it PER VERTEX, which collapses
   to plain affine mapping: the U-bow on the big near-road triangles. (Inference from two
   rounds; the fix below does not depend on it either way.)

3. POPPING = MAXT overflow, MY BUG, measured. MAXT=640 was a frame cap: past it, each
   triangle was drawn immediately (a4_gl_draw_prim overflow path) and then ERASED by the
   gl_clear at the start of a4_gl_flush, and the game had already skipped its software
   draw for it. last_prims pins at exactly 640 in 4 of 8 captures (7a#3, 7b#1-3), and every
   one of those frames has the near road MISSING with the game's city backdrop (blitted
   into bb before the 3D) showing through; frames under 640 have the road. Became frequent
   after the colorkey fix sent sprites/trees to the GPU instead of software (more GPU
   triangles per frame). The overflow tris were also the expensive path (4 GL calls each).

4. Choppy: 7a fps 27 -> 25 -> 24 -> 24 with the 1-1-5 / 1-1-6 cadence lengthening as the
   race moves into the city (ntex 27 -> 45, last_prims up to the cap); 7b reached fps 50-52
   with near-flat deltas at two captures, then 30 with 1-2-4. Consistent with render cost
   (scene complexity + wasted overflow draws + the per-frame readback).

Fix in progress (round 8): A4_HPERSP -- perspective through the VERTEX w: positions
(x*W, y*W, 0, W) GL_FIXED, W proportional to ZW (one shift per triangle so |x|*W < 2^26),
plain (U,V) texcoords; every 3D GPU interpolates perspective-correctly against clip w, so it
does not depend on q. And the overflow: clear at the frame's first primitive (after the
readback), draw the batch whenever MAXT fills, draw the rest at flush -- nothing erased, no
frame limit (MAXT is now a batch size: 512 under HPERSP for the 16-byte positions).

## Round 8 bench validation: HPERSP straight without TEXPROJ; overflow fixed; phone build

Test-the-test first: two runs of the same build do NOT land on the same frame (capture
moment varies with host timing; MAD 22 between them), so build-vs-build pixel diffs are
invalid. Used a frame-independent metric instead -- tools in the session scratchpad
(lanefit2.py): track the left edge of the pale-yellow centre line (e8e8c8) up the road
and fit x = a*y + b; a straight texture line on a flat road stays straight under correct
perspective and kinks at triangle edges under affine. Full-length tracks (89-99 rows,
y112-210):
  REF  (A4_PERSP + EKA2L1_TEXPROJ, known correct)  max resid 6.33 / 5.38 px, kink 0.045 / 0.163
  HPERSP (no TEXPROJ)                              max resid 5.40 px,         kink 0.083
  AFFINE                                           max resid 15.08 px,        kink 0.471 (slope flips)
The metric separates affine from correct (it passes its own test), and HPERSP matches
the reference WITHOUT the emulator's q patch -- the property the MBX needs. (Short
11-row tracks at y200-210 locked onto the pale speedometer; discarded.)

Overflow fix exercised with -DMAXT=64 (a ~400-triangle frame drawn in 7 mid-frame
batches): road complete, errc=0. HPERSP's 4-component GL_FIXED positions: errc=0 in every
run.

Ruled out on the way (A4_TEXCHK, cache validation): 5408 cache hits, 0 pixel-pointer
changes, 0 content changes -- the descriptor-keyed texture cache is not serving stale
textures. The race-start road dropout seen on the bench (old and new builds alike) is the
one-frame START_AFTER bring-up transition: no GPU composite that frame (no sentinel green
anywhere), triangles after the switch only on the pbuffer until the next frame. One frame,
once; not the phone's recurring popping (that was the 640 overflow).

Telemetry: the capture gate is now "this present drew >=100 3D primitives" (had), not
last_prims (which stays high through the pause menu). So captures are distinct real
renders (not the 3x re-presents) and a cycling ring (TEL_CYCLE) keeps the LAST race
captures before a pause. Verified: TEL_EVERY=1 run, 18 captures, files hold capidx 10-17
in slot capidx%8, consecutive captures 45-56% of pixels different, road correct.

Phone build r8_phone.exe = -DA4_HPERSP -DA4_TEL -DTEL_EVERY=40 -DTEL_RING=8 -DTEL_CYCLE
(blit present, colorkey). sha256 ac5d37ac6fd77b1738bf4a5217da976961eaa849302cb0e0d67dfcd1a0169b7e. Bench: GL up, frames 69, errc 0, ntex 24, only the
known boot fault (read 0x34, ngilightserver, EXCLFAULT) present in every build. Telemetry:
8 files C:\a4tel0..7.dat, one capture per 40 real renders (~3-5 s on the phone), ring =
roughly the last half-minute of racing before the player pauses.

Rule 4 -- guesses, named: (1) that the MBX divides q per vertex (inferred from rounds 6/7;
HPERSP does not depend on it either way); (2) that the MBX driver interpolates against the
clip w of 4-component GL_FIXED positions -- standard GL ES 1.1, bench-proven, unproven on
this device; (3) the 2^26 position headroom is a guess at a fixed-point vertex pipeline.
States the bench never entered: frames over 512 triangles (only via the forced MAXT=64);
far-off-screen vertices (large |x| shrinks W precision); the pause/resume path for the
telemetry ring (had==0 in a pause is inferred from the present-diag, not run).

## Phone round 8 (r8_phone ac5d37ac): DISTORTION FIXED; popping gone; new reports = the software mode

Owner: the zebra-crossing distortion is gone (A4_HPERSP works on the MBX -- confirms the
vertex-w approach; the q-per-vertex inference stands). Remaining: lamp posts / traffic
lights misplaced or wrongly occluded (photos 1, 2, 6); scenery that vanishes and reappears
as you approach (photos 4, 5); a blue strip across the top in the tunnel, present for
several builds (photo 6); choppiness, captured by driving past the tunnel.
Telemetry: 13 captures, ring held capidx 5-12 (the cycling ring and the had-gate work on
the device). Not repeats of round 7: new build (HPERSP + overflow fix), new gate.
- capidx 8 has 758 triangles (> the old 640 cap, > the 512 batch): road complete -- the
  batch-flush path works on hardware; no overflow popping in any capture.
- CHOPPY = capidx 11, NOT the tunnel: a crash/takedown ("URBAN HAVOC", a car airborne, a
  red/white barrier filling the right half, player at 53 km/h). fps 15 and the ring before
  it shows the real-render frame at 17-21 ticks instead of 4-5 (1 1 18 / 1 1 20 / ...),
  ~3 game frames/s sustained over ~12 s. The tunnel entrance right after (capidx 12, 176
  triangles) is back to normal (1 1 5). capidx 10 (fps 48, many 0-tick deltas) is a
  low-render stretch. Normal sections: fps 27-30, 1-1-4/1-1-5 (readback-bound).
- BLUE STRIP = CLIP_TOP, measured: tunnel frame rows 0-4 are sky blue (60a0d0, 489cc8),
  row 24 is the tunnel ceiling (303830). The composite deliberately skips rows < 11 (they
  keep the software sky backdrop), so it only shows where the scene there is not sky.

Explanation for the lamp posts and the vanishing scenery is in this file already ("Phase
2, step 2 attempt"): ~116k of ~167k draw_primitive calls per race are flags 0x4000, a
DIFFERENT rasterizer than the flags-1 perspective path the GPU takes, so ~70% of the 3D is
still SOFTWARE. Software triangles land on top of a GPU image composited from the PREVIOUS
frame (one game frame = ~9 m at 270 km/h and ~8 renders/s): misplaced, wrong occlusion,
and a software object drawn before the frame's first GPU triangle is painted over by the
composite -> vanishes, depending on draw order.

draw_primitive (0x300AE) dispatch, read from the image -- a priority chain on flags:
0x2 -> 0x2fbdc (passes flags&4, flags&8); 0x1 -> 0x2f6fc (persp textured, GPU);
0x8 -> 0x2f358; 0x10 -> 0x2efb4; 0x20 -> 0x2ecb4; 0x40 -> 0x2e910; 0x100 -> 0x2e56c;
otherwise (0x4000) -> 0x2e1c8. NB bit 1 is tested BEFORE bit 0, and the hook takes any
bit-0 flags. Span fillers: 0x2f6fc -> 0x333f4 (opaque textured loop 0x3357A);
0x2e1c8 -> 0x33964, the TRANSPARENT span (loop 0x33B16 = a4_transp_tramp): texel 0 leaves
the pixel, else writes it -- a pure COLOUR KEY, affine; second loop 0x33AD6 = the same with
a per-channel saturating ADD of a constant before the write (no blending with the
background). The span switches on ctx[0x64], the per-object mode 0x31246 sets from object
flags: mode 3 -> add ctx[0x70]; modes 1/2 -> table-driven variants (ctx[0x60]/ctx[0x5c]);
otherwise plain colour key. So 0x4000 is drawable on the GPU with the existing 5551 colour
key + alpha test, and the add with GL_ADD texenv + the constant as the vertex colour (GL ES
GL_ADD saturates). The earlier "flat fill" attempt drew it opaque, before the colour key.

## Bench correction: "the bench race is short" was an EMULATOR HANG (fixed)

Every GPU build on the bench plateaued around ~450-470 posts regardless of how long
accelerate was held (45 s vs 60 s gave 468 vs 464), with only ~18 real renders in a
race. I logged that as "the bench race is short". WRONG: it was a hang. A4_TAKEDEF hit
it a little sooner and froze at the countdown, which exposed it: posts frozen at 370-395
with the process alive; PCPROF never reached 3.2G instructions (so the guest was NOT
spinning -- it was stuck in a host call); gdb showed the guest thread in
graphics_buffer_pusher::flush <- flush_to_driver <- gl_read_pixels_emu (our per-strip
readback) and the graphics thread idle. Cause: uint8_t current_buffer_ / loop index in
EKA2L1's GL transfer-buffer pusher, reset only on eglSwapBuffers, which the blit path
never calls -- ~14 readback flushes a frame wrap it after ~18 frames. Fixed in EKA2L1
(EMU_PATCHES.md "The GL transfer-buffer pusher past 255 buffers"); the same run now
climbs 643 -> 2198 posts in 120 s and reaches lap 2 through the tunnel.
Consequences: (1) every bench measurement in this file taken on a GPU build saw at most
the first ~18 GPU frames of a race -- the present-diag 16 real/42 empty, the CYCLETICK
sweep, the rate comparisons were all early-race samples (still valid as samples, not as
whole races); (2) the phone was never affected (no EKA2L1 there) -- the owner's full
races were real. Earlier ring captures on the bench that "froze" were this hang too.

## Round 9 bench validation: the default rasterizer on the GPU (A4_TAKEDEF); phone build

Answer to round 8's lamp posts / vanishing scenery: put the 0x4000 family on the GPU in
draw order with the flags-1 path, so the software layer no longer lands on a one-frame-old
GPU image for those objects.
- A4_TAKEDEF: the hook also takes calls that would reach the DEFAULT rasterizer 0x2e1c8
  (bit 0 clear and none of 0x2/0x8/0x10/0x20/0x40/0x100 -- `(flags & 0x17B) == 0`), drawn
  with the existing 5551 colour key + alpha test (the 0x33964 span is a pure colour key).
- Census (A4_FLAGHIST, one bench race, 187,282 calls, (flags, ctx[0x64])): persp 0x2f6fc
  29% (0x4001 24.0%), DEFAULT 47% (0x4000 m0 25.1%, 0x104000 m1 14.6%, 0x20000 6.2%,
  0x10000 1.0%), 0x20 shadows 0x2ecb4 22.4%, 0x2fbdc 1.6%, 0x2f358 0.2%. GPU share of
  draw_primitive calls: 29% -> 76%. Mode 3 (constant add) never appears in a race.
- Bench full race (possible now, after the pusher fix; same input): guest instructions per
  post, stock 9.36M, GPU base (HPERSP) 6.10M, TAKEDEF 4.47M (-27% vs base). Draw calls per
  render (A4_RUNDIAG): base 172 (448 tris, 2.6 tris/call, max 411), TAKEDEF 291 (987 tris,
  3.4 tris/call, max 539 -- under MAXT 512, so the max is not the batch size).
- tex_index tries the previous hit's slot first (b6fd2fa; TAKEDEF doubles its calls).
- Blue strip: CLIP_TOP now defaults to 11 and the phone build passes -DCLIP_TOP=0 (the
  composite covers the top rows). Measured on the tunnel captures (bytes, not the picture):
  r9 bench capidx 42/43 rows 0-4 are 623d10..7d490b / 6a5c4b..61523d with 0-1 sky-blue
  pixels of 320; round 8's phone tunnel (capidx 12) had 276-284 of 320 there.

Known loss (a guess at its visibility): mode 1 (0x104000, 14.6%) adds 1/4 of a per-scanline
table colour indexed through a second map -- a dependent lookup GL ES 1.1 cannot express --
so it is drawn as a plain colour key: those objects lose that tint.
Still software (one frame against the GPU image): shadows 0x20 (22%; dst*12/16 through a
byte-map stencil -- needs blend + stencil), 0x2fbdc, 0x2f358.

Battery on the exact phone binary (r9_phone.exe = -DA4_HPERSP -DA4_TAKEDEF -DCLIP_TOP=0
-DA4_TEL -DTEL_EVERY=40 -DTEL_RING=8 -DTEL_CYCLE, --gl --capture; sha256
27e232b96bf3c541c48d012b29661eb095fa912149a0de4ce7c4f65e841426ce): full race through lap 2
and the tunnel, framestat=2540, faults=1 (the known ngilightserver boot fault only), 44
captures, ring = capidx 36-43, errc 0 in all eight, GL census 293-303 calls/render,
987-1002 triangles/render, max 512 calls, last-render calls 116-457. Lamp posts, signs,
sprites and road correct in all eight (by eye -- the occlusion claim is a picture until the
phone's captures say otherwise). Header words 27-31 now carry the census, so the phone
round measures the MBX's cost per draw call against the fps block.

Not a repeat of: round 7 (1/z build), round 8 (r8_phone, HPERSP only, flags 1 only, CLIP_TOP
11). New: TAKEDEF, CLIP_TOP 0, census in the header.
Rule 4 -- guesses, named: (1) the MBX driver's per-glDrawArrays cost (291 calls/render vs
172; unmeasured -- the census is in this round's captures to measure it); (2) alpha test on
for every draw costs something on PowerVR (unmeasured); (3) the crash-scene choppiness
(round 8 capidx 11, 17-21 ticks a render) -- cause unknown; more GPU work may help or not.
States the bench never entered: the MBX itself; a long session past one race.

## Phone round 9 (r9_phone 27e232b9): lamp posts not reported; the software classes left

Owner: (1) in the countdown, for an instant, 2/3 of the scenery gone (photo 1); (2) photos
2-4: scenery drawn as flat grey / khaki / dark slabs over trees and markers ("loading
incorrectly, or low poly/texture"); (3) tunnel: light bulbs showing through the wall that
should hide them, and a patch of road ahead seen through the wall (photos 5, 6); (4)
choppiness as usual. Round 8's lamp posts were not reported again.
Telemetry (8 files, ring capidx 14-21; not repeats of round 8 -- new build, TAKEDEF):
errc 0, ntex 144-147. GL census ON THE PHONE: ~300 draw calls/render, ~1030 triangles/
render, max 566 calls. Real-render rhythm 2-5-0 / 2-6-0 ticks per game iteration (7-8
ticks, 109-125 ms) against round 8's normal 1-1-4 / 1-1-5 (6-7): NOT faster on the phone,
although the same change cut bench guest instructions per post 27%. So the phone frame is
not bound by the software rasterizer now; the GPU side is the suspect (per-call driver
cost, the driver's vertex work, the readback, and the tile renderer rendering only when
the readback asks -- the blit path never calls glFlush) -- all unmeasured. Round 10 times
them.

Causes, from the image (draw_primitive's span fillers disassembled):
- (1) the first GPU frame (START_AFTER) has no previous GPU image to composite, so every
  GPU-taken triangle is missing for that frame -- 76% of the scene since TAKEDEF (round 8
  logged the same frame as a road dropout and called it benign).
- (2)(3) the classes still drawn in software, which land AFTER the composite and so on top
  of every GPU object whatever the game's draw order:
  - 0x2fbdc (flags 0x2) is UNTEXTURED: desc+0x30 is an RGB565 colour (a zero word draws
    nothing); flags 4/8 pick 0x33b86 (50% blend, mask 0x0eee) / 0x33c44 (stipple), else
    0x33b46 (solid). A race uses only the solid one: 0x20002, 1.6% of calls -- the slabs.
  - 0x2f358 (flags 0x8) -> 0x33d18: affine textured, dst + texel per channel, saturating
    (0x10008, 0.2%: glows).
  - 0x2ecb4 (flags 0x20) -> 0x33f8c: shadows -- dst*12/16 where a screen-sized byte map
    differs from a stamp, then the stamp is written (so overlaps darken once). 22%.
  The bench shows it too: the r9 bench tunnel capture (capidx 42) has yellow dots on the
  left wall.
- Found on the way: IAT slot [32], "glDisable", held ordinal 36 = glClearStencil (the
  alphabetical libgles_cm def; ROM-matching per this file's ordinal note). Only the
  unshipped A4_WINDOW fps bar used it. Corrected to 52 in round 10.

## Round 10 bench: every race class on the GPU, the game's own 1/z buffer, phase timers

Built on round 9 (r9_phone). Not repeats of: rounds 7-9 builds (no flat/additive/shadow
class, no depth, no timers).

KINDVIS (flat green, additive cyan, shadows red, all opaque; bench, full race, lap 2 to
the tunnel; the first attempt sat at the start line -- xdotool "XGetInputFocus" warning,
no key reached the game; rerun with a fresh Xvfb):
- flat (0x20002) = distant hill / ground LOD slabs at the horizon -- the phone's photo 3
  spot (a hillside right of the road in The Hills);
- additive (0x10008) = the speed streaks and sparks around the player's car, over the GPU
  road;
- shadows (0x20) = the cars' shadows; with GPU draw order they sit UNDER the car (the car
  covers them), so the game draws shadows before the car;
- the tunnel's wall lamps are none of these: ordinary textured triangles.

THE FINDING: the opaque perspective span (0x333f4, flags 1, 29% of calls) depth-tests.
Its non-mode-4 path (0x337a4) per pixel: `if (zbuf[p] > z) skip; else if (texel) {
zbuf[p] = z; write }`, z the span's interpolated 1/w (it indexes the reciprocal table),
zbuf = *(ctx+0x40100+0x18), 32 bits a pixel. 0x31c70 (begin-frame: toggles the shadow
stamp byte, bumps a frame counter) calls 0x2def4, which zero-fills it every frame. Mode
4's lower rows (the loop at 0x3357A we rewrote as a4_span_textured) skip the depth test:
they add per-pixel LCG noise and clear the shadow coverage byte. The default span
(0x33964), flat, additive and shadow spans neither test nor write it. So the stock game
is NOT painter's order for the perspective class -- the GPU's arrival order lets a
farther perspective triangle drawn later overwrite a nearer one: walls that do not hide
what is behind them (the tunnel lamps, the road ahead, round 8's lamp posts).
Stage 5's depth build "lost the road on the phone" with a fixed config 6 whose depth was
never checked and a z linear in ZW; the cause was never diagnosed.

A4_ZBUF reproduces the game's rule: flags-1 triangles only, depth = min(128/ZW, 1) per
vertex (z = -ZK*2^shift over w = ZW*2^shift through the ortho; the clamp keeps every
vertex inside the depth range, so nothing is ever clipped), GL_GEQUAL, depth cleared to 0
at each frame's first triangle, all other classes drawn with the depth test off (so they
neither test nor write, like their spans). pbuffer from eglChooseConfig(PBUFFER, 565,
DEPTH>=16), falling back to config 6; glGetIntegerv(GL_DEPTH_BITS) decides, 0 -> the old
untested drawing. Bench: config 2, 24 depth bits, errc 0.

Also in the build: A4_TAKEFLAT (solid flat only; the 50%/stipple variants stay software
-- a blend would mix with the sentinel), A4_TAKEADD (GL_ONE/GL_ONE), A4_TAKESHADOW
(GL_ZERO/GL_SRC_ALPHA at 12/16; the span's once-per-pixel stamp is not reproduced, so
overlapping shadow triangles darken twice), the first GPU frame drawn by software as well
(dual), and per-window A/B of glFlush at the frame end x RGB565 readback (OES_read_format
probe; the emulator now answers it, EMU_PATCHES.md), with User::NTickCount phase timers
(fast exec 0x10, RM-320 euser ordinal 2103 read off the ROM; 584 = FastCounter is the
constant one on the N95, DEVICES.md).

Sentinel-leak check (tools/leak.py in the scratchpad: pixels a blended GPU triangle over
the magenta clear would leave -- 0.75*magenta, or magenta+glow): 0 in all eight captures
of the depth build, 0 in the stock reference and earlier builds. Test-the-test: the stock
count of the first draft of the additive predicate was ~1000 a capture (it matched white);
narrowed to pinks (G <= 40), stock reads 0.

Bench timers (ms, 1 ntick = 1 ms on the bench: 3419 nticks over 219 TickCount ticks),
per render, composite (read): noflush RGBA 15.4-18.3 (7.6-8.8); flush RGBA 12.1-13.3
(3.4-4.1); noflush 565 12.4-14.8 (7.4-9.8); flush 565 7.8-8.9 (3.4-4.1). Total per render
~85 in every mode (llvmpipe renders on host threads, so the bench cannot show the phone's
gain -- the phone round measures it).

Stock reference (A4_SWONLY: GL up, nothing drawn on the GPU; captures = the game's own
picture): the tunnel's left wall is clean -- none of the small floating dots the GPU
builds show there.
New defect seen in the depth build: the speed-camera signs over the tunnel mouth are black
boxes (stock: "80", "km/h"; also black in KINDVIS run 2). The spans read texels from
desc+0x10, the uploader from desc+0x0C; or the signs are textures the game redraws (a speed
display) that the cache uploads once. Probed next (A4_TEXPROBE).

Probe result (A4_TEXPROBE, one bench race through the tunnel): 144 uploads, desc+0x0C ==
desc+0x10 in all, row shift == width in all, no texel pointer moved and no 64-sample
content change after upload. Neither texture theory. The signs are MODE 1 of the default
span (the 0x104000 class, 14.6% of calls), read from 0x339e4: per texel T = tex[i] (0
transparent), M = map2[i] (ctx[0x5c], the texture's layout); if M the texel is
T + table[M]/4 (ctx[0x60]; quarter = ((C&0xF7DE)>>1 & 0xF7DE)>>1), summed with the carry
trick at 0x33a3c -- texture space only. Black base + table-lit segments = a black box when
drawn as the plain key. Fix (round 10): the mode-1 texture is baked (m1_texel, keyed
desc|1), and once a present a hash of the table entries it uses ([lo, hi] of its M values,
or 64 sampled texels when that range is over 1024) re-bakes it on a change. Bench: the
gantry shows the "80" limit signs and the "74 km/h" speed displays; the traffic light's
lamp lit; ~1 re-bake a render (a table that animates every frame); errc 0, leaks 0.
The phone build records the re-baked texels and their time per window (header words 55,
56) -- the cost on the phone is unmeasured.

Same-frame instrument A4_DUALCAP (bench only): software draws every triangle (the stock
picture) and the GPU draws them too; each capture carries both, the GPU image read back
after the framebuffer. tools/dualcmp.py: per-pixel differences are dominated by sampling
offsets and mode 4's road grain (not reproduced), so the metric is 8x8 blocks fully drawn
by the GPU whose mean differs by more than 1/8 in a channel. First attempt's GPU image had
lines every 16 rows -- the dump wrote its 565 row into the RGBA strip it was converting
(fixed: in place). Two attempts with the depth and no-depth builds sat at the start line
(the dual frame is slow, the key went down before the race and was not seen again):
telerun3.sh re-presses accelerate every 10 s.

Same-frame comparison (A4_DUALCAP, depth build and no-depth build, telerun3): the
object-sized differences are (a) 2D effects the game draws over the 3D (the turn chevrons,
"URBAN HAVOC", the takedown letterbox -- expected), (b) the car's shadow darker on the GPU
(overlapping shadow triangles darken twice: the stamp map is not reproduced), (c) the
roadside chevron BOARDS present in software and absent from the GPU image -- not taken by
any class. The depth/no-depth runs did not land on comparable scenes (different laps), so
they do not yet measure the depth change itself.

CORRECTION, census: the round-9 census (187,282 calls) was taken before the pusher fix, so
it covered only the first ~18 GPU frames of a race. A full race through lap 3 (r10_fh,
3,170,159 calls, census now appended to the captures): persp 0x4001 29.2%, shadows 0x20
24.0%, default mode 1 0x104000 17.7%, 0x4000 12.6%, 0x20000 6.0%, flat 0x20002 2.2%,
persp mode 4 (0x8020001/0x8021001) 3.2%, additive 0x10008 1.3% (+0x8 0.1%), and a class
the short census missed: 0x14100 (1.1%) + 0x411100 -> 0x2e56c, span 0x33ea8:
dst + (T - dst) * a/16, a the top nibble of a byte alpha map at desc+0x2c, no colour key --
the chevron boards. A4_TAKEALPHA: RGBA8888 with alpha (a>>4)*16, GL_SRC_ALPHA /
GL_ONE_MINUS_SRC_ALPHA (leak-checked: a part-transparent texel over the sentinel would mix
with magenta).
Shadows once a pixel: the pbuffer config asks depth + stencil (then depth alone, then
config 6); with stencil bits, shadow runs pass where stencil == 0 and INCR it, cleared with
the frame -- the span's stamp map. Telemetry header word 57 = stencil bits.

The alpha class never reached the GPU in the first try: A4_ALPHAHIST counted 0 uploads.
Its descriptor (probed): +0x0C = 0, +0x10 = the texels, +0x2C = the alpha map, 64x64.
Every span reads texels from +0x10; the uploader read +0x0C (equal on all 144 textures the
other classes use -- A4_TEXPROBE), so tex_index refused it (no pixels) and software drew
it. All texel reads now use +0x10 (TS_PIX). Its alpha nibbles: 24% zero, 32.5% full, 43%
in between -- genuinely soft, so blended, not cut out.
Additive OFF again (A4_TAKEADD dropped from the phone build): with it the leak check fired
for the first time -- 102 and 76 pixels, the police helicopter's rotor against the sky, all
R=31 B=31 G=2..24, i.e. magenta + a glow (an alpha blend would not saturate R and B). The
sentinel cannot be blended over; round 9 drew additive in software without complaint, and
KINDVIS put it over the road (streaks) and the sky (rotor), where on top is right.
Mode-1 cost: the texture re-baked every render is the PLAYER'S CAR body (A4_M1PROBE /
A4_M1VIS: 01f46780, 256x256, M 4189-30236, 30-64 of the first 64 table entries changing a
frame -- its paint reflection), 8-18 ms a render on the bench. A mode-1 texture of 16K
texels or more now re-bakes at most every 8 presents (M1_BIG, M1_BIG_EVERY): 0.9-7 ms a
render on the bench. The table's address is out of the signature (contents decide).

Battery on the exact phone binary (r10_phone = -DA4_HPERSP -DA4_TAKEDEF -DA4_TAKEFLAT
-DA4_TAKESHADOW -DA4_TAKEALPHA -DA4_ZBUF -DCLIP_TOP=0 -DA4_TEL -DTEL_EVERY=40 -DTEL_RING=8
-DTEL_CYCLE -DA4_ABCYCLE, --gl --capture; sha256
8bc3b0c28369bcd3993a4ba2d56b38ff2da007674dab980ba5d23a11d0b7c7f9; telerun3 160 s, lap 2
through the tunnel): framestat 2939, faults 1 (the known boot fault only), errc 0 in all
eight, pbuffer config 2 with 24 depth and 8 stencil bits, dual_frames 1, leak.py 0 in all
eight, ~360 draw calls and ~1410 triangles a render (shadows and the new kinds), mode-1
re-bakes 2-19 a window. Pictures (by eye): the gantry's "60" / "74 km/h", chevron boards,
shadows under the cars, the car's paint, tunnel walls clean.

Rule 4 -- guesses, named: (1) the N95 offers a depth (and stencil) pbuffer config -- if not,
zbits/sbits read 0 and it draws as round 9 did, in the header; (2) the MBX honours GL_GEQUAL
on 16+ bit depth with z = min(128/ZW, 1) -- bench-proven only; (3) the alpha class blended
over pixels the GPU did not draw (sky) mixes with magenta -- leak.py cannot see a partial
blend (it keys on additive's saturated R and B); (4) the car re-bake cost on the phone
(header words 55-56 measure it); (5) glFlush / 565 effect on the phone (the A/B measures
it); (6) shadows: the stencil once-a-pixel matches the span's stamp only where the stamp
toggles per frame (0x31c70), mode 4's coverage clear not reproduced.
States the bench never entered: the MBX; a phone without a stencil config; the takedown
camera cuts with depth (one frame of stale composite on a cut, inherent to the pipeline).

## Phone round 10 (r10_phone 8bc3b0c2): the first phone timing; three visual reports

Owner: (1) nitro and money pickups appear displaced at first and "pop" toward their right
place as you approach (photos 1-3: a pickup left of the road whose place is the right
lane); (2) tunnel lights still visible where the wall should hide them (photo 4, left
wall); (3) one frame after the tunnel with the scenery gone -- backdrop, road, cones and
car only, and no HUD (photo 5). Round 9's slabs, countdown flash and black signs not
reported again.
Telemetry (8 files, ring capidx 43-50, race end at 50; not repeats of round 9 -- new
build, header v4): errc 0 in all; ntex 151; dual_frames 1; leak.py 0 in all.
- pbuffer: config 1, 24 depth bits, 0 STENCIL bits (no stencil config on the N95: shadows
  darken overlaps twice there).
- OES_read_format on the N95: GL_BGRA (0x80E1) / GL_UNSIGNED_BYTE -- not 565, so AB_565
  did nothing on the phone (r565 false); the pairs of windows differ only by glFlush.
- NTickCount = 1 ms on the N95 (184,537,135 nticks against 11,810,372 TickCount ticks =
  15.625 per tick, exactly the 64 Hz tick).
- PER RENDER, phone (ms): total 88-116; composite 22.5-25.2, of which glReadPixels
  17.3-19.3 (the rest, ~5-6, our sentinel blit); batch draw (all glDrawArrays and their
  state, ~375 calls, ~1300 triangles) 9.1-14.5; glFlush ~0. Flush on vs off: read 18.2-
  19.3 vs 17.3-18.3 -- NO gain: the read is not waiting on the tile render, it is the copy
  itself, ~76,800 pixels in ~18 ms = ~78 cycles a pixel at 332 MHz (a per-pixel convert
  from the surface's BGRA to the RGBA asked for, and/or uncached reads -- unknown which).
  Mode-1 re-bakes 0-0.9M texels a window at 148-410 ms a window = 3.7-10 ms a render
  while the car's reflection table changes (the M1_BIG limit holds it to every 8th
  present).
- So GL costs ~35-40 ms of a ~90-115 ms render on the phone: readback ~18, our blit ~6,
  submission ~10-14, car re-bake 0-10. The rest (~55-75 ms) is the game, our hook's
  per-triangle work (~1300 triangles) and the software left (HUD, backdrop, additive).
- Real-render rhythm in the fps ring: 1-1-4 / 2-5-0 / 1-1-6 ticks -- 6-9 ticks a game
  iteration, the same band as rounds 8-9.

## Round 11 bench: no frame of lag (composite at the HUD), the affine fallback, per-table mode 1

Not repeats of: round 10 (pipelined composite, all flags-1 depth-tested, one mode-1 image
a texture, additive in software, RGBA read).

Pickups (photos 1-3) could not be put on the bench: the bench save races the Mini in an
event with none (dense dual captures through lap 1 and lap 2, The Hills included, show
none). What the photos and the draw order (MBX.md: all 3D triangles, then ~17 HUD
sprite-painter calls) do establish: everything the game draws in software -- the
position tags, the additive sparks and glows, anything outside draw_primitive -- landed on
a GPU image ONE FRAME OLD (the pipelined composite), ~9 m of travel at 330 km/h, so near
objects drawn in software part from the road the GPU drew. The dual captures (same frame,
no lag) show the GPU's own objects in their places (chevrons, cars, signs).
- A4_IMM: the frame's own GPU image is composited at the end of its 3D pass -- the HUD
  painter's first call (new capture hook at +2973A, a4_hud_tramp; displaced ldr r4,
  [sp,#0x70] / movs r7,r0, checked by the injector before patching) -- else at the
  present (a frame with no HUD). Triangles after it go to software (late_tris counts
  them: should be ~0 if the painter never runs mid-3D).
- Blended classes (additive, alpha) drawn by GPU AND software. Dst alpha as the drawn-mask:
  the clear leaves alpha 0, opaque classes write 1, blended classes write no alpha
  (glColorMask, ordinal 40); the composite takes only pixels with alpha set, so a blend
  over the backdrop is software's. Without alpha bits: the sentinel, plus "sentinel with a
  glow" (R, B 255, G short of full). Additive back ON (round 10's rotor leak cannot happen
  with the mask). pbuffer config asks depth+alpha+stencil, then depth+alpha, then depth.
- The perspective rasterizer's own affine fallback, read from 0x2f6fc: top vertex (0x2ff60
  order) ZW > 500 and its reciprocal-table spread (RT[ZW] at ctx+0x8C, 0x522c8) squared
  under 100 -> 0x2e1c8, NO depth test or write. Reproduced (persp_fallback): those
  triangles draw untested (KIND_TEX, mode 1 if the object's). Bench: ~30% of flags-1
  triangles (affine_tris 627k -> 746k over 8 windows).
- Mode-1 entries keyed by texture AND table (ctx[0x60]): the tunnel-exit gantry's signs
  were black boxes in a same-frame dual capture (stock: lit faces) -- sign objects share a
  texture with their own tables, and the first drawn each present decided the image for
  all. Re-bakes fell from ~800 to ~19 over a race (round 10's were that ping-pong); ntex
  steady at 147.
- Read A/B: AB_NATIVE reads the implementation's pair -- BGRA/UNSIGNED_BYTE on the N95.
  The emulator now answers BGRA like the N95 and serves it (EMU_PATCHES.md).
Bench (r11 build, 160 s): config 4 (depth 24, alpha 8, stencil 8), errc 0, leak.py 0,
composite 15-17.7 ms/render (read 7-9.4), BGRA and RGBA windows alike on the bench (the
swap is on the host). Pictures: tags on their cars, sparks over the road, chevrons, gantry
signs lit, tunnel entrance clean.

Battery on the exact phone binary (r11_phone = -DA4_HPERSP -DA4_TAKEDEF -DA4_TAKEFLAT
-DA4_TAKEADD -DA4_TAKESHADOW -DA4_TAKEALPHA -DA4_ZBUF -DA4_IMM -DCLIP_TOP=0 -DA4_TEL
-DTEL_EVERY=40 -DTEL_RING=8 -DTEL_CYCLE -DA4_ABCYCLE, --gl --capture; sha256
f8f988152f660787724017f81e8bfd69f8757c46b6e8b6e402a23ae0cbfa23f2; telerun3 160 s, lap 2
through the tunnel): framestat 3755, faults 1 (the known boot fault only), errc 0 in all
eight, alpha 8 / depth 24 / stencil 8, late_tris 0 (the HUD painter never ran mid-3D),
leak.py 0, ntex 149. Pictures by eye: tags on their cars, sparks over the road, the tunnel
clean, The Hills correct.
Rule 4 -- guesses, named: (1) that the pickups are drawn in software (outside the GPU
classes) and so were displaced by the frame of lag -- inferred, not seen: the bench race
has none; (2) the N95's config with depth AND alpha (abits in the header; without it the
sentinel path runs, with its "pink is lost" weakness); (3) immediate composite cost on the
MBX: the GPU's render now sits between the last triangle and the read -- round 10 showed
glFlush did not shorten the read, so it is expected to cost little, unmeasured; (4) BGRA
read speed on the N95 (the A/B measures it); (5) the tunnel lamps (photo 4) and the frame
with no scenery and no HUD (photo 5) are not reproduced on the bench; the affine fallback
and the lag are both candidates, neither shown to be the cause.

## Phone round 11 (r11_phone f8f98815): tunnel fixed; 0.5 s a frame; pickups gone; shadow faceted

Owner: (1) the tunnel renders correctly and its lamps no longer show through the walls
(round 9-10's report, fixed); (2) performance regressed hard -- about one frame every
0.5 s, with slowdowns; (3) every pickup invisible once GPU rendering starts (photo 1, the
software first frame: a white glow in the right lane; photo 2: gone) -- still collected;
(4) the car's shadow "looks like a 3D model of the car" (photo 3: a grey shape crossed
by darker lines and wedges, the car's own facets); (5) a4tel5-7 0 bytes.
Telemetry (5 files, capidx 0-4; not repeats of round 10 -- composite at the HUD, colour
mask, additive on the GPU): errc 0 in all; pbuffer config 1, depth 24, ALPHA 8, stencil
0; late_tris 0; ntex 151; mode-1 re-bakes 2.
- PER RENDER (ms), by window mode: RGBA read 13.1-14.3, BGRA read 1.7-2.8 (the
  implementation's own pair is the copy without a convert: 76,800 pixels in ~2 ms);
  composite 9.7-10.3 (BGRA) vs 20.9-22.6 (RGBA); BATCH DRAW 308-458 (round 10: 9-14.5);
  total render 392-543 nticks (round 10: 88-116). 420-490 draw calls, 1450-1750
  triangles; shadows ~290 a render, additive 7-34.
- The tile render moved: round 10's read (17-19 ms, pipelined) held the render; round
  11's BGRA read is the bare copy, so the render was already done inside batch_draw --
  the driver rendered (or worse) during the draws, ~30x round 10's whole GPU time.
- Files 5-7: TEL_RING creates all eight at start; at ~2 renders a second a 40-render
  window is ~20 s, so the race ended after five captures. Not corruption.
Diagnosis (guess, named; source rank 3-4): the colour mask. Round 11's only per-draw
state new to the phone is glColorMask(1,1,1,0) around every blended run. An N95 8GB
developer traced a "performance breakdown" to disabling alpha writes with glColorMask
("a killer feature for the hardware"); Imagination's reply: a masked channel makes the
hardware treat the object as transparent, its value read from the surface behind
(https://forums.imgtec.com/t/gldrawrangeelements/229, April 2009). The pickups are white
glows (photo 1) -- additive, the class round 10 left to software -- and they vanished
exactly where the colour-masked additive draw was composited over the GPU road.
Shadow (measured part): the N95 has no stencil, so the once-a-pixel stamp is off there
and overlapping shadow triangles multiply 0.75 per layer (round 10 noted this in the
same-frame captures; the bench has stencil 8 and never shows it). Photo 3's dark lines
are where the car's projected faces stack.

## Round 12 bench: no colour mask, billboards on the GPU, shadows once a pixel without a stencil

Not repeats of: round 11 (glColorMask around every blended run, the composite painting
over the billboards, shadows multiplying on a no-stencil config), round 10 (pipelined,
RGBA/BGRA A/B).

1. No glColorMask. Each blended class keeps dst alpha (the drawn-mask) by its own blend:
   additive MODULATEs by a colour of alpha 0 (alpha test off for the run) so ONE/ONE adds
   nothing to alpha; shadows GL_ZERO/GL_SRC_COLOR with grey 12/16 and alpha 1; the alpha
   class premultiplied, GL_DST_ALPHA/GL_ONE_MINUS_SRC_ALPHA, so alpha out = da exactly
   and its blend over the backdrop never reaches the composite. Read fixed to BGRA
   (A4_ABMODE=2, no A/B cycle; the header's mode word names it).
2. The invisible pickups, run down. Same-frame captures (A4_DUALCAP) showed the nitro
   flame in software's picture and not the GPU's. In-race census of the calls
   draw_primitive leaves to software (A4_SWHIST, counted only once GL is up): only the
   two blended classes, by design -- so the flame is drawn outside draw_primitive. The
   back buffer's race-time writers (EKA2L1_WATCH with WATCHONCE, armed by the new
   EKA2L1_WATCHARM at the alpha span's store, which only the track runs): the minimap
   (+26E28, +2B5A0 families), speedometer and text (+2BDxx-+2D1xx), the HUD painter, the
   backdrop, the two blended spans, and +30472 writing bottom-centre -- a scaled 2D
   sprite blit, +3015E (sibling +3059E), dst + texel saturating. Its callers +31360,
   +31B50, +31BF2. Order trace (PCTRACE_REGS on draw_primitive, both blits and the HUD
   painter, armed at the alpha span, 128 frames): ~800-1800 triangles, then billboards
   interleaved with the last ~60-80 triangles, then 18 HUD calls. So round 11's composite
   at the HUD painted the road over every billboard drawn before it: flames, glows and --
   inferred -- the pickups (photo 1's pickup is a white glow; round 10's "displaced
   pickups" are the same blits landing on a one-frame-old road).
   First attempt, composite at the first billboard (hooks after both blits' prologues):
   700-800 late (software) triangles a render -- the city draws billboards mid-pass,
   which the trace's lap-1 frames did not show. Withdrawn.
   The blit, read (+3015E): size = scale table[z] * arg / 64 * r3 >> 16, aspect by the
   sprite's shifts (+0x14, +0x18); rectangle centred on the position, clipped to the
   surface (ctx+0x4c, width at view+0x10); u = 0..w, v = h-1 down to 0 (bottom-up), 8.8
   steps by divide; texel = tex[((v>>8) & vmask) << vshift | ((u>>8) & umask)] (sprite
   +0x10, +8, +0x1C, +4); mode = stack arg 1: 0 key copy (texel 0 skipped), 1 stipple
   (alternate pixels, texels with a zero top nibble), 2 saturating add, 3
   dst + (sat(dst+T) - dst) * a/16 (a = stack arg 2).
   Probe (A4_BBPROBE): 9,566 calls in a race, ALL mode 2; 9 sprites, all square, 64x64 or
   32x32, pitch = width, every one into the back buffer; rectangles up to 264x240.
   A4_TAKEBB: hooked at +30252 (after the clip, before the mode switch; lr dead), each
   billboard is a quad in the frame's batch -- in draw order with the triangles around it
   -- KIND_TEX for mode 0, KIND_ADD for 2, KIND_ADDF (a/16 colour, new) for 3; mode 1 and
   anything odd stay software. Texture cached by sprite|3, uploaded 565/5551. Additive
   modes are also drawn by software (as additive triangles are), so a glow over the sky
   survives. The sibling +3059E (never called on the bench) composites at its first call,
   counted (header word 63).
   Same-frame captures (TAKEBB + DUALCAP): the GPU's flame matches software's in place,
   size and shape.
3. Shadows once a pixel without a stencil (A4_SHADZ, used when sbits = 0 and zbits > 0):
   shadow runs depth-test GL_NOTEQUAL and write; their z is 0 = window depth 0.5, which
   no flags-1 triangle has (0.5 + ZK/ZW/2) and the clear (0) is not, so the first shadow
   at a pixel stamps it and the next fails. Bench in the phone's state (A4_NOSTENCIL):
   without the stamp the GPU shadow shows the layered wedges of photo 3 against
   software's flat one; with it, flat like software's (same-frame crops). A generic
   brightness-difference count could not see it (sampling noise ~2-5k pixels a capture
   swamps the shadow), so this rests on the crops.
Battery on the exact phone binary (r12_phone = -DA4_HPERSP -DA4_TAKEDEF -DA4_TAKEFLAT
-DA4_TAKEADD -DA4_TAKESHADOW -DA4_TAKEALPHA -DA4_ZBUF -DA4_IMM -DA4_SHADZ -DCLIP_TOP=0
-DA4_TEL -DTEL_EVERY=40 -DTEL_RING=8 -DTEL_CYCLE -DA4_ABMODE=2u -DA4_TAKEBB, --gl
--capture; sha256 db6a2b389fcecf372185524b2312bcf5dfcee8001aab091a525d9ecc5a2feb42;
telerun3 160 s, into lap 2 and the city): framestat 3258, faults 1 (the known boot
fault), errc 0 in all eight, late_tris 0, billboards on the GPU ~15,000 / software 65,
+3059E calls 0, leak.py 0, ntex 158. Pictures: the flame behind the car, glows, tags,
chevrons. Same code with A4_NOSTENCIL (the phone's no-stencil state): framestat 3297,
errc 0, late 0, leak 0, shadows flat, tunnel lamp glows drawn.
Seen, not fixed (pre-existing): some spark streaks are red in software and white on the
GPU in the same frame (additive triangles; cosmetic).
Rule 4 -- guesses, named: (1) glColorMask was round 11's 300-460 ms batch draw (the N95
8GB forum report and the timing's shape; removing it is the test: batch draw should be
back near round 10's 9-14.5 ms, the render then showing in the read); (2) the pickups
are drawn by +3015E -- the bench race has none; if they come through +3059E, header word
63 counts it and the frame composites there (pickups visible, its tail in software); (3)
modes 0 and 3 on the GPU are unexercised on the bench (only mode 2 occurs); (4) the
depth stamp lets a farther flags-1 triangle drawn after a shadow show at the shadow's
pixels (the game's buffer kept the road's 1/z there) -- not seen in the crops; (5) the
MBX's GL_DST_ALPHA source factor, GL_SRC_COLOR destination factor and MODULATE with a
zero-alpha colour -- all core GL ES 1.1, none measured on the N95; (6) billboard texels
are not rewritten in place (cached once per sprite).
States the bench never entered: pickups; billboard modes 0, 1, 3; the +3059E blit; the
MBX.

## Phone round 12 (r12_phone db6a2b38): pickups and nitro back, speed at round 10's or better; the takedown letterbox behind the scene

Owner: nitro and pickups are right now; speed back to before round 11, "sometimes it feels
smoother"; NEW: the two black bars of a takedown / crash (and, they think, the helicopter
view) are drawn in the wrong order -- photos 1-2: the bars cover only the sky and sit
behind everything else instead of in front.
Telemetry (8 files, capidx 50-57; not repeats of round 11 -- no colour mask, billboards on
the GPU, depth-stamped shadows): errc 0 in all; config 1, depth 24, alpha 8, stencil 0;
late_tris 0; billboards on the GPU 42,572-47,300 (total), left to software 133-135,
+3059E calls 0; leak.py 0; ntex 164; mode-1 re-bakes 85 total, 0 texels in these windows.
- PER RENDER (ms): total 84-100 (round 10: 88-116; round 11: 392-543); composite
  15.0-16.7, of which glReadPixels BGRA 6.2-7.2 (the tile render and the copy: round 10's
  RGBA read was 17-19); batch draw 9.6-14.4 (round 10: 9.1-14.5; round 11: 308-458).
  So round 11's 300-460 ms went with glColorMask -- the guess, now measured by its
  removal. Our blit is ~9-10 ms of the composite (composite minus read).
- Real-render rhythm in the fps ring: 1-3-0 / 2-4-0 ticks -- a game iteration 4-6 ticks
  (round 10: 6-9).
- Captures by eye: flames behind the car (51, 54), the tunnel right; the end-of-race
  cameras (56, 57). No takedown in a capture.

## Round 13 bench: the letterbox bars on the GPU (the rectangle fill), and the road grain answered

Not repeats of: round 12 (fills left to software, painted over by the composite).

The bars. Race-time writers of the back buffer (EKA2L1_WATCH on the fill's store
+2AB3C, armed by WATCHARM at the alpha span): the solid-fill loop of +2AB08 -- the game's
rectangle fill (surface, x, y, w, h, colour, border; 0x1000 = none: solid when the border
is none, else an outline with the colour as interior unless that is none). Two callers
draw letterboxes with it: +125EA (takedown / crash: top and bottom bars, 35 or 50 rows
animated in) and +127EE (the helicopter news camera, "GNN LIVE": 40-row bars with text
between). Both run after the 3D pass and before the HUD, i.e. before the composite --
round 12's photos (bars only over the sky) are the composite covering them wherever the
GPU drew. (A first try at tracing a takedown frame with PCTRACE_REGRING/DUMPAT at
+12636 dumped nothing: no takedown, or that address is not a block entry -- not
settled; the watch did the job.)
A4_TAKEFILL: hooked after the fill's prologue (+2AB0C; displaced add r1,sp,#0x28 /
ldm r1,{r1,r5,r6} as three loads), a solid fill into the game's surface (pixels = bb,
stride 320) is a flat quad in the batch, in draw order, and the fill returns at once;
outlines, other surfaces and fills after the composite run as before. The quad
emission is shared with the billboards (quad_push). Header word 49 (dual frames, 0
under A4_IMM since round 11) now counts these fills.
Bench instrument A4_FILLCAP (captures the renders that drew a fill on the GPU, one in 8 at
most): eight helicopter-camera captures, the black bars over the road and the cars, the
news overlay over them. The +125EA takedown bars are the same call shape (solid black)
and were not captured on the bench.
Battery on the exact phone binary (r13_phone = r12's flags + -DA4_TAKEFILL; sha256
f9e8cfdf99442f081427c9b711ad519ab75c86671bde4076c2f92ba20fe6be3b; telerun3 230 s, to the
race end): framestat 4818, faults 1 (the known boot fault), errc 0 in all eight,
late_tris 0, fills on the GPU 130, billboards on the GPU ~24,600 / software 74, +3059E
calls 0, leak.py 0, ntex 159. Pictures: normal race frames, flames, the helicopter.

The owner's question (with photos of the countdown at "3", before the GPU starts after
START_AFTER primitives, and at "1"): the road loses its grain and small details on the
player's car change when GL starts -- wrong, or right?
- The grain is the game's, and the GPU path does not draw it (known since round 10's
  same-frame captures). The road's lower rows go through mode 4 of the perspective
  span (flags 0x8020001 / 0x8021001), rewritten bit-exact as a4_span_textured: per
  pixel, light = light*1664525 + 1013904223, a grey li = (light >> 16) & mask, reduced
  below a threshold, added to the texel with saturation. The generator's state carries
  from span to span and frame to frame (ctx+0x48), so the grain moves. The GPU draws
  the plain texture. Reproducing it: a second, additive pass of the mode-4 triangles
  with a screen-space noise texture, depth-tested GL_EQUAL so it lands only where the
  road won, emitted right after each run of road triangles (deferring it to the frame
  end would put grain on the player's car, which draws untested). Not built.
- The car: its body is the default span's mode 1 (a paint-reflection table the game
  rewrites every frame), baked into the texture and re-baked at most every 8th present
  for textures of 16K texels or more (M1_BIG, M1_BIG_EVERY; 3.7-10 ms a bake on the N95,
  round 10), so the reflection steps where software's moves every frame; and GL samples
  texel centres, half a texel from the spans, so thin details shift by a pixel. By
  design, not missing textures.

Correction to the car answer above. Round 12's phone telemetry: m1_rebakes flat at 85 and
0 texels re-baked in all eight windows (capidx 50-57, ~320 renders) -- the Corvette's
mode-1 texture was not re-baked at all late in the race, so "the reflection steps every
8th present" is not what the owner's photos show (they compare a software frame with a
GPU frame a second later anyway). Candidates, unmeasured: (1) the default rasterizer
(0x2e1c8, the car's class) maps textures AFFINELY -- it is the perspective
rasterizer's own affine fallback -- while A4_HPERSP gives every textured class the
vertex W, i.e. perspective-correct mapping, so the car's texture sits slightly
differently on its larger triangles; (2) GL samples texel centres, half a texel from
the spans; (3) the reflection table frozen at its last bake, if the signature misses a
change (64 sampled texels when the range of M passes 1024). A same-frame capture
(A4_DUALCAP) of the car tells (1)-(2) from (3).
Why a re-bake costs the CPU at all: the MBX is fixed-function (GL ES 1.1, no shaders).
Mode 1 is T + table[M]/4 with M a per-texel index into a table rewritten every frame --
a dependent lookup the fixed pipeline cannot do; nor can a palette (M spans ~26K values
on the bench car, a paletted texture holds 256). So the CPU computes the combined
texels and re-uploads them (round 10: 3.7-10 ms a render while it was re-baking).

## Phone round 13 (r13_phone f9e8cfdf): bars right; after the garage, wrong car textures and a software/GPU mix

Owner: the letterbox bars are right now. A second run went through CAREER (the garage)
instead of instant play, and showed: (1-6) garage cars with the wrong textures, a
different wrong one each time they scrolled away and back (Ferrari <-> Mercedes),
stretched or shrunk; (7) the HOME menu's gradients gone; (8/9) the road smooth in one
moment and grainy in the next; (10) horizontal lines through the road; (11-12) a traffic
car behind a road stripe; (13) the far tunnel showing through the left wall. "It felt
like the game was switching back and forth between software and GL."
Telemetry (8 files, capidx 107-114; not repeats of round 12 -- a career run, the garage
visited): errc 0; leak.py 0; late_tris 0; fills on the GPU 597-661.
- ntex 256 in every capture = NCACHE: THE TEXTURE CACHE IS FULL. It has no eviction (ntex
  only grows; round 12's instant race ended at 164). Every texture not cached by then is
  refused (tex_index returns -1) and its triangles fall to software.
- Billboards left to software 4,348-7,655 (round 12: 133-135): bb_tex refused too.
- GPU-taken triangles 722-1,371 a render (round 12: 1,060-1,490), draw calls 96-457;
  per render 99-145 ms (round 12: 84-100) -- software doing the refused share.
- Mode-1 re-bakes 126-171 total, up to 852K texels a window (the garage's cars).
Reading (measured: the full cache; inferred: the pictures): software triangles land in
the game's buffer in draw order, and the composite then writes every pixel the GPU drew
over them whatever their depth -- a software traffic car under a GPU road stripe (11-12),
a GPU-drawn far object over a software wall (13), the grainy road where mode 4 went to
software and the smooth one where the GPU drew it (8/9). The garage: the cache keys a
texture by its descriptor address only; a car model loaded where the last one was freed
gets the last one's GL texture, and its UVs, normalised by the new size, stretch or
shrink it (1-6) -- inferred, the bench has not entered the garage yet.

## Round 14 bench: the texture cache validated, keyed by its texels, and evicting

Not repeats of: round 13 (cache keyed by descriptor only, no eviction).
- Per entry (struct texc at 0x413000, scratch top raised to 0x415000): the texel pointer
  (now part of the key), the size, a 64-sample hash of the texels and the last present
  used. A hit checks the size every time and the hash once a present, and remakes the
  texture if the descriptor holds another one (into a new slot if the entry was already
  drawn with this present: its GL name is in the batch). A miss on a full cache evicts
  the entry used longest ago, never one used this present (cache_slot). Billboards share
  it (key sprite|3 plus texel pointer). The A4_TEXCHK instrument went with it (this is
  what it diagnosed). Header word 38 (glFlush ticks, unused since round 12) now counts
  textures remade, word 46 (the read-format probe's error, 0 on the N95) evictions.
- The garage, reproduced (menudrive.sh: boot, CAREER, CHOOSE YOUR RIDE, then
  Right/Left with a screenshot after each key; the bench shows the screen upside down):
  round 13's binary shows the Mini in the Nissan's orange and the Nissan in the
  Mercedes' silver from the second visit on (views 5-7, 11-13 of 13); round 14's keeps
  every car's own texture in all 13.
- The full cache, reproduced with -DNCACHE=64 (a race uses ~160): round 13's code fills
  it and refuses the rest -- A4_SWHIST counts 198,667 non-blended calls left to software
  (0x4001 roads, the mode-4 road rows, ...), 2,800 billboards refused; round 14's evicts
  (350-400) and leaves 2,650 (frames that used more than 64 textures, impossible at 256).
- Found on the way (A4_REMADEPROBE, a ring of remade entries): two textures in the race
  animate by the game swapping their texel pointer among 2-3 frames (a 64x64 alpha-map
  one, a 32x32 one) -- frozen on their first frame on the GPU through round 13.
  Re-uploading each swap cost an upload a few times a window; with the texel pointer in
  the key each frame has an entry: 0 remakes in a race, ntex 159-164.
- The HOME menu's missing gradient (photo 7) is NOT reproduced: with GL up in the menus
  (-DSTART_AFTER=2000u, 240 GPU renders, ntex 13) and with an 8-entry cache, both builds
  draw the HOME panel like the stock game. The menu's 2D is not in the game's buffer at
  the 3D present (the captures hold only the garage), so the composite cannot be what
  removed it. Open; to look at again in round 14 on the phone.
Battery on the exact phone binary (r14_phone = r13's flags; sha256
d5cd29ab547b96eb672e157d3f06c6cea8b560589d3f4c3c64aaef05b5148253; telerun3 230 s):
framestat 4862, faults 1 (the known boot fault), errc 0 in all eight, late_tris 0,
remade 0, evicted 0, ntex 164, fills 60, billboards on the GPU ~25,700 / software 143,
leak.py 0; per render 85-88 ms on the bench. Garage cycle (menudrive, 13 views): right in all.
Rule 4 -- guesses, named: (1) the phone's road grain switching, cars behind stripes,
tunnel through the wall and horizontal lines (photos 8-13) are all the refused share
drawn by software under the composite -- the full cache is measured, the attribution of
each photo is inferred; (2) photo 7's cause is unknown; (3) the 64-sample hash can miss a
texture replaced by one identical at those 64 texels (it compares size and pointer too).

## Phone round 14 (r14_phone d5cd29ab): the cache holds; only the garage menu's gradient left

Owner, after instant race -> career -> garage cycling -> race: no regressions, the
texture issues resolved, performance as before with "moments of sweet smoothness" in the
tunnel with few cars; the only thing left is the HOME/garage menu's gradient (round 13's
photo 7), still missing.
Telemetry (8 files, capidx 220-227, a long session: 9,150 GPU renders; not repeats of
round 13 -- the validated, evicting cache): errc 0; late_tris 0; ntex 256 (full, as the
garage fills it) with 577-582 evictions and 24-25 textures remade -- the cache cycling
as designed; billboards to software 934-950 over the whole session (164,000 on the GPU);
per render 84-95 ms (one window 252 ms a render: 40 renders over 10 s, a pause or a load,
not a race window).
leak.py: capture 224 counts 11 "additive" pinks -- the player's red tail lights with a
white glow saturating R and B (84 pixels by the looser test, all on the two lamps and the
HUD's corner). A real pink in the scene, not magenta: the predicate's known blind spot
(it assumes no pinks in a race).

## Round 15 bench: the composite targets the frame's own 3D surface (the garage menu panels)

Not repeats of: round 14 (composite into A4_GL_BB as the software span last left it).
The cause, reproduced on the bench. A4_GL_BB was set only by a4_cachebb_tramp, in the
software perspective span -- which the GPU path no longer runs once the cache holds the
textures. The menus' garage renders its 3D OFFSCREEN (ctx[0x4c] = a heap buffer,
0x27xxxxx on the bench; the race renders into the screen surface, 0xD0E9C0 bench /
0x70C760 phone), and the game copies that buffer onto the screen at the START of the next
frame (+2A86E: memcpy(surface pixels, surface[0x40], w*h*2)). Garage frame order (stock,
PCTRACE_REGS): copy, H1, draw_primitive x465, F1 (the stats box), H x89, present. HOME:
P422 H14 T1 H73 present (T = the translucent fill +2ABAA -> ARM veneer +2ABB0 -> +B9A10).
After a race, A4_GL_BB still held the race's SCREEN surface, so the garage's GPU image
was composited onto the screen at the first HUD call -- over every 2D thing drawn before
it: the HOME panel, the stats box (F1), the COMPLETION bar. Phone capture 227 (round 14,
the NEW VEHICLE screen, bb = the screen surface) shows exactly that: text on the ceiling,
no box. Bench (racemenu.sh: race 30 s, F1 = left softkey = pause, MAIN MENU, YES, CAREER,
HOME, garage): round 14's build loses the HOME panel and the bar; a garage-first run
(round 14's bench tests) never did, because the software span had run in the garage
before GL came up and left A4_GL_BB on the offscreen buffer.
Fix: at each frame's first triangle A4_GL_BB = ctx[0x4c] (draw_primitive's ctx IS the
span's: PCMEM at +300AE and +333F4, same ctx, same [0x4c]). A triangle to a different
surface later in the same frame goes to software and is counted (header word 43, which
held HomeTime's low word for the round-10 tick calibration): 0 on the bench.
Bench, round 15: the same race-first route draws the HOME panel and the bar like the stock
game; the garage capture headers show bb = the offscreen buffer.
Battery on the exact phone binary (r15_phone = r14's flags; sha256
b87169d2e75559605abda9a16535b240716498559828c2164c45dc10f0ce77e5; telerun3 230 s):
framestat 4756, faults 1 (the known boot fault), errc 0, late_tris 0, surf2_tris 0,
remade 0, evicted 0, ntex 166, fills 231, billboards GPU ~22,900 / software 96; leak.py 0
except one pink pixel between white ones in capture 61 (a sign, not a sentinel region).
Garage cycle (13 views): every car right, the bar drawn.
Rule 4 -- guesses, named: (1) the phone's photo 7 (HOME) is this bug -- capture 227 and
the bench route reproduce the mechanism, photo 7 itself was not captured; (2) the 3D
target never changes mid-frame in the race or menus (counted; 0 on the bench).

## Phone round 15 (r15_phone b87169d2): the garage menus right; a lap-line band glitch

Owner: "All seems to be fixed" (the garage menu panels included). New: at the start/finish
line after a lap or two -- not every time -- a band or box over the upper screen: photos
1-2, the big "lap 3/3" with a lighter rectangle over the scene whose edge runs the
screen's height; photos 3-4, the lap-time ("00:39:80") band, purple-grey and striped,
covering the sky only -- trees, hills and the house inside it untouched; photo 5, a box
over the top-left trees during "FANTASTIC DRIFT!", before the line.
Telemetry (8 files, capidx 135-142; not repeats of round 14 -- the frame's own 3D target):
errc 0; late_tris 0; surf2_tris 0 (one 3D target a frame, as assumed); ntex 256 with 243-261
evictions and 11 remakes; billboards to software ~565 of ~94,000; leak.py 0; per render
84-105 ms in the race. Captures 140-142, the garage after a race, now carry
bb = the offscreen buffer (3D only), as on the bench.
Reading (inferred from the photos; the band was not in a capture): a translucent effect
drawn by software BEFORE the composite, which then overwrites it wherever the GPU drew --
so it shows over the software-drawn sky and not over the GPU's hills and trees. The
round-15 menu traces name the translucent fill: +2ABAA (Thumb veneer) -> +2ABB0 (ARM b) ->
+B9A10, per pixel dst*k + c; it is not hooked, so it never triggers the composite.

## Round 16 bench: nothing software draws into the 3D surface lands under the composite

Not repeats of: round 15 (billboards the GPU did not take, and outlined fills, drawn by
software before the composite).
What the band is NOT, measured on the bench: the translucent fill +B9A10 is never called in
a 200 s race (a ring armed at the race, dumped at its first call: nothing); the race-time
fills (PCTRACE_REGS + PCMEM on +2AB08, 230 s) are the letterboxes (solid, on the GPU since
round 13) and a 13x2 outlined minimap marker (+26DF6) drawn after the composite.
The race-time writers of the band rows (EKA2L1_WATCH, rows 50-70, WATCHONCE armed at the
race): the alpha span, the HUD routines (+2C524, +2CFD0, +2D09E, +2D136, +2996A, +29A9A),
the composite, the backdrop copy, the billboard blit's software add (+30472), the
additive span, and one outlined fill border (+2AB84).
What the phone does that the bench does not: 565 billboards left to software in round 15
against ~95 on the bench (round 15) and 7 here; every bench billboard is mode 2 (24,414
calls). A billboard the GPU path does not take -- mode 1, the checkerboard stipple, or a
texture the cache refuses -- was drawn by software into the 3D surface BEFORE the
composite, so it survived only where the GPU drew nothing: the sky. A stipple over the sky,
photographed, is a striped band covering the sky only (photos 3-4) -- inferred.
- A billboard into this frame's 3D surface that the GPU does not take now composites the
  frame first (a4_gl_flush), then software draws it, on top; what follows is software, in
  order (late_tris counts it).
- An outlined fill into the 3D surface is drawn as quads (four edges in the border colour,
  the inside in the fill colour unless "none").
- A4_BBPROBE ships (table appended to each capture): calls by mode, non-mode-2 sprites
  (address, size, masks, mode, alpha, count), [120] composites forced by a software
  billboard, [121] outlined fills drawn on the GPU.
Battery on the exact phone binary (r16_phone = r15's flags + -DA4_BBPROBE; sha256
12d2753d97a4c624c08df640f14f5a27a9dc5ba5f6d35afd80eca11c223ae1d0; telerun3 230 s):
framestat 5102, faults 1 (the known boot fault), errc 0, late_tris 0, surf2_tris 0,
ntex 161-163, fills 116, billboards GPU ~24,300 / software 7 (empty rectangles), forced
composites 0, outlined fills on the GPU 0 (the marker comes after the composite), leak.py 0.
Rule 4 -- guesses, named: (1) the band is a mode-1 (or refused) billboard -- unproven; the
probe names the phone's non-mode-2 sprites, and [120] counts the forced composites; (2) a
forced composite early in the 3D pass would put the rest of the frame in software (slow
for that frame, still correct) -- late_tris measures it.
Owner, before testing round 16: the band lasts a frame or two only (hence hard to
photograph). Consistent with a transition effect the game draws for a frame or two at the
line (the inferred mode-1 billboard); it does not settle it -- the probe does.
Round 16 rebuilt with phase timers (the owner asked; not shipped yet, so the earlier
r16 binary 12d2753d is superseded). Each window's time split, in NTickCount ms:
the 3D pass (a frame's first triangle to the composite), our draw_primitive hook inside it
(every 4th call timed, x4: NTickCount is 1 ms, and over many calls the ticks crossed are an
unbiased measure), the composite (last batch, read, blit), the 2D after it (to the
present), the game's present call (a new hook after +1EC's blx, at +1EE: the runtime's
565-to-32 convert and the post, re-presents included), and the rest = the window minus
those (game logic before the 3D and between re-presents). Header words 13, 14, 16, 17, 18
(ntex duplicated at 54, bstep 6 once up, the A4_WINDOW fields: none live in this build).
Bench (the exact binary; host-paced, not phone time): per render 3D 17-25 (hook 7-13 of it),
composite 17-19, 2D 6.5-9, present 18-28, rest 16-19, summing to the 85 ms render.
Battery on the exact phone binary (r16_phone, sha256
cf8940959e3d78f69186d34e7226e2205242cb0ae24f256e40d3e4a8937a1c6c; telerun3 230 s):
framestat 4902, faults 1 (the known boot fault), errc 0, late_tris 0, leak.py 0; the
race-first garage route (racemenu.sh): HOME panel, stats box and bar drawn.

## Phone round 16 (r16_phone cf894095): the band persists; not a billboard; the game's own 85 ms render gate

Owner: the lines still appear for a frame, anywhere between leaving the tunnel and crossing
the line (photos: two thin dark full-width lines ~15-40 rows apart in the upper screen,
once with the lap time between them, once during "ESCAPED!").
Telemetry (8 files, capidx 54-61; not repeats of round 15 -- forced composite for software
billboards, outlined fills as quads, the billboard probe, phase timers): errc 0;
late_tris 0; surf2 0; leak.py 0; ntex 168-254.
- Billboard probe: ~50,000 calls, ALL mode 2; no non-mode-2 sprite; forced composites 0;
  outlined fills on the GPU 0; the 356 "software" billboards are empty rectangles (off
  screen). REFUTED: the band is not a billboard (round 16's guess 1).
- Phase timers, per render (ms), race windows with the HUD (54, 55, 56):
  54 (106.5 total): 3D pass 47.7 (our hook 20.3 of it), composite 15.4, 2D 6.9, present
  18.6, rest 17.9; 55 (85.0): 17.2 (13.4), 14.3, 7.2, 28.7, 17.6; 56 (85.0): 15.9 (9.2),
  14.8, 6.6, 30.2, 17.5. Demo race without the HUD (58-61, the Nissan, ~2,000 triangles a
  render): 93.8-107.8 total, 3D 38-50 (hook 26-36), composite 15.4-16.5, present 15.4-19.5,
  rest 21-24. Window 57 (1,198 ms a render): a load or pause.
- THE FINDING: light windows sit at exactly 85.0 ms a render, and the slack is inside the
  game's present calls (28-30 ms for the three, against 18.6 when the frame is heavy).
  Bench test (A4_SKIPREPRES: DoPresent's caller stashed by a4_fps_tramp, the presents from
  +950 and +97E skipped): still 85.0 ms a render on the bench, the present phase down to
  ~1 ms and the "rest" up to ~50 ms, presents per second 360-576 (the loop now spins,
  rendering 3D on ~1 iteration in 16). So the race loop gates its 3D render at ~85 ms
  (~11.8 a second) by itself; the three presents only absorbed the wait, which masked the
  gate in the round-25-era RE ("the loop runs free", MBX.md "RE conclusion" -- corrected
  here: free-running iterations, gated renders). Light scenes are already at that cap on
  the phone; heavy ones are above it, work-bound. Not shipped (no gain at the cap; it
  turns the loop into a spin).
- Our hook costs 9-20 ms a render in the race and 26-36 in the demo race: the largest
  item of ours after the composite.

## Round 17: the A/B -- the same build with the GPU drawing off (A4_SWONLY)

Not repeats of: rounds 13-16 (GPU builds), nor of any stock measurement (none was ever
taken on the phone with timers).
Two questions one phone round answers and the bench cannot: (1) are the lap-section lines
ours? -- if they appear with the GPU drawing nothing, they are the game's or the runtime's
(the present, the LCD); (2) the stock frame on the N95, split by the same phase timers --
the baseline for any speed-up claim, which the plan has lacked since round 0.
Build r17_sw = round 16's flags minus A4_TAKEBB/A4_TAKEFILL, plus A4_SWONLY: GL is up and
the frames counted (telemetry, timers, the billboard probe), every triangle, billboard and
fill drawn by the game's own software; no readback (nothing drawn, miny > maxy).
A4_SKIPREPRES (round 16's bench test) is in the source, off.
Bench, the exact binary (sha256 085ac5211dd220d805857b54e44178807a6466627843aa4d6d1ecbbd2ace8ac7;
telerun3 230 s): framestat 3752, faults 1 (the known boot fault), errc 0; captures are the
stock picture (the mode-4 road grain back); per render (bench host time) 3D pass in
software 40-54 ms, 2D 6.6-10.4, present 13-24, rest 13-19; the 85 ms gate as before.

## Phone round 17 (r17_sw 085ac521, GPU drawing off): the lines still appear; the first stock frame split

Owner: the lines still appear with this build.
So they are not the GPU path's drawing, composite or billboards. What this build still
carries of ours: the CPU rewrites every build has had since the early rounds (FILL_HOOKS:
the translucent fill +2ABAA -> a4_fill_alpha, patch/fill.c; the Lib3D vector leaves
+51D96/+51DB8 -> a4_vec_add/mulsub; LOOP_HOOKS: the mode-4 textured span +3357A and the
transparent span +33B16), the hooks (all inert here), and the telemetry's capture write
(155 KB every 40 renders). Remaining candidates: one of those rewrites, or the stock game /
the phone's screen update (a tear: two frames meeting on the panel at a line, in the
fastest section of the track). The decisive test is the original game, untouched.
Instrument gap found on the way (rule 4): round 16's "the translucent fill is never called
in a race" traced the ORIGINAL routine +B9A10 -- which no injected build reaches, because
+2ABAA is replaced by a4_fill_alpha. Re-measured at +2ABAA itself (PCTRACE_REGS, 230 s
bench race): 0 calls. The conclusion stands; the first measurement could not have shown it.
Telemetry (8 files, capidx 38-45; errc 0): the stock frame on the N95, per render, race
windows with the HUD (38-42): total 94-118 ms; 3D pass in software 53-75; 2D 6-8.4;
present 15.6-19.4 (the three presents' converts, no waiting: never at the 85 ms gate);
rest 15-18. Window 43: a takedown camera (letterbox); 44-45: the race end (45: 174 ms).
Against round 16 (GPU, same phone): 85-106.5 ms a render, light scenes held at the 85 ms
gate. Measured gain of the GPU path: ~10-20% a render -- the 3D pass falls from 53-75 ms to
16-48, but the composite (14-15) is new cost and the gate takes the rest in light scenes.
The game's own 85 ms gate is now the limit for any further gain from rendering.

## Bench RE: the 85 ms gate is the game's fixed logic step, and the physics is tied to it

Not a repeat of: round 16's A4_SKIPREPRES bench test (that showed the gate exists, not
what it is) nor the round-25-era "RE conclusion" (corrected in round 16).
Source: the binary (static, tools/tdis.py on the code section) and two bench runs.
- The chain: race loop +808 -> render path +99A -> +1916 -> +1836 -> +1D28 -> state
  dispatch +92C8 (state 12 = race) -> +9A10 -> +7036 -> the race object's update, a vfunc
  at +1670E: a fixed-timestep loop. It calls the step counter +15B1C, runs the logic N
  times, and renders (+12992) only if N > 0.
- +15B1C: t = game time (the timer object reached through +52CA0, +0x1100, [+0x14],
  [+0x2c]); N = t/21333 - last/21333 through the divide helper +52B80 (ARM, umull by
  0x18938DE7 at +52B98, >>43; remainder factor -21333 at +52B9C; its only callers are
  +15B38 and +15B40), clamped to 5. A second count M uses the period
  ([this+0x84C] * 0x5355) >> 8 (literal at +15F28, divide +50EBC), also clamped to 5:
  [this+0x84C] = 256 is normal speed, so it is the time scale (slow motion). last = t.
- 21333 units = 85.33 ms (the light-scene cap measured on the phone, rounds 16-17, and on
  the bench), so one unit is ~4 us (inferred from that match, not read from the timer;
  a guess until the timer's source is traced). The game's logic runs at 11.72 Hz and it
  draws at most once a step: the compile-time design rate. Two constants hold it, no
  setting chooses it (the other two 0x5355 literals, +A6C1C and +B67F4, not looked at).
- Bench test (r16_half: round 16's build with the step halved -- +52B98 -> 0x312684D0,
  +52B9C -> -10667, +15F28 -> 10667; the division checked exact): renders 58.75-66.4 ms
  (bench-bound; the gate moved off 85). Then speedtest.sh, both builds, instant race,
  accelerate held from the last Return, a timestamped screenshot every ~0.77 s:
  stock step: GO (first speed) between 3.9 and 4.7 s; 87 km/h at 7.7 s; the city
  outskirts at 17.8 s. Halved step: GO between 1.6 and 2.3 s; 88 km/h at 4.6 s; the same
  outskirts at 8.5 s and DOWNTOWN at 11.5 s. The countdown, the acceleration and the
  distance all run ~2x the wall rate (13.5 s of stock racing = 6.5 s halved).
- So the logic advances a fixed amount a step, not by measured time: shortening the step
  speeds the whole game up. The gate cannot be lifted by a constant. Consequences for the
  plan: (1) below 85 ms a render nothing more shows -- the target is every scene at or
  under 85 ms (a steady 11.72 a second; a render over 85 ms costs a double step, the
  judder heavy scenes show); (2) more than 11.72 frames a second needs frames drawn
  between steps from interpolated state (camera and objects), each costing nearly a whole
  3D pass of the game's own transform -- not affordable while that pass is 16-48 ms of a
  85 ms budget.

## Round 18 bench: fewer draw calls, a cheaper hook and composite, and the re-presents A/B

Not repeats of: round 16's A4_SKIPREPRES bench test (at the cap, bench only; never on the
phone, where heavy scenes are above the cap) nor any earlier hook or composite change (the
code below is new). Goal: every race scene at or under the 85.33 ms logic step.
- Where the time goes (round 16's phone windows, 8 of them): batch draw against draw
  calls and triangles fits ~32 us a CALL and ~0 a triangle (least squares; the two
  correlate, but 751 more triangles at 27 more calls cost only 1.2 ms). So the GPU-side
  cost is the call count, 400-500 a heavy render = 13-16 ms. The hook outside its GL calls
  is 5-9 us a triangle on the phone.
- Exact bench profile (EKA2L1_PCPROF, 1.5 G instructions of race, round 16's code): the
  blob is 36.7% of all guest instructions -- composite pixel loop 18.9%, draw_prim_take
  11.4%, tex_index 4.8%; 723 instructions a hook call. Lines: tex_index's linear search
  20% of the hook, the bit-length loops 13%, the vertex loop and batch append most of the
  rest.
- Census (A4_SEQDUMP, a bench diagnostic: every batch triangle's texture, screen box and
  texcoord range per frame, 80 race frames, tools/seqan.py): 345 calls
  a frame; 60 distinct textures. A triangle may be drawn earlier when its box misses every
  run it moves past (no shared pixel: same image, depth and blending included): 345 -> 210
  with that rule alone (TEXZ-TEXZ overlaps NOT treated as free -- coplanar ties would
  change); with a texture atlas as well, ~110 (not built: 66% of triangles have texcoords
  inside [0, 1]). Batch size (512 vs whole frame) and scan depth past 32 runs change
  nothing.
- Built: A4_REORDER (batch_draw groups by that rule, copies the vertices into run order,
  a call a run; triangle boxes at 0x414000, the reorder at 0x415000, the scratch grown
  0x14000 -> 0x20000, bss 0x20F08), the texture-cache hint table (0x420800, verified in
  full before use), CLZ for the bit lengths, the row extent once a triangle, and the
  N95's composite case (BGRA + alpha mask) two pixels a step with word stores.
- Checked (bench, all on the final code): A4_REOCHECK -- every pair put out of input order
  has disjoint boxes: 0 of ~6-10 M pairs a run, no triangle lost; A4_BLITCHECK -- the fast
  composite against the general loop on every strip: 0 pixels differ in ~19,600 strips.
  Both tests tested (rule 4): a mutant that skips the overlap test and one with a wrong
  green shift give 1.1 M overlapping pairs and 11 M differing pixels.
- Bench instructions a hook call 723 -> 578 (-20%); the reorder adds ~134 a call (grouping
  and copying), which a call saved (~32 us on the phone, ~10,000 cycles) repays many times;
  the composite's instructions -24%. Bench calls a render with the reorder: 230-505 ->
  150-270 (-30 to -47%).
- The phone round A/B: A4_ABCYCLE2 steps AB_SKIPREP (4: the race loop's two re-presents
  skipped) x AB_REORDER (8) one pair a capture window (modes 2, 6, 10, 14). Header words
  repurposed (constants known since round 10): [44] re-presents skipped, [45] presents,
  [57] reorder ticks, [59] calls before grouping. Bench, mode 6/14: the present phase
  ~28 -> ~1 ms a render, the time moving to the loop's spin (the bench is at the cap);
  screen updating and race pace unchanged (speedtest.sh, all-on build).
- Exact binary r18_phone (sha256 84cd0a87a37efcabedfd506b4adfabc652f6d920cba879e8bf5b6d888f9f3960):
  telerun3 230 s: framestat 5144, faults 1 (the known boot fault), errc 0, all four modes;
  racemenu after a race: pause, main menu, career garage -- panels and the car right.
- Named risk: the bss grew by 48 KB; the one time a large bss broke the game (~372 KB,
  MBX.md "A large image .bss corrupts the game") the bench showed it at load, and this one
  boots clean on the bench.

## Phone round 18 (r18_phone 84cd0a87, the A/B cycle): no glitch; the ring kept only the garage

Owner: no graphical glitch noticed (a race with the four modes alternating every ~3.5 s,
then the garage). The eight files (capidx 62-69, errc 0) are all the career garage's car
browser -- the ring keeps the last 8 windows (~25 s) and the garage after the race filled
it, so there is NO race window this round. The garage runs the same loop (its re-presents
come from the same two sites: 2 skipped a render in modes 6/14) without the 85 ms gate.
What it measures (same session, alternating windows, ~500-830 triangles a render):
- Re-presents skipped vs kept: per render 54.4, 56.4, 52.6, 60.3 ms (modes 6/14) against
  66.2, 80.7, 74.1, 72.3 (modes 2/10): mean 55.9 vs 73.3, -17 ms (-24%). The present phase
  0.3-0.5 ms against 15.6-19.6; the other phases unchanged. So on the phone, as on the
  bench, the re-presents are the expensive presents (the real one is cheap); the time is
  their own, not a convert every present needs.
- The reorder: calls a render 108 -> 67, 68 -> 44, 59 -> 41, 84 -> 50 (-35 to -40%).
  Grouping and copying 0.75-1.12 ms a render = 1.3-1.6 us a triangle (phone). GL time
  (batch draw minus grouping), all 8 windows, least squares: 32.6 us a call + 2.0 us a
  triangle (residuals <= 0.23 ms) -- the call cost as round 16 found, and with calls no
  longer tied to triangles a per-triangle cost shows that round 16's fit could not
  separate. Net in the garage ~0 (few calls saved); in a race (345 calls, 1325 triangles a
  frame on the bench census) ~-6 ms of calls against ~+2.8 ms of grouping: a guess until
  race windows exist.
- The composite's new pixel loop (all modes): composite 9.6-10.2 ms (read 5.8-6.6, the loop
  3.2-4.0) against round 15's garage windows 141-142 (13.2-13.6, read 5.8, the loop
  7.4-7.8): -4 ms a render on comparable scenes (different cars in view).
Instrument lesson (rule 4, what the run never entered): a ring of 8 windows holds only the
last ~25 s of 3D; anything 3D after the race (the garage) displaces the race. Next time:
quit from the race or the main menu, not through the garage.

## Phone round 18, second run (same binary): race windows -- the reorder pays, the re-present skip does not show

Not a repeat: round 18's first run held only garage windows. Same binary (84cd0a87), owner
quit after the race without the garage. Eight race windows, capidx 46-53, lap 3 of 3 (the
Enzo at 340-430 km/h, 1,061-1,604 triangles a render), errc 0, modes 10 14 2 6 10 14 2 6:
  46 m10 (kept, reorder):  96.6 ms  3D 41.8 (hook 22.6)  comp 11.1  2D 7.4  pres 15.2  rest 21.1  calls 533->328
  47 m14 (skip, reorder):  92.4     3D 47.9 (hook 20.4)  comp 12.2  2D 7.8  pres  0.6  rest 23.9  calls 535->289
  48 m2  (kept):           86.6     3D 28.4 (hook 17.8)  comp 12.5  2D 7.2  pres 20.7  rest 17.8  calls 453
  49 m6  (skip):           84.1     3D 29.3 (hook 19.5)  comp 12.6  2D 5.8  pres  2.4  rest 34.0  calls 429
  50 m10:                  87.9     3D 32.5 (hook 14.8)  comp 11.8  2D 5.6  pres 22.3  rest 15.7  calls 263->196
  51 m14:                  83.2     3D 39.8 (hook 17.1)  comp 11.9  2D 4.2  pres  1.0  rest 26.2  calls 252->192
  52 m2:                   84.4     3D 28.0 (hook 21.5)  comp 11.8  2D 5.8  pres 21.8  rest 16.9  calls 336
  53 m6:                   88.4     3D 50.1 (hook 15.1)  comp 12.2  2D 6.7  pres  2.0  rest 17.3  calls 427
- The reorder, measured: GL time (batch draw minus grouping), the 8 race windows, least
  squares: 30.6 us a call + 1.5 us a triangle (the garage's 32.6 + 2.0 agrees). Calls saved
  against grouping: 533->328 saves 6.3 ms for 2.6 ms of grouping (net -3.7 ms a render),
  535->289 net -5.4, the light windows (263->196, 252->192) net -0.5. Grouping 1.37-1.61 us a
  triangle on the phone. Kept: a net gain where frames are over the step.
- The re-present skip, in the race: the present phase falls 15-22 ms -> 0.6-2.4 ms, but the
  frame does not fall with it. Over the step, 46 -> 47 is -4 ms with 7% fewer triangles;
  53 (skip, 1,300 triangles, 1.1 loop iterations a render, so not spinning) costs 86 ms
  outside the present against 66 for 48 (kept, 1,307 triangles). The windows at the step
  (49, 51) spin (2.8-4.5 iterations a render) and say nothing. Read: in the race the
  present phase is mostly the one screen update the frame needs, which a re-present waits
  for; skipped, it lands in the next frame's 3D pass (the game's own part of it: 27.5 and
  35 ms in 47 and 53 against 6.5-19 elsewhere). A guess -- one CPU-bound piece of ours, the
  grouping, is not slower in skip windows (1.46 vs 1.61, 1.37 vs 1.37 us a triangle), so
  if time is taken it is taken outside our code. Two windows a mode with scenes this
  different cannot settle it.
- CORRECTION to the garage run above: "the re-presents are the expensive presents; the
  time is their own" held in the garage (no 3D-pass growth there) and does NOT carry to the
  race. The garage's -17 ms is the garage's.
- Composite 10.2-11.2 ms a render in the race (read 6.7-7.9; the loop ~3.5-4.5) against
  round 16's race 12.7-15.7 (loop ~7-8): -4 ms, as in the garage.
- Window 51's capture is a crash camera (letterbox, no HUD) with a large black shape at a
  tunnel mouth; the owner saw no glitch. Not a measurement either way.
- Where a heavy frame goes now (46, 96.6 ms): ours ~37 ms (hook CPU ~11.6, GL 14.4, composite
  11.1), the game's ~48 (3D 19.2, 2D 7.4, logic 21.1), present 15.2. To the step: -12 ms.

## Round 19 bench: the A/B over the whole race, and two small cuts

Not a repeat of round 18: the reorder is no longer cycled (measured net -4 to -5 ms over the
step, kept on), and the re-present skip, which round 18's two windows a mode could not
settle, is now measured over whole segments.
- A4_AGG (0x421000, the scratch to 0x22000): per segment (a gap of 1.5 s between renders --
  a load, a pause -- starts a new one; three kept) and per mode, the window sums (renders,
  ticks, every phase, grouping, calls before and after, triangles, presents, skipped) and
  a histogram of render-to-render times in 5 ms bins (the render after a capture's write
  left out). Written after the census tail in every capture, so any one of the 8 files
  carries the whole race. A4_ABCYCLE3: modes 10 (re-presents kept) and 14 (skipped)
  alternate a window; native readback and the reorder always on.
- The grouping's copy: unrolled, with the next triangle's source prefetched (PLD) -- the
  batch was written long before and is mostly out of the cache (the phone's 1.4-1.6 us a
  triangle against ~150 instructions). tex_hash samples 16 texels instead of 64 (each a
  likely cache miss, once a present per texture used); the stored hash uses the same
  function.
- Checks on this code: A4_REOCHECK 0 overlapping of ~13.6 M pairs, no triangle lost;
  A4_BLITCHECK 0 of ~19,000 strips.
- Bench, the exact binary r19_phone (sha256 555c310ff433500d411e7ed5a7f68e18bc385536017d0a278112709ec928ac6b):
  telerun3 230 s, framestat 5084, faults 1 (the boot fault), errc 0; racemenu after a race
  right (pause, main menu, garage). Its whole-race A/B (bench, at the step): kept 85.9 ms a
  render, 38% of renders 90 ms or more, spread 70-105; skipped 85.2 ms, 9% at 90+, packed at
  80-89. Same mean at the step, steadier pacing skipped. Bench pacing is not the phone's.

## Phone round 19 (r19_phone 555c310f): over the whole race, skipping the re-presents wins

Owner sent 8 files (capidx 47-54, race, errc 0). The aggregate tail (A4_AGG) of the latest
capture covers the race segment: 27 windows a mode, ~1,080 renders each.
  kept    (mode 10): 94.5 ms a render  3D 35.9 (hook 18.9) comp 10.6 (read 7.0) draw 11.9 2D 6.3
                     pres 20.6 grouping 1.87  calls 244 (397 before) tris 1306  >=90 ms: 67%
  skipped (mode 14): 87.6 ms a render  3D 38.8 (hook 19.5) comp 10.8 (read 7.1) draw 12.6 2D 6.1
                     pres  1.7 grouping 2.00  calls 263 (426 before) tris 1402  >=90 ms: 23%
  histogram kept:    75-79 206, 80-84 61, 85-89 40, 90-94 360, 95-99 91, 105-114 203, 120+ 62
  histogram skipped: 75-79 61, 80-84 296, 85-89 411, 90-94 138, 95-99 35, 100+ 74
- Skipping the re-presents: -6.9 ms a render over the race (-7%) while drawing 7% MORE
  triangles; renders at or over 90 ms 67% -> 23%. At the step a render every 85.33 ms means
  94.5/85.33 = ~11% of renders kept costing a double logic step (the judder), ~3% skipped.
  The present phase saves 18.9 ms; the 3D pass grows 2.9 ms (some of the screen update
  lands there, plus the triangles) and the rest of it is spin at the step (rest 21.1 -> 30.2).
- CORRECTION to round 18's race run ("the skip does not show in the race"): two windows a
  mode were too few for scenes this different; over the whole race it shows. Rule 2 note:
  the aggregates exist because of that miss.
- The kept histogram is bimodal (75-79 and 90-94): with three presents an iteration the
  render lands on a present boundary, not on the step; skipped, it packs at 80-89.
- Grouping 1.43 us a triangle in both modes (round 18: 1.37-1.61): the prefetch did not move
  it measurably. ~100 KB read and ~100 KB written a render plus the search -- next: draw the
  runs with an index list (glDrawElements, IAT slot of ordinal 55, verified against the ROM
  in stage 4) instead of copying, as an A/B (a driver may walk a whole index range a call).
- Now (skipped, reorder): 87.6 ms a render over a race; the step is 85.33. Ours ~33 ms
  (hook CPU ~10, GL 10.6, grouping 2.0, composite 10.8), the game's ~31 (3D 19.3, 2D 6.1,
  logic), spin the rest.

## Round 20 bench: re-presents skipped always; the A/B is copy against an index list

Not a repeat: the index list has never been drawn with (stage 5's glDrawElements drew a
depth-sorted frame, a different thing). The re-present skip is now fixed on (round 19:
-6.9 ms a render over the race), the reorder too.
- AB_INDEXED (16): batch_draw writes the grouped order as indices (t*3, t*3+1, t*3+2 a
  triangle, in the copy's buffer) and draws each run with glDrawElements (IAT [27],
  ordinal 55) over the batch's own arrays -- no 100 KB copy. A4_ABCYCLE4 alternates it a
  window (modes 14 copy, 30 indexed). The aggregates' slot is now bit 0 skip, bit 1 indexed,
  marked by a format word 'AG20' after the segments (round 19's files decode as before).
  What the phone answers: the grouping's cost without the copy (us a triangle) and whether
  the driver's per-call or per-triangle cost changes with indices (it may walk an index
  range) -- the batch-draw fit against calls and triangles, by mode.
- Checks on this code, both modes: A4_REOCHECK 0 overlapping (~15 M pairs), no triangle
  lost; A4_BLITCHECK 0 of ~20,000 strips. Bench grouping 1.06 -> 0.82 ms a render (copy ->
  indexed).
- Bench, the exact binary r20_phone (sha256 c927c880b5ea02486414b29423c7b84cb2744c034b76f5ab5299a66245ffa48c):
  telerun3 230 s, framestat 5621, faults 1 (the boot fault), errc 0; whole race: copy 85.5
  ms a render, 6% at 90+; indexed 85.2, 5% (at the step); racemenu after a race right.

## Phone round 20 (r20_phone c927c880): the index list ties with the copy; tearing reported

Owner: "if by flicker you mean something akin to screen tearing, then yes, sometimes it
happens" -- not said to follow the ~3.5 s A/B rhythm. 8 race files (capidx 46-53), errc 0.
Whole race (aggregates, ~1,050 renders a mode; re-presents skipped and reorder in both):
  copy    (slot 1): 86.6 ms a render, 1254 tris, 226 calls, grouping 1.75 ms (1.40 us/tri),
                    batch draw 11.0, >=90 ms 18%
  indexed (slot 3): 86.5 ms a render, 1339 tris, 250 calls, grouping 0.88 ms (0.66 us/tri),
                    batch draw 12.2, >=90 ms 20%
- The index list halves the grouping (the copy was ~0.74 us a triangle) but the driver's
  indexed draw costs about that much more: a joint fit of the 8 windows' GL time (batch draw
  minus grouping) puts indexed at +0.9 ms a render. Net: nothing (86.5 vs 86.6 with 7% more
  triangles). The copy stays (the path with more phone hours); AB_INDEXED kept, off.
- Over the race now: 86.5 ms a render against the 85.33 step; 18-20% of renders at 90 ms or
  more. Heaviest window (46): 87.5 ms -- 3D 44.2 (hook 18.5), composite 12.8, 2D 8.8.
- The tearing: open. Candidates -- the re-present skip changes when the real present lands
  against the screen's refresh (fixed on since this round, alternating in round 19, absent
  in 18's kept windows and before); or the same thing as the lap-section lines of rounds
  15-17, which appeared with the GPU drawing nothing (round 17). Not settled by these files:
  the telemetry sees our frame, not the panel.
- Owner on the tearing: a VERTICAL split (the frame meeting the panel's scan, which on the
  N95's portrait panel runs across the landscape picture); there since the first builds, in
  the original game too and worse there ("due to performance"); much rarer now; in heavy,
  intense moments, nowhere in particular. Not new, not ours to chase now (owner: do not
  focus on it). The lap-section "lines" of rounds 15-17 are likely the same thing.

## Round 21 bench: the hook's lean path, checked word for word; the A/B is old against lean

Not a repeat: the hook's conversion code was never rewritten. Re-presents skipped, reorder
on and the copy kept (round 20) in both modes.
- Where the hook stands: the phone's hook time across the 24 race windows of rounds 18-20
  fits 6.96 us a triangle + 0.67 x batch draw (the batches drawn inside it): ~9 ms a render,
  now the largest item of ours. The game's own 3D part is 6.5-35 ms (mean 18.2); its HUD
  painter +29736 is 12.8% of all bench instructions and the sprite blit +3015E 6.0% (the 2D
  phase, 6-9 ms on the phone) -- the HUD-on-GPU item of the plan.
- A4_LEAN (AB_LEAN 32): lean_conv/lean_tail -- the three vertices read once, outputs written
  into the batch slot (no local copies), the box and extents from the same pass, the row
  extent once, tex_index's commonest outcome inline (the last hit, used this present: the
  conditions on which tex_index returns that slot untouched). A4_ABCYCLE5 alternates it;
  aggregate slot bit 1 = lean, format word 'AG21'.
- A4_LEANCHECK (bench): the lean words for every triangle against the old path's slot:
  0 of 1,395,020 differ (positions, texcoords, id, box, rows). Tested: a mutant with the wrong
  v shift on one vertex gives 66,933 differing (first at word 17); a first mutant (one term
  dropped from the extent maximum) changed no output in 497,848 triangles -- a mutant the
  data cannot reach, not a blind check. Lean forced on: REOCHECK 0, BLITCHECK 0, errc 0.
- Bench instructions a hook call: 538 -> 441 (-18%); what is left is near the minimum for
  the arithmetic (one ~120-instruction block: shifts, clamps, the 12 position words).
- Exact binary r21_phone (sha256 a3e59e23bd48ba07afc83ebb4914f034f23d81b94866aaf98cb8aabc1282a3b4):
  telerun3 230 s, framestat 5622, faults 1 (the boot fault), errc 0; racemenu after a race
  right. The phone answers what 18% fewer instructions buys there (the hook's us a
  triangle by mode).

## Phone round 21 (r21_phone a3e59e23): the lean hook path, a small win

8 race files (capidx 46-53), errc 0. Whole race (aggregates; re-presents skipped, reorder,
copy in both):
  old  (slot 1): 88.3 ms a render, 1315 tris, hook 18.6, batch draw 11.6, >=90 ms 27%
  lean (slot 3): 87.8 ms a render, 1351 tris, hook 18.0, batch draw 11.9, >=90 ms 25%
- Hook outside its batch draws (hook - 0.67 x batch draw, the round 18-20 fit): 8.2 -> 7.4 us
  a triangle (-10%), ~-1 ms a render; the frame -0.5 ms with 2.7% more triangles. Lean
  kept on. 18% fewer bench instructions bought ~10% on the phone: the hook runs at ~2,400
  cycles a triangle for ~440 instructions, so most of its phone time is not instruction
  count (stalls -- what kind is not measurable here; trimming more code is the wrong lever).
- This race ran heavier than round 20's (old 88.3 here, round 20's copy mode 86.6): races
  differ; only same-race A/B comparisons are measurements.
- Next, by size: the HUD (the game's painter +29736 and blit +3015E, 19% of bench
  instructions, the 2D phase 6-9 ms on the phone) onto the GPU with the composite moved to
  the present; then the texture atlas (~3-4 ms of calls in heavy scenes).

## Round 22 bench: the hook only records; the batch converts (the A/B is lean against deferred)

Not a repeat: the hook has never been split from its conversion. Re-presents skipped,
reorder, copy and lean on in both modes; A4_ABCYCLE6 alternates AB_DEFER (64), slot bit 1
= deferred, format word 'AG22'.
- The census of who writes into the race frame after its first composite (EKA2L1_WATCH over
  the surface, WATCHONCE, armed from a4_gl_flush): about eight routines -- the HUD painter
  +29736 (a per-pixel blitter that re-reads its palette pointer and converts a 24-bit
  palette entry for every pixel: 4/8-bit palette or 16-bit, a 4-bit alpha map, a global
  alpha, five blend modes), +26E28, the +2B5xx-+2C5xx family, +2CE90, +10A96, the sprite
  blit +3015E, the screen fill +3602B (through a ROM memset), our composite. The HUD on the
  GPU means all of them; parked behind the hook question.
- The hook question: ~2,400 phone cycles a triangle for ~440 instructions. A guess that
  fits the size: the hook's ~1.8 KB of code leaves the 16 KB instruction cache between the
  game's per-triangle calls. A4_DEFER: defer_take writes a 16-word raw record per triangle
  (kind, texture word, the mode-1 map and table read now, the three (x, y) as int16, depths,
  texcoord words) into the batch slot; batch_draw's pass 1 finds every record's texture and
  box (and the frame's drawn rows), the grouping runs as before, pass 3 converts each run's
  records straight into the arrays the GL reads (the copy gone). Quads (billboards, fills)
  record their vertices, texcoords and box. tex_index/tex_make now take the mode-1 map and
  table from the caller (they read the live context before: by batch time it describes
  another object); tri_tex/tri_conv are shared by the lean and deferred paths.
- Checks: A4_DEFERCHECK (lean_conv at the hook into a shadow, compared in pass 3): 0 of
  1,452,113 differ, 0 lost at the batch; tested -- a mutant swapping two depths in the record
  gives 448,942 differing. A4_LEANCHECK on the refactored code: 0 of 1,416,888. REOCHECK 0,
  BLITCHECK 0, errc 0.
- Exact binary r22_phone (sha256 61cb029ae7ebade6eb8f80b62e42036ab3eda4a704ba967c3d3fc837a725b5dd):
  telerun3 230 s, framestat 5513, faults 1 (the boot fault), errc 0; racemenu after a race
  right. On the phone the measure is hook + batch draw per triangle by mode (the deferred
  hook is small, its batch draw carries the conversion).

## Phone round 22 (r22_phone 61cb029a): deferring the conversion buys nothing; the cost is reading the game's data

8 race files (capidx 51-58), errc 0. Whole race (aggregates; skip, reorder, copy, lean in both):
  lean     (slot 1): 87.6 ms a render, 1287 tris, hook 18.4, batch draw 10.9 (grouping 1.82), >=90 ms 24%
  deferred (slot 3): 87.4 ms a render, 1416 tris, hook 18.7, batch draw 15.7 (grouping 5.52), >=90 ms 24%
- Our CPU outside GL (hook + the third of batch draw outside it, minus GL time): lean 10.0
  us a triangle, deferred 9.7 -- equal. The instruction-cache guess is REFUTED: a warm loop
  over the batch converts for ~2.5 us a triangle (deferred grouping 3.9 minus lean's 1.4),
  but the capture-only hook still costs ~5.8 us a triangle, and all it does is read the
  game's per-triangle data (desc, three vertex records, the texture descriptor) and write
  16 words. So the hook's phone time is mostly the first touch of the game's data -- loads
  that miss (a guess about the mechanism: the ARM1136's data cache allocates on reads only,
  so the vertices the game has just written are not in it). Deferred off; lean stays.
- Next measurements, both cheap: prefetch (PLD) the three vertex records and the texture
  descriptor at the hook's entry, so their misses overlap instead of following one another;
  and the readback in 24-row strips (10 glReadPixels a frame instead of 15) for its per-call
  overhead.

## Round 23 bench: prefetch at the hook's entry x 24-row readback strips (four modes)

Not repeats: neither was ever built. Skip, reorder, copy, lean on in all modes; deferral off.
A4_ABCYCLE7 steps AB_PLD (128) x AB_BIGSTRIP (256) one pair a window; aggregate slot bit 0 =
PLD, bit 1 = 24-row strips, format word 'AG23'.
- AB_PLD: at draw_prim_take's entry, PLD of the three vertex records and desc+0x30 (the
  texture descriptor; a flat colour there is a harmless address -- PLD never faults), so the
  first-touch misses round 22 found overlap instead of following one another.
- AB_BIGSTRIP: the readback in 24-row strips (10 glReadPixels a frame instead of 15) into
  the reorder's arrays, free once the frame's batches are drawn (30 KB of 36). The strip
  start without a divide: (x * 2731) >> 16 = x / 24 for every x in 0..239 (checked); a
  first version's loop was turned into __aeabi_uidiv by the compiler and failed to link.
- Checks: A4_STRIPCHECK -- every composited frame re-read in 16-row strips and compared with
  what the 24-row composite wrote: 0 of 14,298 strips' drawn pixels differ; tested (a mutant
  reading one row off: 10.4 M differing). REOCHECK 0, BLITCHECK 0, errc 0.
- Exact binary r23_phone (sha256 ba10fe0bdd3171a60d2713f01a483708be150c9f58f65b30b4c6a31e920b21a4):
  telerun3 230 s, framestat 5652, faults 1 (the boot fault), errc 0, all four modes;
  racemenu after a race right. (Bench reads are host-timed: only the phone measures them.)

## Phone round 23 (r23_phone ba10fe0b): neither the prefetch nor the bigger strips measure

8 race files (capidx 47-54), errc 0. Whole race, four modes (~520-573 renders each):
  no PLD, 16 rows: 87.0 ms a render, 1328 tris, hook 20.3, batch draw 11.9, read 7.0
  PLD,    16 rows: 87.9 ms,          1370 tris, hook 18.9, batch draw 11.9, read 7.1
  no PLD, 24 rows: 88.9 ms,          1227 tris, hook 16.8, batch draw 10.5, read 7.0
  PLD,    24 rows: 88.3 ms,          1314 tris, hook 19.1, batch draw 11.8, read 7.0
- 24-row strips: the read is 7.0 ms either way -- glReadPixels has no per-call overhead
  worth having; its cost is the pixels. Off.
- Prefetch: the hook outside its batch draws 9.3 -> 8.0 us a triangle with 16-row strips but
  8.0 -> 8.5 with 24-row: the sign changes between pairs, mean -0.4 us (-5%) -- inside a
  race's noise (scenes per mode differ by +-1 ms a render at ~550 renders). Not shown; off.
  What remains of the hook's first-touch cost is not cured by asking early (a guess why: the
  misses already overlap as far as the core allows, or the cost is elsewhere -- persp_fallback's
  three reads into its 128 KB table, m1_table's context reads).
- Where it stands (these races were heavy): 87-89 ms a render, ~25-31% of renders at 90 ms
  or more; round 16's heavy windows were 93-108. Each remaining item of ours is 1-3 ms; next
  the texture atlas (~2 ms of calls, the only GPU-side cut left that is understood).

## Round 24 bench: the texture atlas (the A/B is with and without)

Not a repeat: the atlas was never built. Skip, reorder, copy, lean on in both modes;
A4_ABCYCLE8 alternates AB_ATLAS (512), slot bit 1, format word 'AG24'.
- A4_ATLAS: two shared textures -- [0] RGB565 for opaque textures, [1] RGBA5551 for colour-
  keyed ones (each texture's own format, tex_make's tr) -- 1024x1024 each (4 MB of texture
  memory), falling back to 1024x512 each if the device refuses (GL error at creation; 0x412D00
  [118] says which: 1 = 1024x1024, 3 = 1024x512, else the error). A triangle draws from the
  atlas when its kind is TEX, TEXZ or ADD (ALPHA's texture is RGBA8888; mode 1 is re-baked),
  its texture is a power of two up to 128x128, and all three texcoords lie in [0, 1]; its id is
  then the atlas's, so runs of different textures merge. Copies are padded with a texel of
  the opposite edge (what GL_REPEAT reads one texel out), built in the reorder's arrays (free
  in the hook), shelves by padded height; a texture remade (hash, eviction) marks its copy
  stale, re-uploaded in place if the size matches; a refusal (full) is remembered per slot.
- Sizes: the census (seqan, reorder + atlas, formats lumped): 210 calls a frame -> 153 with
  textures up to 64, 125 up to 128. A first build at 1024x512 each filled after 118 copies and
  refused 16,440 triangles (and asked again each time); at 1024x1024: 148 copies placed, none
  refused, in a 230 s bench race.
- Exactness: not bit-exact, and cannot be. Two bench runs without the atlas differ only in
  the random traffic; with it, ~50 isolated pixels a frame more (~0.1%) -- a sample on a texel
  boundary rounded the other way (the same texcoords interpolated at the atlas's scale, 1/64
  of a texel per GL_FIXED step against 1/1024). Same texture, neighbouring texel. REOCHECK 0,
  BLITCHECK 0, errc 0.
- Bench whole race (exact binary below): calls a render 212 -> 170 (-20%, with 8% more
  triangles: 0.155 -> 0.115 calls a triangle, -26%).
- Exact binary r24_phone (sha256 f9251ecdc10e32abd9fc6756c213cef9c2dd6bbcaa944e1181c6b02481453e9f):
  telerun3 230 s, framestat 5613, faults 1 (the boot fault), errc 0; racemenu after a race
  right (garage textures included). Named risk: the 4 MB of atlas memory on the phone (the
  fallback halves it; telemetry [118] reports which).

## Phone round 24 (r24_phone f9251ecd): the atlas cuts calls, but its uploads stall the start of the race

Owner: no texture looked wrong; at the start of the race heavy frame drops, "especially
whenever I used a turbo or drift (not exclusively)"; after a while as smooth as the rounds
before. 8 race files (capidx 46-53), errc 0; [118] = 1: both atlases made at 1024x1024 on the
N95, 150 copies placed, none refused.
  whole race, no atlas: 86.7 ms a render, calls 242 (391 before grouping), 1346 tris, hook
                        18.5, >=90 ms 17%, 1 render at 130+
  whole race, atlas:   101.0 ms a render, calls 180 (317 before), 1405 tris, hook 28.6, 3D
                        50.2, >=90 ms 24%, 36 renders at 130+
  last 8 windows (steady): atlas 83.2, 89.5, 83.3, 84.4 ms with 128-188 calls; none 84.7,
                        84.7, 85.9, 83.9 with 203-271 calls
- Calls a triangle -26% as the bench said; the frame time is lost to stalls early in the race
  (the 130+ bin, the hook +10 ms a render on average): the copies are uploaded inside the
  hook, mid-frame, into a 2 MB texture that draws already submitted this frame use -- a tile
  renderer's driver has to keep the old contents for those (a copy of the texture, or a
  wait) at every upload (a guess at the mechanism; the timing is what is measured). 150 at
  the start, more with each new effect (nitro, drift sparks), then none: the owner's report.
- The no-atlas half is the best whole race on record: 86.7 ms, 17% at 90+.
- Fix to measure: atlas copies placed when first wanted, but uploaded after the frame's
  composite (its glReadPixels has finished the frame's GPU work), at most 4 a frame; until
  then the triangle draws from its own texture.

## Round 25 bench: the atlas copies uploaded after the composite (the A/B again)

Not a repeat: round 24's atlas uploaded mid-frame; only that changes. Same cycle (A4_ABCYCLE8,
'AG24').
- atl_get places a copy when first wanted and queues it (ATLST_PENDING; queue of 32); the
  triangle draws from its own texture meanwhile. atl_flush_pending, at the end of a4_gl_flush
  after composite_rows (its glReadPixels has finished the frame's GPU work), uploads up to 4
  (ATL_PER_FRAME) oldest first; an entry whose slot was remade or reused since is dropped and
  its place left. [119] counts the uploads.
- Bench (atlas forced): every placed copy uploaded after a composite (151 of 151), ~900 K
  triangles drawn from the atlas, none refused; REOCHECK 0, BLITCHECK 0, errc 0. Race-start
  screenshots against a run without the atlas: no wrong texture anywhere; the differences
  are the start's fade-in and speed at slightly different moments (0.15-0.5 s apart), 3-7 K
  pixels where the moments match. States the bench did not enter: a full queue (32 waiting),
  a slot remade while its copy waits -- both return the triangle to its own texture.
- Exact binary r25_phone (sha256 6eda2eeb3700ac2ca6cf3eff3e24628e9b3fe5a47556d2537f3fa202becc439f):
  telerun3 230 s, framestat 5618, faults 1 (the boot fault), errc 0; whole race calls a render
  229 -> 164 with the atlas (1473 vs 1464 tris); racemenu after a race right.

## Phone round 25 (r25_phone 6eda2eeb): the atlas's uploads cost ~100 ms each wherever they go -- atlas off

Owner: the start-of-race frame drops are still there. 8 race files (capidx 40-47), errc 0;
151 copies placed, 151 uploaded after a composite (as designed).
  whole race, no atlas:  90.7 ms a render, calls 259, 1425 tris, 2D 6.8, >=90 ms 33%, 6 at 130+
  whole race, atlas:    108.8 ms a render, calls 176, 1384 tris, 2D 23.2, >=90 ms 28%, 68 at 130+
  last 8 windows: atlas 83.2, 84.0, 85.2, 84.4 ms (2D 4.8-6.4: no uploads left); none 90.5,
  86.4, 84.3, 83.4
- The uploads now land in the 2D phase (atl_flush_pending runs after the composite's mark):
  (23.2 - 6.5) ms x 962 renders / 151 uploads = ~106 ms an upload. Round 24's mid-frame
  uploads: hook +10 ms x 1080 / 150 = ~72 ms each. So the cost is not a frame's
  synchronisation (round 24's guess, REFUTED) but a glTexSubImage2D into a 1024x1024 texture
  itself: the N95's driver spends ~0.1 s on one (a guess at why: it re-processes the whole
  texture on any update). A texture that grows during a race cannot be had on this driver;
  one built whole at a load could, but needs a 2 MB staging buffer the blob does not have.
  AB_ATLAS off; the code stays.
- Same kind of cost, found on the way: a mode-1 re-bake of the 256x256 car body (65,536
  texels, tex_upload through glTexSubImage2D) costs ~31 ms (window 41: 5 re-bakes, 327,680
  texels, 155 ticks); 50-100 a race, when the reflection table changes -- each a frame spike,
  in every build since round 10.
- Telemetry itself: every capture writes 155 KB to C: (every 40 renders, ~3.5 s). The
  windows leave the write out by design, so no measurement here shows what it costs the
  player -- a hitch every ~3.5 s is the likely price. A build without telemetry is the
  thing to play.

## Play build 1 (no telemetry): the best configuration, to be judged by playing

The settled configuration with no instruments: native readback, re-presents skipped (round
19), the reorder with the copy (rounds 18, 20), the lean hook (round 21); no atlas (round
25), no deferral (22), no prefetch, 16-row strips (23); no A4_TEL (no captures written, no
hook timer), no A4_AGG, no A4_BBPROBE. Flags: A4_HPERSP A4_TAKEDEF A4_TAKEFLAT A4_TAKEADD
A4_TAKESHADOW A4_TAKEALPHA A4_ZBUF A4_IMM A4_SHADZ CLIP_TOP=0 A4_TAKEBB A4_TAKEFILL
A4_REORDER A4_SKIPREPRES A4_LEAN A4_ABMODE=46u, --gl --capture.
- Exact binary play1 (sha256 5c625ee59e950ff542e98832e836485849f8b1d1731482523fd6452f5c34ce0b):
  telerun3 230 s, framestat 5394, faults 1 (the boot fault); racemenu after a race right; the
  GPU path live mid-race (glr by EKA2L1_MEMSNAP: up = MAGIC, 131 textures, triangles counted,
  errc 0).
- What it answers: how the game feels without the capture writes (a 155 KB file every ~3.5 s,
  never measured by the windows). The owner's eyes are the instrument; there is no
  telemetry in it.
- Owner on play build 1: "feels kinda the same as the test builds prior to the start-of-race
  frame drop bug: smooth in general, frame dropping on heavy scenes. It doesn't hold a
  constant frame rate across heavy and light scenes. But it definitely feels better
  compared to stock game." So the capture writes were not a felt cost of note; heavy scenes
  remain over the 85.33 ms step.
- Where the remaining time is, per render in a heavy race (rounds 21-25): ours in the 3D pass
  ~24 ms at ~1,400 triangles (~17 us a triangle: the hook ~8.6, of which ~5.8 is the first
  touch of the game's per-triangle data; grouping 1.4; GL 1.5 a triangle + ~31 us a call),
  the composite ~10.7 (read 7); the game's own 3D ~18, 2D (HUD) ~6, logic ~20. Each
  incremental item left is 1-3 ms; holding the step in heavy scenes needs ~5-15 ms, i.e.
  ours cut per triangle at the source -- not reading the game's per-triangle descriptors at
  all (taking its objects' vertex arrays at a higher level), which is a reverse-engineering
  project of its own.
- Owner, after: "the garage menu is way smoother than stock game, that's for sure" -- the
  garage has no 85 ms gate, so every millisecond saved shows there (round 18's garage A/B:
  ~56 ms a render with the re-presents skipped against ~73 kept; stock garage was never timed).

## Decision: the structural step (owner's choice)

Asked: two targeted fixes (the HUD painter rewrite, the car-body re-bake spikes), the
structural step, both, or stop and polish. Owner: the structural step -- stop reading the
game's per-triangle data and take whole objects to the GPU at a higher level of its 3D
pipeline. First, bench reverse engineering: who calls draw_primitive (+300AE), the per-object
loop, the vertex arrays it holds (format, transformed to screen with w?), where culling and
clipping happen, and how a face picks its texture and rasterizer class.

## Structural step, map 1: the game's 3D pipeline (static, from the binary and its RTTI)

Source: the code section (tdis.py) and the RTTI names in it (typeinfo -> class-name strings).
- Classes (Lib3D): CMesh, CBasicMesh (sub-meshes: array +0x38, count +0x3C), COTObject (an
  ordering-table object: vtable {sort key, draw}), TFace : COTObject (vtable +B08EC: sort key
  +3121E = minus the deepest view z of its vertices; draw +31246), TFakeZFace : TFace (vtable
  +B08CC: its own key +522C2, same draw), TBillboard : COTObject (vtable +B08BC: key +31368,
  draw +312E6). The two-entry vtables of CMesh/CBasicMesh are their destructors.
- Per mesh per frame: CBasicMesh +32C8C -> +34AEA -> +349F8(ctx, verts, nverts, faces, nfaces,
  mode, ...): +345E4 transforms every vertex (40-byte records) to view space and sets clip
  flags (+0x24); then for each face (52-byte TFace records, the mesh's own array): rejected if
  its three vertices' flags AND to non-zero (all outside one plane); skipped if (OR & 0x20);
  optional 3D back-face test (+3118E); face flags (+4) set from a per-mode table, a near-clip
  bit if (OR & 0x10), texture bits from the descriptor (+0x28 byte != 0xFF, +0x2C alpha map);
  then +349B0 inserts it into the ordering table.
- Drawing: the table walked back to front, face draw +31246 sets ctx[0x64] (the span mode)
  from the face flags and takes one of three paths -- +35D3A (near-plane clipping: may emit
  two triangles), +347EA (2D back-face cull), +34798 (plain); each projects any vertex not yet
  projected (vertex +8 == 0) with +51E44(ctx, v+0xC, v, camera params) and calls
  draw_primitive +300AE -- our hook.
- Records: TFace = {vtable, flags, ?, v0, U0, V0, v1, U1, V1, v2, U2, V2, texture descriptor}
  (the "desc" draw_primitive gets IS the mesh's persistent face record); vertex = {screen x,
  y, zw (0 = not yet projected), view x, y, z (+0xC..+0x14), ..., clip flags (+0x24)}.
- What the step means concretely: per mesh, keep the static part (faces' vertex indices,
  texcoords, textures, classes) in our own arrays once, and each frame hand the GPU the mesh
  with its transform -- no ordering-table insertion, sort, projection, clipping, per-face
  hook, nor per-face reading of the game's records. Open: the projection's parameters
  (+51E44 and the camera params), where a mesh's object-to-view matrix comes from (+345E4),
  which faces depend on painter's order (TFakeZFace: decals, forced order; the blended
  classes), and depth-buffer equivalence for the rest.

## Structural step, map 2: projection, camera, census (bench measurements + the binary)

Sources: the code section (tdis.py) for the formulas; bench dumps (EKA2L1_PCMEM, now with
`EKA2L1_PCMEM_ARM` = the race minimap +26E28 so only race frames are recorded, and the
`:off` / `:+off` forms for a pointer member or a deep offset; EMU_PATCHES.md) for values.
- **Transform** (+345E4, per vertex, 40-byte records): object xyz at v+0x18..0x20 through
  the matrix cam[0x3c] (12 words: x' = (m0 x + m3 y + m6 z) >> 14 + m9, y' = rows m1 m4 m7
  + m10, z' = m2 m5 m8 + m11; +523A2/+523C4/+523E6) into v+0xC..0x14; v+8 = 0 (not
  projected); flags v+0x24: 0x20 if z' outside (-29900, 6024] (the face is then dropped,
  not clipped), 0x10 if z' > -64 (near), 1/2/4/8 from four planes in cam[0xc..0x34]
  (measured: |y'/z'| = 16576/63405 = 0.2614, |x'/z'| = 21572/61884 = 0.3486 -- 4:3,
  half-angles 14.6 and 19.2 degrees); cam[0] = AND of all flags (non-zero: whole mesh out).
- **The "camera" is the mesh context**: the object +349F8 gets as ctx (cam) is the one the
  face draw passes to the projection; F = cam[4] (focal), constant within a frame, 0x1CC
  (460) at the race start then 0x104 (260) in the bench window -- it changes across frames.
  The renderer object R = {gctx (rasterizer context), cam}; draw_primitive gets gctx.
- **Projection** (+51E44, lazily at face draw): zw = -z'; r = RT2[-z'] (gctx+0x2008C, a
  second table after the perspective span's RT at gctx+0x8C) -- **RT2[z] = floor(2^22/z)**,
  all 16,383 entries checked (RT at +0x8C is floor(2^18/z), all 16,347 checked); r = r*F >>
  6; sx = cx + ((x' r + 0x8000) >> 16), sy = cy - ((y' r + 0x8000) >> 16), i.e. sx ~ cx +
  F x'/(-z'): integer pixels. cx, cy = *gctx[0x40110] >> 1, *gctx[0x40114] >> 1. Near
  vertices (z' > -512) go to +51F08 (same formula, x', y', z' pre-shifted 4-7 bits for
  precision). F = 460 puts |x'/z'| = 0.3486 at 160 px: the planes are the screen edges.
- **Face flags** (face+4, from the per-mode table at +AC860 when inserted, then
  draw_primitive's class flags): bit 0 perspective class (KIND_TEXZ unless the affine
  fallback), bit 12 clip (near), bit 14 2D back-face cull, bits 20/24/26/27 set the span
  mode ctx[0x64] = 1/3/2/4 at draw. Mode 0 (normal) = 0x4001, mode 1 = 0x4000, 2 =
  0x104000, 3 = 0x804000, 4 = 0x1004000, 6 = 0x5000; the near-clip variants add 0x1000 (and
  0x400000 for mode 0). Modes 0/6 then take texture bits from the descriptor: +0x28 byte !=
  0xFF -> |0x10040 & ~1, +0x2C alpha map -> |0x10100 & ~1. Face draw +31246: a bit-0 face
  goes to +35D3A, which back-face culls in 2D when asked and CLIPS IN 3D against all
  planes whenever any vertex flag is set; other faces clip only when bit 12 is set.
- **Two kinds of mesh into +349F8**: (a) +CE8C, ~89 calls a frame: compact static objects
  (int16 xyz verts at obj+0x20, 10-byte faces at obj+0x28: indices b1 b0 b2, texture index
  b3, texcoords b4..b9 << 4), unpacked EVERY call into frame-scratch vertex and face arrays
  (+31D28, +31D02), mode 0, no 3D back-face test; 92 distinct objects, 4-32 faces each;
  (b) +34AEA (CBasicMesh: verts +4, n +8, faces +0xC, n +0x10), ~7 a frame: the cars (393
  verts/702 faces -- the player's -- and 171-248 verts/272-376 faces), persistent arrays.
- **Census** (18 race frames, play1.exe, insert sites' return blocks): ~2,013 face draws a
  frame (+31246), ~1,232 draw_primitive calls; inserts by site: +349F8 1,582 a frame (79%;
  of 2,845 faces submitted: 781 from +CE8C objects, 2,064 from the cars), +27402 328 (16%,
  its own transform +346B0: the road, a guess), +34F26/+34F80 112 each, +35156 50, +350E2 16,
  +35356 14. 
- **Ordinals for the GPU projection** (rank 5 table cross-checked: register.cpp dispatch
  order is the alphabetical DEF freeze, and 48 glDepthFunc / 52 glDisable / 55
  glDrawElements / 90 glMatrixMode from the same mapping already run on the N95):
  glLoadMatrixx 84, glDepthMask 49.

Design (decided from the above; nothing built yet):
- Replace +349F8 by an exact C reimplementation (transform by the game's own +345E4, the
  same culls, flags and inserts) that diverts each opaque face whose three vertices are in
  front of z' = -128 (ZK, so the GPU never near-clips) and fit int16 into a frame-wide
  per-key list: view x'y'z' (GL_SHORT) + the face's own texcoord words (GL_SHORT, scaled
  by a per-key texture matrix, 1/(16 size)) -- no projection, no ordering table, no
  per-face hook. Drawn with one projection matrix for the frame (x_c = 2F/W x' + (2cx/W -
  1)(-z'), y_c = 2F/H y' + (1 - 2cy/H)(-z'), z_c = ZK, w_c = -z': the same window depth
  0.5 + ZK/ZW/2 as the hook's KIND_TEXZ) and the depth test on, writing; one draw call per
  key for the whole frame, before the ordering table's first triangle.
- Everything the ordering table still draws then depth-tests GEQUAL at its true depth
  without writing (KIND_TEXZ still writes): painter's among themselves as now, hidden
  behind a nearer mesh face, drawn over a farther one.
- Shadows: the depth stamp (A4_SHADZ) cannot be kept -- a shadow passes NOTEQUAL over a car
  drawn first. Instead the stamp moves to dst alpha (the phone's config: alpha 8, round
  11): blend GL_DST_COLOR / GL_ONE_MINUS_DST_ALPHA with src (0.75, 0.75, 0.75, 0.75) gives
  dst*0.75 where alpha is 1 and leaves it (alpha 0.75 stays 0.75) where a shadow already
  darkened; alpha 0 (the backdrop) stays 0. Then shadows can depth-test at their true
  depth, biased slightly toward the eye so the road under them does not fight them.
- Open before building: the billboard blit has no depth (z 0), and must test against the
  meshes -- TBillboard's key bb[0x18] is its depth (+31368), to be passed from +312E6;
  coplanar faces of different keys in one mesh (decals) would z-fight; TFakeZFace faces
  stay in the ordering table.

## Mesh bench 1: the list built and checked against software (A4_MESH, bench only)

Built (a4_gl.c A4_MESH; injector MESH_HOOKS): the `bl +349B0` at +34ADC becomes a BLX to
a4_mesh_insert(cam, face), which keeps a face for the frame's list or calls +349B0 itself
(placeholders a4_otins/a4_tfvt patched to the game's insert and TFace's vtable, checked in
the built image: code+349B1, code+B08EC). The list (0x423000-0x43B000, 2048 triangles,
GL_SHORT x'y'z' + texcoord words) is drawn at the frame's first triangle after the clear,
one glDrawElements a texture, projection and texture matrices by glLoadMatrixx (ord 84),
depth test on and writing, GL_CULL_FACE; in that frame the table's classes test their true
depth (tri_conv computes zq for all of them; quads at window depth 1), only KIND_TEXZ writes
(glDepthMask, ord 49), shadows stamp dst alpha instead of depth (DST_COLOR /
ONE_MINUS_DST_ALPHA, src 3/4) at a depth 1/64 nearer. Back faces are dropped at the insert
by det[v0 v1 v2] >= 0 (view space; the screen area is F^2 det / (w0 w1 w2), w > 0).
A4_MESHCHECK (with A4_DUALCAP): the list's faces go to the table as well and the hook
leaves them off the GPU, so the GPU picture of a frame is the mesh path's and software's is
the stock one; A4_DUALCAP's lean path now leaves software drawing (it returned 1 there).

Bench (RM-320, telerun3 40 s, play1 flags + A4_TEL; m0 = without A4_MESH):
- Live: ~1,530-1,570 list triangles a render in ~29 draw calls; draw calls a render 132-136
  against 236; 0 lost; errc 0; framestat 1269-1299, faults 1 (the boot fault) in every run.
- Pictures: a normal race in all eight captures of each build (cars, buildings, trees,
  road); a picture is not a measurement, so the same-frame check:
  mesh check vs software: pixels differing 25-37% a capture, 8x8 object-sized blocks
  0.5-12.9%; the current renderer (m0 + DUALCAP) vs software: 25-39%, 0.5-12.0% -- the same
  level, with the same kinds of block (glows over the sky, skid marks, the 2D tags and
  banner). One difference was the list's: the player car's rear window lost its
  reflection (trees) and the sparks over it.
- Class-painted capture (A4_KINDVIS, now also painting KIND_ALPHA yellow): the reflection
  stays textured -- not a blended class. Faces sharing all three vertex records among the
  mesh routine's inserts: none (823 in a frame). Leaving the 0x104000 class (span mode 1)
  out of the list (MESH_EXCL=4): the window right again.
- Why: ctx[0x5c] (the map) is constant through every walk (four frames, every
  draw_primitive), but the mode-1 table ctx[0x60] is 0xD0E9C0 -- the render target, the
  capture header's bb. Its contents are one fixed image for most of the walk and change,
  frame by frame, about 200-250 draws before its end (where the player's car is drawn;
  writers include +360A2, which writes the surface ctx[0x4c] in rows after two +522F0
  transforms). Mode 1 adds a quarter of a framebuffer pixel picked by the second map: the
  paint and window reflection samples the frame being drawn. The hook bakes it when the car
  is drawn; the list baked it at the walk's start, against other contents.
- So the list leaves 0x104000 to the table (A4_MESH_M1 takes it, for later: a bake deferred
  to the frame's end, used one frame late, is the candidate). It was 22% of the mesh
  routine's inserts in the round-2 census (9,379 of 42,832).

## Round 26 bench: the mesh list A/B (A4_ABCYCLE9), the phone candidate

Build r26_phone: round 25's flags without A4_ATLAS, plus A4_MESH and A4_ABCYCLE9 (a4_tel.c
alternates AB_MESH, 1024, per capture window; native, skip, reorder, lean always on;
aggregates' slot bit 1 = AB_MESH, format word 'AG25'). A4_SCRATCH_SIZE 0x3A000 (bss 0x3AF08,
236 KB). In a mesh build the header's words 38/43/46 are the window's list triangles, ticks
drawing the list, and list draw calls (a4tel_decode.py prints them when the mode has 1024).
The mode-1 class stays in the table (mesh bench 1). IAT: glLoadMatrixx 84, glDepthMask 49.
- Exact binary r26_phone (sha256 3035eb25408aa6894e65079b7458c2a570c2baf6644e8e3467a6e72a58df2757):
  telerun3 230 s: framestat 5462, faults 1 (the boot fault), errc 0 in every capture, both
  modes; racemenu after a race: pause, main menu, career garage -- the same screens as play
  build 1's battery, the garage car right (list on in its windows).
- Bench aggregates (llvmpipe; the bench is held at the 85 ms step, so the total cannot move
  here -- per render, ~32 windows each):
  no list: 85.6 ms | 3D 14.6 (hook 7.0) comp 13.5 (read 8.4) draw 1.8 2D 7.3 grouping 1.12 |
           calls 222, triangles 1,347
  list:    85.1 ms | 3D 9.1 (hook 4.3) comp 12.1 (read 6.9) draw 1.0 2D 7.3 grouping 0.63 |
           calls 125 (of them the list's 22-36), triangles 1,683 (the list's 709-1,205)
  More triangles reach GL with the list: back faces are dropped at the insert, but faces the
  game's draw drops at zero screen area (integer-rounded projection) are not -- ~+340 a
  render, cost unmeasured on the phone (a guess: ~1.5 us each from rounds 21-25's fit).
- What only the phone can answer: the per-render cost there (cache misses, not instructions,
  are its cost model); whether the MBX draws the list right (GL_SHORT positions through a
  loaded projection, the texture matrix, GL_CULL_FACE, depth with the table's classes, the
  alpha-stamp shadows); whether ordinals 84 and 49 resolve on the N95's libgles_cm; whether a
  236 KB bss loads (the one bss that broke the game was ~372 KB, on the bench, at load).
- Not a repeat of: rounds 18-25 (every one a variant of the per-triangle hook path); stage 5
  (depth linear in ZW, the whole frame sorted by texture -- the road lost on the phone): this
  keeps the table's own order for everything not in the list, and its depth is 1/w as
  A4_ZBUF's, which the phone has drawn right since round 10.

## Phone round 26 (r26_phone 3035eb25): the list holds the step; the tunnel signs and car lights

Telemetry (capidx 78-85, errc 0, pbuffer config 1, depth 24; the new ordinals resolved and
the 236 KB bss loaded -- the game starts and runs). Race aggregates (seg0, 25-26 windows a
mode), render-to-render histogram (bin centres):
  no list: mean ~86.4 ms, >=90 ms 19%, >=95 7.8%, >=100 5.2% | 3D 34.5 (hook 20.0), draw 11.5,
           comp 10.5, calls 241, triangles 1,356
  list:    mean ~85.2 ms, >=90 ms 11%, >=95 1.5%, >=100 0.9% | 3D 22.3 (hook 12.5), draw 5.7,
           comp 10.2, calls 124, triangles 1,647
(The window-time means, 86.8 vs 88.0 ms, disagree with the histograms; they include
non-render loop time -- the list windows spin more, 14.65 presents a render against 10.60 --
so the histogram is the measure.) The 3D pass is 12.2 ms shorter a render with the list; the
slow frames nearly gone. Best test so far: this, the list half.
Owner: runs fine; with each switch some effects vary; (1) in the LA tunnel, three groups of
signs hanging from the ceiling show in one mode and are pitch-black in the other; (2) in an
earlier run (Monaco, he thinks) car lights were seen through cars.

Bench investigation (tunrun.sh in the scratchpad: telerun3 that snapshots the framebuffer
every 0.5 s and stops a set time after the tunnel entrance; the throttle-only race does
complete laps and passes the LA tunnel):
- (1) reproduced: list snapshots show the exit gantry's round signs black, no-list ones show
  them red and white. Same-frame captures (A4_MESHCHECK + A4_DUALCAP, plus A4_DUALALPHA,
  new: pixels the composite would not take painted magenta): with the list the gantry's beam
  is drawn over the lower half of the round signs and they come out dark; without it, and in
  software, the signs are drawn over the beam. List faces painted flat green (MESH_VIS, new):
  the far signs are list faces. Leaving 0x4001 out of the list (MESH_EXCL=1): the gantry
  right. A 1/64 depth margin in the table's favour (now in: the list's z_c is ZK less 1/64)
  did not fix it.
- Cause: the perspective rasterizer's affine fallback (persp_fallback, rounds 10-11): a far,
  nearly flat 0x4001 face neither tests nor writes the game's depth -- it is painted in table
  order -- and the signs, hanging inside the beam's depth range, come after it in that order.
  The list depth-tests them. mesh_fallback (new) runs the same rule at the insert on the
  vertices +51E44 would make (mesh_sy: its integer projection, near pre-shifts included):
  bench check, 470,152 faces compared at the draw against the hook's own persp_fallback: 0
  differing; 452,032 of them (96%) fall back.
- So the list's bulk is fallback faces: leaving them to the table (A4_MESH_FALLBACK, a switch,
  off) is exact but shrinks the list from ~1,530 triangles a render to ~310 (1,132 a render
  fall back) -- most of round 26's gain gone. Not taken as the fix.
- (2): billboard sprites were queued at window depth 1 (always pass) in a list frame. Before
  the list, a nearer car drawn after them in table order covered them; now the car's list
  faces are drawn first and the sprites land on them. Fix (in): a TBillboard's sprite gets its
  depth -- the blit's caller is read from its frame (lr at f+0x8C == +31365, TBillboard's
  draw; r1 at f+0x70 = bb+0x10), bb[0x18] is its distance (bench: 470-1886 a race, sort key
  +31368, ZW units), positions (x zw, y zw, -min(ZK, zw), zw). Other sprites and fills keep
  depth 1. Not seen on the bench (no car lights in the bench race); a guess until the phone.
- Next: the list must keep the game's table order for these faces. Plan: at the insert,
  project the three vertices (the game's integer arithmetic, as mesh_sy) and take the face's
  table key; at the draw, sort the list back to front by key and group same-texture runs only
  where screen boxes do not overlap (the reorder's rule), drawn with depth writes but no test
  (painter's, as the game draws them), the table's classes then testing against it as now.
  The CPU saving (no table insert, walk, projection or hook for these faces) stays; the draw
  calls rise toward the table's.

## Round 27 bench: Monaco on the bench, the list in table order

Owner (after round 26): test the car lights on the bench on a track that triggers them (New
York, Paris, Monaco).
- A fresh bench career has only Los Angeles (New York "unlocked at $40,000", Paris $180,000;
  the city screen shows the cash, $0; the throttle-only car never places, so no cash). The
  city price is display only (+3A132, read by the city screen +3E74C); the lock is +3A15C,
  which returns 1 at once when a global byte (game global + 0x44C5) is set and otherwise asks
  the city's own state. A4_BENCH_UNLOCK=1 (injector, BENCH ONLY -- never in a phone build)
  makes +3A15C return 1: every city opens (Los Angeles, New York, Paris, Monte Carlo, Dubai,
  Shanghai). tunrun.sh's MENUKEYS drives Career > Start Race > a city; Monte Carlo is a sunset
  race with a tunnel, police cars and light sprites.
- Light sprites in Monaco, same-frame (A4_BBRECTS, new, bench: each TBillboard sprite's
  rectangle written after the GPU picture; bbrects.py counts GPU pixels inside them that differ
  from software). Test the test: the first old-handling run recorded no rectangles (the record
  keyed on the depth MESH_NOBBZ switches off); fixed to record the detected sprite either way.
  Old handling 39.6% of 29,448 pixels differing, new 32.4% of 53,280 -- different frames, so
  not a measurement; the most-differing sprites in both are the player's nitro flame and white
  light streaks (distance 1,242-1,656), differing in blending, placed as software places them.
  No light-behind-a-car case in these 20 captures: the fix stays a guess until the phone.
- The list in table order (the plan from round 26), built: at the insert each face's three
  vertices go through the game's integer projection (mesh_scr) for a screen box, and its bucket
  is +349B0's (deepest ZW >> 3); at the draw a counting sort puts the list back to front,
  newest first within a bucket, and same-texture runs are grouped only where boxes miss every
  run passed (the reorder's rule, REO_SCAN). Drawn in that order writing depth: the fallback
  and default classes untested (GL_ALWAYS), the 0x4001 faces that keep their perspective span
  tested (GEQUAL) -- the game's own rule for each; the table's classes then test against it as
  before. Correction to round 26: at the insert the perspective-span 0x4001 faces are common
  (~900 a render, with the fallback ones ~1,060); the "96% fall back" was counted at the hook,
  where large near faces arrive clipped under another descriptor and were not compared.
- LA bench, 40 s: ~765 list triangles a render in ~78 list calls (round 26's unordered list:
  ~29); total calls 218 against 222 without the list. Pictures normal. Exit gantry, same-frame:
  the round signs right on the GPU (red rings, white centres, warning triangles).
- Exact binary r27_phone (round 26's flags, A4_SCRATCH_SIZE 0x46000, bss 0x46F08 = 284 KB;
  sha256 1321eca91169b87b6fddc8be3f4747395b9e6be869e81aa3452f984d6b24d305): telerun3 230 s
  framestat 5050, faults 1 (the boot fault), errc 0; aggregates (31-32 windows a mode):
    no list: 85.4 ms | 3D 16.9 (hook 8.6) draw 2.0 grouping 1.23 | calls 228, tris 1,421
    list:    85.3 ms | 3D 11.1 (hook 5.4) draw 1.0 grouping 0.59 | calls 208, tris 1,760
  (list windows: 525-1,992 list triangles, 66-292 list calls, 1.2-4.1 ms in mesh_draw a
  render); racemenu after a race: pause, main menu, career home, the car browser -- right.
- Expected on the phone, a guess: less than round 26's 12 ms off the 3D pass, since the list's
  draw calls are back near the table's (~31 us each there); the CPU half of the saving stays.
- Named risks: bss 284 KB (the one that broke at load was ~372 KB; this boots on the bench);
  the sprite depth unverified where it matters (light behind a car).

## Phone round 27 (r27_phone 1321eca9): car lights right; tunnel signs' flashing and pickups wrong in one mode

Telemetry (capidx 41-48, errc 0; LA and Paris races). Race aggregates (seg0, 23-24 windows a
mode), render-to-render histogram:
  no list: mean ~87.1 ms, >=90 ms 19%, >=95 6.9%, >=100 3.4% | 3D 35.7 (hook 18.9), draw 11.9,
           comp 10.5, calls 250, triangles 1,407
  list:    mean ~86.1 ms, >=90 ms 16%, >=95 5.5%, >=100 2.9% | 3D 29.8 (hook 18.0), draw 6.1,
           comp 10.6, calls 231, triangles 1,745
(list windows: 353-628 list triangles a render in 29-127 list calls, 2.4-6.9 ms in mesh_draw.)
The ordered list keeps 5.9 ms of the 3D pass against round 26's 12.2 (the expected loss: its
calls are back at 231 against round 26's 124), and the hook is back near no-list's (18.0 vs
18.9; round 26 12.5). Best test still round 26's list half for speed; this one for picture.
Owner: game feels smooth. (1) Car lights are now good (the TBillboard depth, round 26's fix
(2), confirmed on the phone). (2) Tunnel signs: in one mode they flash intermittently as they
should; in the other they either stay lit or stay pitch-black. (3) Paris: in one mode the
pickups and the turbo render right, in the other they look like they clip into the road a
bit (two photos: the pickup sprite and the turbo streak at night). The owner cannot tell
which mode is which; both are assumed to be the list's, to be confirmed on the bench.

## Round 28 bench: the tunnel signs and the sinking sprites (from phone round 27)

Not repeats of: round 26's tunnel investigation (MESHCHECK same-frame captures of the exit
gantry, MESH_VIS, MESH_EXCL) nor round 27's ordered list bench (LA, the exit gantry right).
- Video of the presented frames (tunrun.sh REC=<fps>, new: ffmpeg x11grab of the emulator
  window; the picture is upside down in the window and cropped/rotated afterwards -- the
  framebuffer snapshots catch half-drawn frames in a GPU build and are no good for this).
  LA, list fixed on (fx_on, A4_ABMODE=1070) and off (fx_off, 46), 8 fps: the three gantries
  each carry two red-ringed "80" discs and two warning triangles over a "74 km/h" panel.
  No flashing seen in either mode at 8 fps; none pitch-black on the bench either.
- A4_MESHSTATE (new, bench, with A4_MESHCHECK: A4_SCRATCH_SIZE 0x54000): at the table's draw
  of each listed face, its texture word, texcoords, vertices' view x'y'z' and flags against
  what the list copied at the insert. LA through the tunnel, 864,776 compared draws: 0
  differing in any field. Re-run with duplicates counted: 0 faces inserted twice in a frame,
  0 drawn twice. So the list draws what the game would, from the same data -- a sign whose
  state the game changed after the insert is ruled out.
  The listed faces the table draw never matched (41%) are the clipped ones: +35D3A draws a
  face with its own record only when front-facing and no vertex has a clip flag (OR of the
  +0x24 flags 0); otherwise it clips and draws a stack copy. Not a difference.
- Read for the sprites: the road comes from another insert site (+27402, map 2's census: its
  own transform +346B0), not +349F8 -- so it is still the table's, and in a list frame a
  table KIND_TEXZ face writes depth (mesh_enter). A TBillboard drawn after it in the walk
  then tests against it at its centre's depth, and the road under its lower half is nearer:
  the sinking. In a no-list frame quads sit at window depth 1 (untested). To confirm which
  class the road under the pickups is (MESH_VIS now colours list runs by class: texz red,
  painted green).
- Class view (MESH_VIS by class, video): road, terrain, the tunnel's walls and ceiling and
  the player's car are textured -- the table's; scenery, traffic and the gantries are list
  faces. So the road is table KIND_TEXZ (confirmed).
- A4_BBSINK (new, bench, check build): at each TBillboard blit, the sprite's opaque texels
  (its own texture walked as the blit walks it) where the game's 1/z buffer -- every
  perspective face drawn BEFORE the sprite -- is nearer than the sprite's depth: pixels a
  depth-tested quad loses that the game paints. LA, 9,904 sprites: 12.2% of 2.39 M opaque
  pixels hidden; the same 12.2% at depth zw * (1 - f) for f = 1/16, 1/8, 1/4 and 1/2 -- no
  bias reaches them. By quarter of the sprite's height, top to bottom, 14% 37% 35% 14%:
  the middle, not a sunk bottom edge. The worst sprites are the nitro flame's frames (37-45%)
  and one at 88%. A4_BBRECTS shows why: the flame is a trail of ~20 additive quads at
  zw 191-230 on the bottom rows (y 194-240), where the road is far nearer. A sprite's
  distance word is a sort key, not where it shows.
- Fix (in): a TBillboard is depth-tested only when a list face of a strictly nearer bucket
  -- one the table would draw after it -- has its box over the sprite's rectangle
  (mesh_bb_covered: the sprite's bucket as +349B0 takes it, key +31368 = bb+0x18 less 0x20
  for flag bit 13, then >> 3 and +-16 for flag bits 16/17; the list's tail in MESH_ORD by
  binary search, a box test each); otherwise window depth 1, untested, as the game paints
  it. Round 26's car lights stay tested (the car in front is a nearer list face over them).
  MESH_BBALL (bench) restores round 27's rule.
  LA, A4_BBSINK on the fixed build (different frames): 1,494 of 9,251 sprites (16%)
  depth-tested; the sprite pixels hidden by what was drawn before fall from 17.6% (every
  sprite tested, as round 27) to ~2.4% (34.7% of the tested ones' 227 K pixels).
- The tunnel signs, measured:
  - MESHSTATE span modes: the table draws every listed face in span mode 0 (644,585 draws),
    so the list's plain texture is the game's (a mode-1 bake is ruled out).
  - The stock picture (A4_SWONLY, video at 25 fps): the exit gantry's signs lit from afar,
    then black from one frame to the next (~80 ms apart); the middle gantry black on that
    pass, lit on fx_off's and fx_on's. The frame-alternating A/B (A4_ABFRAME=1, new: AB_MESH
    flips every frame, a green 8x8 mark in list frames) shows both modes black on the same
    pass. On the bench the two modes agree frame for frame.
  - A4_MESHDUMP (new, bench, check build: the frame's whole list after the capture, 64 bytes
    a triangle with the face's flags and a sample texel): the signs are 4 quads of one 64x64
    texture (descriptor 023d1798), 0x4001, bucket 57 against the beam's 92; far captures
    have them on the affine fallback, near ones perspective. Same descriptor in every
    capture; its texel pointer (+0x10) differs: 0246d098 (dark texels) in one, 0247ea60
    (red rings, white) in the others.
  - EKA2L1_WATCH on that word: the game's texture animator +D32E (called once a render from
    the map's render, +D718) alternates the two pointers. For each animated texture (count
    at map+0x6908, 24-byte entries at map+0x6788: target, frame, accumulator, frames, frame
    textures, durations) it adds dt = (G[0x1114]->+0x2C - G[0xCDB0]) >> 8 to the
    accumulator and steps a frame (accumulator back to 0) past the frame's duration, then
    copies the frame's texel pointer and alpha map into the target descriptor.
  - EKA2L1_PCMEM on its table (bench, 669 calls): 3 animations, durations ~490 and ~1,000;
    dt per render 5-137, long runs at 5-6 -- the length of the loop's last iteration, not
    the time since the last render (the loop spins between gated renders, round 16). On the
    phone a state of the sign then lasts several seconds, longer than an A/B window (40
    renders, ~3.5 s): a pass under a gantry can see it lit, or dark, the whole way. Nothing
    in this path depends on the list; the list reads the same pointer the table does.
  - Conclusion: not reproduced as a mode difference; on the bench both modes and stock show
    the same flash. The owner cannot tell the modes apart, so the phone report cannot be
    attributed. Proposed: a 3x3 marker in list frames for the next round (A4_ABMARK), so a
    report says which mode it saw.
- The sprite rule, revised twice the same day (superseding the "Fix (in)" above):
  1. With the box rule alone the flame's frames were still depth-tested for 35-93% of their
     pixels: the player's car has plain parts in the list (wheels, underside), in nearer
     buckets than the flame. A4_MESHDUMP over 8 captures: 52-55 listed faces a frame (6.2-
     6.6%) have their deepest vertex within 384-512 ZW. MESH_NEAR (in, 512): a face whose
     three vertices are all nearer than that stays in the table -- drawn in the walk's
     order with the sprites around it, as the game draws them. Then the flame's frames were
     tested for 0-1.6% of their pixels.
  2. The sprites still tested were far glows (buckets 712-812, flags 0xA000) under a small
     face of a nearer bucket (703), losing to the road under them. A covered sprite is now
     tested at the far edge of its deepest covering bucket ((bucket + 1) * 8, the first
     cover found in the deep-to-near tail), not at its own distance (mesh_bb_depth):
     hidden by the faces the game paints over it, not by the road behind those. Here the
     cover sat at nearly the sprite's own depth, so it changed little: LA, 1,309 of 9,093
     sprites tested, 33.8% of their 79 K pixels hidden at the depth used -- about 1.1% of
     all 2.33 M sprite pixels, against 14.3% under round 27's rule (every sprite tested at
     its distance) in the same run. 13x fewer.
  Monaco video (a4_r28_P, the candidate's flags + A4_BENCH_UNLOCK): pickups and the flame
  whole in list frames (marker visible: list windows 27 frames at 8 fps, alternating); the
  tyre wall's tyres flying after a hit (physics), under the HUD. Paris opens with the bench
  unlock but its events are locked on a fresh career ("LOCKED"): not reachable on the bench.
- Car lights (round 26's fix) are kept by construction: a traffic car in front of a light is
  a list face of a nearer bucket over it, so the light is tested at that car's far edge. Not
  re-observed on the bench (no light-behind-a-car case in the captures, as in round 27).
- The round 28 candidate, exact binary r28_phone (round 27's flags plus A4_ABMARK: a 4x4
  green square in the top-left corner of a list frame, so a report says which half of the
  A/B it saw; A4_SCRATCH_SIZE 0x46000, bss 0x46F08 as round 27; no A4_BENCH_UNLOCK -- the
  bytes at +3A15C checked, B510 0004; sha256 2fc101a101f07a8812bf910b498a6b8207dc2a883fedbd347b82b9de0e297982):
  telerun3 230 s, framestat 5281, faults 1 (the boot fault), errc 0; aggregates (31-34
  windows a mode):
    no list: 85.4 ms | 3D 15.7 (hook 7.4) draw 1.8 grouping 1.09 | calls 218, tris 1,361
    list:    85.3 ms | 3D 12.1 (hook 5.7) draw 1.1 grouping 0.71 | calls 235, tris 1,653
  (round 27's bench: list 3D 11.1, hook 5.4, calls 208 -- the near faces kept in the table
  cost the list ~1 ms of 3D and ~27 calls on the bench, a named cost: on the phone, at ~31
  us a call, ~1 ms more.) racemenu after a race: the race (marker in the corner), the quit
  dialog, main menu, career home, the car browser -- right.
- For the owner: the green corner square marks list frames. The tunnel signs flash on the
  game's own slow timer (several seconds a state at the phone's pace), so one pass under a
  gantry can see them lit or dark all the way; what would be a list bug is a mode in which
  they never change over several passes.

## Round 28b: the rule for every table class (owner, more on round 27)

Owner, after r28 was sent (on round 27): (1) "game feels smooth" but no difference noticed
in heavy scenes against the previous build -- the telemetry agrees: round 27's list cut the
3D pass by 5.9 ms but >=90 ms frames only 19% -> 16% (round 26's unordered list: 11%); the
order cost most of round 26's draw-call saving (list calls 124 -> 231). (2) Other sprites
or assets, like the helicopter's light, clip a bit through the road in one mode.
- Not a repeat of round 28's sprite rule, which covers TBillboard blits only. In a list
  frame every table class but the perspective one -- additive glows (0x10008), alpha
  cut-outs, flat slabs, shadows, the default span -- was depth-tested (mesh_enter), against
  the table's road too; the game tests none of them. The helicopter (CPoliceHelico, the
  night texture objects\police_helico_night.rle) is daytime on the bench's Monaco race and
  its light was not seen; which class draws it is not settled (a guess: an additive face,
  or a TFakeZFace -- whose key +522C2 returns 1, so the game draws it after everything).
- Rule (in, lean_conv): such a triangle is drawn at window depth 1 (untested) unless a list
  face of a strictly nearer bucket covers it, by a 20x15 grid of 16-pixel tiles holding each
  tile's nearest list bucket (built in mesh_draw from the drawn faces' boxes; the struct
  mesh header has the room, 1,920 of 2,048 bytes). Its bucket as +349B0 takes it: TFace
  key (deepest ZW) >> 3, +-16 for flags 0x10000/0x20000; a TFakeZFace (vtable +B08CC,
  placeholder a4_tfzvt) bucket 0, never covered; a clipped copy (+35D3A's pool records)
  by its deepest ZW without the flag adjustment. MESH_TRIALL (bench) keeps round 27's rule.
- Same-frame check (A4_MESHCHECK + A4_DUALCAP, LA, 8 captures each, different frames per
  run; dualcmp.py): GPU pixels differing from software 24.7% (round 27's rule) -> 15.8%;
  8x8 blocks differing by object 4.4% -> 3.5%. The remaining object blocks are the floating
  position labels (software HUD) -- nothing showing through.
- Candidate r28b_phone (r28's flags, the same A4_ABMARK; sha256 898cea75821e9180b4929a1377425fb13618e0f2d030bcd661b1859c0f3e6084;
  bss 0x46F08; +3A15C bytes B510 0004; placeholders patched). Supersedes r28 (not run).
  telerun3 230 s: first run stuck on a barrier for its second half (0 km/h in all 8
  captures; aggregates not comparable, set aside); second run framestat 5364, faults 1 (the
  boot fault), errc 0, lap 3 reached:
    no list: 85.3 ms | 3D 15.7 (hook 7.8) draw 1.9 grouping 1.15 | calls 232, tris 1,494
    list:    85.4 ms | 3D 11.9 (hook 7.0) draw 1.0 grouping 0.65 | calls 233, tris 1,915
  (r28: list 3D gain 3.6 ms, hook ratio list/no-list 0.77; r28b: 3.8 ms, 0.90 -- the tile
  check costs some hook time; on the phone a guess, ~1 ms). racemenu: race, quit dialog,
  main menu, career home, car browser -- right.

## Phone round 28b (r28b_phone 898cea75): owner's report, no telemetry yet

Owner: car headlights fine. Helicopter light: not seen misplaced, okay in both modes so far.
Tunnel signs: they change inconsistently -- sometimes during a marked (list) window,
sometimes during an unmarked one; most of the time the two modes hold opposite states, and
after a change each holds the other's.
- Reading: the sign's frame lasts until the game's animator accumulates ~490 of its dt
  units (+D32E; dt = the last loop iteration's length >> 8, MBX.md round 28 bench), which
  at the phone's pace is several seconds -- of the order of one A/B window (TEL_EVERY 40
  renders, ~3.5 s). A state that lasts about one window flips at about every window
  boundary, so each mode seems to hold the opposite state, and the phase slips slowly
  (changes landing inside windows, the association swapping) -- aliasing, not a mode
  difference. It also fits a mode that draws the inverse image; the two differ in that
  the inverse would flip the picture at EVERY boundary, aliasing only at most.
- Evidence so far against a mode difference: the list reads the same descriptor and texel
  pointer as the table (A4_MESHSTATE, 0 differences in 864,776 draws); on the bench both
  modes and stock agree frame for frame (A4_ABFRAME video). Open: the clipped sign faces
  (+35D3A pool copies, 41% of listed faces) were not compared by MESHSTATE.
- Test (no extra round): the next candidate's A/B window at a length unrelated to the
  sign's period (TEL_EVERY 23, prime and well under it). Aliasing: the states stop pairing
  with the marker. A mode difference: the pairing stays.
- Telemetry (capidx 38-45, errc 0, depth 24). Race aggregates (seg0, latest capture, 22-23
  windows a mode), render-to-render histogram:
    no list: 87.5 ms, >=90 ms 22% | 3D 35.8 (hook 19.4) draw 11.7 grouping 1.95 | calls 249 (before 410), tris 1,375
    list:    88.0 ms, >=90 ms 28% | 3D 33.7 (hook 19.1) draw 7.3  grouping 1.23 | calls 256 (before 191), tris 1,703
  Against round 27 (no list 3D 35.2, hook 18.8 -- the scenes compare): the list's 3D 29.8
  -> 33.7 (+3.9 ms), hook 18.0 -> 19.1, draw 6.1 -> 7.3, calls 231 -> 256 (before grouping
  159 -> 191), the rest of the 3D pass (3D less hook, draw) 5.7 -> 7.3; slow frames 16% ->
  28%, now worse than no list. A regression in list mode; my "~1 ms" guess was wrong.
- Reading, by item (guesses until measured): MESH_NEAR (512) puts ~55 faces a frame back
  in the table: the hook converts them (~15 us each on the phone, ~0.8 ms), they add runs
  among the player's car and the sprites (calls +25, the bench's +27 for r28), and the
  game walks, projects and sets up each one again (the rest of the 3D pass). The per-
  triangle tile check, the grid build and the sprites' tail scans are the smaller part.
- Next (round 29): MESH_NEAR 256 (the dump: 10 faces a frame within 256 against 55 within
  512; the flame's quads sit at buckets ~12-25, so only faces nearer than ~256 can cover
  them -- to be measured with A4_BBSINK); the sprites' tail scan only when the grid says a
  nearer list face is there; the A/B window at 23 renders (the signs' aliasing test).

## Round 29 bench: the near cut only while a near sprite is about; the 23-render window

Not repeats of: r28b (near cut on every frame), r28 (sprite rule only).
- MESH_NEAR 256 (A4_BBSINK, LA, against 512 in the next run): near sprites (zw < 512)
  depth-tested again -- 80 K pixels, 40.6% of them hidden by what was drawn before; the
  flame-like texture tested 65% of its pixels (0% at 512). So the faces over the flame
  sit between 256 and 512 ZW (the sprite's bucket can be 16 farther, flag 0x20000), and
  the cut must stay at 512. (Test the test: the first near/far split counters shared words
  with the cover records, [57]-[60]; only the clean words, [61] [62], were read.)
- The cut made dynamic (in): a TBillboard drawn within MESH_NEAR + 128 ZW sets
  MESH->nearf; mesh_take keeps faces within MESH_NEAR out of the list only for MESH_NEARF
  (8) frames after that. Bench LA: near sprites depth-tested 0 px; the cut on in 430 of
  1,159 list frames (37%; the bench's driver boosts often). Off-nitro frames pay round
  27's cost again; the first frame of a burst sinks (one frame).
- mesh_bb_depth: the tile grid first -- no tile under the rectangle with a nearer list
  bucket, no tail scan.
- Candidate r29_phone: r28b's flags with TEL_EVERY 23 (the A/B window; a capture every
  23 renders, ~1.7x as many file writes, outside the timed windows).
- Exact binary r29_phone (sha256 553a920397a45ed3224eed283cca2cf452e2384f4f1e286ddf15f58b1df19d39; bss 0x46F08;
  +3A15C bytes B510 0004; placeholders patched): telerun3 230 s, framestat 5553, faults 1
  (the boot fault), errc 0, lap 3 reached, the flame whole in the captures; aggregates
  (58-59 windows of 23 renders a mode):
    no list: 85.3 ms | 3D 14.5 (hook 7.7) draw 1.8 grouping 1.07 | calls 234, tris 1,464
    list:    85.7 ms | 3D 10.8 (hook 6.6) draw 0.9 grouping 0.54 | calls 214, tris 1,824
  (r28b bench: list calls 233, 3D gain 3.8 ms; r29: calls 214, gain 3.7.) racemenu: race,
  quit dialog, main menu, career home, car browser -- right.

## Phone round 29 (r29_phone 553a9203): the list holds; the owner turns to presentation

Telemetry (capidx 326-333, errc 0). Race aggregates (seg0, latest capture, 43 windows of 23
renders a mode), render-to-render histogram:
  no list: 86.9 ms, >=90 ms 17% | 3D 35.3 (hook 17.9) draw 11.5 grouping 1.89 | calls 239, tris 1,348
  list:    87.2 ms, >=90 ms 19% | 3D 28.6 (hook 19.0) draw 6.4  grouping 1.09 | calls 233, tris 1,654
The list's 3D pass the shortest yet (6.7 ms under no list; r27 5.4, r28b 2.1): the dynamic
near cut recovered r28b's regression. Slow frames do not follow (19% vs 17%): both
histograms pile at 80-85 ms, the game's own render gate (round 16: ~85 ms, ~11.8 renders a
second, by itself); the frames over it are bound by the rest of the loop.
Owner: no difference felt in heavy scenes. Tunnel signs now work in either mode -- the
pairing with the marker is gone at a 23-render window: round 28b's report was aliasing
(MBX.md round 29 bench), not a mode difference. Noticed: the faster they drive, the more
often the signs flash; the Moulin Rouge's blades (Paris) spin faster the faster they pass
-- consistent with the animator's dt being the last loop iteration's length (+D32E), which
grows with load. Pickups, turbo, helicopter light fine. Not pursued for now (owner).
New direction (owner): a more fluid frame rate on screen -- uncap the presentation or
whatever it takes -- and less tearing.

## Presentation study (owner: a more fluid frame rate on screen, less tearing)

Sources: the record (rounds 16-20, the step RE), the runtime's binary (ngibackbuffer.dll,
the uncompressed copy in the scratchpad's mbxrecon/), the RM-320 ROM (romimg.py, capstone),
the platform's source (graphics/.../screendriver/sbit/Cdsb.cpp, SCDVU.DEF, WS322U.DEF).
- The cap is not a presentation cap: the 85.33 ms is the game's fixed logic step (the step
  RE, rounds 16-17: halving it ran the whole game at twice speed); +1670E calls the step
  counter +15B1C, runs the logic that many times and draws once if any ran. Presenting more
  often shows the same frame again (the re-presents did exactly that; skipped since round
  19). More than 11.72 new pictures a second need frames made between steps.
- How the runtime presents: ngibackbuffer imports WS32 233/234 (CDirectScreenAccess::NewL,
  StartL), 387 (RWindowBase::Size), 469 (CWsScreenDevice::GetScreenNumber), 82
  (RWindowBase::DisplayMode), SCDV 43 (CDirectScreenBitmap::NewL(TInt)) and 10
  (RHardwareBitmap::GetInfo) -- ordinals from the platform DEFs, the ROM's export counts
  agreeing for SCDV (45 = 45). The DSB is made at +4A04 (kept at [obj+0x68]); +5100 calls
  Create(rect, 1): EDoubleBuffer; then BeginUpdate (vtable+0x10) into [obj+0x6C],
  EndUpdate (+0x14) with a request status at [obj+0x88] (checked against KRequestPending),
  Close (+0x1C). Before each BeginUpdate (+51B4) and Close (+5258, +599C, +5A00) it waits on
  [obj+0x88] if it reads KRequestPending, through the stub +5E60 = euser ordinal 604,
  resolved against the RM-320 ROM: 803A82D4, a loop on WaitForAnyRequest until the one status
  leaves 0x80000001, then re-signalling the count (605 next to it is the two-status form):
  User::WaitForRequest(TRequestStatus&). So the runtime never writes a frame while the last
  one is still going out; it is not a race on our side.
- The phone's DSB (ROM scdv.dll; NewL(TInt) 8087953C, vtable 8087ECD0): Create accepts flag
  1 or 2 only (0 -> KErrArgument); BeginUpdate and EndUpdate hand off to a driver object
  ([this+4], its vtable +0xF0/+0xF4) unless [this+0xC] is set. Not the generic platform
  version (Cdsb.cpp: one buffer, a Mem::Move into video memory, no sync). So the runtime
  already asks for double buffering and the transfer is the display driver's: a tear is the
  driver's transfer meeting the panel's scan, below the game and below us. What we control
  is how many updates there are (one a render since round 19; the stock game made three,
  the owner's "worse there") -- and whether the frame goes out another way (an EGL window
  swap, whose sync on the N95's MBX driver is unknown).
- Budget on the phone (round 29, per window): heavy windows 3D 37-45 ms, rest (logic, no
  waiting) 19-26; light ones 3D 12 ms, rest 50-52 (~30 ms of that waiting for the next
  step). Putting one frame on screen costs ~20 ms now (readback and composite 10-13, the
  game's 2D 6, present 2.5). A made frame between steps fits only light scenes until that
  cost falls.
- Slots between steps (round 29 phone headers [44] skipped, [45] presents, 23 renders a
  window): 70-398 re-presents skipped a window = 3-17 a render, the loop going round 1.5
  (heaviest, capidx 330) to 8.7 times (lightest, 333) a step. A frame made between steps
  has somewhere to go in every window; whether it has the time is the budget above.
- Tearing: below us (the driver), so the only lever is what consecutive frames differ by.
  A tear shows the seam between two frames; at 11.72 new frames a second each jumps ~85 ms
  of motion, so the seam is as wide as that. Frames made between steps halve the jump and
  the seam with it. An EGL window swap is the other door (sync unknown on the MBX driver;
  the window path's DSA fight, rounds 1-2, still unsolved) -- kept for later.

## Bench: does a list face keep its pointer from one render to the next? (phase A, first look)

Not a repeat: the list dumps of rounds 28-29 (A4_MESHDUMP) were single frames; nothing has
compared two. Build: C29's flags with TEL_EVERY=1, A4_ABMODE=1070u (the list on in every
render, no A/B), A4_MESHSTATE A4_MESHDUMP A4_MESHCHECK A4_DUALCAP (the dump rides the dual
capture and the check build -- the first try without them wrote no dump: logged so it is
not mistaken for a result). telerun3 50 s, 16 consecutive renders in two rings (s->frames
steps by 3 a render: the loop's three present hooks). tools/faceid.py.
- No pointer twice in one frame. Of a render's list faces, 55-90% of the pointers come back
  in the next render, most with the same texture and texcoords (the same polygon, by those
  two); 10-25% come back holding another texture (the slot reused), the rest are new.
- Is the motion of a returning polygon real? A render here carries 1-5 logic steps (the
  capture's own load), so per triple of renders one ratio r = steps(b->c)/steps(a->b) is
  fitted and each polygon tested for d2 ~ r*d1 on all nine coordinates. 9 triples had
  motion: in 5 every matched polygon fits (r 2.02, 1.72, 0.36, 0.60, 2.00; 97-573 polygons
  each), in 1 93% (217 of 233, r 0.66); in 3, 7-19% (r -0.96, 0.29, ~0) -- frames where the
  camera turned or cut (the last one is the race-end camera, |z| median jumping 340 -> 7767); one
  ratio cannot describe a turn, so these say nothing either way.
So the pointer names the polygon for the faces that keep it, at least on straight motion
-- plausible, not settled. Missing to settle it: renders without the capture's load and
the step count of each render (N at +15B1C) in the dump, and a track with turns.

## Plan: frames between steps (owner: more fluid, less tearing)

The only way past 11.72 new pictures a second without touching the game's physics.
Each made frame: the last frame's triangles drawn again, every polygon moved on by its
own motion between the last two frames (extrapolated half a step), then the HUD of the
last frame laid over, presented in one of the skipped re-present slots at mid-step.
- Phase A (bench): identity. A polygon must be recognisable from one frame to the next
  for its motion to be known. Face pointers across consecutive renders (faceid.py).
- Phase B (bench): keep the frame's geometry (list faces in camera space already; the
  table's faces as screen x, y and depth), and the HUD (the backbuffer before and after
  the game's 2D, their difference the HUD mask).
- Phase C (bench, then phone): draw the made frame at mid-step from a skipped slot,
  only when the last render left the time (light scenes first); a telemetry split for
  made frames (count, cost) and an A/B (made frames on/off) per window.
Guesses: the made frame's cost on the phone (~20 ms: a draw, the readback, the HUD and the
present -- from round 29's items, not measured as a whole); extrapolation errors where
motion changes inside a step (a crash, a sharp turn) -- a polygon overshoots for one made
frame; polygons new in a frame (no motion known) drawn where they are.

## Round 30: the game's logic per step, timed (owner: measure it before choosing)

Owner, on the plan above: frames between steps would add input lag (the game already
suffers from it); then, after the trade-offs (made frames vs a cheaper present vs a faster
step): "Measure the logic cost per step first." The question it settles: whether the
game's own logic leaves room to run the step faster (the only change that cuts input lag
at its source). Round 29's "rest" (19-26 ms a render in heavy windows) is everything not
timed, not the logic alone.
Not a repeat of: round 16's phase timers (3D, composite, 2D, present -- the logic was
never timed, only inferred as the remainder), the step RE (static), round 17's stock split
(same phases).
- The race update +1670E (disassembled this round): +16796 bl +15B1C (the step count: m into
  [sp+4] = t/21333 - last/21333, the real-time count; n into [sp+8] with the period
  [this+0x84C] * 21333 >> 8, the logic steps at the race's time scale -- equal at 256;
  both clamped to 5); the main loop runs n times (+167A6..+1692C: bl +25E8, bl
  +15A52, the race checks, bl +141B0 ...); a second loop runs m - n times (bl +1300C, the
  slow-motion updates); then if m: bl +12D5A (when +691E and n), bl +12992 (the render).
- A4_STEPTIME (a4_gl.c a4_step_*, injector STEP_HOOKS): the four bl's at +16796, +167B4,
  +16966, +1696C become BLX to hooks that call the game's own function (placeholders
  a4_stepcnt/stepmain/stepcam/steprend, patched in every GL build so no 0xDEADBEEF is
  left). A span = from +15B1C's return to the render call; per window: header [60..63]
  logic ticks, steps, spans, +12D5A ticks; per segment (rotated with the aggregates): the
  step block at 0x421700, written after the aggregates (spans, steps, ticks, the longest,
  +15A52's ticks and calls, spans by n, loads of 500 ms or more left out, n = 1 spans by
  length). Ticks are NTickCount (1 ms on the N95, round 10).
- Build r30: C29's flags with A4_ABMODE=1070u (the list on in every render) and
  A4_STEPTIME, no A4_ABCYCLE9 (no A/B), no A4_ABMARK (no green square). tools/steptime.py.
- Also timed (r30b, after the first bench run showed the logic small against "rest"): the
  render call +12992 whole, and its part before the frame's first triangle (the game's own
  transforms and sort, which the 3D pass timer -- first triangle to composite -- never saw).
- What decides it, written before the phone answers: a step twice as fast needs a frame
  (logic + render + present) in 42.7 ms. Light scenes today: 3D 12 + composite 10-13 + 2D 6 +
  present 2.5 = 31-34 ms, plus the render's part before its first triangle and the logic --
  both unmeasured until this round. If those two together come to ~10 ms or less in light
  scenes, a faster step is worth an RE look; well over, not on this phone.
- Bench run 1, build r30 (sha256 a817d184..., without the render-call words): telerun3 230 s,
  framestat 3063, faults 1 (the known boot fault). The whole race (seg0, latest capture):
  2,618 spans, 2,765 steps (n = 1: 2,485 spans, n = 2: 127, n = 3-5: 6), no load left out;
  2.7 ms a span, 2.5 ms a step (bench host time), the longest 340 ms; n = 1 spans: 2,304
  under 4 ms, 179 at 4-7, 2 at 8-11. +15A52 under 1 ms a call (2,765 calls: once a step, as
  read); +12D5A 0 ms (not called, or under a tick each); m - n = 0 throughout (no slow
  motion in this race). The last 8 windows: 84.5-89.1 ms a render, "rest" 27.8-46.3 ms of
  which the logic is 2.3-3.0. On the bench the step's logic is ~7% of "rest"; the rest of
  "rest" is not the logic. Bench time is not phone time (the interpreter's costs are not
  the ARM11's), so this says what to look at, not the answer.
- Bench run 2, build r30b (sha256 edd6e040a51af9df324c326771f8d7c55914e38d1996e536159bd45bcfba716d;
  +3A15C bytes B510 0004; placeholders patched; code C187C, +92 bytes on r30 for the
  render-call words): telerun3 230 s, framestat 3233, faults 1 (the boot fault). The whole
  race (seg0): 2,662 spans, 2,765 steps (n = 1: 2,567, n = 2: 91), 2.5 ms a step, the longest
  span 333 ms; the render call +12992: 68.0 ms a call (2,662 calls), of it 18.8 ms before the
  frame's first triangle (37 calls set no first-triangle mark inside: cause not traced; the
  mean is over the rest). The last 8 windows: 82.7-107.5 ms a render.
  So on the bench a render (~86 ms) is: the logic 2.6, the render call 68 (the game's own
  transforms and sort before the first triangle 18.8 -- our list's taking of faces runs in
  there too, at the insert -- then the 3D pass, the composite and the 2D, all inside the
  call), the present ~1, and ~15 left (the loop's spin at the cap and the rest of the loop).
- Correction to round 29's budget line ("rest (logic, no waiting) 19-26"): "rest" was never
  the logic. It holds the render's part before its first triangle (on the bench 18.8 ms
  against the logic's 2.6), and the wait. The phone round says how it divides there.
- Candidate r30_phone = r30b (edd6e040). What the phone round can say that the bench cannot:
  the ARM11's own split of a render -- the logic per step, the game's transforms before the
  first triangle, the render call -- in light and in heavy scenes; bench host time does not
  carry over. One full race asked (three laps), then the 8 capture files. The owner sees
  the list in every render (no A/B, no green square).
  racemenu (r30b, 30 s race then 17 keys): framestat 3015, faults 1 (the boot fault); the
  race, the pause menu stepped down to MAIN MENU, the quit dialog (NO, YES), the main menu,
  career home, the car browser and back -- all drawn.
- Sent to the owner unbaked (a4_r30b_SHIP.exe, sha256 edd6e040...); the owner bakes.

## Phone round 30 (r30_phone edd6e040): the game's logic is 1.5 ms a step

Telemetry (capidx 73-80, errc 0; one race, the owner's). Whole race (seg0, the latest
capture): 1,846 renders in 80 windows, 88.4 ms a render, >= 90 ms 28% | 3D 30.8 (hook 17.5),
composite rows 10.6 (read 6.8), batch draw 6.4, 2D 6.6, present 2.3. Not comparable with
round 29's 19% at 90 ms or more: another race (the owner's track), the list there in half
the windows.
- The step block (seg0): 1,900 spans, 1,981 steps (n = 1: 1,822, n = 2: 77, n = 5: 1), one
  load left out; the logic 1.52 ms a step, the longest span 6 ms, n = 1 spans 1,780 under
  4 ms and 42 at 4-7 (NTickCount, 1 ms; a sum of many spans is unbiased). +15A52 under a
  tick a call; +12D5A never more than 0 ticks (header [63] 0 in every window: a wasted word,
  for the render's prep next time). No slow motion (m - n 0).
- The render call +12992: 66.5 ms a call (1,901), of it 16.9 ms before the frame's first
  triangle (32 calls without the mark, as on the bench).
- A render on the phone, the race's means: the logic 1.6, the render call 66.5 (the game's
  transforms and sort, with our list's taking of faces, 16.9; then the 3D pass, composite and
  2D), the present 2.3, ~18 outside them (the wait at the cap and the rest of the loop).
  Per window (the last 8): the logic 1.2-1.7 ms a render in every scene; light windows
  (78, 79) 3D 10.7-11.7, composite 10.8-11.0, 2D 6.3-7.0, present 2.7, rest 51.7-52.3.
- Against the rule written before the round (logic + the render's prep at ~10 ms or less in
  light scenes): logic 1.2-1.3 + prep 16.9 (the race's mean; the light scenes' own prep was
  not recorded per window) = ~18, over. As the rule was framed (every step drawn), a step
  twice as fast does not hold 23.4 frames a second in light scenes: ~49.5 ms a frame there.
  But the rule's frame was the wrong frame: when a render takes longer than the step, the
  loop renders again at once with n = 2 (as it does now in heavy windows), so a halved step
  would render back to back -- ~20 a second in light scenes at today's costs, heavy scenes
  as now (~11.5) -- and the logic itself (1.5 ms against a 42.7 ms step) is no obstacle.
- So the CPU does not forbid a faster step; the frame's cost limits how much it shows, and
  the cost of the change is the RE: the period lives only in the two step counters (+15B1C,
  and its copy at +B63F0 reading the literal at +B67F4; the third 0x5355 at +A6C1C has no
  Thumb literal load), no time delta reaches the logic, so every per-step quantity (the
  cars' physics, the AI, the camera, timers, animations, particles) assumes 85.33 ms.

## Bench: how much code one logic step runs (sizing a faster step)

Owner: "Continue" -- the bench probe proposed after phone round 30. The question: the step's
period lives only in the step counters, so a faster step means rescaling every per-step
quantity; how much code is that?
Not a repeat of: round 16's halved-step test (it showed everything runs 2x, not how much
code does the running), round 30 (time, not code).
- A4_TRACEARM (bench only, a4_gl.c): an empty a4_trace_arm() entered once at the N-th
  timed span; EKA2L1_PCTRACE_START at its address (+BA520, code at 0x70000000 on the bench's
  RM-320 as in the boot-crash trace) starts the trace mid-race. Range the whole image.
  tools/steptree.py: slices from the step loop's head +167A6 to its exit +1692E, blocks
  mapped to functions by the nearest Thumb push {..., lr} at or before them (leaf functions
  merge into the one above: the count is low, the bytes are not).
- Trace 1 (A4_TRACEARM=1500, MAX 6M entries = 48 MB): the 6M entries hold one race update
  (one step) -- a render is millions of block entries, a step ~21,000. That step: 20,899
  block entries, 1,617 distinct blocks, ~117 functions, 57.7 KB of code (7.6% of the game's
  0xB9E48 bytes). Static context: 8,736 Thumb BL sites in the image calling 1,698 distinct
  targets; 1,519 push-with-lr sites.
- Trace 2 (same build, MAX 40M = 320 MB): 29 race updates (30 steps; one update with n = 2)
  -- a render is ~1.4M block entries here, so trace 1 held one by chance of the arm point.
  Each step: 20,639-20,980 block entries, 1,451-1,486 distinct blocks, 106-108 functions;
  the work is nearly the same every step (as the phone's 1.2-1.7 ms says). Union over the 29:
  1,877 blocks, ~127 functions, 57.2 KB of code (7.5% of the game); 106 functions in every
  step. A third of the entries (213,024 of ~605,000) fall in the run of small leaf helpers
  from +52E44 (fixed-point and RGB565 arithmetic: +52E5C blends two 565 colours).
- Side find: the step's first call, +25E8 (on [race+0x1114]+0x14), walks a queue of key
  bytes (top two bits the event, low six the key; repeat counters at +0xB/+0xC): the game
  takes its input once a step -- a key waits for the next step, up to 85 ms.
- What it means for a faster step: the code to audit for per-step quantities is ~127
  functions / 57 KB of Thumb (~28,000 instructions), most of it probably not time-dependent
  (arithmetic, collision, track queries), the rest (the cars' integration, camera smoothing,
  timers, animation and AI counters) unknown in count. And it is all-or-nothing: a step
  halved with only part rescaled runs the rest of the game at double speed, so no part
  ships alone.

## Bench: where the cars' speed lives (the physics probe, step 1)

Owner: "continue" -- the physics probe: find the cars' speed and position update and see
whether its per-step amounts sit in a few tables or are scattered.
Not a repeat: no earlier round looked at the game's simulation data.
- The HUD's speed number: PCTRACE_REGS at the two HUD painters (+29ADA, +29FB4) through a
  40 s race (hudregs.regs, 1,845 records): the race HUD's calls by return address and their
  (x, y) in r2, r3; the speed digits are +29FB4 called from +11C56 at (263, 213), right-aligned
  (the digits' 236..257 in the sprite trace of the earliest rounds). Its value: +11BFE
  bl +206A0 on the player car ([sp+0x124]), then (an option's flag at [ctx+..+3]) x62 / 100 for
  mph, abs. The gauge needle before it: [car+0xA00] x 900 through +52A84.
- +206A0 (the speed in km/h): car[0x7EC] x car[0x7D0] / car[0x7E8] (signed divide import).
- The player car object dumped at every HUD speed read (PCMEM at +206A0, r0, 0xA00 bytes;
  60 s race, 593 dumps, one object 0x022F9A48; tools/carfields.py): 54 of 640 words change.
  Per render (1 step, sometimes 2):
  +004/+008/+00C the position (x, y, z; copies at +04C, +0A8, +0D0); +7D0 the speed (0..277,
  the HUD's km/h = +7D0 x +7EC / +7E8, both constant: 213 km/h at 277), deltas 1 (223) and 2
  (112) -- one unit a step; +7E4 and +0B4 alike; +7D8 deltas 16 (219) and 32 (55) -- 16 a
  step; +0B8/+0C0 +-12 a step (steering-like); +0E0 and +820 +1 a step (step counters); +15C
  +1 a step wrapping at 10 (an animation cycle); +164 -1 a step from 64 (a countdown); +66C
  flips between two values every step; +678 +-20 steps; +01C +1 now and then (37 -> 105).
- So the per-step amounts are integers of one or a few units: a halved step needs half a
  unit (speed +0.5 a step, a flip every other step, a countdown of 128). Not a constant to
  halve but the units themselves -- each field's scale and every reader of it (the HUD's
  +7E8, the position integration, the AI, the engine sound).
- Who writes the player car in a race: EKA2L1_WATCH over the object (0x022F9A48, 0xA00
  bytes; the same address as the dump run), WATCHPC the game's code, WATCHMAX 400000; 40 s
  race; tools/watchsum.py from post 400 (the race running): 52,981 writes from 152 code sites
  in ~30 functions. The largest: +31EA4 and +321A6 (the orientation, +6D4..+700, 12 sites
  each), +22832 (+10..+18, +A8..+CC), +22D64 (+4C..+78), +1DE78 (+7D8, +7E4, +678.., 17
  sites), +220A4, +23E72 (the position +4/+C, 19 sites), +522A4 (+704..+718); the speed +7D0
  from +1C0B0 alone (2 sites; it reads the gear's entry at +700 + 20 x gear and a curve table
  through [car+0x168] -- the tuning is data, the step is in the arithmetic); +7D8 from six
  functions (+1DE78, +1BF26, +1BDEC, +1BB98, +1384C, ...); the step counters +820 (+1B560),
  +0E0, the flip +66C (+1B084), the countdown +164 (+2097A), the cycle +15C (+28508).
- Verdict (the test set before the probe: a few tables and one shared integration, go; per-
  step amounts scattered across dozens of functions, not on this phone): scattered. The
  player's car alone is written from 152 sites in ~30 functions in a race, its per-step
  amounts are integer units (a unit of speed, 16 of +7D8, 12 of steering, counters, a flip,
  a 64-step countdown) that a halved step cannot halve without new units and every reader
  changed; the opponents, the camera, the race's timers and the effects are on top. A faster
  step is set aside; the owner is told and the frame's cost is the next work.

## Round 31 (bench): the faster step, set up (owner: "let's gamble")

Owner, told the cost (the car alone written from 152 sites, integer per-step units, nothing
ships until all of it is right): "let's gamble". The plan: a halved step (42.67 ms); what
must be smooth and answer the keys (the cars' speed, steering, position; the camera) runs
every half-step with its per-step amounts halved; everything else (the AI's decisions,
timers, countdowns, effects) runs every other half-step and so behaves as now. Measured,
not judged by eye: scripted keys and the car's state each step, normal against halved.
- A4_HALFSTEP (injector, bench): +52B98 0x18938DE7 -> 0x312684D0, +52B9C -21333 -> -10667,
  +15F28 21333 -> 10667 (round 16's halving; the divide checked exact for 0 <= t < 2^31).
- A4_CARREC (a4_gl.c, bench): at each step's +15A52 call (after the keys are taken), a
  record ('CREC', step, the key bits K+0 and K+4, the car, the player's index) and the
  player car handed to an empty a4_rec_emit(hdr, car) at +BA5AC; EKA2L1_PCMEM with two ats
  at its address (r0 0x20 bytes, r1 0xA00). The player car = [race + 0x394 + 4 x idx], idx
  = [[race+0x390]+0xC] (+52206, the render's and the HUD's own getter). The keys: K =
  [[race+0x38]+0x1114], K+4 the held bits after +25E8, K+0 the step before; the bench's '5'
  (accelerate) is bit 6 (0x40). tools/crec.py.
- A4_AUTOPILOT (bench): K+0 = K+4 = 0x40 at every step -- accelerate held from the first,
  the same in game time in both builds, the host's key presses overridden.
- The cars' update: +141B0 loops over the cars (+14686..+1498A, counter [sp+0xC8] to
  [r6+4] + [r6+0xC]): per car, +1F1F8 if the byte [car+0xAC4] is set, else +1DE78 (r0 car,
  r1 [sp+0xD0] -- 8 when [..+0x28] > 12 -- r2 [r5+0x10]+0x10, r3 r6, [sp] 0). Of the 28
  functions that write the player car in a race, 20 sit under +141B0, 10 under +1F1F8, 9
  under +1DE78; 7 under the render +12992 (+31EA4 and +321A6, the orientation words
  +6D4..+700, are written while drawing).
- Where the player car's writers sit (callers up the image's BL graph): the speed +1C0B0 and
  the step counter +1B560 under +1DE78 (the car loop's physics); the position writer
  +23E72 under +226E8 under +1EA64 (called by +141B0 at +14C94/+14CA2, after the car loop),
  +FB12 and +1FD5C; the flip +66C (+1B084) and the countdown +164 (+2097A) under the render:
  +12992 -> +12952 -> +D3BA -- per render, not per step, so at more renders a second they
  would run faster too unless held to game time.
- Normal run 1 with the autopilot (apn1, a4_ap_n.exe): 772 steps, keys 0x40 at every one,
  the car (0x022F9A20 this build) away at step ~130 and at 253 by step 575.
- Determinism, normal against normal (apn1, apn2; same build, same scripted keys, host
  clock): of 60 live fields (changing in the race, |value| < 10^6), 56 differ; the per-render
  flip +66C from step 11 (renders fall on different steps in two runs), everything else
  from step 71 on (the position by ~100 units at once). So what the render does feeds the
  simulation, and the host clock decides how renders and steps interleave: the comparison
  needs a clock that does not depend on the host. Next: EKA2L1_CYCLETICK=80 (guest time from
  emulated instructions) for both runs.
- Halved step, untouched otherwise (aph1, a4_ap_h.exe, host clock): 1,360 steps in the time
  the normal build made ~770; the car's state per step as in the normal run (speed 115 at
  step 227 against 99 at 193), so per second of real time it gains speed twice as fast --
  round 16 again, now in the car's own fields.
- The render's per-car work (+12952 -> +D3BA, 6.6 KB): the texture animator +D32E (the
  flashing signs, round 29), +1B084 (the flip), +2097A (the countdown), +20EB4, +2133C and
  the drawing calls -- per render. With more renders a second these would run faster unless
  held to game time; and since they feed the simulation (above), the order of renders and
  steps has to be the same in every run for the comparison to mean anything.
- Milestone 1 plumbing (A4_HALFLOGIC; a4_tramp.s a4_hstep_tramp, a4_gl.c a4_halfstep):
  +167A6 (ldr r0,[r4,#0x38]; adds r0,r0,r7 -- the step loop's head) becomes a BLX; on even
  half-steps the pair runs and the game's step follows, on odd ones the trampoline returns
  to the loop's tail +16926 (lr + 0x17C). a4_step_render skips the render when no whole
  step ran since the last one. Expected: the game in game time exactly as the normal build
  (half-steps alternate whole and empty), so normal step k = milestone-1 whole step k.
- Determinism on the instruction clock (ctn1, ctn2; EKA2L1_CYCLETICK=80): better, not
  enough -- 51 live fields; the flip +66C differs from step 12, the cycle +15C from 138,
  the position from step 347 (by ~230 units at once). The clock is the emulated
  instruction count of every thread, so the other threads still move the moment the game
  reads it. Next: A4_LOCKSTEP (bench, a4_step_count): every race update that would render
  runs exactly A4_LOCKSTEP steps (1 normal, 2 halved) and then renders -- game time cut
  loose from any clock, renders and steps in the same order in every run and both builds.
- +141B0's second car loop (+14C3C..+14CB0, r7 to [r6+4] + [r6+0xC]): per car ([sp+0xA4])
  the movement -- +1FD5C(car, [r5+0x10]+0x10) when +691E and the byte at
  [[race+0x38]+0xE40+0x1C]+0x119 (a mode), else, by bits of [car+0xB28], a flag set or
  +1EA64(car, [r5+0x10]+0x10, r6) (twice in the code, +14C94 and +14CA2). The position
  writer +23E72 is under +1EA64. So a step is: the keys (+25E8), +15A52, the race checks,
  then in +141B0 the physics loop (+1DE78/+1F1F8 per car) and the movement loop (+1EA64 per
  car), and the rest of +141B0. The half-steps' motion: those two loops' calls, replayed
  with the arguments the whole step used.
- Lockstep alone (lsn1, lsn2; host clock with CYCLETICK=80): identical to step 238, then
  every field at once (the position by ~90 units) -- an outside cause, the race's traffic
  or anything else reading the clock (the texture animator reads it: round 29). The game's
  PRNG: the Numerical Recipes LCG (x * 1664525 + 1013904223; 17 literal pools, 33 loads).
  Next: the clock itself synthetic in lockstep mode -- before +15B1C, t ([K+0x2C], K =
  [[race+0x38]+0x1114], assumed the same object as +15B1C's [[+52CA0()]+0x1114]) set to the
  last t ([race+0x44]) + A4_LOCKSTEP periods (21333 or 10667), recorded per step in the
  CREC header (words 6, 7) to check the assumption.
- CORRECTION (rule 1): the recorder writes a record twice now and then (a4_rec_emit's block
  entered twice -- same step number in both), and crec.py compared by position, so one
  extra record shifted everything after it. De-duplicated by step number: lockstep normal
  against normal (lsn1, lsn2) identical over all 536 race steps -- the "divergence at step
  238" above was that artefact; on the instruction clock (ctn1, ctn2) only the render-side
  +66C and +15C differ, the physics never; on the host clock (apn1, apn2) the physics does
  part (step 345). Lockstep is the oracle; the synthetic clock is not needed (built, not
  run).
- MILESTONE 1 PASSED: the halved step with the whole step on even half-steps and nothing on
  odd ones (lsm1, a4_ls_m1.exe, LOCKSTEP 2) against the normal build (lsn1, LOCKSTEP 1):
  0 of 51 live fields differ over 536 race steps. The plumbing (+167A6's trampoline, the
  halved period, the skipped renders) changes nothing in game time.
- Milestone 2a (lsm2a, a4_ls_m2a.exe: milestone 1 plus the step's car calls replayed on the
  odd half-steps, unhalved) against normal: 50 of 51 live fields differ, from step 0 -- the
  car now gets two steps of physics and movement per step. The fields the replayed calls
  move: the position +004/+008/+00C (and copies +04C.., +0A8.., +0D0), +010 and +018 (velocity-
  like), +020, +02C, +01C, +024/+028/+030, +0B4, the steering pair +0B8/+0C0, +0C4/+0CC,
  +0D4, the speed +7D0, +7D8 (16 a step), +7E4, +7F8, the gear +7BC, +7C4, +680/+688, the
  orientation +058.. -- and NOT the step counters +0E0/+820 (written outside these calls).
  That is the halving work's list: each of those fields' writers inside +1DE78/+1F1F8 and
  +1EA64/+1FD5C to be made to advance half as far per call (or, for the discrete ones, only
  on even half-steps), until 2a's records match the normal build's at every whole step.

### Milestone 2b, changed course: the speculative half-step (A4_HALFSPEC)

Halving ~30 integer fields' writers by hand gives up the original handling and exactness
(half units of 1, 12, 16 do not exist). Instead, on an odd half-step: save every car the
step touched (the recorded calls' r0; every car is a 0xC7C-byte allocation: the six
+4B89C(0xC7C) sites +17A54 +17B12 +180F6 +1823C +18416 +25262, constructor +1999C, vtable
+B0434) and the key object's 16 words; take the keys (+25E8); replay the step's car calls
(a whole step ahead, with the keys of now); set each car half way between the saved and
the new state in its position and orientation words (+004..+00C, +04C..+054, +0A8..+0B0,
+0D0, +058..+06C) and render that. At the next even half-step put everything back, and the
game's own step runs from exactly where it was. Whole steps stay the original game's (the
oracle should read 0 differences); the screen gets a frame between steps, and a key held
from the first half shows in it -- half a step sooner, at half strength. Snapshots at
0x448000 (16 x 0xC7C; A4_SCRATCH_SIZE 0x5F000). Lockstep for the oracle: A4_LOCKSTEP=1 with
A4_LOCKSTEP_P=10667 (one half-step per update, a render after each, the even ones'
render-side car updates kept, the odd ones' undone with the cars).
Guesses to check: that nothing outside the cars and K is changed by the replayed calls
(the oracle sees any leak); which words the renderer draws the car from (only those need
the midpoint); the camera (if the step moves it outside these calls, the odd frame's
camera stays put while the car moves -- next).
- spec1 (a4_spec.exe; LOCKSTEP 1, LOCKSTEP_P 10667; 60 s): whole steps against the normal
  build (lsn1): 213 aligned race steps, 1 of 51 live fields differs -- +15C (the 0..9 cycle,
  written on the render side from time: the normal run's clock was the instruction count,
  this one's the synthetic half-period; it reads back into nothing measured). Every physics
  field identical: the speculation leaves no trace in the game's own steps.
- Next: what the speculative frames show. A4_RENDREC: after every render the mesh context
  (MESH->cam, the context the frame's faces were taken under) and the player car, through
  the same emitter ('CRNC', 'CRNP' headers, the half-step count): which words move into the
  frames after a speculative half-step, which only after whole steps (tools/rendrec.py).
- BUG (mine, found by a crash): the bench records (CREC 0x421800, HSTEP 0x421840, HCALL
  0x421860) sat inside the step block's third segment (STEPB 0x421700 + 3 x 128 bytes =
  0x421880). A segment rotation (a gap of 1.5 s between renders) copied segment 1 over
  them: the recorded car calls became garbage, the replay called it, and the render-recording
  run (specr) died with sp 0x310008 at a4_step_render's epilogue (1,034 faults after). Moved
  to 0x421900 / 0x421940 / 0x421960 with static asserts (STEPB below 0x421900, HCALL below
  the atlas at 0x422000). spec1, lsm1 and lsm2a ran with the overlap; their results stand
  only because no rotation fell inside the race (a corrupted call list would have crashed,
  as specr did) -- rule 4: the static assert that covered STEPB did not cover what came after.
- BUG 2 (mine; the real cause of specr's crash, specr2 died the same way after the move):
  the span trampolines (a4_tramp.s: the textured one moves the light into r11, the
  transparent one uses r8-r11) clobber high registers without saving them. Safe for the
  game's Thumb code, which never holds r8-r11 across a span; not for our ARM code that calls
  the render: a4_step_render came back with fp = 0xAAACC and sb = 0xD2282A and its epilogue
  took the stack (sp 0xAAAD4). The RENDREC code changed register allocation and exposed it;
  round 30's r30b (phone) has the same call and survived by its allocation (its pf reads
  after the render came back right: the data were sane) -- latent there. Fix: a4_gcall1 (a4_gl.s)
  pushes r3-r11/lr, calls the game function, pops; the render (+12992) and +12D5A go through it.
- specr3 (the shim build; rendrec.py over 272 renders of the player car): the position
  (+004/+00C and +0A8/+0B0, +008/+0AC/+0D0) changes into renders after whole steps and after
  speculative ones alike (56/56) -- the frames between carry the car's motion; speed,
  steering and the rest change into speculative renders (the speculative step's values) and
  hardly at all into the whole ones after them (the real step lands where the speculation
  did). +04C..+078 change only after whole steps: not drawing data but the collision box,
  built lazily once a step (+22C60: flag byte +7C, a virtual call for the extents, then
  +22D64 copies three vectors and the extents) by +1FF56/+ED9E/+1FDEC in +141B0, outside
  the replayed calls. The orientation matrix +6D4..+718 is the render's own (+31EA4,
  +321A6). The mesh context (MESH->cam) holds no camera pose (only +04C changes, 28 times).
  Open: the camera -- if only whole steps move it, the world still steps at 11.72 Hz on screen.
  A4_RENDREC now also dumps the race object and its context (0x4000 bytes each) per render
  (a4_rec_emit3; tools/camfind.py: words that change after whole steps, not after
  speculative ones, near the car's position).
- The camera (specr4, camfind.py over 317 renders): the race object's +0x58/+0x5C/+0x60
  change into renders after whole steps (111/101/111) and never into speculative ones, and
  sit by the car's x and z. It is an object at race+0x48 (vtable +B05FC; the pointer +12952
  hands to the render's per-car pass +D3BA as r3): +0x10/+0x14/+0x18 the position (e.g.
  18528, 77, 46952 against the car's ~18700, ~47000), +0x1C 2040 and +0x20 1660 angle-like.
  So on the speculative frames the car moves and the camera does not -- the world would
  still step at 11.72 Hz. Next: its writer (EKA2L1_WATCH on race+0x58..+0x6B, normal build),
  to speculate it with the cars.
- The camera's writers (wcam: EKA2L1_WATCH on race+0x58..+0x6B, normal lockstep build, 3,670
  writes after the race began, 18 sites in 5 functions): +4B40 (2,622), +3D2A, +57AC, +5968,
  +49C4 -- all under +5968, the camera's update, which three callers reach: +141B0 (the step,
  at +14F58), +13E10, and +12DEC (+12E44) -- and +12DEC is what the game's own between-steps
  routine calls: +1300C, run m - n times by the race update in slow motion, advances the
  effect objects of the list at race+0x3D0 (+22C0E: counters +0x34, +0x48) and then calls
  +12DEC(race, &0). The game already moves its camera between logic steps when it needs to.
- Spec v2 (a4_spec2.exe): the race object saved and restored with the cars (0x9EC bytes: +778A
  allocates it before its constructor +16BDC; vtable +B01E0; the camera at +0x48 inside it),
  and after the replayed car calls +12DEC(race, &0), then the camera half way (position
  +0x58..+0x60 plainly; +0x64/+0x68 -- ranges -33..0 and 533..2131 over a race -- the short way
  round a 4096 circle, a guess at the unit).
- SPEC v2 PASSED (spec2, LOCKSTEP 1 / LOCKSTEP_P 10667, 50 s): whole steps against normal
  (lsn1): 170 aligned race steps, 1 of 51 live fields differs (+15C, the time-driven render
  counter, as in spec1) -- the physics exact. Motion between renders (tools/smooth.py, 212
  renders after whole steps and 212 after speculative ones): the speculative frame's share
  of each whole step's motion, median 0.50 with quartiles 0.50..0.50 over 171 steps, for the
  camera and for the player car alike; mean move 42.0 into renders after whole steps, 42.2
  after speculative ones. The camera's +0x68 turns on both kinds of frame in a bend
  (196, 187, 177, 173, 168, 166, 163, ...). Before the camera was speculated (specr3/4) the
  camera's share was 0 and the car's 0.5.
- Not yet looked at: other moving things outside the car list and the camera (the effect
  objects +1300C advances, props), sound from the replayed physics (a crash sound twice?),
  real-time operation (renders back to back, n of 1, 2 or more half-steps per update: a
  speculative step followed by an even one in the same update is wasted, not wrong).
- Candidate r31 (real time, no lockstep, no recorder): C30's flags with A4_ABMODE=3118u
  (1070 + AB_HSPEC 2048: the half-step frames on in the first window), A4_ABCYCLE10 (a4_tel.c
  flips AB_HSPEC per window of TEL_EVERY = 23 renders; aggregates 'AG26', slot bit 1 =
  AB_HSPEC), A4_ABMARK (the 4x4 green square now marks AB_HSPEC on), A4_HALFLOGIC
  A4_HALFMOTION A4_HALFSPEC; env A4_HALFSTEP=1, A4_SCRATCH_SIZE 0x5F000. With AB_HSPEC off the
  odd half-steps are empty and draw nothing: the original game's frames and feel, on the same
  build. tools/r31sum.py.
- r31 bench run 1 (real time; 8 windows peeked): BUG 3, found by the half-steps a second --
  37.2, 44.7, 36.2, 33.2 in the AB_HSPEC-on windows against 23.44 at real time: the game ran
  ~1.7x fast with the speculative frames on. The race snapshot holds the step count's last
  time (race+0x44, written by +15B1C at the start of every race update, before the loop);
  restored at the next even half-step it went back, and the next update counted the same time
  again. The lockstep oracle could not see it (m and n forced). Fix: spec_restore right after
  the render that drew the speculative state (a4_step_render), before the next update's
  bookkeeping; the restore at the even half-step stays for an update that runs odd then even
  with no render between. Also: a skipped render's half-steps now go into the window's step
  count (the AB_HSPEC-off windows read 15.8-23.3 because they did not).
- r31 bench run 2 (the restore after the render; binary before the skipped-render count fix):
  half-steps a second in the AB_HSPEC-on windows 23.67, 22.93, 23.58, 23.42 -- real time
  (23.44); the off windows still undercount (that binary). Renders a second on the bench:
  on 8.1-13.9 (10.56 over 161 renders), off ~11 (138 renders): the bench's render costs
  70-120 ms of host time, longer than a half-step, so every update runs two half-steps and
  draws the speculative one -- no room for more frames here; the gain can only show where a
  render is shorter than 85 ms, i.e. the phone's light scenes.
- Rule 4 on the candidate: a speculative state left pending past its race update (the
  update ending after an odd half-step with no render) could be restored into the next
  race's objects. The step loop's early returns are all in whole steps (after the restore),
  so it should not happen; a4_step_count now drops (never restores) a pending state at the
  start of an update and counts it at 0x447000 word 30.
- Final candidate r31c = r31 + the restore after the render + the skipped renders' steps
  counted + the stale drop. Chain: lockstep oracle again (spec3: the same flags as spec2 with
  the new restore point), then telerun3 230 s and racemenu on r31c.
- r31 bench run 2, whole (telerun3 230 s): framestat 2843, faults 1 (the boot fault), the race
  through; aggregates 53 windows a mode: off 1,219 renders at 96.5 ms (10.36 a second), on
  1,219 at 91.7 ms (10.91 a second), >= 90 ms 49% / 46% -- the bench at its render-bound rate
  in both (above).
- spec3 (lockstep oracle with the restore after the render): whole steps against normal,
  129 aligned race steps, 1 of 51 live fields differs (+15C, as before) -- exact. Its motion
  split read 0.00 for the speculative frames: the instrument, not the drawing -- the
  per-render recorder (A4_RENDREC) sat after the new restore and recorded the restored state.
  Moved before the restore (the phone build, without RENDREC, compiles byte-identical:
  checked); spec4 re-measures after the battery.
- What the phone round can say that the bench cannot: whether the speculative frames show
  where the phone has room for them (light scenes: a render under 85 ms), how the frames and
  the keys feel (the owner's eyes and hands: smoothness, steering response), and whether
  anything outside the speculated set looks or sounds wrong (other moving objects at 11.72 Hz,
  a crash or skid sound twice from the replayed physics) -- the bench's renders are too slow
  for the first and it has no ears for the last. The A/B: the green square = AB_HSPEC on.
- r31c bench (peek, capidx 39-46): half-steps a second 22.8-24.2 in both modes (the off
  windows counted right now) -- real time. Window 43 (AB_HSPEC on, a light moment: 3D 5.0 ms
  a render): 24 renders in 1,411 ms, 58.8 ms a render, 17.0 a second -- more renders than the
  11.72 steps where the render is short, the effect the phone's light scenes should show.
- Exact binary r31c (sha256 f669e1b4b660baefa3d18a72a1f883a1c18fe66ac3686958da2c0b0397947b55;
  bss 0x5FF08, scratch top 0x460000; +3A15C bytes B510 0004; placeholders patched): telerun3
  230 s, framestat 2645, faults 1 (the boot fault), errc 0; aggregates: AB_HSPEC off 1,143
  renders in 49 windows at 100.0 ms, on 1,151 in 50 windows at 100.2 ms (the bench at its
  render-bound rate); half-steps a second 22.8-24.2 in every window of the ring.
  racemenu (r31c, 30 s race then 17 keys): framestat 2881, faults 1 (the boot fault); the race
  (166 km/h, the car, the city, the HUD), the pause menu stepped to MAIN MENU, the quit dialog
  (NO, YES), the main menu, career home, the car browser and back -- all drawn.
- spec4 (lockstep oracle, the recorder before the restore; LOCKSTEP 1 / LOCKSTEP_P 10667,
  framestat 922, faults 1 -- the boot fault): whole steps against normal (lsn1), 138 aligned
  race steps, 1 of 51 live fields differs (+15C, the time-driven render counter, as in spec1-3)
  -- the physics exact. Motion (tools/smooth.py, 190 renders after whole steps, 191 after
  speculative): the speculative frame's share of each whole step's motion, median 0.50,
  quartiles 0.50..0.50, camera (143 steps) and player car (138) alike; mean move 37.1 / 37.2.
  The camera's +0x68 in a bend over the last 12 renders: 405 405 405 382 358 346 334 328 321
  318 314 312, alternating whole and speculative -- both kinds turn. The spec3 0.00 was the
  instrument (now confirmed). Milestone 2b closed on the bench; r31c goes to the phone.
- Owner, during round 31: the green square shows as "around 1x3 pixels on the top left";
  for future builds a proper square, 4x4, top right ("I know you can see pixel-perfect, but
  I can't"). Cause, measured on the bench's r31cm screenshot (the emulator window shows the
  game at x2.75, turned half round): 3 x 11 window pixels green = a 1 x 4 game-pixel strip.
  The quad was pushed with the 3D, and the HUD, drawn after the composite, covered three of
  its four columns. Rounds 28 and 31 both shipped it so: their A/B reports rest on a mark the
  owner could only just see.
- Fix (A4_ABMARK, a4_gl.c mark_draw): the square is written into the game's 565 surface at
  the present (a4_gl_frame, after the flush's composite -- A4_IMM -- and after the HUD; only
  in a frame with 3D, had != 0), x 316-319, y 0-3, size A4_MARK_W (default 4). The condition
  is unchanged (AB_HSPEC under A4_ABCYCLE10, MESH_ON otherwise) and is read before
  a4_tel_tick flips the mode, so a frame is marked with the mode it was rendered in.
  Recipe check: C31's flags from the committed source rebuild r31c byte-identical; with the
  fix (r31d, sha256 19063cf9..., +144 bytes) markrun (30 s race, then 24 screenshots 0.5 s
  apart): framestat 842, faults 1 (the boot fault); the mark 11 x 11 window pixels, all 121
  green (= 4 x 4 game pixels), the game's top-right corner, in 13 of 24 shots in runs of
  2-4 -- off in the off windows, so the game redraws that corner every frame and the mark
  does not linger. r31d is not a phone build; the next candidate carries the fix.

## Round 31, phone (r31c, sha256 f669e1b4...): the speculative half-step on the N95

Owner's report (green = AB_HSPEC on): "the game feels a bit glitchy during the green mode.
Opponent and traffic cars would jitter or go back and forth as if they were lagging in an
online match. Steering also feels jittery. When I crash during green mode, it drops
framerate. Sometimes when I hit other vehicles during green mode, almost all assets would
disappear from the screen during one frame. I didn't hear any issues with the sound. [...]
the speedometer looked normal." (The mark they watched was r31c's 1 x 4 strip, above.)
- Telemetry (8 files, capidx 100-107; tools/r31sum.py): aggregates seg0 off 391 renders at
  85.3 ms (11.73 a second), on 391 at 70.9 ms (14.11 a second); seg1 off 828 at 87.7 ms
  (11.41), on 828 at 72.0 ms (13.90). Renders >= 90 ms: seg0 off 10% / on 17%, seg1 off 20%
  / on 10%. Last 8 windows: off 12.06-12.25 renders a second, on 12.44 (3D 40.7 ms), 17.20
  (20.0), 21.70 (14.1), 23.09 (12.4) -- in light scenes nearly every half-step drawn.
  Half-steps a second 23.80-24.53 in every window: real time. So the frame-rate half works on
  the phone (+20% renders on the whole, up to 2x in light scenes); the picture does not.
- What the bench never measured (rule 4, the guesses named before r31c, now answered):
  1. Only the player car and the camera were measured on the speculative frames -- and on
     the words spec_run itself writes (+004..+00C etc.), so the 0.50 split could not fail
     for anything spec_run touches. The opponents and the traffic were never looked at.
  2. Which words the renderer draws a car from was a guess (spec_mid); the steering pair
     +0B8/+0C0 and the rest of the replayed state show the speculative step's full value.
  3. Nothing checked what the replayed calls write outside the saved cars, the race object
     and K (a collision touches the other car, effects, sector or contact lists, sounds).
     The crash drop and the empty frame point there.
- Candidate causes to measure, not guess: (a) cars outside the recorded calls (the call log
  holds 32 calls; 14 car slots at race+0x394 in spec4's dumps, two loops of calls each, and
  the movement loop sets only a flag for some cars) or outside the 16-car snapshot stay put
  while the camera moves half a step -- on screen they slide back and jump forward;
  (b) the drawn pose read from words not midpointed; (c) the replay's writes outside the
  saved objects, never restored (effects spawned twice: the crash cost; a list left
  inconsistent: the empty frame -- and a crash risk).
- Bench, spec5 (spec4 + A4_RENDREC 'CRNA' every car slot race+0x394..+0x3CC and 'CRNS' the
  call log and the speculation header; tools/carsplit.py, 395 renders): calls per step 9
  (log 32); the player car share 0.50 (167 steps); the seven opponents (slots 1-7) share
  0.00 in all 167 -- saved, but their only recorded call was the movement +1EA64; slots 9-14
  parked at one spot (an idle pool: traffic or police not spawned on this route). So the
  opponents never moved on the speculative frames while the camera moved half a step: the
  owner's "back and forth as if they were lagging".
- Why: EKA2L1_WATCHONCE on an opponent's position (wopp): the race-time writer +23E4E,
  under +1EA64 -- which applies a step amount computed by the physics. The physics car loop
  (+14686..): every car whose index is not the player's takes +1F1F8 at +146FC (or +1FD0E at
  +146D6 in the +691E mode); the player's branch reaches +1495E/+14974, the only sites
  hooked. Fix: A4_HALFMOTION hooks +146FC (a4_h_1f1f8) and +146D6 (a4_h_1fd0e, kind 5;
  placeholder a4_hc_1fd0e), the call log widened to HCALL_MAX 48 with an overflow count at
  HSTEP[4].
- spec7 (the fix): calls per step 22; every opponent share 0.50, quartiles 0.50..0.50 (166
  steps each); whole steps against lsn1: 165 aligned, 1 of 51 fields (+15C, as always) --
  still exact.
- The drawn pose (spec7, the render's own words in the 'CRNA' dumps): the matrix the render
  writes, +6D4..+718 (a rotation about y in 1.14 fixed point), jumps a whole step on every
  speculative frame (share 1.00, player and opponents); it is exactly word +678 on a 2048
  circle (error 0.000 rad on every frame, three slots). +058..+06C, midpointed as
  "orientation" since 2b, never move on speculative frames (share 0.00): they are the
  collision box (+04C..+078), built outside the replayed calls -- the spec_mid guess was
  wrong. The steering pair +0B8/+0C0 and +680/+688 also show the full step (1.00). So when
  a car turns, its body turns only on the speculative frames, a whole step at a time: the
  owner's jittery steering.
- The camera's yaw +0x68 is on a 2048 circle too (fitted against the camera's direction
  vector +F4/+FC, a 1.12 unit pair: error 0.034 rad against 0.20 for 4096, three runs), and
  runs past 2048 (2131 at the start). spec_run's 4096 wrap (a guess written down in 2b) is
  wrong: a wrap on a speculative frame turns the camera half round for that frame -- one
  candidate for the owner's frame with nearly everything gone. The camera words that move
  into speculative frames: +58/+60/+68 at 0.50, and +70, +F4/+FC (the direction vector),
  +100, +388 at 1.00 -- which of them the render draws from is being measured (rcam8:
  EKA2L1_RWATCH over race+0x48..+0x390, gated to the render by A4_RENDGATE).
- The replay's writes outside the saved objects (leak6: A4_SPECGATE opens EKA2L1_WATCHGATE
  at 0x44707C from the keys to the camera update; EKA2L1_WATCH 0x400000..0x4000000; spec5's
  hooks, 309 openings, 638 words): the stack (149), the cars and the race object (saved),
  K at 0x01F0029C (saved), and three outside: the heap's own words (0x01F00014/18/34,
  RHeap's cell count, total size and lock, written from the ROM), a 3 KB block at 0x02020FFC
  zeroed from the ROM and filled by the game, and a list at 0x01F39780 linked into it -- the
  camera update (+5912 bl +5205A: unlink a node and delete it, +4B8E4) allocates and frees
  during the speculative update. The same addresses every time and no heap pointer in the
  race object changes on speculative frames, so it looks freed within the call; collisions
  were not in this run (the autopilot drives straight).
- Which camera words the render draws from (rcam8, spec8 = spec7 + A4_RENDGATE: SPEC[31] = 1
  around the render call; EKA2L1_WATCH on the gate word, EKA2L1_WATCHGATE, EKA2L1_RWATCH over
  race+0x48..+0x390): +0x58..+0x6C (position, then three angles; read at +D3D6..+D3EC from
  the render +12992), +0x70 (+C9BC), +0x74, +0x8C/+0x94 (constant), +0xB4..+0xE0 (constant),
  +0x368..+0x38C. Not the direction vector +F4/+FC. So the drawn camera on the speculative
  frames was already half way in position and yaw; its yaw's unit was the bug.
- Which car words the render draws from (rone8: one opponent's 0xC7C bytes, gated to the
  render, reads by our own code skipped -- EKA2L1_RWATCH_SKIP; the first try, rcar8, watched
  an address that in that build held a texture, and the second, rcar9, over all cars, filled
  its 60,000 pairs with texture reads): +004..+00C, +01C, +020, +034, +108, +168, +1A8, +678
  (at +A44C), +818, +824, +A04, +A20 (+A6F4), +A24, +A68, +A78, +A7C, +AAC, +AB0, +AC0, +AE0,
  +B14, +B28, +C3C. Not the matrix +6D4 (computed in the step; it stays at the whole step's
  value on speculative frames and is not drawn).
- The fix (a4_gl.c spec_run): cars -- position (+004..+00C, +0A8..+0B0, +0D0), +7D0, +7F8,
  +A20, +A24, +A7C half way plainly; the heading +678 the short way round 2048; the wheels
  +020 (0..4095, at most 666 a step at 135 km/h) round 4096; the collision box +04C..+06C
  dropped. Camera -- position plainly, +0x64/+0x68/+0x6C round 2048 (mid_ang2048: the
  difference modulo 2048, so values past 2048 work; host-tested on wraps, angtest.c, which
  also shows the old 4096 rule putting 2040 -> 10 at 1025, half round), +0x70 plainly.
  +0x388 left alone: a midpoint written before the render read 1.00 after it -- the
  render's own counter.
- spec10 (the fix, lockstep, full 0xC7C car dumps; tools/drawshare.py): every car's position,
  heading, wheels and +A20/+A24 at 0.50 on the speculative frames (player and the seven
  opponents); the camera's +0x58/+0x60/+0x68 at 0.50; small integer counters (+7D0, +0x70,
  +A7C) read 0.00 -- half of a one-unit step rounds down; whole steps against lsn1: 144
  aligned, 1 of 51 (+15C) -- exact.
- Leak check with collisions (leak10: the phone build's flags + A4_SPECGATE, real time, the
  race driven by keys -- '5' held, '4' and '6' two seconds each, into walls; 333 openings):
  the game thread wrote, outside the saved cars, race object and K, one word: 0x00500148
  from 0x7020AAFA, inside libc.dll (its own data -- errno from a maths call or rand()'s
  state; if rand(), a crash's randomness after a speculative frame differs from the
  original game's: a guess, not a glitch). Writes from other threads (the second stack at
  0x3401xxx: audio) land while the gate is open and are not the replay's. The camera's
  allocation seen in leak6 did not appear here.
- Rule 4 on the earlier record: the spec2-spec4 "0.50 for the camera and the player car"
  measured the words spec_run itself wrote, so it could not fail; the opponents, the drawn
  heading and the camera's unit went to the phone unmeasured. carsplit.py and drawshare.py
  measure every car slot and the words the render reads (by the read-watch), not the words
  spec_run writes.
- Telemetry for round 32: header [63] in a half-step build = the speculation's ticks this
  window (low 16 bits) and its longest single run (high 16), replacing +12D5A's ticks (0
  since round 30) -- what the replay costs on the phone, and in a crash.
- Rule 4, a state the bench never entered: the game's slow motion (m > n: the race update
  runs m - n +1300C updates and renders with no logic step). A4_HALFLOGIC skips a render
  with no half-step since the last one, so those frames would be dropped, and with the
  period halved m counts half-periods, so +1300C would run twice as often. Latent: the
  round 31 phone race had m - n = 0 over 2,506 updates (both segments, crashes included),
  as had every bench race. Not fixed; written down.

## Round 32 candidate (r32): the speculative frames, corrected

r31c + the opponents' physics hooked (+146FC, +146D6), the midpoints moved to what the render
draws (cars: position, heading +678 on 2048, wheels +020 on 4096, +7D0 +7F8 +A20 +A24 +A7C;
camera: position, angles on 2048, +0x70), the A/B mark a 4x4 square at the top right drawn
after the HUD, the speculation timed into header [63]. C31's flags unchanged (A4_ABCYCLE10:
AB_HSPEC flips every 23 renders; green square = on).
- Exact binary r32 (sha256 97b3ee20e90086275f283e61100c66dcb2fbfdee1f424735511d0e7f7322b00b,
  800,648 bytes; no 0xDEADBEEF; +3A15C bytes B510 0004; no bench flag in C31).
- Battery on that binary: telerun3 230 s: framestat 2916, faults 1 (the boot fault); half-steps
  a second 23.0-24.2 in every window (real time); the speculation 2.1-3.0 ms a run on the
  bench (host time), longest 6 ms; aggregates off 1,200 renders at 94.5 ms, on 1,219 at 91.1
  (the bench render-bound). Steering race (steerrun.sh, 90 s, '4'/'6' into walls): framestat
  1540, faults 1; the speculation 1.4-2.1 ms a run, longest 3. racemenu (30 s race, 17 keys):
  framestat 3009, faults 1; the 18 screens drawn, non-black counts matching r31c's run screen
  for screen (pause menu, quit dialog, main menu, career, car browser). Mark (markrun): an
  11 x 11 window-pixel green square (4 x 4 game pixels) at the top right in 13 of 24 shots,
  absent in the rest.
- What the phone round can say that the bench cannot: how the corrected frames look and feel
  (opponents, traffic, steering, crashes), the speculation's cost on the ARM11 (header [63]:
  per run and the longest, in crashes too), and whether the frame drop on crashes remains.
- Guesses still in it (rule 4): moving things outside the car slots (if any: police
  helicopters, props) still step at 11.72 Hz; traffic was parked on the bench route, so its
  share was never measured moving (same class and physics loop as the opponents); libc's
  word written in a collision replay (errno or rand); slow motion (latent, above).

## Round 32, phone (r32, sha256 97b3ee20...)

Owner: "For the first time, I see the game running at what it seems to be 30 fps during
light scenes (namely, tunnel with almost no cars) on green mode. However some of the physic
bugs are still there (steering jittering, heavy framedrop during takedowns and even more
during crashes.) There was one instance where the game would do the same 1 frame bug I
mentioned before. Another bug that I've notice several builds back is that sometimes it
seems like the car's shadow would start clipping through the road. And sometimes the cars
would also seem to pass just a bit through the pavement, especially after a big jump from
the tunnel (it can happen anywhere, but I saw it more consistently there). Also please make
the green box bigger (16x16 maybe?)"
- Telemetry (8 files, capidx 86-93; r31sum.py --r32): aggregates seg0 off 1,058 renders at
  88.7 ms (11.28 a second), on 1,082 at 79.3 ms (12.62); renders >= 90 ms 25% / 24%. Last 8
  windows: off 12.05-12.21 a second, on 11.87-15.80; half-steps 23.7-24.4 a second (real
  time). The speculation on the ARM11: 1.17-1.53 ms a run, longest 2-4 ms (these windows
  only; the ring holds the last 8). Step block seg0: 2,194 spans, 3,754 half-steps (n = 1:
  851 spans, n = 2: 1,143, n = 3-5: 200), logic 1.3 ms a half-step, longest span 31 ms;
  m - n sum 1 (one slow-motion update in the race); the render call 66.4 ms.
- Reading it: with renders at ~65 ms and half-steps every 42.7 ms, the speculative frames
  show the game at half-step instants -- each frame advances one or two half-steps (42.7 or
  85.3 ms of motion) for ~65 ms of real time, unevenly. The original game's frames each
  advance one whole step and are presented a step apart. That unevenness is a candidate for
  the steering jitter that the measured 0.50 split cannot show (the split is per frame, not
  per real time). Measured next: show each frame the game at the time it is drawn
  (fraction of a step from the game's own clock), not at the last half-step.

## Round 33 (bench): frames at the game's time; cuts; the render's own changes kept

- Render-time speculation (a4_step_render, A4_HALFSPEC): the odd half-steps only mark a
  render as worth drawing; at every render with AB_HSPEC on, the fraction of a step since the
  last whole step comes from the game's clock (t = K+0x2C, the time +15B1C counted from:
  t mod 10667 by the multiply +52B98 uses, plus a half-step when the last half-step was odd;
  q16 = that * 65536 / 21334 by a reciprocal multiply), and spec_run draws the frame that far
  between the whole step and its prediction (lerp_q16; angles lerp_ang on 2048 / 4096). Under
  1/32 of a step (2.7 ms) the whole step is drawn as it is.
- Cuts: a car or the camera whose position moves more than 1024 in x or z over the predicted
  step (a respawn, a camera switch) is drawn where it was (the car restored from its snapshot,
  the camera's +0x58..+0x70 put back), not half way across -- the other candidate for the
  one-frame empty picture.
- Pacing, measured against the game clock (tools/pacing.py: per render, the player car as
  drawn over the game time between renders, against its neighbours' median; real time, the
  recorder builds with the autopilot): r32's logic (rtold) median 1.00, quartiles 0.91..1.07,
  10-90% 0.73..1.27, 21% of frames off by more than 25%; render-time (rtnew) 1.00..1.00,
  0.99..1.01, 0%. Game time between renders ~91 ms on the bench (render-bound); on the phone,
  frames ~65 ms against 42.7 ms half-steps, r32 advanced 1, 2 or 3 half-steps a frame (39 /
  52 / 9%), so its unevenness there was larger than the bench's.
- BUG found by the oracle (rule 4): spec11 (lockstep, every render now speculated, the whole
  restore after each) -- the player car's per-render flip +66C differed in 167 of 167 steps:
  the restore undid what the render itself changes (the flip, the countdown +164, the race's
  +0x388), and r32 had kept those changes on its whole frames by accident. Fix: spec_run marks
  the words it changed (bitmaps at 0x455000: 25 words a car, 20 the race, 1 K); the first
  render after each whole step (HSTEP[5]) keeps its own changes and gets only the marked words
  back; every other render is undone whole. spec12: 162 aligned steps, 1 of 51 (+15C) --
  exact again, the flip advancing once a step as in the game.
- Telemetry: STEPB[3] the speculation's ticks, [6] its runs, [7] the longest run and (<< 16)
  runs of 8 ms or more, per segment (they rotate with the aggregates) -- for the takedown and
  crash drops over the whole race, not only the ring's last 8 windows.
- The A/B mark 16 x 16 (owner).
- Shadows clipping into the road (owner), measured: A4_SHADSINK (new, check build with
  A4_MESHCHECK + A4_DUALCAP: software draws the frame and its 1/z buffer holds every
  perspective face drawn so far): at each shadow triangle of a list frame, its pixels where
  the game's buffer is nearer than the shadow -- what a depth-tested GPU shadow loses that the
  game (which never tests a shadow) paints -- at biases 0..1/4. telerun3 120 s (shsink):
  219,104 shadow triangles, 43,940 covered (tested on the GPU); of their 2,183,883 pixels
  2.9% hidden at every bias from 0 to 1/4 alike; the untested ones' 1.5% likewise. No near
  tie with the road anywhere: what hides them is far nearer (car parts drawn before the
  shadow, which the game paints the shadow over). So on the bench route the road does not
  cut a shadow at all; the owner's case is terrain the route lacks (ramps, the tunnel jump)
  or the speculative frames (a straight line between two steps runs under a crest or a ramp's
  lip by a few units: the car, and a shadow placed from it). Not changed; the owner asked to
  say which mode it shows in.
- Depth precision ruled out for the sinking cars: the N95's pbuffer has 24 depth bits (round
  11), depth = ZK / w with ZK 128 -- at w 400 a step of 2^-24 is 0.00008 units.
- Candidate r33 (C31's flags; render-time speculation, cuts, the selective restore, the 16 x 16
  mark, the per-segment speculation counters; sha256
  0c5138e2fefef66cd682bb797f51cef1c0bba1dff7d22fe9657f7caff9ad229c, 802,276 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery on it: telerun3 230 s framestat 2948, faults 1 (the
  boot fault), half-steps 22.7-24.0 a second; the speculation 3.0 ms a run on the bench (host
  time; the phone ran r32's at 1.2-1.5), 1,257 runs, longest 7 ms, none of 8 ms or more;
  steering race (90 s, into walls) framestat 1520, faults 1, longest run 11 ms, 2 of 8 ms or
  more; racemenu framestat 2922, faults 1, the 18 screens as r32's run; mark 43 x 43 window
  pixels (16 x 16 game pixels) at the top right in 9 of 24 shots, absent in the rest.
- Asked of the phone round: whether the steering still jitters (the pacing fix is the
  candidate), whether the empty frame and the drops in takedowns and crashes remain (STEPB
  [3]/[6]/[7] say what the speculation costs over the whole race), and in which mode (square
  on or off) the shadow cutting into the road and the cars sinking show.

## Round 33, phone (r33, sha256 0c5138e2...)

Owner, with 10 photos: "the car clipping during a crash. It started to clip before the green
came in. The shadows seem to clip or draw wrongfully with the green off. The steering felt
much better this time. The empty frame looks a lot like the last picture. In fact, this is
what I was talking about. The crashes and takedowns are still sluggish."
- Photos (r33photos/): 1-4 the crash camera ("TOO CLOSE..."), the player's car upside down --
  1 and 2 (no mark) with black slabs reaching off the car, 3 (no mark) and 4 (mark) the car in
  fragments over the road; 5, 6, 8, 9 the LA tunnel, no mark, the car's shadow as a grey band
  beside and behind the car; 7 = 10 (no mark): the "empty frame" -- the skyline backdrop, the
  car, one striped grey block and a strip of road, the scenery missing. So the empty frame and
  the crash pieces both happen with the speculation off: renderer faults, not round 31-33's.
- Steering: "much better" -- the render-time speculation (round 33's pacing fix).
- Telemetry (capidx 84-91): aggregates off 1,035 renders at 89.6 ms (11.16 a second), on
  1,058 at 79.9 (12.52); >= 90 ms 29% / 27%; light windows on 17.2-19.1 a second. Step block
  seg0 (2,148 spans): the speculation 1,007 runs, 2.20 ms a run, longest 6 ms, none of 8 ms
  or more; logic 0.9 ms a half-step, longest span 6 ms; m - n 0 (no slow motion). So the
  crash and takedown slowness is not the speculation (its longest run in the whole segment is
  6 ms) and not the logic: it is in the frame (3D, composite or the game's own render).
  errc 0, depth bits 24, textures cached 167.

## Round 34 (bench): the round 33 photos

- Same-frame comparisons (check build, A4_MESHCHECK + A4_DUALCAP: the game's own picture and
  the GPU's 3D in each capture; tools/dualpic.py). The GPU picture holds the 3D only (the HUD,
  the sparks and the tags are the game's 2D after it), so a whole-frame pixel count is mostly
  the HUD; the pictures were compared in the car's region.
  - The LA tunnel (tunrun.sh, the tunnel reached at 172 s; 8 captures): the player car's
    shadow the same in both -- a darker grey patch beside the car in the game's own picture
    too. With A4_SHADSINK's measurement (no road tie at any bias), the tunnel shadow is not
    reproduced as a fault on the bench; the photos' grey band beside the car may be the
    game's own shadow there.
  - A camera at a quarter of its distance from the car (A4_CLOSECAM=4, new bench knob: the
    camera pulled toward the player car for the render and put back): both pictures alike.
    Faces nearer than ZK (128) never enter the list (mesh_take), so GL's near plane cuts
    nothing; the crash camera's pieces are not a near-plane fault.
  - A crash on the bench (crashrun.sh, new: steering into things, stopped when the crash
    camera's black bars show): none in 240 s; the crash camera stays unreproduced.
- The empty frame, read from the bench's own snapshots: the game's surface caught between the
  backdrop and the scene looks exactly like the owner's photo 7 (skyline, one grey block, no
  scenery). In the GPU path the scene reaches the surface only at the composite, and the list
  (scenery, most car parts) is drawn at the frame's first taken triangle. One path drops it
  silently: mesh_draw discarded a gathered list when a present had come between its inserts
  and the walk (m->frame != s->frames) -- its faces taken from the game and drawn nowhere.
  Fills and billboards cannot start a frame (both need prims != 0), and the list overflowing
  goes back to the table, not lost. Changed: such a list is drawn (counted, MESH->stale). A
  guess at the mechanism, not a measured one: the bench never hits it.
- A4_SLOWREC (new, for the phone): the race's 16 slowest 3D frames split by phase (3D, hook,
  composite, 2D, present, logic, speculation, the render call and its part before the first
  triangle, triangles, list drawn / lost, mode, list never drawn by the present, stale lists),
  and the last 8 frames in which list triangles were lost -- appended to every capture after
  the step block (tools/slowsum.py). Bench run (r34a, telerun3 230 s, framestat 3009, faults
  1): the slowest frames 150-211 ms (host time), nearly all of it inside the game's render
  call; several with the list full at 2,048 triangles (the rest to the table); no frame with
  list triangles lost, no stale list.
- Candidate r34 = r33 + A4_SLOWREC + the stale list drawn (C31's flags + -DA4_SLOWREC; sha256
  24f98e6b66646e070ad50ca0bd3ba9f21bac04e844f4c569e084fd9a2d8d4858, 803,584 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery on it: telerun3 230 s framestat 3009, faults 1 (the
  boot fault); steering race 90 s framestat 1572, faults 1; racemenu framestat 3087, faults 1,
  the 18 screens as r33's; mark 43 x 43 window pixels at the top right in 12 of 24 shots;
  slowsum reads the block from the captures.
- Asked of the phone round: whether the empty frame or the crash camera's pieces come back
  (the lossy-frame record says whether a list was dropped or lost then), and a race with
  crashes and takedowns (the slowest-frame record says where those frames spend their time).

## Round 34, phone (r34, sha256 24f98e6b...)

Owner: "So with the takedowns and the crashes it no longer drops framerate. It's hard to
measure and time it with the mode switching constantly. But the moments I crashed and it
wasn't green, it looked like previous builds prior to the framedrops. And that same sequence,
when green came in, looked smoother. The car shadow clipping happened regardless of the mode."
(No empty frame or crash pieces reported this time.)
- Telemetry (capidx 175-182): aggregates seg0 off 2,093 renders at 87.6 ms (11.42 a second),
  on 2,094 at 80.5 (12.42); >= 90 ms 24% / 22%; the light windows on 22.1 a second. Step
  block seg0 (4,241 spans): the speculation 2,022 runs, 2.10 ms a run, longest 8 ms, one of 8
  ms or more; logic longest 7 ms; m - n 20 (slow-motion updates: round 33's race had none);
  2 loads of 500 ms or more left out.
- A4_SLOWREC: 31,953 3D frames seen; the slowest 108-136 ms, all but one with the render
  call 99-142 ms of it (3D 41-81, composite 11-33, 2D 0-38) -- the game's render, not the
  logic (0-6 ms) or the speculation (0-2 ms); one frame of 932 ms with 142 ms of render call
  and nothing else timed: a load or a stall outside every phase (green mode, frame 15987).
  No frame lost list triangles; no list was left undrawn by its present.
- The stale-list counter (MESH->stale) is in neither the header nor the record: a stale list
  that was drawn shows in no entry unless it is also among the slowest. Not knowable from
  this round whether round 34's change fired. The crash drops the owner no longer sees are
  therefore not attributed to it; nothing else in r34 touched the frame's work.
- The shadow: the owner's tunnel photos (r33photos, cropped) show the car's shadow in
  pieces -- a wedge beside the car, the rest missing, or cut across -- in both modes. The
  round 33 measurement (A4_SHADSINK, 120 s) never reached the tunnel (172 s on the bench).
- The shadow, again (bench): shsink8 (A4_SHADSINK with captures every 8 renders, tunrun.sh
  EXITMODE) stopped at a dark stretch at 83 s with a frozen scene (five captures with equal
  counts) -- not the tunnel; no result. The real tunnel's same-frame pairs (shtun, round 34's
  run: captures 26-33): a darkening count by brightness ratio (game picture over GPU picture
  in 0.62-0.85, a shadow's 12/16) is swamped by the road texture's grain (the game's road
  dithered, the GPU's smooth: speckle over the whole road, tools in the scratchpad); the
  pictures themselves show the shadow the same way in both -- a dark patch beside the car's
  rear, in pieces. So on the bench route the patchy shadow is the game's own drawing; the
  owner's "clipping in both modes" fits that. Not settled for every place (the owner's photos
  are lap 3 of a WANTED race); the stock game in the same tunnel would settle it.
- Next build r35: the A/B ended (owner: "hard to measure and time it with the mode switching
  constantly"; green "looked smoother"). C35 = C31's flags without A4_ABCYCLE10 and
  A4_ABMARK, plus A4_SLOWREC: the mode fixed at 3118 (the speculation, the list, lean,
  reorder, the re-presents skipped, native read), no mark (it would mark every list frame).
- Candidate r35 (C35; sha256 1cd950fd7f8f957d41d4ceed960c8912badccd19ebc0e66a5754a55ab9413388,
  803,288 bytes; no 0xDEADBEEF; +3A15C B510 0004). Battery on it: telerun3 230 s framestat
  3006, faults 1 (the boot fault), mode 3118 in every window, the speculation 2.8-4.0 ms a
  run in every window (2,592 runs in seg0, longest 19 ms on the bench host, 6 of 8 ms or
  more), half-steps 23.2-23.8 a second; steering race 90 s framestat 1589, faults 1; racemenu
  framestat 3244, faults 1, the screens as r34's; no green square in 24 of 24 shots.
- Owner, after r35 was sent: "the frame lost scenery almost always happened constantly when
  going up the hill before the pavement surrounded by cones". So the empty frame is a place,
  not a rare event -- reproducible on the bench if the route passes it. Round 34's record (no
  frame with list triangles lost or a list undrawn, 31,953 frames) then says it is not the
  list being dropped, if the owner passed the hill in that race. Measured next: the presented
  frames themselves on the hill (EKA2L1_MEMSNAP of the surface, its PC range narrowed to
  a4_gl_frame -- 0x700BEFAC..0x700BF5B8 in r35 -- so each snapshot is a finished frame;
  snaprun.sh, every 100 ms).
- The hill (bench, r35): the race passes it each lap (laps ~70 s on the bench: "lap 3/3" by
  ~200 s). Window screenshots every 0.25 s through a whole lap (winshots.sh, 240 shots): the
  tunnel, the climb past "WHAT A JUMP!", a takedown camera, lap 2, the green climb and the
  coned start area -- every frame whole. The empty frame is not reproduced on the bench.
- A measurement thrown out (rule 4, test the test): EKA2L1_MEMSNAP_AT (new, emulator: a
  snapshot at the entry of the block at exactly one PC) at a4_gl_frame, then at a4_pres_tramp
  (which a skipped present also reaches: the detour returns past the present call into +1EE),
  then at +1EA (reached only by a real present) gave 85% "skyline frames" -- the backdrop over
  the last frame, no top HUD -- with the speculation on and off alike, while the telemetry's
  own capture of the surface at the same place (inside a4_gl_frame) and the window's
  screenshots show whole frames. The snapshots do not show what is presented; why is not
  found. Counting instead (A4_SLOWREC words 19-31): 851 real presents in 60 s, one without a
  render since the last (the race's first), no render skipped.
- The owner's photo 7, read again: the HUD, the car and the road are drawn, the terrain, the
  trees and the houses are not -- both list and table classes missing, so not one path. r36's
  record adds, to the lossy ring, any frame with half the triangles of the 3D frame before or
  fewer (from over 600), with its breakdown and the triangles before it (A4_SLOWREC word 18
  counts them), and keeps the present/render counters (words 19-31).
- Candidate r36 (C35, r35's flags; the record grows the drop-frame entries above; sha256
  aa391f02057e5d62a7842c526306956256a961ec72965c9d2e2a61c2f86768fe, 803,552 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery: telerun3 230 s framestat 3266, faults 1 (the boot
  fault); steering race 90 s framestat 1743, faults 1; racemenu framestat 3074, faults 1, 18
  shots; mark framestat 942, faults 1. Its record: drop frames 3 -- (23 ms, 70 triangles,
  list 0, before 1,378), (75 ms, 1,048, before 2,208), (25 ms, 113, list 0, before 1,879);
  real presents 2,932 (8 with no render since the last, 10 with no 3D), skipped 5,862,
  renders 2,924, none skipped by the half-step. Not sent: superseded by round 37 below.

## Round 37 (bench): the empty frame, reproduced and traced

- A4_DROPCAP (a4_tel.c, new): captures only the frames A4_SLOWREC counts as drop frames
  (the capture gate otherwise skips any frame under 100 triangles, where these sit).
  r36d (C35 + DROPCAP, 230 s): 3 captures -- the race intro's cut (991 of 2,269) and two
  EMPTY FRAMES, laps 2 and 3, the same place on the hill: the HUD, the car, the near road
  and grass, the city skyline where the hill's terrain should be, no terrain, trees or
  houses (48 and 27 triangles against 1,550 and 1,660 the frame before; the game's render
  call 19 and 15 ms, its part before the first triangle 4 and 3 ms, against ~120 and ~50).
  The owner's photo 7, on the bench.
- Not the speculation: r36doff (C35off, mode 1070, + DROPCAP): the same empty frame, laps 1
  and 2 (62 of 1,384 and 78 of 1,320).
- EKA2L1_POSTDUMP (emulator, new; EMU_PATCHES.md): every direct-screen post, as posted,
  to a file -- what reaches the panel, where MEMSNAP caught half-drawn frames. The stock
  game (baseline exe, 230 s, 2,460 posts, 3 laps): the hill located in each lap by the HUD
  minimap's track line (mapmatch.py: posts 953, 1646, 2340, ~76 s apart); the frames there
  all whole. A first one-frame-glitch finder (frame unlike both neighbours) missed the
  green build's own empty frame (glpd: posts 1205-1206, posted twice, the neighbours
  themselves far apart at speed) -- rule 4, test the test: thrown out; tools/postfind.py
  instead finds each drop capture's picture among the posts (0.00 = that very frame).
  The stock result does not exclude the stock game: the empty frame is narrower than one
  frame's travel (never two empty renders in a row; our builds hit it in about half the
  laps: r36d 2 of 3, glpd 1 of 3, r36doff 2 of 3), so 0 of 3 laps at stock's rate is
  weak.
- A4_DROPHOLD (a4_gl.c, new): a real present whose 3D is under an eighth of the 3D frame
  before (from over 600) is not made. The empty frames measured 0.002-0.06 of the frame
  before; every legitimate cut seen 0.33-0.47 (the intro, the takedown and crash cameras:
  r37d captures 1-5). r37d: held once (43 of 1,608) -- and the frame still reached the
  panel, posted twice (posts 1146-1147). Two causes, found one at a time:
  1. The game also presents from the same site (+9A6) with no render since the last
     present (r37d: 159 times); that posts the back buffer again, still holding the frame.
     r37e holds those too (same call site, bounded): held once (77 of 1,341), 0 such
     presents after it -- and post 2275 is the held frame again.
  2. EKA2L1_POSTDUMP_CODE (new: the first game-image Thumb return address on the guest
     stack at each post) named two posting sites, +1EF (DoPresent's present call +1EC) and
     +6B7; the held frames' posts came from +6B7. (The attribution reads stale stack words
     too: r37f's lap 1 is all +6B7, so it is a hint, not a count.) The trace settled it:
     EKA2L1_PCTRACE_REGS at 13 PCs in a ring dumped at a4_drop_mark (a marker the hold
     calls; A4_PRINTSYMS prints its address), r37g: each race iteration is update +1670E,
     render +12992, DoPresent's present +1EA, the race loop's +9A6 returning, then the
     framework frame routine +606 called from +9BE (state 0x0C), its post +6AC/+6B4
     ([r4+0x3C]->vtbl[0x34](this, 0)), then the two re-presents +950/+97E. At the hold:
     no +1EA (our skip) -- then +6AC: the framework posted the back buffer itself; the next
     iteration (no render) the same. +606 is the framework's frame: clear the back buffer
     unless a flag says not, the game's draw (vtbl 0x1C) unless the state is 0x1E, then the
     post unless the state is 0x1E and [g+0xCDC0+0x30] byte 10 is set (+4B848).
- r37h: +6B2/+6B4 (movs r1,#0 ; blx r2) become a BLX to a4_fwpost_tramp (a4_tramp.s),
  which skips the post while a frame is held (a4_fw_hold, bounded with the held presents
  at 8) and otherwise makes it as a tail call; +606's caller does not read its result
  (+9C2 ldrb r0,[r7,#8]). Bench r37h (with the trace, 230 s): no empty frame that run (0
  held, 1 drop frame: a cut, 446 of 1,349, posted as it should be). r37i, the same exe
  without the trace: held once (47 of 1,612), with 4 render-less presents and 4 framework
  posts held after it -- the bound of 8 -- and the frame then posted twice (posts 2334 by
  +6B7, 2335 by +1EF, 56 and 73 ms after the posts before: no stall). Not slow motion: the
  empty render is quick (19 ms against ~70), so the race loop comes round before the next
  half-step is due and iterates with no render (m = 0), and a held present skips the
  present call's pacing, so 8 holds go by within milliseconds. r37j: the hold lasts until a
  render brings a frame, at most 250 ms (a4_ntick, 1 ms on the bench and the N95), with no
  count.
- Rule 4, the guesses: the cause of the empty render itself is NOT found -- the theory is
  the game's own visibility failing for a camera inside a narrow stretch of the hill; the
  hold hides it, it does not fix it. The 1/8 threshold rests on 6 empty frames and 7 cuts
  seen. What +6B4's method does besides posting is not read; it is skipped only for a held
  frame (1-2 times a lap at most).
- r37j (the 250 ms hold): no drop frame in that race (chance). r37k, the same exe: held
  twice (18 of 690, a crash camera at speed 0 -- held one frame, harmless; 67 of 1,086,
  lap 2's empty frame) and neither reached the panel (nearest posts 46.9 and 20.6) -- but
  lap 1's empty frame did (post 1480, by +1EF), and the record never counted it as a drop.
  That post dump was deleted before it was read further (lost; rule 1).
- r37m (A4_FRAMEMARK, bench only: every present that brings 3D or goes out calls
  a4_frame_mark(had, prev, frames, skipped) for EKA2L1_PCTRACE_REGS, and stamps its present
  count in the bottom row, so each post names its present; tools/poststamp.py ties them):
  held twice (87 of 1,546, 107 of 1,771), all 2,767 race posts tied to a present, no post
  of a frame under 300 triangles -- yet tools/emptyfind.py (posts against the captured
  empty frames, y 40-200 x 50-320) finds post 2977 at 8.7 (neighbours 52-56): stamp 8007,
  a present MADE with 0 triangles since the last, the 3D frame before 1,771 -- three
  presents before the held 8010. So two empty renders in a row (rule 4: "never two in a
  row" was wrong), the first with no 3D at all. And what the empty frame is: the game draws
  its skyline backdrop in software every render; our composite rewrites only the rows the
  GPU drew (miny..maxy); a render with no or almost no 3D leaves the backdrop over the
  previous frame's bottom (the car, the near road) under a fresh HUD -- round 35's MEMSNAP
  "skyline frames" (the backdrop over the last frame) were this, not a bad snapshot. After
  the hold at 8010: 220 presents and no post in 83 ms (render-less spinning, all held).
- So the question is why some renders on the hill submit no 3D. Next: the GL build with
  the half-step scheme off (no A4_HALFSTEP, no HALFLOGIC/HALFMOTION/HALFSPEC, mode 1070),
  r37nh, through emptyfind.
- r37nh (GL, the half-step scheme off: no A4_HALFSTEP, HALFLOGIC, HALFMOTION or HALFSPEC,
  mode 1070, 230 s): emptyfind finds no post nearer an empty frame than 45.7 (the hill
  passed twice, mapmatch posts 1165, 2179). With the stock game's 0 of 3 laps: 0 of ~5
  laps without the scheme against ~15 of 33 laps with it (r36d-r37n) -- the half-step
  scheme makes the empty renders (p ~ 0.04 by chance), the speculation not needed for it.
- r37n (A4_FRAMEMARK + a4_render_mark: at every render's entry the words of the object at
  [race+0x38] whose bytes 4 and 9 make +12992 skip its 3D, +12A18-+12A26): 0 at all 2,837
  renders -- not that flag, at least not at entry. Held twice, and another empty frame
  posted (2079, by the framework's post): the collapse took two renders, 1,398 -> 178
  (0.127 of the frame before: just over the 1/8 threshold) -> 68 (the frame before 178:
  under the 600 floor) -> a render-less present that posted it -> 1,350. Its posts: 2078
  (178) the hill drawn flat with no car, no trees, no helicopter; 2079 (68) the skyline,
  no hill, no car. Not the drawing: the CAMERA -- ahead of the car, then inside the hill,
  where the terrain's faces turn away and are culled, leaving the far buildings. The step
  record: m - n sum 0 (no slow motion: the +1300C theory is out); 37 render calls with no
  triangle at all in the race.
- r37o: the camera (race +0x58..+0x68) and the player car (+0x04..+0x0C, heading +0x678)
  logged just before each render call, after the speculation's camera change.
  r37o result: held twice (6 of 1,044 and 8 of 1,676), each followed by a render with no
  triangle at all (held too), and no empty frame posted (emptyfind's nearest 28.3). The
  camera is NOT the cause: its offset from the car stays (-225, +111..+145, -190) and its
  angles move smoothly through both. The empty renders share a place -- the camera at
  about x 21,680..21,920, z -24,520..-24,400 (lap 1: 21,872 and 21,917 empty, 21,755 and
  21,959 whole; lap 2: 21,683 and 21,770 empty, 21,605 and 21,957 whole) -- always two
  renders in a row, about one whole step's worth. A ~250-unit stretch sampled every ~300
  units at the stock rate would be hit most laps, and stock and r37nh were not: so not the
  place alone; something the half-step scheme does at that place.
- EKA2L1_PCTRACE_RING (emulator, new; EMU_PATCHES.md): the block trace in a ring, written
  at DUMPAT -- r37p, the code path of the empty render against the whole render before it.
- r37p (the ring, 230 s): no empty frame that race (the trace slows the bench). r37q: the
  ring written at the first hold (8,000,000 entries, 13 renders). tools/renderdiff.py: the
  empty render ran as much game code as the whole one before it (221,085 blocks against
  191,255 -- the game did not skip its scene). Their first parting (+D354) is incidental:
  +D32E advances animated textures by elapsed time. By block counts, the empty render
  alone: +CEA0..+CF10 builds a mesh -- 325 vertices copied from packed shorts into 0x28-byte
  records, 372 faces from byte indices into 0x34-byte records (+CEC2 x325, +CEEC x372) --
  and +345E4 (from +34A0C) classifies its 325 points against the view (depth limits, then
  four plane outcodes, the same test as +34440, which ran 146 times out of range in the
  whole render and 7 in the empty one); a helper +523A2 507 times against 9; and our
  a4_gl_flush's composite loop 10,240 times inside the render call (a mid-render
  composite). So at that place the game builds a terrain piece during a render and draws
  next to nothing until it is ready, about one whole step later.
- Theory, NOT verified (rule 4): stock always has a logic step between two renders; the
  half-step scheme renders again after a half-step with no logic, which can land in the
  middle of the game's switch. Not chased further this round: the hold covers it.
- The hold reworked (r37r): a render's present is compared with the last frame that went
  out (a held frame does not move PF_PREVHAD), so a two-render collapse and a render with no
  3D at all (struct glr's new `rendered`, set after each render call) hold as one; a sixth
  (178 of 1,398 was 0.127; the legitimate cuts 0.33 or more); and never longer than 250 ms
  from the hold's start, renders included.
- r37r (the reworked hold, with the marks and stamps, 230 s): 3 holds -- 169 of 1,218
  (lap 1's first collapse), then renders with no 3D at all after 1,472 and 1,485 -- each
  followed at once by a whole frame (1,239; 1,572; 1,623); emptyfind's nearest post 45.6,
  no post of a frame under 300 triangles (2,668 posts tied).
- Candidate r37 (C37 = C35 + A4_DROPHOLD: r36's flags, the hold, the framework-post
  detour at +6B2; no bench switch; sha256
  b8f2d72adf0d696c6aa50ae8fc978682c555a014e08423c122c67179e0236e0a, 803,892 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery on it: telerun3 230 s framestat 3099, faults 1
  (the boot fault), with EKA2L1_POSTDUMP: no posted frame near an empty one (emptyfind
  nearest 45.3; the hill passed at posts 1136, 2035, 2937), 6 renders held (the record's
  drop ring: 18 of 1,494, 39 of 1,512, 18 of 1,382 -- one per lap, ~2,900 presents apart --
  each with a render of no 3D after it), 370 render-less presents and 376 framework posts
  held with them; the longest gap between posts 178 ms (median 80; 40 race posts of 140 ms
  or more, ordinary on the bench); steering race 90 s framestat 1548, faults 1; racemenu
  framestat 3063, faults 1, 18 shots; mark framestat 882, faults 1.
- Where we are (rule 3): the empty frame is reproduced, located (a render during the
  game's build of a terrain piece on the hill, which the half-step scheme exposes; stock
  and the scheme off showed none), and on the bench kept off the panel. Its root cause in
  the scheme is a theory. Best build: r37 (r35's speculation, held empty frames).
- Owner, on the stock game: "stock game draws the shadow correctly." This overturns round
  34's reading ("on the bench route the patchy shadow is the game's own drawing"): that
  rested on a check build's same-frame pair, whose "game picture" is the game's software
  render INSIDE our build (our hooks, our flags), not the stock game. So something in our
  build breaks the car's shadow, in both modes (round 34: "regardless of the mode").
  Not yet known whether the software picture in our build is wrong too, or only how the
  check build drew it. Next, while the owner races r37: the stock game's posts against
  ours in the tunnel on the bench (EKA2L1_POSTDUMP, the same frames by the minimap) --
  not a repeat of round 34 (a check build against itself; the stock game never dumped
  in the tunnel).
- Bench, the shadow against stock: the stock game (stock2, 230 s, EKA2L1_POSTDUMP) and
  r37's battery race (r37t), the tunnel found in each by its orange walls (tunnelfind.py:
  stock posts 938-1061 and 1782-1916; r37 1199-1400, 2095-2278, 2999-3097), and the same
  places paired by the minimap (pairmatch.py). The pictures (tunnel_pairs1.png, ours over
  stock): a darker region beside the car in both, ours sharper-edged in places; the stock
  road's dithering hides a shadow's edge. Not a measurement; not conclusive.
- What our build does that the stock game does not: in a list frame (mode bit 1024, on in
  r37) the list's faces -- the road among them -- are in the depth buffer before the game's
  own submissions draw, and mesh_enter depth-tests every class, the shadows (KIND_SHAD)
  included; the stock game paints a shadow over whatever is there. A shadow a little under
  the road surface loses that test in patches. Round 33's A4_SHADSINK found 2.9% of covered
  shadow pixels hidden on the bench route, mostly behind car parts, and never reached the
  tunnel. r37sh (bench: A4_SHADNODEPTH, shadows untested in a list frame) against r37t at
  the same places: in flight.

## Round 37, phone (r37, sha256 b8f2d72a...)

Owner: "The empty frame appeared in the last lap, at the same spot. I was going really
fast. I didn't notice any particular freezing. However, I must mention that I'm so pleased
to see the game actually going super smooth during the light scenes. It still struggles
during heavy scenes as usual, though. but unlike previous build, seeing the game running
this smooth on light scenes is amazing!"
- Telemetry (capidx 102-109, the race's end): mode 3118 throughout; the windows 43.5-66.6
  ms a render (15-23 a second). The record (capidx 109, 8,805 3D frames): real presents
  2,846 (318 with no render since the last), skipped 5,960; renders 2,532, 39 skipped by
  the half-step; held 4 renders, with 86 render-less presents and 90 framework posts after
  them -- no freezing seen. Drop frames 9; the ring's last 8: 184 of 1,324 (frame 2487,
  0.139: held), 781 of 2,149, 436 of 1,913 (0.23, list 5), 306 of 925, 954 of 2,630 (cuts,
  0.33-0.39), 281 of 1,685 (frame 7827, list 0, render call 38 ms: the last lap's empty
  frame -- 0.1668 against the hold's "under a sixth", 0.1667: missed by one triangle), 619
  of 1,579 then 202 of 619 (frames 8355/8358, lists 96/46: a step down through a cut-like
  ratio, not held). The slowest frames 116-135 ms, all render-bound.
- So the phone's empty render draws more than the bench's (281 against 0-178), and the
  ratio alone cannot part it from the cuts (the phone's 0.23 and up) with any margin.
  Next: detect the cause instead -- the terrain build +CE8C (one caller, +D2F6, inside a
  per-object loop) during the render.

## Round 38 (bench): the hold, corrected

- r38m (the build counter: +D2F6's bl to +CE8C counted per present, with the marks): the
  "terrain build" is no event -- +CE8C runs every frame, 55-120 times in a race render, 76
  in the menu, and only 1-11 times in the empty renders. It counts the objects drawn: the
  round 37 reading ("the game builds a terrain piece during the empty render") was wrong
  (rule 4: a difference between two renders is not a cause). The counter and its +D2F6
  detour are taken out again.
- And a bug in r37's hold (the phone build carries it): when a hold times out through the
  render-less presents (hold_live), it cleared itself but kept PF_PREVHAD at the last good
  frame, so the next sparse render began a fresh hold -- again and again. r38m's crash
  close-up camera (~150 triangles after 2,250, list 0) was held that way across thousands
  of presents (stamps 20814 -> 24726 between two posts): a frozen screen. The phone's r37
  race held 4 renders and 86 render-less presents and the owner saw no freeze -- the bug
  needs a sparse view after a hold, and no such view came. Fixed: a timeout ends the hold
  for good (PF_PREVHAD = 0).
- The criterion, round 38: the camera's offset from the player car (race +0x58..+0x60
  less the car's +0x04..+0x0C), recorded at each render after the speculation, must be
  within 96 units on each axis of the offset at the last frame that went out -- a camera
  cut (crash, takedown, intro) moves it, the empty render does not (r37o: steady to ~15
  units a render) -- and the 3D under a quarter (the phone's empty render 0.1668; its cuts
  0.23-0.39 now parted by the camera, not the ratio). r38n: in flight.
- r38n (the round 38 hold, with the marks, 230 s): 3 holds, all the hill -- 0 of 1,326, 0
  of 1,337, 10 of 1,722 then 63 -- the camera offset (-207..-225, +111, -189..-210)
  through each; no posted frame near an empty one (emptyfind 45.8); the longest gap
  between race posts 121 ms; 526 render-less presents held (the spinning while held).
- r38s (the steering race into walls, 90 s, with the marks): not one frame at half the
  one before or less -- the crashes on this route do not collapse the triangles, so the
  camera test's cut side is untested on the bench (rule 4: a guess that a cut moves the
  offset by more than 96 on some axis; and a timeout now ends a hold for good either way).

## Round 37, phone, Paris (r37 again)

Owner: "I ran another round with the same build but in the paris map and noticed the frame
lost bug more often. i also noticed the steering jitter of past builds and some other
physic-related bugs that most likely have to do with the time-step."
- Telemetry (capidx 229-236): mode 3118; 60.9-100.5 ms a render (heavier than LA's end
  windows); logic 0.7 ms a step, longest span 6 ms; m - n sum 1; 2,778 render calls, 1 with
  no triangle. The record (20,036 3D frames): real presents 6,647, 1,615 with no render
  since the last -- 1,187 from +9AB as before and 428 from a second site (0x78B6988D, code
  +6988D on the phone's base 0x78B00000: not seen in LA, not yet read); held 6 renders,
  312 render-less presents and 318 framework posts with them. Drop frames 18; the ring's
  last 8 mostly 234-292 triangles after 605-721 with ~100 list triangles still drawn
  (0.35-0.43), one 236 of 1,318 (0.18, list 83: missed by r37's sixth, inside r38's
  quarter if the camera held), 559 of 1,169 (list 0), 813 of 1,782, 956 of 2,222.
- Paris draws fewer triangles (frames before the drops 605-1,318); if its empty frames are
  the ~250-of-~650 ones, the hold's floor (the frame before over 600) and ratio miss them.
  No pictures of them (the captures are every 23rd render, not at drops). The steering
  jitter and the physics: not in the record; Paris has never run on the bench with the
  speculation (round 27's night-track route, A4_BENCH_UNLOCK, reaches it).
- Candidate r38 (C37's flags; the round 38 hold: camera-steady, a quarter, a timeout ends
  it for good; the +D2F6 counter removed; sha256
  b2d64972ad748c875825c9bd413d4ed87a36cb9884a05716ce9831b78b2fe4c1, 804,260 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery (after the worker restart the bench host runs
  faster: more posts in every run): telerun3 230 s framestat 4919, faults 1 (the boot
  fault), with EKA2L1_POSTDUMP: no posted frame near an empty one (emptyfind 45.6); held 5
  renders (the drop ring: 12 of 1,342 and 12 of 1,740 among them), 734 render-less presents
  and 739 framework posts with them; race post gaps 588 and 345 ms -- the 345 the loading
  screen, the 588 a takedown in the tunnel whose slowest frame is 334 ms with 307 ms in
  the composite (the host's llvmpipe read-back; 1,204 triangles, not held; the phone's
  composite is 10-14 ms); the rest 143 ms or less. Steering race 90 s framestat 2521,
  faults 1; racemenu framestat 4063, faults 1, 18 shots; mark framestat 1363, faults 1.
- Sent while Paris is studied: r38 is r37 without the re-hold freeze and with the camera
  test (it catches LA's 0.1668). It does NOT address Paris (its sparse drops ~0.35-0.43 from
  ~650) or the steering jitter there.

## Round 38, phone (r38, sha256 b2d64972...)

Owner: "The frame lost still happens at the same place. Additionally, there was another
camera bug a few meters before entering the fame avenue where the camera would get in a
helicopter position for a frame or two, a second or two before the news helicopter event
triggered. And yes, there was a bit of a slowdown during a crash cam, but I'm not sure if
it was because I was entering a heavy scene at that moment or because there is an actual
related bug."
- Telemetry (capidx 106-113; record at capidx 113, 9,732 3D frames): real presents 2,929
  (309 with no render since the last), renders 2,625 (34 skipped by the half-step); held 5
  renders with 311 render-less presents and 316 framework posts. Drop frames 7: 247 of 651
  (list 207), 198 of 653 (0.30), 244 of 616 (list 150), 146 of 786 (0.186, list 0, render
  call 17 ms: empty-render-like), 460 of 1,143, 228 of 973 (0.23), 660 of 2,037. The
  slowest list holds two frames whose frame before had 174 and 187 triangles (frames 1491,
  7257): low renders the drop rule never saw as drops (the frame before them not over 600,
  or held). So on the phone the empty renders draw ~150-280 triangles, near the cuts'
  ratios; which were held is not in the record (no camera there).
- The crash-cam slowdown: no frame in the record over 165 ms (the slowest, render-bound);
  the holds' render-less spinning is the suspect to rule out, not shown.
- The helicopter view, bench: r38n's camera log (every render's camera and car, a whole LA
  race) has one camera far from its place mid-run -- 646 units above the car, then 442,
  171, 105 -- but at present ~10,500, after the race ended (the end camera coming down).
  The fame-avenue event is not on the bench route's log: not reproduced.
- Theory (rule 4), from the code: spec_run's camera cut tested x and z only (+0x58, +0x60);
  a camera that rose to an event view in one step was drawn half way up. Round 39: a cut on
  any axis over SPEC_CUT, or an angle turned over 256 of 2048 in one step.
- The empty render, round 39: held under a half of the last frame shown, from over 400,
  with the camera within 96 (the camera test parts the cuts, not the ratio); and the
  record's drop entries carry the verdict: word 12 = lost | camera deviation << 16 | held
  << 31 (slowsum: camdev, held), so the next phone round says which drops were cuts and
  which empty renders, and which were held.
- r39m (the round 39 hold, with the marks, 230 s): 4 holds -- 0 of 1,743 and 0 of 1,908
  (the hill), 23 of 907 (after the race), and 99 of 512: a crash close-up camera (no top
  HUD, the car filling the screen), its offset from the car ~830 units (the camera ahead,
  looking back) and steady within 96 while its triangles halved, held ~300 ms (post gap 296
  ms, 3,291 presents spun through) -- the kind of crash-cam slowdown the owner felt in r38.
  The camera test alone is not enough. Over the race the chase camera sat 200-300 units
  from the car on the ground in 4,481 of 4,595 renders, 75-175 above it. r39n: a hold also
  needs the last frame shown to be the chase camera's (150-350 on the ground, 50-200 up).
- r39n (with the chase test, 230 s): holds at the hill only -- 6 of 1,516 (camera
  deviation 10), 72 then 118 of 1,494 (3), 0 of 1,363 -- each followed by a whole frame;
  the collapses NOT held: 191 of 514 and 152 of 480 (crash camera: not the chase), 1,106 of
  2,286 (deviation 541) and 474 of 1,606 (724: cuts); no posted frame near an empty one
  (emptyfind 46.1); the longest race post gap 143 ms.
- Candidate r39 (C37's flags; the round 39 hold -- under a half from over 400, the camera
  within 96 of the last frame shown and that frame the chase camera's -- its verdict in the
  record, the camera cut on every axis and angle; sha256
  759ed914c84619c1127d7d62a579a2ea96da1cd42a4df5135c79e7d068a84f06, 804,624 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery: telerun3 230 s framestat 5419, faults 1 (the
  boot fault), with EKA2L1_POSTDUMP: no posted frame near an empty one (emptyfind 45.8);
  the record's drop ring with its verdicts: 6 of 1,134 (deviation 11, held), 19 of 1,607
  (12, held), 1,100 of 2,233 (334, not held), 1,016 of 2,191 (540, not held); 5 renders
  held; the longest race post gap 111 ms. Steering race 90 s framestat 2488, faults 1;
  racemenu framestat 3964, faults 1, 18 shots; mark framestat 1391, faults 1.

## Round 39, phone (r39, sha256 759ed914...)

Owner: "Still the same issues.. In fact, I could notice some steering jitter this time on
LA. Let's stop guessing. Adjust the emulator or patch it as you need it to reproduce the
bugs on the bench. It doesn't matter if it takes several bench rounds."
- The record's verdicts (capidx 102, 7,650 3D frames): held 1 render. Drops: 1,001 of 2,204
  (camera deviation 473: a cut, not held), 984 of 2,012 (53, held), 324 of 877 (92, not
  held: 0.37, over the half), 226 of 1,832 (deviation 2, NOT held: under the half, the
  camera steady -- so the chase-camera range failed: the owner's chase camera sits outside
  150-350/50-200, which came from the bench car's camera alone; rule 4's named guess, now
  shown wrong), 841 of 1,706 (1,068: a cut). 35 render calls with no triangle.
- The bench does not run the scheme at the phone's rhythm: steps per render on the phone
  [n=1 809, n=2 1,386, n=3 206, 4 17, 5 3] -- a render usually after two half-steps --
  against the bench's (r39t) [4,586, 471, 4, 1, 2]; the render call 62 ms on the phone, 37
  on the bench. The speculation's fractions and the steps' and renders' interleaving differ
  -- where steering jitter and stale-state frames would live. Next: EKA2L1_CPUCAP
  calibrated by these numbers (not by eye) so the bench runs at the phone's rhythm, then
  reproduce on it.

## Round 40 (bench): the bench at the phone's rhythm

- EKA2L1_CPUCAP calibrated by numbers against r39's phone telemetry (render call 61.9 ms;
  steps per render n=1/2/3: 33/57/9%): uncapped (r39t) 37.1 ms, 89/9/0%; cap 100: 53.1 ms,
  44/53/3%; cap 85: 61.8 ms, 24/66/10% (seg0 1,949 renders). Cap 85 is the bench's phone
  from here (the record's "115 by eye" ran on a slower host). Not matched: the 2D after
  the composite (bench 13 ms, phone 6-7) and the composite (15 against 11).
- r40c (the measuring build at cap 85, 230 s accelerating; per render the car as drawn
  against real time -- tools/pacing2.py): 2,562 renders, 77 ms apart (65..100); drawn speed
  against its neighbours 1.00 (quartiles 0.99..1.01, 10-90% 0.98..1.03, none off by 25%);
  never drawn going backwards; the camera's offset from the car changes 0 a render
  (median), 18 (90%), 35 (99%). Holds at the hill, 8 of 1,230 and 16 of 1,579 (deviation
  11 and 1); a cut not held (1,013 of 2,399, 539); no posted frame near an empty one.
  Straight-line driving at the phone's rhythm is even: the jitter, if the bench shows it,
  is in steering -- next, a tapping steering race (steertap.sh: 150-300 ms taps, a player's
  corrections) at cap 85, green on and off, with heading blips and sideways wobbles.
- The chase-camera range (r39) is replaced, not re-guessed: the chase camera is ~97% of
  race renders, so a running average of the offset is the player's chase camera whatever
  the car or view; a hold needs the last frame shown within reach of it.
- CORRECTION (rule 4, the instrument was not what it said): the speculation runs whatever
  the A/B mode unless the build is A4_ABCYCLE10 (the mode test at a4_step_render is inside
  #ifdef A4_ABCYCLE10). So C35off/r36doff ("mode 1070, the speculation off") speculated
  every frame -- its record's "spec" column (2 ms) said so and was waved off -- and round
  37's "the empty frame happens with the speculation off" is WRONG: no build without the
  speculation has shown the empty frame (stock 0 of 3 laps, r37nh 0 of ~3, both without the
  half-step logic either). Likewise r40toff below was not off.
- The tapping steering race at cap 85 (r40ton, 90 s; r40toff speculated too): 1,034
  renders 79 ms apart; drawn speed 1.00 (10-90% 0.96..1.04), none off by 25%, never
  backwards; heading blips 2 of 1,033, sideways wobbles 0 of 1,032; the camera's offset
  change 0 a render (median), 23 (99%). So the player car's drawn path at the phone's
  rhythm, steering in taps, is even on this route: the owner's steering jitter is not
  there -- the camera's angle, the other cars or the keys' response are what remain.
- r40ns: the half-step logic WITHOUT the speculation compiled (no HALFSPEC, no HALFMOTION)
  at cap 85, the hill: in flight.
- r40ns (the half-step logic, NO speculation compiled, cap 85, 230 s): the hill passed
  twice (mapmatch posts 1554, 2534) and no empty render -- the collapses 664 of 1,443, 938
  of 1,935 and 157 of 537 (cuts and a crash, rcall normal), no hold, no posted frame near an
  empty one (45.6). With the speculation, same cap, same route (r40c): two. With stock and
  r37nh: the empty render is the SPECULATION's (not the half-step's, as round 37 had it).
  Its pacing for reference: drawn speed quartiles 0.97..1.03, 4% off by 25% (the same state
  drawn twice when a render follows a half-step with no step), camera yaw blips 28.
- What the speculation leaves the render: the drawn words interpolated (spec_mid, the
  camera), but every other word the replayed half-step changed in the cars and the race is
  at its speculated value during the render. Experiment next: those put back to the whole
  step's for the render (the drawn ones kept), then halve the set to find the word.
- The same count on the controls (per-present marks: under a quarter from over 600): r40c
  (the speculation) 2 -- 8 of 1,230, 16 of 1,579; r40ns (none) 0.
- A4_SPECLEAN=7 (bench: every word the speculation changed in the cars, the race and K put
  back for the render, the drawn ones kept), two races at cap 85: 105 of 1,088 and 18 of
  1,275 -- still there. So not the cars', the race's or the keys' words: state outside those
  snapshots. The speculation calls the game's camera update +12DEC, which writes outside the
  race object (round 31's trace: 18 sites in 5 functions under +5968). Next: A4_SPECNOCAM
  (the speculation without +12DEC; the camera then stays at the whole step's -- a test, not
  a fix).
- A4_SPECNOCAM, two races at cap 85: NO empty render (2,975 and 2,979 3D presents, none under
  a quarter from over 600). With the camera update in the speculation: in every race. So
  the speculative +12DEC is the cause. Round 31's leak6 had seen it: +12DEC allocates and
  frees list nodes in a block outside the saved objects (+5912 -> +5205A), filed then as
  "freed within the call". Reading now: run on the speculated state, the camera update
  moves the camera's bookkeeping (zones, events) to where the car will be half a step on;
  the render culls by it and nothing restores it -- the hill's empty render; and an event
  camera could start a frame early (the owner's helicopter view before its event: a
  theory, not on the bench route).
- The fix, A4_SPECCAMFOLLOW: +12DEC is not run speculatively; the chase camera moves with the
  player car by the car's interpolated motion; any other camera (offset off the chase
  camera's by over 128 on an axis) stays at the whole step's. The chase camera is a running
  average of the camera's offset from the player car, quick while near it, 1/256 a render
  while off it -- the hold's chase test uses it too (r39's fixed range is gone). r40cf: two
  races and a tapping race at cap 85, in flight.
- r40cf (A4_SPECCAMFOLLOW, cap 85; races 1-2 230 s, a tapping race 90 s): empty renders 1
  (143 of 1,970, not identified: the post dumps were deleted before being read -- rule 1)
  and 0. The camera WORSE: its offset from the car changed 18-31 a render (median), 120-163
  (90%), 237 (99%) against 0/18/35 before; its sideways offset reversed 441-583 times
  against 1. My bug: the chase test compared the offset in world coordinates, which turns
  with the car -- in every bend the camera was judged "not the chase camera" and left
  behind for the frame. Fixed: the distance on the ground and the height, which do not
  turn (an integer square root, isqrt64, host-tested); the hold's test likewise.
- r40cf2 (the rotation-invariant test): the camera still worse -- offset change 17-40 a
  render (median), 98-180 (90%), reversals 454-740. The game's chase camera does not move
  rigidly with the car (it lags and smooths), so a camera moved with the car on the
  in-between frames and put back where the game's camera is jumps every step. The follow
  is dropped. Collapses: race 1 186 of 937, 115 of 1,454, 99 of 609 (no posted frame near an
  empty one: 44.0); race 2 a run of 65-96 of 607-739 (a sparse scene). Next: keep the
  game's camera update in the speculation and stop only what it leaves behind (the list
  change round 31's leak6 saw: +5912 -> +5205A).
- +57AC (called from +5DE4, under +12DEC -> +5968): a camera state machine. r4 is the
  camera object (race +0x48 on: its words +0x58.. are the render's camera), r7 = r4 + 0xC0;
  at +5902, while the counter [r7+4] (race +0x10C) is under 2, it frees a slot's node
  (+5912 bl +5205A, "unlink a node and delete it"), sets the slot up again from a camera
  script (+5926 bl +3D2A: allocates -- leak6's 3 KB block), and advances the counter. The
  slots are race words (r4 + 0xC8 + 0xC0 * k). THEORY (rule 4, to be tested): run
  speculatively, a transition frees the old node; the restore after the render copies the
  race's old slot pointers back -- to the freed node -- and the renders until the next whole
  step read freed memory: the empty render, two in a row; SPECLEAN (the race words put back
  BEFORE the render) puts back the same dangling pointers, so it could not help. Test: the
  transition path's block (+5902) logged with the per-present marks.
- The gated watch (A4_SPECGATECAM: EKA2L1_WATCHGATE opened around the speculative +12DEC
  alone, tagged with the present count; the emulator now logs the opening value), two
  races at cap 85 (gatelog.py): from a point in each race (present 2430 / 3651) almost every
  speculative camera update writes words new to the watch -- the ROM heap (0x103A2320) and
  the game's list code (+520D2/+520D4) -- at addresses climbing 0x48 at a time from
  0x02021C88: heap nodes freed and allocated by the speculation. Race 2's collapse (present
  2259) came before any of it (the watch reports each word once, so reused addresses are
  silent). +520D4 is +5205A: a CAMERA SCRIPT PLAYER -- per call its clock [obj+0x8C] + 1,
  eleven channels interpolated from keyframe nodes, each keyframe passed unlinked and
  deleted (+4B8E4); callers +556E (in +554C, which then sets a script up with +3D2A) and
  +5912 (in +57AC, likewise +5926). Under the speculation the script's clock, values and
  list heads (race words) are put back by the restore while the deleted keyframes stay
  freed: the race points at freed memory, reused by the next allocations -- camera data
  read from freed memory (the helicopter view at an event; renders culled from garbage).
- A4_SPECCAMSAFE: the four bl's (+556E, +5912 -> +5205A; +5578, +5926 -> +3D2A) become BLX's
  to trampolines that skip them while SPEC word 24 is set (around the speculative +12DEC
  only), else enter the game's functions untouched (no caller reads the result). Image
  checked: the placeholders resolve to +5205B/+3D2B, the four BLX's in place. r40s: a gated
  watch race and two counted races at cap 85, in flight.
- r40s (A4_SPECCAMSAFE, cap 85): race 1 with the gated watch -- the speculative camera
  update now writes NOTHING new outside the race object after the race's start (the heap
  churn gone); in all, outside the stack, only our flag and race words +0x54..+0x350. The
  camera as steady as with the game's update (offset change 0/17/37; sideways reversals
  5-8). But the empty render remains: collapses 71 of 1,815, 34 of 724, 12 of 1,568 (race
  1), 7 of 1,650 (race 2), 50 of 1,682 (race 3), and race 3 posted one (posts 1242-1244,
  emptyfind 8.0 -- the hold missed it). So the use-after-free was real (and is fixed) but
  is not the empty frame.
- What is left: the empty render needs the speculative camera update (A4_SPECNOCAM: none),
  survives putting back every race word but the camera's +0x58..+0x70 (A4_SPECLEAN), and
  the update writes nothing else outside the race. So the camera words we interpolate.
  lerp_ang does not wrap its result, and the game's angle words run 0..4095 (pitch seen
  3,875-4,071, yaw passing 4,092 -> 13): an interpolation across the wrap hands the game
  4,096 and over or below 0 -- out of a trig table, a broken view matrix, the scenery culled
  while the camera's position is right. Theory; test: pitch/roll/+0x70 logged per render
  (a4_render_mark3), and A4_ANGWRAP (& 4095) against no wrap, two races each at cap 85.
- The angle test (cap 85, a4_render_mark3 per render): the empty renders' camera, the
  renders before each (pitch, roll, +0x70, yaw): r40a (no wrap) -- (-37,0,125,1305),
  (-34,0,125,1304), (-31,0,96,1304), (-31,0,62,1304); likewise 125 -> 43, 125 -> 43, 125 ->
  65 at the other three. Pitch is SIGNED (-222..2,045 over a race): no wrap problem -- the
  wrap theory is wrong. What moves at every empty render is +0x70: normally 125, at the empty
  render 43-65, four of four. r40w (& 4095) the same at the hill (125 -> 37, 125 -> 64) and
  worse elsewhere (it rewrote the negative pitches; collapses in crash cameras, whose +0x70
  is 19-30 and which draw fine -- so +0x70 is not a plain draw distance). Next: A4_KEEP70,
  +0x70 kept at the whole step's on the in-between frames.
- A4_KEEP70 (+0x70 kept at the whole step's on the in-between frames), two races at cap 85:
  NO empty render -- not even a frame under HALF of the one before from over 600 (2,999 and
  2,888 3D presents marked: the instrument recorded, rule 4). With +0x70 interpolated, every
  race at this rhythm had 1-3. +0x70 is not constant: over a race it takes every value from
  0 up, 125 on the chase camera -- a camera-transition counter. The speculated camera update
  resets it where the game starts a camera transition (the hill), and the interpolation
  handed the render a transition half done. FOUND: the empty frame is +0x70's
  interpolation (since round 32's speculation interpolated it).
- Where we are (rule 3): the empty frame's cause found and fixed on the bench (A4_KEEP70);
  a use-after-free of camera keyframes in the speculation found and fixed (A4_SPECCAMSAFE);
  the bench runs at the phone's rhythm (EKA2L1_CPUCAP=85). Not reproduced: the steering
  jitter (the player car's drawn path and the camera are even at the phone's rhythm, steering
  in taps), the helicopter view (not on the bench route; +0x70 and the freed keyframes are
  both candidates), Paris (never on the bench with the speculation).
- Candidate r40 (C40 = C37 + A4_SPECCAMSAFE + A4_KEEP70; sha256
  66df115f403fc5dd72689571c6f87b80cc6b4d07e03d25ca15371219842f007f, 805,332 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery: telerun3 230 s at cap 85 (the phone's rhythm),
  framestat 3281, faults 1 (the boot fault), with EKA2L1_POSTDUMP: no posted frame near an
  empty one (emptyfind 46.0); the hold never needed (0 held); 1 drop frame. Steering race
  90 s framestat 2485, faults 1; racemenu framestat 3984, faults 1, 18 shots; mark framestat
  1335, faults 1.

## Round 41 (bench): steering, as the owner steers

- Owner (before testing r40): "you won't see the error if you only tap. You would need to
  leave the direction key pressed down so the car is continuously steering. you will see
  that the car is kind of going back n forth. Remember to try this in both light and heavy
  scenarios, and also try it by going fast and slow." And the helicopter event: three
  turbo taps a second apart at the start, take down racers, build speed, avoid rails and
  traffic -- "eventually, the helicopter event will trigger."
- The game's controls (pause -> CONTROLS, landscape 270, read on the bench): turn left 8,
  turn right 2, brakes 6, nitro boost 5 or A, next/previous camera; accelerating is
  automatic. So EVERY bench race so far held 5 (nitro, not "accelerate") and the steering
  races pressed 4 (nothing) and 6 (brakes): no bench race has ever steered. Round 40's
  "the steering is even at the phone's rhythm" measured a car that was not being steered
  (rule 4: the instrument's input was never checked). Withdrawn.
- The controls list in full (it wraps): brakes 6, nitro boost 5 or A, turn left 8, turn
  right 2, drift left 7/9, drift right 1/3, drift #, next camera *, previous camera 0 -- no
  accelerate entry. Yet a race with 5 not held (steerhold, first version) left the car at 0
  km/h facing backwards ("REVERSE", "ARE YOU LOST?!"): on the bench, holding 5 is what moves
  the car (telerun's 190 km/h). Steering race v2: 5 held, 2 held 2 s / free 1.5 s / 8 held 2
  s / free 1.5 s, cap 85, green on (r41on) and green not compiled (r40ns).
- Steering race v3, the weave (2 held 0.8 s, free 0.7 s, 8 likewise, 5 held; cap 85, 150 s;
  tools/pacing2.py on the render marks), green on (r41on) against green not compiled (r40ns):
  renders 2048 (69 ms median) / 1627 (87 ms); moving 1323 / 1346; turn rate off its
  neighbours by >25%: 15% / 20%; heading reversals 30 / 60; heading blips 8 / 19; car against
  camera change median/90%/99% 2/10/38 against 4/15/29; its blips 17 / 46, reversals 68 / 119.
  By render-start time green is not worse; its 99th percentile is.
- Timed by the present (a4_present_mark: the ms the made present went out; tools/pacing3.py
  joins it to the render marks): render start to present median 62 ms (green, r41p) / 67
  (r41pns); drawn speed off >25%: 0% by render start, 2% by present (green) against 3% / 5%;
  turn rate 11% / 12% (green) against 22% / 23%. The lag between drawing and showing is
  steady enough not to unsettle what was drawn evenly.
- The picture (EKA2L1_POSTDUMP posts 700-1599 of a green weave; tools/carscreen.py, the blue
  Mini's pixels): too noisy to rank (the colour mask moves with the shading: ~48 reversals per
  100 frames in place and turn alike, the noise floor, not a finding). Rule 4: a picture is not
  a measurement; this one was not even a usable picture measurement.
- The prediction itself (round 41 bench marks a4_spec_mark..4 in spec_run: the player car and
  camera as the whole step left them and as the replayed step predicts them a step on;
  tools/predres.py against the next step's own state; r41son, the weave, 1,325 steps): car
  heading error median 0, 90% 5, 99% 17 /2048 a step (the true turn median 26 on turning
  steps; error/true 10-90% 0.00..+0.14, beyond +-0.5 in 5%); the car's place exact (across
  99% 0.002 of a step); camera place error 90% 4.2, 99% 36 units (a step 85); camera yaw 90%
  5, 99% 31; car against camera heading-less-yaw 99% 39. Every green frame is the last step
  plus a fraction q of the prediction (q16 from the game clock; median largest q in a step
  0.56), so a wrong prediction would show as q times its error, gone at the next step: on
  the bench the prediction is right to within a few units in 90% of steps. The held-steer
  jitter is not reproduced on the bench by any measure so far.

## Round 40, phone (r40, sha256 66df115f...): the empty frame gone; takedown cameras lag

Owner: "I didn't notice the frame lost bug, but now the cars "online lag" during takedown
cameras. I also noticed that at high speed is hard to connect or crash against traffic cars.
Also I still noticed jittering during steering."
- Telemetry (8 files, capidx 82 the latest; slowsum.py): 2,001 real presents, 4,000 skipped,
  1,915 renders; frames held back 0 (the hold never needed); 4 drop frames, each with the
  camera moved (camdev 109-365: cuts, not the empty frame); slowest frames 111-141 ms, the
  render call 107-138 of it. Consistent with the empty frame fixed (A4_KEEP70).
- The takedown cameras, two candidates found in the code, both measured on the bench:
  1. A4_SPECCAMSAFE (new in r40) skips the camera script player in the speculation, so a
     scripted camera stays at the whole step while the cars are drawn up to a step ahead.
     The trampolines now count the calls they skip (SPEC word 25); r41c, a crash-prone race
     (holds 1.5 s / free 0.5 s), tools/skipana.py: 62 of 2,722 speculations skipped a script
     call, all in camera mode 0x18 (race+0x94; set at +12EBE/+12EDE: the crash camera), one
     run of 61; the speculated camera's step against the car's 0.00 there against 1.04
     elsewhere. Fix A4_SPECSCRIPTHOLD: such a frame is drawn at the whole step (camera and
     cars as the game left them; SLOWREC word 14 counts them).
  2. Slow motion. +15B1C counts the logic steps with the period [race+0x84C] x 10667 >> 8
     (256 normal, more in slow motion: a takedown's), but the speculation's fraction used
     10667 always: in slow motion it swept a whole step more than once per step, the cars
     drawn ahead, back, ahead. Fix A4_SLOWMOQ: the fraction from the game's own period
     (shift-subtract divide; the blob has no libgcc), only when the scale is not 256, so the
     normal path is unchanged. Bench knob A4_BENCH_SLOWMO=<scale>: the scale forced 3 s of
     every 10 (never in a phone build).
- The helicopter (for the round-38 "helicopter position for a frame or two"): race car
  slots 8-14 (race+0x3B4..) are the police, created at +18338 with a type at car+0xC70 by
  slot: 1 Pomoto, 2 helicopter (cars\helicopter.cfg), 3 cop, others police; 4 = the
  helicopter. Spawned (+20520 from +18B12/+18B9A) with state +0xC6C = 0 for the helicopter
  (1 for the others). The camera update +12DEC sets camera mode 0x2A (+3FE6, cam+0x4C =
  race+0x94) while the helicopter's state is 0; the GNN LIVE letterbox (+12738, called from
  +12A78) draws while its timer +0xC44 is under 0x1E0. Not yet reproduced on the bench.
- Slow motion on the bench (A4_BENCH_SLOWMO=512u: the scale forced 3 s in 10; telerun3 120 s,
  cap 85; tools/slowpace.py on render mark 2): the forced slow motion is real (drawn speed 1.03
  against 2.15 units/ms). Old fraction (sm_old): slow stretches 8% of frames off their
  neighbours by >25% (quartiles 0.96..1.04, 10-90% 0.90..1.08), normal ones 1%; A4_SLOWMOQ
  (sm_new): slow 0% (0.98..1.02, 0.96..1.04), normal 0%; 404 speculations used the scaled
  period. Modest on the bench because its renders fall just after each step boundary; the
  phone's ~65 ms frames fall anywhere in the step.
- The leak check with traffic (lk41: C40 + A4_SPECGATE + a4_spec_ptrs marking what is saved;
  EKA2L1_WATCHGATE over 0x400000-0x4000000; the weave 150 s, through traffic; tools/leakcls.py):
  691 words written by the speculation outside what it saves. Most of them: the camera
  scripts' heap. The script object is inside the race object (cam+0x248 = race+0x290) but its
  eleven keyframe lists hang off heap sentinel nodes; +554C (a scripted camera's start, called
  from the camera update +5DF0) runs +5202C (script init, at +5618/+5620) and seven +51FEC
  (keyframe add: a 16-byte node and an 8-byte value, linked before the sentinel) -- round 40's
  A4_SPECCAMSAFE skipped only the player +5205A and the setup +3D2A. The restore rewound the
  race words; the lists kept the speculation's keyframes: the real camera script then played
  keyframes built from a predicted state (the takedown and crash cameras, the event cameras).
  Fix (A4_SPECCAMSAFE extended): +51FEC at all 15 call sites and +5202C at all 5 skipped and
  counted while speculating (a4_cs_51fec_tramp, a4_cs_5202c_tramp; the injector checks each
  site is a BL to its target). Rule 4: no caller reads r0 after them except +579A, which falls
  into its own function's return (a camera-script start; a guess that its callers ignore it).
  lk42 (the extension, holds 1.2/0.6 s): 34 words outside, every camera-script heap write gone;
  58 frames held at the whole step for a script (A4_SPECSCRIPTHOLD).
- What remains outside (each word reported once): libc 0x00500148 (rand or errno, round 31),
  an event ring at 01F8FBA4.. (+18BFE: 16 entries, last id and time; fed from +1D9E3), a small
  state machine at 0263B66C.. (+2482C), 01F91A94.. and 01F32EDC.. (+49xxx, +52D40, partly
  called back from the ROM: the sound system, a guess), and ROM / other-thread words (audio
  thread, round 31). Frequency unknown (the watch reports a word once).
- The traffic: every speculation saved and replayed the same 14 cars (player, 7 opponents, 6
  police); no traffic car. Traffic cars are CTrafficCar (vtable +B0044, typeinfo +AF5C0, base
  CCarBase), 0x148 bytes, 16 slots at G+0x6F4C (literal 0x6F4C at +14EC8 and 9 more sites),
  moved once a step by +FB12(G, ...) from +14574 (bench trace: 461 calls in ~40 s, one per
  step), drawn by +FFAC (from the render, +D834), collided with cars by +1FF56 (from +14AEC,
  +14BF6). The same layout as a car where it matters (tfm, PCMEM of a traffic car): position
  +004..+00C and copies +04C..+054, +0A8..+0B0; rotation +058..+06C in 4096ths; heading +11C;
  direction +120/+124; wheels +020. So on every green frame the traffic stood at the last
  whole step while the player and the scene were drawn up to a step ahead: same-direction
  traffic drawn closer than it is, oncoming farther -- "hard to connect or crash".
- Replaying +FB12 in the speculation (first try) leaks: it writes the track's spawn records
  inline (+FCC6, 9 records over 150 s, lk43). Instead A4_SPECTRAFFIC (no game code replayed):
  a4_h_fb12 (BLX at +14574) copies the traffic array just before each real +FB12 (0x45A000);
  a green frame saves the array (0x458400, bitmap after it), draws each moved traffic car
  carried on by q of its last whole step (position, rotation, heading, direction, wheels; a
  respawn past SPEC_CUT or a hard turn left as the step left it) and puts it back after the
  render, selectively like the cars. Used only if the whole step just past made the call
  (the half-step count stamped at the call), so a stale array from an earlier race is never
  touched.
- Traffic on the bench (tools/trafpace.py on a4_traf_mark: the first moved traffic slot as
  drawn, per render; the weave 120 s, cap 85): carried on (tr_x) drawn speed against its
  neighbours quartiles 0.98..1.02, 10-90% 0.96..1.04, off by >25% 0%, never drawn backwards
  (1,201 windows); the extrapolation compiled out (tr_nox, A4_TRAFNOX) 0.96..1.04, 10-90%
  0.00..1.12 (standing still between steps), off by >25% 20%. Leak check of the traffic build
  (lk44): 31 words outside, none in the traffic array or the spawn records.
- Where we are (rule 3): takedown-camera lag -- two causes found in the code and fixed, both
  measured on the bench (the keyframe leak and the script skip: crash camera 0x18; slow
  motion: forced); traffic -- cause found (never in the speculation) and fixed, measured; the
  steering jitter -- not reproduced by any bench measure (prediction error, render-start and
  present timing, the picture), so the phone build records its own frames (A4_FRAMEREC); the
  helicopter view -- not reproduced; its likely mechanism (a camera script started by the
  speculation leaving keyframes behind) is now closed. Best test this round: the gated leak
  check (691 -> 34 -> 31 words), which named both the keyframe leak and the traffic gap.
- Candidate r41 (C41 = C40 + A4_SPECSCRIPTHOLD + A4_SLOWMOQ + A4_FRAMEREC + A4_SPECTRAFFIC, with
  A4_SPECCAMSAFE's extension; sha256
  5055b5433deece0ae2d14feb29357915ac2ed5fc143ab59ad7aadd3517989075, 807,532 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery on that binary: telerun3 230 s at cap 85 framestat
  3256, faults 1 (the boot fault), EKA2L1_POSTDUMP: no posted frame near an empty one
  (emptyfind 46.3), held 0, drop frames 2, frames held for a camera script 56, the frame record
  read back from all 8 captures (417 frames, every one presented; render to present median 63
  ms); crash-steer race 150 s framestat 3168, faults 1, the record read back; racemenu framestat
  3942, faults 1, 18 shots; mark framestat 1344, faults 1. (The first r41 candidate's telerun,
  before the keyframe and traffic changes, held 6 frames; this one 0.)
- Guesses in it (rule 4): that the takedown camera is the scripted mode 0x18 with slow motion
  (+12EBE/+12EDE set 0x18 for a crash and for the race's +0x834 event; no takedown on the bench
  yet; the forced scale 512 is not a measured takedown scale); the traffic words drawn (taken
  from which words move, not from a read-watch of +FFAC; the heading +11C carried on only when
  it moved under 512); +579A's callers ignoring r0; the remaining outside writers (event ring,
  +2482C, sound) left as they are, frequency unknown.

## Round 41, phone (r41, sha256 5055b543...): the empty frame and the heli view gone; snap-backs remain

Owner: takedown cameras "kinda lag a bit. not as before"; traffic easier to hit, "but sometimes
they look like they snap back into position as i get closer to them, in a similar effect of
that of online lagging. this effect is kind of what I experience with the steering
jittering"; "The frames are good now, and so is the heli view." They test with the Bugatti
Veyron tuned past 550 km/h; the bench drove the Mini Cooper, the slowest car. Two telemetry
batches: a Veyron run (capidx 333) and a Mini run (capidx 125; the crash-cam lag and the
steering jitter seen in it).
- Telemetry (slowsum): Veyron -- 2 frames held, 19 drop frames, 712 frames held at the whole step
  for a camera script, 64 speculations in slow motion (the first takedown slow motion ever
  recorded: A4_SLOWMOQ in use); Mini -- 0 held, 7 drops, 178 script holds.
- The frame records (frec.py, the phone's own last ~417 frames): drawn speed even (0-4% of
  frames off by >25%). One-frame reversals (|change| >= 4 /2048): camera yaw 13 (Veyron) and 6
  (Mini), car heading 3 and 1 -- nearly every one a frame drawn late in its step (q 0.83-0.99)
  followed by one in the next step (q 0.4-0.8): a frame swung toward a prediction the next step
  did not follow.
- The Veyron on the bench (bench data only, never the phone: the bench's own copy of carsb.bar
  rewritten with the 16 mini_cooper_s.* and bugatti_veyron.* entry names swapped -- .cfg, .obj,
  the .p meshes, the low-detail set; tools/barswap.py; the original kept in the scratchpad):
  the career's starting car drives on the Veyron's handling (304-308 km/h early in the lap
  against the Mini's 203 with nitro), drawn dark (the Veyron mesh with the Mini's textures).
  The carsa.bar .rc records alone did not change the car.
- Blind key patterns crash at that speed; tools/autodrive2.py (autodrive.py for the current
  window: the game shown turned 180 degrees, the road band y 330-388, keys 8/2 held while the
  road's centroid is off centre) steers continuously, as a player does. r42m (r41 + marks),
  120 s: the bench frame record shows the phone's signature -- 5 camera-yaw and 5 car-heading
  one-frame reversals in 417 frames, each a frame at q 0.93-0.99 then one at 0.55-0.67. The
  speculation marks at one (t 45794): drawn at q 0.93 toward a predicted heading +51; the real
  next step turned +19 (a key released after the frame was drawn); the next frame came back 28.
  At another (t 40930): predicted -23, real -6. FOUND: the snap-backs are the prediction meeting
  an input it could not know -- the keys at render time stand in for the next step's. Anything
  else the replay cannot foresee (traffic braking, a collision, a takedown) does the same.
- A4_INTERP: each frame drawn between the state before the last whole step (ip_capture at its
  even half-step) and the state after it, at the same fraction q; nothing replayed; the
  speculation's save and restore kept, so the render's own once-a-step changes are kept or
  undone as before; the traffic between A4_GL_TPREV and now; the camera likewise (a cut left as
  it is); every frame (at q 0 it shows the step before). One step (85 ms) later on screen.
  Review (rule 4) caught two bugs before the first run: a slot car not saved by the speculation
  would have kept its blended words (only saved cars are blended now), and frames under 1/32 of
  a step skipped the speculation (a whole-step jump in this mode; every frame runs it now).
  r42im (+ marks), the same autopilot drive: camera-yaw and car-heading one-frame reversals 0
  and 0 in 417 frames; car-less-camera reversals 8 of 162 (r42m: 19 of 88); traffic even (1% of
  frames off, drawn backwards 1 of 1,310).
- Candidate r42 (C42 = C41 + A4_INTERP; sha256
  04f4fd09ec90ddb2dc74c70630ed7bdc8e35f1219de6978af998e08999b27255, 807,012 bytes; no
  0xDEADBEEF; +3A15C B510 0004). Battery on that binary, the bench's original archives put back
  first: telerun3 230 s at cap 85 framestat 3275, faults 1 (the boot fault), EKA2L1_POSTDUMP:
  no posted frame near an empty one (emptyfind 46.3), held 0, drop frames 1, the frame record
  read back (417 frames, one camera-yaw reversal of 5 units, none of the heading); crash-steer
  150 s framestat 3229, faults 1; racemenu framestat 4118, faults 1, 18 shots; mark framestat
  1436, faults 1; then the Veyron swap and the autopilot 120 s: framestat 2321, faults 1,
  reversals 0 and 0. The bench's archives restored to the originals afterwards.
- Where we are (rule 3): the snap-backs (traffic, steering, takedown and crash cameras) traced
  on the bench, at the owner's speed, to the prediction meeting input it could not know; the
  interpolation mode removes the signature on the same drive. Best test this round: the Veyron
  on the bench with a continuously steering autopilot -- the first bench run to show the phone's
  frame signature. Asked of r42 against r41 (same everything else): the feel -- smooth but half
  a step later on average than the stock game (a full step later than r41).
- Guesses (rule 4): that the key change is the only common unforeseeable input (traffic
  braking and collisions are argued, not measured); that half a step of extra delay is
  acceptable -- the phone decides; the autopilot's quick key changes exaggerate what a held key
  does on the phone, so the bench's 5 reversals are not the phone's rate.

## Round 42, phone (r42, sha256 04f4fd09...): interpolation rejected

Owner: "It feels sluggish. and it didn't fix the takedown and camera lags. I feel like this frame
interpolation or insertion is not helping us. I think we need to find a way for the game to
actually produce these frames with the physics working accordingly."
- Telemetry (capidx 105): 2,663 real presents, 2,437 renders, held 0, drop frames 8, 44 frames
  in slow motion. Frame record: drawn speed even (0-2% off); car-heading one-frame reversals 0,
  camera-yaw 11 -- with every frame drawn strictly between real steps, so the game's own camera
  yaw goes back and forth from step to step.
- Do our builds change the game's real steps? (The oracle with steering, the Veyron, the
  camera: A4_LOCKSTEP=1 (P 10667 in the half-step builds), A4_CARREC + a4_rec_emit2 (the header,
  the player car 0xA00, the race object's first 0x100 -- its camera), A4_AUTOPILOT +
  A4_AUTOSTEER (throttle; from step 260 right held 3 steps, free 14, left 3, free 14; key bits
  '2' 0x8 and '8' 0x200, the latter seen to turn the car), the Veyron archive swap; crec.py
  extended to the race words.) Stock logic (orn, C37nohalf, no half-steps) drives almost two
  laps (steps ~145-2290) before the scripted keys wedge it. Against it, prediction (orp, C41)
  and interpolation (ori, C42): every live field -- car and camera -- identical until whole step
  734 (prediction) and 599 (interpolation), except the render-side cycle +15C; then everything
  at once, from a collision force (+0FC/+104) present in one run and not the other. So the
  builds do not change the game's physics or camera; the runs part at one contact, after
  which the trajectories are chaotic. Likely (a guess, not traced): the extra renders and the
  speculation draw random numbers (round 31 and this round's leak checks saw libc's word
  0x00500148 written), so traffic and AI decisions shift. And the camera-yaw swings in r42's
  record are the game's own camera between real steps, which the stock game shows at 11.7
  frames a second.
- Interpolation is withdrawn. Next (the owner's ask): the game computing every half-step for
  real -- the cars' physics and movement and the camera run each half-step with the keys of
  that moment, their continuous fields moved half of what the game computed (the remainder
  carried so integer units sum exactly over two half-steps), discrete fields taken only at the
  whole step, the result kept (nothing put back). Rounds 30-31 set a hand rewrite of the 152
  writers aside; this halves the game's own results instead of its code.

## Round 43 (bench): the game computing every half-step -- first attempts (A4_HALFPHYS)

The owner's ask after r42. Bench only; nothing ships until the half-step car drives like the
stock car on the oracle (A4_LOCKSTEP, A4_AUTOSTEER, the Veyron swap; tools/crec.py).
- Design: every half-step the game's own code runs -- at odd half-steps the keys (+25E8), the
  whole step's recorded car calls again, the camera (+12DEC; on a script it waits for the whole
  step) -- and of each call's change to the car's continuous words a half-step keeps half
  (hp_half: floor, the remainder carried, so integer units add up over two half-steps); the
  game's own whole step at even half-steps, its calls halved the same way through the hooks
  (+1DE78, +1F1F8, +1FD0E, +1EA64, +1FD5C; the camera at +14EEC and slow motion's +1306C,
  a4_h_12dec). Nothing put back; only the render after an odd half-step has its own
  once-a-step changes undone (hp_snap and the speculation's whole restore). The step's other
  work (race logic, AI choices, traffic, timers, effects, sound) once a step as now.
- hp1-hp3 (my first field lists, from the oracle's per-step deltas): the car gained speed
  twice as fast and topped out higher (speed 201 at step 200 against 129; 339-355 against
  ~267); a fault at +1DF7C (inside +1DE78) reading garbage through [car+0x168] after a crash --
  +0B8/+0C0 move only in 12s (table offsets, a guess) and halving them in 1s broke them; halved
  in 12s now. The race also started 15 steps early (a countdown somewhere runs per call).
- The census (A4_HPCENSUS: the player car copied before each of its calls at an odd half-step,
  unhalved, both emitted; 1,451 calls): my labels were inverted. +1DE78 moves the position on
  every call (+004/+00C by the whole velocity) and runs the engine (+7D0, +7D8, +7E4) and
  counters (+820, +A2C/+A34, +A00, +ADC); +1EA64 changes the velocity (+010/+018), the height,
  the revolutions +02C, the wheels' phase +020, the steering and slip (+0B8/+0C0/+0C4/+0CC),
  the heading +678, the contact forces +0F0/+0F8 and flags +B28. So handing +1EA64 half the
  velocity (hp1-hp3's movement wrapper) never halved the distance: +1DE78 moved the car a
  whole step every half-step.
- hp4 (field lists per call from the census): no fault, but now the car barely accelerates
  (speed 10-20): some halved words are recomputed from others each call (the speed from the
  velocity, a guess), so halving them on top of their inputs compounds. And the race still
  starts 15 steps early.
- A nudge probe (A4_HPPROBE: the player's call run twice from one start, as is and with one
  word nudged first; a nudge that survives is the car's own state, one the call overwrites is
  derived) ran on a car that never moved (speed 0 in 2,008 of 2,010 records): no answer yet.
- Where we are (rule 3): the method runs the game's own code every half-step without a fault;
  the dynamics do not match the stock car yet. Next: the nudge probe on a moving car (the
  probe's own car must drive -- the halving off for the probe run), then halve exactly the
  words that carry state, find the per-call countdown, and only then the oracle again.
- Guesses (rule 4): +0B8/+0C0 as table offsets; the speed derived from the velocity; that the
  other cars' +1F1F8/+1FD0E change both call's words (not measured: the census saw the player).
  Shipped builds unaffected: r41 and r42 rebuild byte-identical; the bench archives restored.

### Round 43, continued (bench): the half-step car drives like the stock car

All on the oracle (A4_LOCKSTEP, A4_AUTOPILOT + A4_AUTOSTEER, the Veyron swap); stock is
crec_orn (a4_or_n), each half-step build crec_orh (a4_or_h, C43 + the oracle flags);
tools in the scratchpad: spdcmp.py (speed / distance at steps after the start), trk.py (step
by step: speed, place apart, heading), early2.py (any words, +A00..+C7C too: orchain now dumps
the car past +A00 with `pc:1:27C:+A00`).
- The probe rebuilt so its car drives (A4_HPPROBE: no halving, the odd half-step changes
  nothing -- the player's calls run twice, plain and nudged, then car and race put back):
  the race starts at step 149 as stock; the car follows stock exactly until a contact (386).
  Set 1 (36 words), set 2 (12, what the velocity is measured against), set 3 (27, the rest of
  the census; +034/+048 are pointers -- nudged, the game faulted, dropped). Findings:
  1. +1EA64 MEASURES the velocity: +010/+014/+018 = place less +0A8/+0AC/+0B0 (a nudge of
     +0A8 moves +010 by -1.00, +0B8 by -12), then +0A8.. = place. +0B4 (its length), +0B8..
     +0C0 (slip, 12 x), +0C4..+0CC (filters fed from it) follow. +1DE78 moves the car by
     +680/+688 (recomputed from speed and heading) and copies +0B4 to +7E4. So in a half
     step the measured speed was half the real one and the speed model fed on it.
  2. Recomputed (overwritten) words: +008 height, +010..+018, +020, +028, +02C, +0B4..+0C0,
     +01C, +07C, +680/+688, +7E4, +7F8, +7FC, +A00, +A18..+A20, +A78, +AF8, +B28; +7D0 (the
     speed) carries -0.45 (recomputed from +7D8 and the measured speed).
  3. Integrated, with per-call changes from the census: position (+004/+00C), heading +678
     (+1EA64, and +1DE78 while a steering key is held: -26 a call, A4_HPTRACE), the engine
     +7D8 (+-8/16 a step; a gear change -256 is an event), counters +1 a step (+820, +A2C,
     +A34, +ADC), countdowns (+7C4 the gear-change clutch 3..0, +AEC, +A6C 9..5 reloaded),
     slews (+A7C: a -8 kick at a steering edge, back +1 a step; +B18 -4 kick), filters
     (+808 7/8, +80C 3/4, +0C4/+0C8/+0CC 7/8, +7C4/+AEC 0.98), +A24, +ABC (rates), +B20 (+1 a
     step to 48 -- the steering hold -- and x3/4 at a steering key's edge: an event).
     +B98 is the top speed reached (a max). +0E0 is not changed by either call.
- Fixes, each measured (spdcmp at steps 10/20/40/80/120 after the start; stock 58/75/107/161/182):
  - hp5 (only integrators halved): 17-23, start 134. The fly-in camera (mode 0x17) ran 1.5x
    fast at odd half-steps -> only the chase camera (mode 2) runs at an odd half-step, the
    others hold (and their whole step is not halved). The keys call (+25E8, the key queue:
    K[0] = K[1], the presses since) dropped at odd half-steps: it ate press edges.
  - the measured velocity: at a half-step call +0A8.. is set to the place two half-step calls
    back (exactly a whole step, the halves' rounding cancelling): 25/52/89/68/84, start 149.
  - per-word event thresholds (a change bigger than `jump` stands whole) and +B20 halved only
    when it rises: the car never left the line (+7D8 210 -> 0 at the start).
    A4_HPTRACE (the player's calls around the start, before/after): the odd half-steps
    replay the whole step's arguments, whose key bits the step forces to 8 (the brake) until
    the start -> no car call at an odd half-step until the player's +0E0 counts. Still dead:
    the first whole step after the start was halved with no odd half-step before it, the car
    moved half a step in a whole step's time, +1EA64 measured half the speed and the next
    call took it for a crash -> a whole step is halved only for a car whose calls ran at the
    odd half-step before it (HP->ran / halve); after a whole step not halved, the next half
    call doubles its own half. Then: 56/75/111/163/172 -- the stock profile.
  - +1DE78's heading change (key steering) halved too: the turn over a held-right pulse
    -106 against stock -117 (it was 1.56x).
  - The half build's record moved to the state the whole step left (as stock records it):
    speed 56/74/109/163/171, place apart 0-62 units to step 80, heading within 10 /2048
    through the steering pulse (-117 stock, -110 half).
- Divergences left: the game's own steering assist (no key: a -8 kick on +A7C) fires a few
  steps apart from stock after step ~85 (positions differ by tens of units, the assist reacts
  to them); after a contact runs part (stock itself is not repeatable past a contact).
  Two of four half runs stalled after a hit (speed 0 for good). A4_BENCH_STALL (stock, the
  car zeroed at one step): stock stays stalled too -- with the bench's keys the game only
  accelerates while moving (the step adds +1DE78's bit 4 only at speed > 0), so a stall is
  the bench's input, not the half step. Whether the half build stalls more often: unknown
  (0 of 4 stock runs, 2 of 4 half; not repeatable past contacts).
- Guesses (rule 4): the thresholds (from one census and two stock runs); +0F0/+0F8 left as
  computed (unclear rate or impulse); +1F1F8/+1FD5C/+1FD0E (the other cars) use the player's
  word lists -- not measured yet; the chase camera is mode 2 only.
- Where we are (rule 3): the player's car on the half step tracks the stock car (speed,
  place, heading, key steering) until the game's assist or a contact parts them. Next: the
  other cars (their own calls, not yet measured), the chase camera, and the frames themselves
  (frec.py at the phone's rhythm) before any phone build.
- The opponents (A4_CARREC_AI: race slot 1 recorded too, 'CREA'; tools aicmp.py): speed at
  steps 10/20/40/80/120 after the start 39/63/91/132/171 (half) against 37/62/88/129/171
  (stock), the same distance at 120 -- the other cars' calls (+1F1F8 and the rest, on the
  player's word lists) give the stock pace. Two stock runs differ by as much (34/60/92/163/186).
  (A first try wrote the 'CREA' header at CREC + 0x40 = A4_GL_HSTEP and the race never
  started; the header is on the stack now. Rule 4: the memory map before a new block.)
- The chase camera (camcmp.py: chase-mode steps, camera to car): halving the camera's update
  at both half-steps left it 398 units behind the car against stock's 319 -- a filter toward a
  point behind the car, halved, settles half a step's travel further back. Now the whole
  step's camera is the game's own (a4_h_12dec passes through) and the odd half-step's is drawn
  and put back before the next whole step (HP->cw / camx), so the game's camera runs exactly
  as in a normal build: 311-323 against 319.
  The odd half-step's camera, as drawn (frec.py over the run's frame records):
  - extrapolated by half its own last step: camera yaw one-frame reversals 8 in 417 frames
    (it overshoots where a turn eases), car heading less camera yaw reverses in 50 of 68
    moving frames;
  - moved with the player car's own half-step (place by the car's, yaw by 3/4 of the car's
    turn -- stock's chase camera takes about 3/4 of a turn in the step it happens, then closes
    the rest at 1-2 a step): reversals 0 in 417, heading-less-yaw 90% 3 /2048 (was 6),
    reversing in 19 of 37 moving frames. The car moves 91, 92, 92, 93 ... units a frame, the
    camera the same, 320-326 apart: even. Other cameras (fly-in, crash, helicopter):
    extrapolated by half their own last step (scripted paths), not across a cut.
- Battery of the half-step candidate (a4_r43_cand: C43, real time, no oracle; r43bat.sh as
  r42's): tele cap 85 framestat 3365, crash-steer cap 85 2740, menu 3438 (18 shots), mark
  1090, Veyron autopilot 1866 -- faults 1 in each (the boot-time one every run has); drop
  frames 4/4; held 0. Frames (frec.py): presents every 79 / 68 ms (median) at cap 85, and
  drawn speed off its neighbours by >25% in 25% / 16-18% of frames (r42 on the phone: 0%).
  The phone's r42 record: presents every 68 ms (54..90). At that rate half-steps of 42.7 ms
  fall one or two to a frame, unevenly -- judder from the frame rate, not the physics. The
  Veyron run's car-heading reversals are the game's own (-7, +26, -1: a key-steering turn of
  26 a call against the steering assist).
- So the cars now move at the renders, not at fixed half-steps (A4_FRACPHYS, C44 = C43 +
  it): each render runs the whole step's car calls again from the state the cars are in, each
  integrator's change taken by the fraction of the step between the last render and this one
  (q16, from the game clock and the scaled period, as r41's speculation measured it; hp_frac:
  16.16, the remainder carried), and KEEPS it; the whole step then moves the cars by the rest
  of the step (65536 less the fraction the renders took) for every car that moved at a render.
  Every frame is the game's state at its own moment; nothing is predicted, nothing taken back.
  The velocity measurement: at a render's part-step the last whole step's (+0A8.. = place less
  it), at the whole step over the whole step (+0A8.. = the place the last whole step left).
  The camera: the game's own at the whole step; at a render moved with the car since the
  whole step (or along its own path, other cameras), put back before the whole step. The
  traffic: drawn q16 on and only those words put back (the render's own changes kept, as r41).
  The odd half-step does nothing. HALFPHYS is the case q = 1/2.
  Oracle (lockstep; the record now at the first render after a whole step, before its part-step
  -- recording right after the step's logic caught the camera a step behind, 484 units, since
  the camera step comes after the logic): speed 54/73/109/163/174, the camera 318 behind
  (stock 319), heading within 14 /2048 through the assist turns (the half-step build: 54).
- Real time, cap 85 (r44q.sh: a4_r44_cand = C44; tele 150 s, crash-steer 120 s, Veyron
  autopilot 120 s; frec.py), drawn speed off its neighbours by >25%, by render start:
  tele 1% (half-step build 25%), crash-steer 0% (16%), Veyron 2%; Veyron camera-yaw and
  car-heading one-frame reversals 0 and 0 (half-step build 24 and 10). Presents every 77 /
  55 / 70 ms (the part-steps cost no visible frame time). Fraction q drawn: median 0.44-0.48,
  10-90% 0.07..0.91 -- frames anywhere in the step.
  Left in tele: during a wreck (camera mode 24) the crash camera jumps on its own step to step
  (yaw 1166, 1019, 1180) and extrapolating it put a frame past the next step (1288, then
  1182); the car's heading at the whole steps of the wreck jumps too (1342, 1414, 1305).
- Two changes from it: (1) the start gate was "the player's +0E0 > 0" -- but +0E0 counts the
  player's steps since its last CONTACT (crec_orn step 1076: speed 221 -> 135 and +0E0 0 for 4
  steps; not a lap), so the cars stopped moving at the renders for ~340 ms at every wall scrape
  and hit; now a once-a-race latch (HP->started), cleared when the race's step count (+0x30)
  goes back (a restart). (2) Other cameras are drawn on by their last whole step's move only
  when it repeats the one before (within half of it, +8): the crash camera's jumps are held.
- Oracle + battery of a4_r44_cand (C44, the latch and steady-camera rule; r44all.sh):
  oracle speed 55/74/111/164/173 (stock 58/75/107/161/182), camera 320 behind (319), the
  opponent 39/63/91/133 (37/62/88/129). Battery: tele cap 85 framestat 3375, crash-steer
  2648, menu 3409 (18 shots), mark 1152, Veyron 1813 -- faults 1 each (the boot one);
  emptyfind's nearest post 45.3 (no empty frame posted). Frames: drawn speed off by >25%
  0% (tele), 0% (crash-steer); car-heading one-frame reversals 0 / 0 / 0; camera-yaw 2 / 3 / 4.
  But: tele held 5 frames (A4_DROPHOLD), two with the camera still (camdev 13 and 1) and the
  triangles collapsed (1696 -> 419, 713 -> 27), and crash-steer one (camdev 24, 1561 -> 775).
  Every earlier battery's halved frames (r40t-r43v) come with a camera cut (camdev 154-778):
  these are new with A4_FRACPHYS. Held, never posted -- but the empty-frame symptom.
  A camera census (A4_CAMCENSUS: the race object before and after each +12DEC) finds no culling
  key there: the chase camera writes +058..+06C, +070, +08C (a countdown), +0F4/+0FC (a vector
  moving ~240 a step), +100 (+1 a call). Round 37's finding is the likely one: the empty
  renders were the camera inside the LA hill (terrain faces culled). The render-time camera
  moved with the car's whole displacement, height too -- down over a crest with the car while
  the game's own camera is smoothed. Now its height continues its own last whole-step move
  (capped 256) instead; place across and yaw still follow the car.
- The camera-height change (a4_r44_cand rebuilt, sha256 421a278f...): two tele runs at cap
  85, 230 s each (r44h1, r44h2): held 0 and 0, every halved frame with a camera cut (camdev
  180-543), drawn speed off by >25% 0% / 0%. Before it one 230 s run held two with the camera
  still. (Two runs is not proof; the rate before was ~1 a 115 s, so 0 in 460 s is p ~ 0.02.)
- Battery on this exact build (r44h1/2, r44bat2, r44v2; the container restarted after the
  mark, the Veyron run repeated; the archive found swapped and restored): crash-steer
  framestat 2739, held 0, drops 0, drawn speed 1%; menu 3589 (18 shots); mark 1234; Veyron
  1903, held 0, drops 0, drawn speed 0%, car-heading reversals 0, camera-yaw 2. Faults 1 in
  each (the boot one). shipchk: +3A15C B510 0004, no 0xDEADBEEF. Round 44 phone candidate:
  a4_r44_SHIP.exe (unbaked; the owner bakes).
- Guesses going out (rule 4): the word lists and event thresholds (one census, the player car;
  the other cars on the player's lists, their pace measured equal); +0F0/+0F8 left as computed;
  the chase camera's 3/4 yaw at render time; the traffic still drawn by extrapolation (not the
  game's own between steps); key bits at render time are the last whole step's (as stock:
  input acts at the game's step); the camera height at render time on its own move.

## Round 44, phone (r44, sha256 421a278f...): fractional physics breaks the physics

Owner: "this one felt even worse. physics are broken (cars no longer jump longer distances,
turbos won't last what they used to, a lot of jittering and 'online lag', a lot of
back-and-forth). Didn't see any empty frame, tho."
- Telemetry (8 files, capidx 333; only a4tel5 carries the frame record -- the other seven are
  152-159 KB, short of it): 10,718 real presents, 7,689 renders, 3,031 presents with no render
  since the last; held 2; two 1-second frames (loads, a guess). Frame record (256 frames): the
  player car drawn evenly (drawn speed 0% off by >25%), car-heading one-frame reversals 3,
  camera-yaw 9; presents every 67 ms. The record follows only the player and the camera.
- What it means: running the game's car calls at every render keeps everything the census
  and the probes classified right -- and runs everything they never saw at the frame rate:
  jumps (in the air the height and vertical velocity are integrated, on the ground they were
  "recomputed" -- the probe saw only the ground: rule 4 named it, and it shipped anyway) and
  the turbo (never used in the bench's runs) both run ~1.5-2x per step. The other cars and the
  traffic are where "online lag" and back-and-forth live, and nothing records them on the phone.
  The method needs every situation's words classified; one census of normal driving is not
  that. r44 is withdrawn.

## Round 45 (bench): kinematic frames (A4_KINEPHYS)

The lesson of r44: any game code run at a render runs at the frame rate whatever it does that
the census never saw. So none runs there now. The game's step computes everything as in a
normal build (engine, turbo, gravity in the air, AI, collisions, timers); a render moves each
car the step moved (+1DE78 / +1F1F8 cars: an opponent's calls are +1F1F8 and +1EA64 only,
A4_HPTRACE with A4_HPTR_SLOT=1) along the velocity that step measured (+010/+018) and turns
it at that step's turn rate, by the time since the last render (16.16, carried), and keeps
it; the height (+008, +0D0) and the wheels (+020) are moved for the drawing only and put back
before the step. At the step, for a car that moved at the renders, the movers' position change
and the heading change (+1DE78/+1F1F8 and +1EA64) are scaled to the rest of the step, and
before +1EA64 its +0A8/+0B0 are shifted by the step's move less the game's own, so it measures
exactly the move the game made (the speed model never sees part of a step -- round 43's start
stall). Words touched: position x/z, heading, +0A8/+0B0 (measured), drawn-only height and
wheels. C45 = C44 + A4_KINEPHYS. The camera and traffic as in r44 (the camera's height on its
own move).
- Oracle (lockstep): speed at 10/20/40/50 steps after the start 58/75/107/129 -- the same as
  stock to the unit (every earlier build was a few off); the camera 317 behind (stock 319);
  the opponent 34/60/92/163. Paths part from ~step 60 (the blend: a render moves along the last
  step's velocity, so a step's move is part old velocity, part new -- ~50 units by step 50).
- Real time, cap 85 (r45q.sh: a4_r45_cand, C45, sha256 30550903...): drawn speed off its
  neighbours by >25% 0% / 0% / 0% (tele / crash-steer / Veyron, by render start); car-heading
  one-frame reversals 0 / 1 / 1; camera-yaw 6 / 1 / 5 (mostly 5-9 /2048). Turn rate a frame
  uneven at steering changes (31% / 10% / 22% of frames off by >25%): a render turns at the
  last step's rate until the step applies the new one -- a step's delay, as stock, never back.
  Held 0 / 0 / 0; drops 3 / 0 / 4, all with a camera cut but one (Veyron: 1309 -> 651 triangles,
  camdev 19, not held) -- named, not traced.
- Battery on this build: tele 2103, crash-steer 2240, Veyron 2001, menu 3206 (18 shots), mark
  989 -- faults 1 each (the boot one); shipchk +3A15C B510 0004, no 0xDEADBEEF; the archive
  restored. Round 45 phone candidate: a4_r45_SHIP.exe (unbaked).
- Guesses going out (rule 4): the blend (a render moves along the last step's velocity and
  turn); the police (slots 8-14) may move through +1FD0E, not moved at renders (stock rate);
  a car drawn into a wall between steps until the step resolves it; the traffic still drawn
  ahead by extrapolation; the one halved frame above.

## Round 45 (bench, continued): a modelled N95 -- CPU, GPU and time dilation in the emulator

Owner, after r44: "Did you test this build thoroughly, adjusting the emulator to always mimic
the n95's CPU and GPU behavior?" -- no: every bench run so far capped instructions (`CPUCAP`,
85-115) by eye, with the GL free. Asked for: (1) the cap recalibrated against the phone's
telemetry, (2) why the phone presents repeated frames and the bench does not, (3) a jump and
turbo test against the unmodified game, and N95 GPU documentation behind an emulator model.
- Emulator (EMU_PATCHES.md, 2ec86e5 + the ARM/Thumb split): `EKA2L1_CPUMODEL` (ARM1136 cycles:
  a CPI for Thumb-state code -- the game -- and one for ARM-state code -- the port, compiled
  `-marm -mtune=arm1136j-s`, and the ROM; multiply and LDM/STM timings; a 32 KB 4-way data
  cache), `EKA2L1_GPUMODEL` (each GLES call stretched in place to the MBX driver/GPU cost: per
  call, draw, vertex, bind, upload byte, and the tile render plus copy a frame read waits for),
  `EKA2L1_TIMESCALE` (every guest clock D times slower; the cap and GPU charges paid x D -- the
  container is too slow for the model in real time), `EKA2L1_CPUCAP_STATS` (host keeping up).
- Sources for the GPU figures: Futuremark's certified 3DMark Mobile06 on an OMAP2420 (Symbian:
  CPU 300 MHz, MBX 50 MHz, 2.36 Mtris/s unlit, 0.79 lit; Linux 66 MHz, 33.7 Mtexels/s single
  texture) [design-reuse 12479, beyond3d 26169]; TI's OMAP2420 brief (MBX + VGP, "up to 2M
  polygons/s"); PowerVR is a tile-based deferred renderer, so a read of the frame waits for the
  whole tile render. The phone's own figures anchor the read: round 12's BGRA glReadPixels of a
  frame 6.2-7.2 ms (tile render and copy) -- the model charges 3.1-3.4 + 3.7.
- Calibration against the phone (r42 and r44 telemetry agree: render call 64.2/64.3 ms, before
  the first triangle 16.3, 0.8 ms a logic step, steps a render 30/60/9 %, presents every 68 /
  67 ms median): r42 build, 60 s accelerating race each (calib.sh; seg0 = the race
  segment's aggregates; windows = the last 8 telemetry windows of 23 renders):

  | run | Thumb/ARM CPI | GPU (call/draw/vtx/bind) | render call | pre-tri | ms/step | steps a render 1/2/3 | presents median (10-90%) | windows ms/render |
  |---|---|---|---|---|---|---|---|---|
  | phone r42 | - | - | 64.2 | 16.3 | 0.8 | 30/60/9 % | 68 (54-90) | 54-75 |
  | cal7 | 2.5 | 5/25/250/20 | 67.2 | 13.9 | 0.7 | 16/73/11 | 81 (68-107) | 72-114 |
  | cal8 (= cal7, stats) | 2.5 | 5/25/250/20 | 68.2 | 14.2 | 0.7 | 15/72/13 | 85 (69-109) | 64-93 |
  | cal9 | 2.8 | 3/20/150/8 | 68.1 | 16.1 | 0.8 | 13/73/14 | 89 (71-111) | 69-115 |
  | cal10 | 2.8/1.6 | 3/20/150/8 | 56.4 | 14.4 | 0.6 | 37/61/2 | 71 (57-88) | 53-88 |
  | **cal11** | **3.0/1.6** | 3/20/150/8 | 57.5 | 13.8 | 0.6 | 35/61/3 | **73 (60-92)** | **60-76** |

  cal11's windows against the phone's (r42 capidx 98-105): 3D 19.6-27.0 (16.7-28.0), composite
  12.5-15.8 (11.1-13.1), 2D 8.0-13.4 (5.7-6.9), rest 17.8-25.5 (16.6-24.7), logic a span 0.7-1.0
  (0.7-1.3), steps in 23 renders 32-41 (30-42). Host: asleep ~60%, behind the model 0.0-0.4%
  (EKA2L1_CPUCAP_STATS), so the bench is the model's, not the host's. GPU charges a frame
  (cal10/11): ~1,000 GL calls, ~310 draws, ~6,000 vertices, ~285 binds, 15 strip reads of 320x240
  BGRA. Before this (cal1-cal6): a single CPI of 1.0 ran the game at 85 ms a render call with a
  28 ms pre-triangle part; virtual time (EKA2L1_CYCLETICK) hid waits the phone pays (pre 6.9 ms
  against the phone's 16.3) and so was dropped for wall time dilated 4x. Calibrated settings:
  `EKA2L1_TIMESCALE=4 EKA2L1_CPUCAP=330 EKA2L1_CPUMODEL=300:30:2:4:5:160
  EKA2L1_GPUMODEL=3,20,150,8,2500,400,12,30,2300` (EMU_PATCHES.md).
- Remaining differences, named: the bench has no audio device (the phone mixes the game's
  sound on the same CPU); the display post lands in 'rest' on the bench and in 'pres' on the
  phone (same total); the model has no I-cache, TLB or per-instruction interlocks (one CPI per
  instruction set stands in for them).
- (2) The phone's "presents with no render since the last" (r44 28%, r42 8.5%) are menu
  presents: the r45 bench menu run had 3,870 of 4,415 presents with no render, r44's 5,153 of
  5,722 -- the counter covers the whole session. In races the half-step skip counter is the one
  to read: 45 (r44 phone) and 39 (r42 phone) in thousands of renders. Not a race defect.
- (3) Jump and turbo on the calibrated bench, real time, against the unmodified game (bench
  only: Veyron archive swap, restored and compared after each run; `A4_CARREC` +
  `A4_AUTOPILOT` -- '5' scripted in game steps, the bench script pressing nothing, NOKEY5=1;
  records per game step, crec2.py / jt.py / rates.py in the scratchpad). Builds: stock =
  C37nohalf (no half-step), r45 = C45, r44 = C44 (the build the owner reported broken -- the
  control the test must fail).
  - The jump: one ramp on the route (x 32,100-32,300, z ~28,600, y ~2,050). Stock (jt_n, 267
    km/h): airborne 14 steps, ~4,000 units, peak 260 above the ground; per step dy -23, -10,
    -10, -19, -28 ... -127: gravity exactly -9 a step. r44 (jt_f2, 341 km/h): airborne ONE
    step, then glued to the downslope (dy -101, -75, -87, ...) -- "cars no longer jump"
    reproduced. r45 in lockstep (orka, 2 renders a step): 13 steps, the same dy sequence and
    -9 a step. r45 in real time never reached the ramp (below).
  - Per-step clocks: +9FC (time since the last contact, 4096 at a contact, +3,413 = 85.33 ms x
    40 each step; 1,048,576 idle) advances 3,413 a step in stock (real time and lockstep) and
    in r45 (real time and lockstep), and 6,826 or 10,239 in r44 -- once per RENDER, 2-3x a
    step. rates.py (every car word's per-step change, r44 against stock) flags +9FC, +0B8,
    +0C0, +018 and +028 at x2. r44 also ran faster: mean 266 km/h, top 410 (stock 225, 320).
    The turbo's own word is not identified: '5' is the throttle at the countdown (+7D8 falls
    when it is let go, round 45 tap oracle ort/orr) and a tap mid-race changed no car word;
    +ABC (0..4096, +240 a step at pickups) is not it -- it drains the same with '5' let go
    (orr). So the turbo test stands on the per-step clocks, not a named turbo word (coverage:
    a turbo state outside the car object is not covered).
  - r45 in real time: three races (jt_k, jt_k2, jt_k3), three wrecks -- at k 391, 259 and 113
    (k = steps from the start), each the game's scripted wreck (speed 166, 135, 92, 52, 0,
    +A00 8500, 7751, 8500, 5199, 900 -- the same values every time) and then a dead car for the
    rest of the race (+A00 900; the bench never sees a respawn in any build: stock's lockstep
    wreck orna k 1020 stayed down 1,822 steps; an unstick rule, A4_AUTOPILOT_UNSTICK, '6' then
    '5'+'2' alternately, did not revive it either, orv vs orw). Stock in real time (jt_n): no
    wreck in 1,333 steps. Why: stock is pacing-independent (real time = lockstep for 112
    steps); r45 parts from stock at the FIRST step (x off by 5 at k 1, growing; ~200 units
    sideways by k 89) -- the blend round 45 named: render moves are committed into the game's
    state, so a step's move is part old velocity, part new. At the left-hand curve at x
    ~12,000 stock turns from k 88; every r45 run holds its heading ~10 steps longer and meets
    the outside there (speed down 20-30 km/h in all three), wrecking there (jt_k3) or later
    (jt_k2 k 259, jt_k k 391; orka, lockstep, got through with a contact at k 116). Per-step rates right, positions wrong: positions feed the road, the walls and the
    other cars. **r45 is not to go out.**
- Next (not built): kinematic frames drawn only -- the render moves and turns each car along
  its last step for the drawing and puts it back before the step, as round 45 already does for
  the height and wheels, with no scaling at the step; the test is the car records equal to
  stock's word for word in lockstep (the pointer words aside), then this bench's curve and ramp.
  The cost to expect: a snap at the step when the velocity changed (collisions), the r41 trade.

## Round 46 (bench): drawn-only frames with a corrector (A4_KINEDRAW)

Owner, after round 45's bench: "Do it [frames drawn only], but make sure that snap is fixed either
at the moment or afterwards. You may want to analyze this better before committing to it."
- Analysis first (the record and the stock car records). r41's snap-backs were the prediction
  meeting a key change it could not know (round 41 bench: drawn at q 0.93 toward a heading +51,
  the step turned +19); r42's interpolation had none but sat a step late ("sluggish"). Any
  predictor is sometimes wrong, so the fix is in how a wrong prediction is undone. On jt_n (stock,
  real time, calibrated bench) "the last step again" misses the position by a median 1.4 units,
  99% 43, at most 107 over a whole step; the heading is the velocity's direction (median 1 /2048)
  and the game turns in pulses of 20 /2048 a step, 2-4 steps long: a full last-turn prediction
  misses by >= 16 /2048 in 144 of 1,331 steps, 3/4 of it in 71, half in 70 -- the heading is
  where a snap would show (the camera turns with it).
- Design: no game code at a frame (the physics bit-identical by construction: r44/r45's lesson).
  Each car the step moved is drawn from the state the step left -- along its velocity, turned by
  3/4 of its last turn (A4_KD_TURN), its height on its vertical velocity but never under the
  ground drawn on by the ground's own last change -- and every drawn word (+004, +008, +00C,
  +0A8, +0B0, +0D0, +020, +678) is put back at the even half-step before the step, whose car
  calls run untouched (HP_HOOK empty). The snap fix, a corrector (kd_correct): at the first frame
  after a step, the frame continues from where the last frame drew the object, at the new
  velocity, and the difference fades out over a step (A4_KD_TAUSH 16): no frame jumps, and the
  picture is the game's own within a step; not across a cut (a respawn). The same for the chase
  camera (+0x58, +0x5C, +0x60, +0x68), and for the traffic (+004/+00C and the words that moved
  exactly as the position did that step and sit near it -- its copies, found per car per step).
  A4_KD_NOCORR (bench) turns the corrector off.
- Rule 4 caught before the first run: the frame at q 0 skipped the old 256-unit gate (now every
  frame draws), K must not be restored (the keys that arrive before the next half-step are the
  player's), a reused car slot resets its corrector.
- Lockstep against stock (crec_ors0 = C37nohalf, 1 frame a step; crec_ord / ord2 = C46, 2 frames
  a step; eqcheck.py / aligncheck.py): every physics word identical; the render's own counters
  differed (+15C ran 2 a step, two +B28 bits): since round 43 every frame kept the render's own
  changes. kd_snapall now saves the moved cars and the race object after a frame other than the
  step's first and puts them back whole at the next half-step (rounds 33-42 did the same). After
  it, aligned by the race's own step (race+0x30): positions, height, heading, speed identical for
  946 race steps (k 797); render-side words differ before that (+15C one render apart -- stock
  records at the step's start, after the last render, r46 before its first; +AC0 4..8, +ACC/+AD0,
  +AD8/+ADC from race step 350-532: words the render sets, +AC0 like a detail level by camera
  distance). For scale: stock lockstep against stock real time parts at k 829.
- Real time, calibrated bench (jt_d, the round 45 settings, 120 s, Veyron swap restored): no
  wreck; positions/height/heading/speed identical to stock's real-time run (jt_n) for 1,100 race
  steps (k 951), through the left-hand curve where r45 wrecked and over the ramp: airborne at
  k 923, 14 steps, 3,994 units, peak 260, 267 -> 278 km/h -- stock's numbers to the unit (its
  recorder skips a step where two whole steps fall between frames; aligned by the race step).
  Presents every 66 ms (10-90% 53-85; the phone 67-68); drawn speed off its neighbours by >25%
  0%; camera-yaw and car-heading one-frame reversals 0 and 0 in 417 frames. The corrector at each
  step's first frame (kdan.py, 'CRKD' records): player position offset median 3.1 units (90% 49,
  99% 83), heading 0.6 /2048 (90% 36, 99% 79), camera yaw 1.0 (90% 43, 99% 62); cuts 4.
- The corrector A/B on one weaving drive (A4_AUTOSTEER, calibrated bench, 120 s; st_d against
  st_dn = A4_KD_NOCORR; their physics identical for the first 567 race steps): corrector off,
  camera-yaw one-frame reversals 8 in 417 frames, each a frame late in its step (q 0.64-0.92)
  then the next step's frame back -- round 41's snap-back signature; corrector on, 0 (car heading
  0 and 0; drawn speed off >25% 1% and 1%). On the corrector-on drive the heading misses it took
  up at a step's first frame: median 1.7 /2048, 99% 63, max 107 (key changes), each spread over
  a step instead of a jump.
- The traffic A/B (tm_d against tm_dn, FRAMEMARK, trafpace.py on a4_traf_mark, straight drive):
  drawn speed off its neighbours by >25% 6% / 5%, drawn backwards 17 of 1,164 / 26 of 1,259
  windows; over every frame, backward moves 17 / 38, the largest 371 / 609 units in a frame
  against a normal 70-100 -- traffic knocked about by the player, which a frame cannot foresee:
  the corrector halves how often it shows and shortens it, and cannot remove it.
- Candidate r46 (C46 = C45 + A4_KINEDRAW; sha256
  06eeb2637d91d540baa12c2622c86a5741343d068a25ed1f9f32c983facb07a5, 811,068 bytes; no 0xDEADBEEF;
  +3A15C B510 0004). Battery on that binary (r46bat.sh): tele on the calibrated N95 bench 120 s
  framestat 2263, faults 1 (the boot fault), held 0, drop frames 1, camera-yaw one-frame
  reversals 2 (7 and 9 /2048), car heading 0, drawn speed off >25% 0% by render start (6% by
  present); crash-steer at cap 85 framestat 1882, faults 1, reversals 0 / 0; racemenu framestat
  2877, faults 1, 18 shots; mark framestat 890, faults 1; the Veyron with the continuously
  steering autopilot at cap 85 (the drive that showed r41's 5 / 5) framestat 1783, faults 1,
  camera yaw 2 (16 and 13 /2048), car heading 0. The archive restored and compared. Round 46
  phone candidate: a4_r46_SHIP.exe (unbaked).
- What the residual camera swings are: not a snap at a step (the corrector keeps those
  continuous) but a frame late in a step still turning with a turn the game had just ended (a
  road-follow pulse or a key let go), then the next step's frames correcting smoothly.
- Guesses going out (rule 4): A4_KD_TURN 3/4 (from the bench's stock records, not the phone's);
  the fade over one step (a choice); the traffic's copies found by moving exactly as the position
  did and lying within 64 units of it; the height kept above the ground drawn on by its own last
  change; the first frame after a step keeps its render's own changes at drawn positions, so
  render-set words (+AC0, +ACC/+AD0, +AD8/+ADC) differ and the race parts from stock's after
  ~800-950 steps -- as stock's own lockstep and real-time runs part at 829; the race object put
  back whole after a frame other than the first (as r33-r42), so anything written into it
  between such a frame and the next half-step would be undone.
- States the bench never entered (rule 4): a respawn after a wreck (the bench's car never
  respawns, in any build -- the corrector's cut at >1024 units is read in the code, not seen run),
  a race restart, a pause mid-race, the phone's human steering and its audio load.

## Round 46, phone (r46, sha256 06eeb263...): the camera loses the car

- The owner: "this version was terrible. The camera doesn't follow the car appropriately. I didn't
  even finish a lap." Four photos: the car off the centre of the view, one partly off the screen.
- Telemetry (8 files, r46ph): presents 14,801 (14,067 without a render: menus), renders 734, held
  0, drop frames 2; the game's render call 67 ms; camera-yaw one-frame reversals 2, car heading 0,
  drawn speed even. Every number round 46 measured was fine -- and none of them measured what the
  owner saw.
- What they saw, measured (tools/camrel.py, new: per chase-camera frame, the player car's bearing
  from the camera less the camera's yaw, the car's heading less the yaw, its distance): bearing
  off its median 50% 35, 90% 76, 99% 91 /2048 (r42ph 2/6/23). The bench showed it too and nobody
  looked: jt_d 10/84/122, the Veyron run (r46v) 75/144/214, with the corrector off 0/6/17; the
  round 46 mark screenshots show the car off centre. Rule 4 failure: the battery measured each
  object's smoothness, never the car and camera together, and its screenshots went unread.
- Cause: round 46 put the camera's position through a corrector of its own. In a curve the game's
  camera orbits the car step by step; the corrector held the camera off that orbit while the car
  drew on its own, so the car slid across the view.

## Round 47 (bench): a rigid chase camera

- The game's chase camera (mode 2) sits straight behind the car along its own yaw: offset
  direction yaw - 1024, spread 0-2 /2048 on r42ph, r44ph, r45t. So a frame sets the camera
  rigidly on the drawn car: the game's offset from the car at the step, turned (kd_rot, sin/cos
  by series in Q30) by the yaw the frame draws less the game's camera yaw at the step, added to
  the drawn car's position. The yaw is round 45's: the game's camera yaw and 3/4 of the car's drawn
  turn. The camera has no corrector; the height keeps its own rate. The framing is the game's by
  construction.
- A/B on the Veyron at speed (continuously steering autopilot, cap 85, Veyron archive swapped and
  restored), camrel bearing / heading-less-yaw spread (50/90/99%):
  r46v 75/144/214, 33/71/98; r47a (the yaw through the corrector, A4_KD_CAMYAWCORR) 0/1/3,
  22/93/128 -- and one 100 /2048 yaw jump, a large offset faded out across one sparse frame;
  r47b (no camera corrector) 0/1/2, 4/39/68 (r42ph 7/25/59 for heading). r47b camera-yaw one-frame
  reversals 3 in 417 (7, 12, 5 /2048 ...), car heading 0. The large frame-to-frame jumps of heading
  less yaw (99% ~1000) are camera-mode changes (chase <-> mode 6), not frames. The mark screenshots
  of r47b (shots_r47bk) all have the car centred. A4_KD_CAMYAWCORR stays a bench switch, off.
- How evenly the camera turns (tools/yawjerk.py, new: the chase camera's turn rate per presented
  frame, /2048 a game step, and its change from the frame before, at a step's first frame and
  within a step; the same for the car's drawn heading). r47t: camera at first frames 90% 18, 99%
  48, within 6 / 16; the car's own heading 15 / 43 and 17 / 40. The camera's turn steps at a
  step no more than the car it follows; r46 on the phone 23 / 55 at first frames. The yaw keeps
  round 45's 3/4 model, so the step-boundary change is the game's camera lag against that model.
- Candidate r47 (C47 = C45 + A4_KINEDRAW, the r47b code; sha256
  3e28589fd9f662482d808a803351add4c93ec52cdf44bb7fce84e75686577af0, 811,268 bytes; no 0xDEADBEEF;
  +3A15C B510 0004). Battery on that binary (r47bat.sh):
  - tele on the calibrated N95 bench, 120 s: framestat 2229, faults 1 (the boot fault); presents
    1,886, renders 1,885, held 0, drop frames 0; drawn speed off its neighbours >25% 0% by render
    start, 1% by present; camera-yaw and car-heading one-frame reversals 0 and 0 in 417; camrel
    bearing 0/1/4, heading less yaw 0/3/6, distance 295 (294..295).
  - crash-steer at cap 85: framestat 2783, faults 1; camrel 0/1/4, 0/1/7; reversals 1 (4 /2048)
    and 0.
  - racemenu: framestat 4117, faults 1, 18 shots, looked at: menus, garage, the pause over the race
    with the car centred.
  - mark (Mini): framestat 1466, faults 1; 12 of 24 shots looked at: the car centred behind in
    every one, in a curve, under nitro, among traffic.
  - the Veyron with the continuously steering autopilot at cap 85: framestat 2097, faults 1;
    camrel 0/3/5, 0/6/17 (274 chase frames; the rest a mode-6 camera); camera-yaw one-frame
    reversals 6 (6, 4, 22, 5, 4, 31 /2048), car heading 1 (20, the same frame as the 22): the
    camera turns with the drawn car; r47b on the same drive 3 and 0, r46 2 and 0.
  - mark on the Veyron: framestat 1378, faults 1; 12 of 24 shots looked at, 177-286 km/h: the car
    centred in every one. The archive restored and compared (carsa, carsb).
  Round 47 phone candidate: a4_r47_SHIP.exe (unbaked).
- Guesses going out (rule 4): the chase camera's offset from the car taken as the game's at the
  step and turned rigidly (the game's own offset direction spread 0-2 /2048 says it is straight
  behind; its length is not modelled to change within a step); the yaw's 3/4 of the car's drawn
  turn (round 45, from the bench's stock records); the camera's height on its own rate (round 37);
  everything round 46 listed for the car and traffic corrector. Not entered on the bench: a
  respawn after a wreck, a race restart, a pause mid-race, other camera views than chase while
  steering (they keep round 45's handling), the phone's human steering and audio load.
- Lesson kept (rule 4): a battery that measures each object alone can pass a build whose
  objects do not agree. camrel.py (car in the view) now runs in every battery, and the mark
  screenshots are looked at before a build goes out.

## Round 47, phone (r47, sha256 3e28589f...): cars fly off and snap back

- The owner: "it's still the same ... look at how the car seems to start 'flying' upwards, then
  snap back to the road. This can happen in any direction, and all cars suffer from the same."
  Seven video frames in a tunnel at 412-430 km/h with nitro (Veyron, 3rd/8, lap 1/3): the car
  rises up the view and grows over about six steps (the speed read-out changes each frame), is
  drawn above the camera's eye at the top of the screen, then is back on the road.
- And the directive: disassemble the game and run bench rounds only until the engine is
  understood -- no more rounds sent blind.
- Telemetry (8 files, r47ph): presents 2,504 (163 without a render), renders 2,342, held 1, drop
  frames 5; camrel bearing 1/2/8 /2048 and heading less yaw 5/17/29 -- the framing in x-z holds,
  as on the bench. The frame record has no heights (car x, z, heading; camera yaw, x, z), so
  what the owner saw is outside everything it measures: again a battery blind to the fault.
- Read in the code, not yet measured: the drawn height is the step's y plus the measured +014
  times the fraction, through the corrector (cut 512 units), and kept above a drawn ground; the
  camera's height runs on its own last change (capped 256 a step). Both are models of an engine
  never read: what +014 holds, how the game sets a car's height and the camera's, and what
  the render builds a car's transform from.

## Engine map 1 (bench): why the cars fly -- read in the code, reproduced on the bench

Owner (round 47): bench rounds only until the engine is understood. The map is ENGINE.md; this
entry is the round. Not a repeat: no earlier round recorded heights per frame, nor read +22832,
+1FD5C or the vertical logic of +1EA64.
- Instrument: A4_YREC (bench), every car as each frame draws it with the step's y, +0D0, the
  corrector's y offset beside it (a4_y1.exe = C47 + A4_CARREC + A4_YREC + A4_AUTOPILOT; jtrun.sh,
  calibrated N95 bench, Veyron swap, 120 s: framestat 2236, faults 1, archive restored).
- Reproduced: every racing car's drawn height leaves the line between the game's heights at
  the steps around the frame -- 99% 160-214, max 276-550 units, slots 0-7 alike (the camera is
  ~300 behind). Not seen before because the frame record and every battery metric had no height.
- Cause, in two parts. (1) The engine: +014 is measured (+22832) before the vertical logic
  moves y (kind 4 snaps y to +0D0 after it, +1FD70; kind 3 runs gravity and landing after it and
  rewrites +0AC). Measured: +010/+018 equal the true x/z change in 1,301 of 1,314 player steps,
  +014 misses the true y change in 967 (99% 69, max 127). (2) Ours: with about one frame per
  step, the corrector renews its offset from the last drawn value at each step's first frame and
  fades it only between a step's frames, so a missed motion accumulates and the car holds still
  -- slot 3 airborne down the LA hill, +014 0, offset +121, +203, +294 ... +497, cut at 512,
  snap (steps 1026-1033).
- The engine's own vertical rule, read: airborne y += +A58 then +A58 -= 9 (20 with +A64), a
  hang counter +AF0, landing at +0D0, a floor at +0D0 - 64. An airborne car's next height is
  y + +A58 exactly; a car on the road follows +0D0.
- Not yet read (next): the chase camera (+4B40: modes, distance, the direction vector begun),
  +1F1F8 and +1DE78 in full, the traffic's move (+FB12), the render's car tilt (+A434: +678,
  +A04, +A20, +A24), the road query behind +0D0 (+226E8 -> +24604, +23E72).

## Engine map 2 (bench): the camera and the road read; frames built on the engine's rules (A4_ENGDRAW)

Owner: "go ahead" (the engine map). Not a repeat: no earlier round read +4B40/+5968/+49C4, the
road functions (+24524, +24604, +246F8) or the traffic's +FB12 order, nor measured the camera
against the car per step.
- Read (ENGINE.md): the chase camera is placed at each step from the car alone -- x, z = car less
  a vector along `heading + race+0xE4` times a distance, y = car y + 52 (+A8C) + 53 + a slope term,
  pitch = car +674 - 25 - the slope term, yaw = heading + race+0xE4 -- then +49C4 shakes it at
  speed. Measured over run y1's 1,238 chase steps: yaw less heading -1..0, race+0xE4 0, camera y
  less car y = 105 + 0..20. **No lag**: the 3/4-of-a-turn rule our frames used since round 43
  has no basis. The heading turns in pulses (0 in 1,096 steps, -20 in 130, +20 in 34; the next
  repeats the last in 87%). The road is a straight line within a segment (+24524: t along the
  segment, height prev + (seg - prev) t, in mode 0); +020 is that t (not the wheels), +0D0 the
  height. The traffic sets its y before the measure, so its +014 is true.
- A4_ENGDRAW (bench switch, a4_gl.c): heading by the whole last turn (A4_KD_TURN 65536); height
  by the engine's rule (in the air +A58, on the road +0D0's own rate, a landing on the drawn road);
  the camera rigid on the drawn car in x, z, y and yaw; the corrector's first frame continues the
  last frame's model part at the new velocity with its offset still fading (r46-r47 carried the
  offset whole: the freeze); +020 clamped, not wrapped; a respawn (a jump over 2,048 units) zeroes
  the rates.
- A/B on one drive each (calibrated N95 bench, Veyron autopilot, 120 s; y2a = C47 frames, y2b =
  A4_ENGDRAW; tools/offline.py: each frame against the line between the game's values at the steps
  around it), all eight racing cars, 99th percentiles:
    height off the path   161 (max 542)  ->  10 (max 408: an opponent's respawn, now handled)
    x-z off the path       97 (max 222)  ->  30 (max 125)
    heading off the path   47 (max 125)  ->  12 (max 59)
    camera yaw less the drawn heading  -11..10  ->  -1..0;  camera y above the drawn car
    -11..286  ->  105..159
- Physics against stock (aligncheck vs jt_n, positions/heading/speed): y2b identical until race
  step 1,100 (as r46's jt_d), y2a until 940 -- the frames change nothing the game computes.
- Phone telemetry gains heights: 'FRC2' (A4_INTERP's unused block 0x45C000, appended after FRC1;
  per render the drawn y, camera y, the step's y, +0D0, x and z), read by tools/frec2.py.
- Candidate r48 = C47 + A4_ENGDRAW: sha256
  ae513927452140fbdf67deb434c394779a80e4f51a730a65b23f2e1f0390acfd, 812,152 bytes; no 0xDEADBEEF;
  +3A15C B510 0004. Battery running (r48bat.sh: the recorder run y3, then r47's battery with
  frec2 on each).
- Battery on the candidate (r48bat.sh), each run's heights by tools/frec2.py (FRC2 against FRC1;
  the record has no step number, so a step pair moving twice its neighbours is taken as two steps
  -- the first reading's 244-unit "miss" was the tool's line drawn two steps ahead):
  - recorder run y3 (a4_y3.exe = C48 + A4_CARREC + A4_YREC + A4_AUTOPILOT, all racing cars): off
    the game's path 99% x-z 14 (max 150), height 3 (max 32), heading 9 (max 112); the camera
    105-112 above the drawn car, its yaw the drawn heading (0..0). The two maxima are one contact
    (steps 560-561: the player hits a stopped opponent, its heading jumps 128 in a step) -- a frame
    drawn before the step that registers a contact cannot foresee it; the corrector fades it.
  - tele on the calibrated N95 bench 120 s: framestat 2187, faults 1; presents 1,887, renders
    1,843, held 0, drop frames 1; drawn speed off >25% 0%; camrel bearing 1/1/4, heading less yaw
    0/1/4; FRC2: height off the path 99% 7 (max 8), x-z 99% 42 (max 64), camera y above the car
    105-112; camera-yaw one-frame reversals 9 (4-14 /2048; r47t 0 -- the camera now turns with the
    drawn heading's own misses at a turn's start and end), car heading 1.
  - crash-steer at cap 85: framestat 2783, faults 1; camrel 0/1/3; height 99% 1, x-z 8;
    reversals 0 and 0.
  - racemenu: framestat 3491, faults 1, 18 shots (looked at: pause over the race, menus, garage).
  - mark (Mini): framestat 1248, faults 1; 12 of 24 shots looked at: the car on the road and
    centred; the last four after the autopilot drove into a wall (stopped, on the ground).
  - the Veyron, steering continuously, cap 85: framestat 2242, faults 1; camrel bearing 0/1/24,
    heading less yaw 7/42/84; height 99% 24 (max 29), x-z 99% 60 (max 75); camera-yaw reversals 7
    (up to 35, 29, 30 in one run of frames), car heading 0. Read in the code: in the drift state
    (+B28 bit 12) +4B40 adds +808 >> 6 to the yaw (and a roll, and moves the vector's angle by
    +808 >> 7): the game's own camera is off the car's heading then and steps with +808; the frames
    hold that offset through a step. r47 on the same drive: reversals of 22 and 31.
  - mark on the Veyron: framestat 1241, faults 1; 12 of 24 shots looked at, 113-277 km/h: the car
    on the road in every one (a traffic car thrown upside down in a takedown is the game's).
  The archive restored and compared. Round 48 phone candidate: a4_r48_SHIP.exe (unbaked).
- Guesses going out (rule 4): +020 clamped at the segment's end (where the render reads it is not
  found); the road's height rate taken from its last change (exact within a segment in mode 0;
  modes 1-4 not read); the heading's next turn = its last (87% exact; misses at a turn's start and
  end); the drift's camera offset held through a step; a contact unforeseen for a step. States the
  bench did not enter: Monaco's tunnel at 420 km/h (the owner's photos; the LA hill and jumps
  stand in), a race restart, a pause mid-race, human steering.

## Round 48, phone (r48, sha256 ae513927...): the camera fixed; back and forth in four events

- The owner: "Camera has been fixed. However there's still the back and forth in all four events:
  takedown, crash, helicopter view, and finished race." Make the next build fix them, and make the
  emulator mimic the N95's CPU and GPU so these bugs replicate: "we shouldn't be testing blindly".
- Telemetry (8 files, r48ph; the framebuffers: LA, the Veyron at 412-575 km/h, lap 3/3, the finish):
  presents 2,759 (346 without a render), renders 2,413, held 0, drop frames 15, 22 slow-motion
  frames; camrel (chase) bearing 2/8/23, heading less yaw 4/18/31; FRC2 height off the path 99% 86
  (max 166), x-z 62 -- the windows hold the last 256 renders before each capture, mostly the finish.
  The finish camera is mode 1; its frames: the camera moves with the car except some (t 51404:
  the camera still, the car on 204 units, its bearing -56 then +62 the next frame).
- Read in the code (ENGINE.md): +12DEC picks the camera each step for the player car (+52206):
  0x18 when the player crashes (+B28 flag or the race's crash state), 0x2A while a police slot
  holds the helicopter in its first state, a jump camera on the air counters, 0x17 the start's
  fly-in (and other scripts 0x19/0x1A), else the player's view (0-7, 0x16 remembered at cam+0x50
  by +3FE6; 0-3 cycled by the camera key, +421AA). +5968's switch: 0 +50EC, 1/2/3/0x2A +4B40 (the
  chase family, rigid on the car with a turn race+0xE4: the finish orbits ~8 /2048 a step), 4
  +4F3C, 5-7 +490A (car-relative), 0x14 +4764, 0x16 +42C8, 0x17 +57AC and 0x18 +554C (keyframe
  scripts, +5205A), 0x19 +4382, 0x1A-0x1D others. Every one follows the player car.
- Ours (r46-r48): outside mode 2 the frame moved the camera on its own last step's move only while
  that move repeated the one before, else held it at the step -- while every car moved on. The
  measure (tools/camback.py: the player car's bearing and elevation in the camera's view, frame by
  frame, by mode), on y2a (r47's frames, a crash at race step 958 -- r48 the same outside mode 2):
  crash camera 0x18, 48 frames: bearing change 50% 11, 90% 69, 99% 109 /2048, reversals 13;
  elevation reversals 13 -- the game's own steps there: 1 and 1. Chase mode on r48 (y3): 0 and 0.
- Slow motion read: +15B1C counts steps by floor(clock / P), P = 10667 x race+0x84C / 256, so the
  fraction A4_SLOWMOQ computes (clock mod P) is the game's own, scale changes included.
- The bench against the phone (same track, the Veyron; r48ph against c0 = the calibrated model
  on the r48 binary): presents 64 (48-87) against 67 (56-83); the render call 62.2 ms against
  50.9; its CPU part before the first triangle 15.1 against 9.3; the 2D phase 6-8 against 8.5-14.
  A try (c1: Thumb CPI 4.5, ARM 1.4, GPU costs lower) went the wrong way (presents 75, composite
  18-19): the CPU part scales with memory, not the Thumb CPI. c0 kept for the event runs (its frame
  interval, hence frames a step, within 5%); the split is open.
- A4_CAMALL (a4_gl.c): outside the chase, the camera's offset from the player car in the car's own
  frame carried on by its own last change (o_k + (o_k - R(dh_k) o_k-1) q, turned by the drawn
  turn, on the drawn car), its height above the car and yaw less heading likewise, pitch and roll
  by their own change; a cut (over 512 units, 128 /2048) carries none. Exact for a camera fixed to
  the car, exact for one standing still as the car passes, near for the finish's orbit.
- Bench recorder: 'FRKC' per frame (the mode, the slow-motion scale, the race step, the player
  car, the race object). Event battery running (r49ev.sh): crash-steer (old y4os / new y4s), the
  Veyron with a watch on race+0x84C (takedowns; old y4o / new y4), a whole race to the finish
  (old / new), forced slow motion (A4_BENCH_SLOWMO=512, new). The helicopter view (0x2A, the chase
  family through +4B40, as mode 1) is not on the bench route.

## Round 49 (bench): the event views reproduced and fixed; the model's split probed

- The finish (mode 1), reproduced and measured (tools/telback.py on the phone-format records: the
  player car's bearing in the view frame to frame, by mode): r48's frames (fin_o, the Mini on the
  autopilot to the line, cap 85) 146 frames, bearing change 99% 25 /2048, reversals 10 -- the phone
  (r48ph) 93 frames, 99% 62, reversals 16; A4_CAMALL (fin_n) 135 frames, 99% 5, reversals 1.
- The crash (0x18): r48's frames (y2a) 13 reversals in 48 frames, 99% 109. A4_CAMALL (lw_n, fin_n,
  slo): 1-5 reversals in 43-48 frames, 99% 34-67; against the game's own step path (lw_n's records)
  most frames within 1-20 units, but where the script's turn speeds up the yaw misses by 30-77
  /2048 (steps 760-761): a script's turn cannot be foreseen. A4_SCRIPTHOLD: in the scripted modes
  (0x14, 0x17-0x1D) the frame is the game's step -- camera and every car as it left them --
  round 40's A4_SPECSCRIPTHOLD again; checked by A4_BENCH_HOLDTEST (the hold forced 3 s in 10,
  tools/heldchk.py: every car exactly at the step in those frames) -- running.
- The helicopter (0x2A) goes through +4B40 as the chase and the finish do: A4_CAMALL's rule there;
  the bench route never reaches the news event. The takedown: its camera mode and the writer of
  the slow-motion scale not found (the watch on race+0x84C reported a menu bitmap blit at +295F8
  before the race occupied that memory; no immediate-offset store to +0x84C in the code); a race
  crash state at race+0x834/+0x83C next to the scale sets 0x18 (+12ECC) -- held by A4_SCRIPTHOLD if
  that is it. Forced slow motion (A4_BENCH_SLOWMO=512, slo): chase frames 0 reversals, drawn speed
  even (A4_SLOWMOQ's fraction is the game's own, +15B1C).
- A determinism note: lockstep runs of builds that draw differently part (the race start's fly-in
  is drawn differently, the render draws random numbers) -- lw_o/lw_n parted, ls_o/ls_n stayed
  together with no event; A/Bs of events are by statistics.
- The phone's climb (r48ph, LA): two steps between frames, the road's rate zeroed, the car held 160
  under the road. A4_GAPRATE: a rate over n steps; the gap A/B at a slow cap (presents 78-90 ms):
  height off the path 99% 14 (max 90) -> 12 (max 64), camera height 21 (max 90) -> 8 (max 45).
  Its run showed the player put back after a crash 708 units away with its velocity 0, bridged by
  the corrector for a frame: a step that moved otherwise than +010/+018 by over 256 is a placement,
  the corrector restarts (in A4_GAPRATE).
- The model's split (one parameter at a time, the r48 binary, the Veyron 120 s; phone r48ph: presents
  64 (48-87), the render call 62.2 ms, its CPU part before the first triangle 15.1):
    c0 (300:30:2:4:5:160, GPU 3,20,...)  presents 67 (56-83), call 50.9, before 9.3
    c1 (Thumb 450, ARM 140, GPU lower)    75 (70-88), 57.5, 11.1 -- worse
    c2 (read miss 60)                     69 (49-82), 53.5, 10.1 -- the spread the phone's
    c3 (ARM CPI 240)                      70 (59-86), 55.1, 8.8 (the composite 16-18: ARM-bound)
  The CPU part before the first triangle moves with none of them (Thumb CPI, read misses, ARM CPI);
  next probe: write misses (the game's sky and background drawn before the first triangle). c2 is
  the bench's model from here (the battery runs on it).
- Candidate r49 = C48 + A4_CAMALL + A4_SCRIPTHOLD + A4_GAPRATE: sha256
  a63da3a09929b09a244edb9fbbe8dd9aacb07f75bac2701c9ad6695ae7a61a38, 813,232 bytes; no 0xDEADBEEF;
  +3A15C B510 0004. Battery queued (r49bat.sh, c2 model): the recorder run, tele, crash-steer, a race
  to the finish, menus, mark, the Veyron, the Veyron's mark; telback on each.
- The steady-change gate (ca4d0ae, in A4_CAMALL): a component of the camera's change over the car
  is carried only when it repeats the step before's (|g - h| <= |g|/2 + 4 units, + 2 /2048), so the
  game's random shake (the helicopter's +-6 yaw a step) is drawn at the step, not thrown ahead.
- Probe c4 (c2 + write miss 8 cycles): presents 68 (56-87), the render call 56.8 ms, before the first
  triangle 11.1 -- the first parameter that moves that part at all (c2 10.1; phone 15.1), and the
  call nearer the phone's 62.2. Not adopted yet (the battery was already on c2); the rest of the
  gap (~4 ms) is still unexplained by any CPU parameter -- a guess: the driver's frame start (the
  MBX's tile setup and the first bind), not modelled in EKA2L1_GPUMODEL.

## Round 49 battery: the final candidate (r49b)

- Candidate r49b = r49 + the steady gate: sha256
  81193beb03a2e563bf0d0ab58bde21dc0239bce1ca80a7ea129b47c165b63d1b, 814,060 bytes; no 0xDEADBEEF;
  +3A15C B510 0004. Battery r49bbat.sh on the exact binary, c2 model where marked.
- Recorder (a4_y8 = the same flags + A4_CARREC/A4_YREC/A4_AUTOPILOT, the Mini 120 s, c2 model, TS 4):
  a crash on the route (0x18, 48 frames): drawn held, bearing reversals 0 (the game's own steps 2);
  chase 1436 frames, reversals 0. All cars off the game's path (tools/offline.py, now leaving out
  respawn height jumps > 600 and the held script frames): x-z 99% 98 (max 235, the crash), height
  99% 15 (max 95), heading 99% 12. The height's tail is a landing: a car at its floor (+0D0 - 64)
  is drawn up to ~100 under the road for one frame before the landing step -- proposed: when the
  step's y is under the road, aim at the road plus its own rate; not in this build.
- Tele (calibrated N95 bench, the phone-format records): framestat 2083, faults 1; chase framing
  bearing 99% 3, heading less yaw 99% 3; height off the path 99% 5 (max 8); presents 1748, held 0,
  drop frames 0. The helicopter view (0x2A) reached on the bench for the first time (after a crash):
  90 frames, bearing change 99% 8, 2 reversals -- both at step boundaries, the game's own shake;
  inside a step the change is 0.
- Crash-steer (cap 85): framestat 2300, faults 1; 417 chase frames, camera yaw and car heading
  one-frame reversals 0; height off the path max 1.
- Race to the finish on the autopilot build (NOKEY5, 330 s): the Mini reached the line early and the
  run ended on the still results scene (417 frames of no motion) -- re-run on the exact binary with
  the key held (below).
- Menus (r49bmn): the pause menu, every row, restart, confirm, main menu, the garage -- all drawn.
  Mark (r49bk, the Mini 170-196 km/h; r49bkv, the Veyron 225-290 km/h, Fame Avenue banner): drawn,
  centred. Archive restored (cmp).
- Veyron on the autopilot (cap 85): framestat 2257, faults 1; it hit the wall at the start and was
  put back every ~3 s (mode 6, the view from in front looking back, 142 frames: 0 reversals; chase
  275 frames: 0 reversals) -- the route, not the build; the mark run is the Veyron at speed.
- Takedowns: no slow motion in any bench capture (tools/frkc.py over every 'FRKC' capture: race+0x84C
  is 256 throughout), so no takedown has been reproduced here. The evidence it is the scripted 0x18:
  round 41's speculation skipped script calls only in 0x18 (skipana, r41c), its hold of those frames
  is what the owner reported as better ("not as before", r41), and the race's crash state next to the
  scale (+0x834/+0x83C) sets 0x18 at +12ECC. Held by A4_SCRIPTHOLD if so; if it is a player view
  (0-7) A4_CAMALL draws it. A guess until a phone run says.
- Race to the finish on the exact binary, key 5 held (cap 85, 300 s; r49bf2): framestat 4472, faults
  1; the finish (mode 1) 124 frames, bearing change 99% 5 (max 5), reversals 0 -- r48 on the phone
  (r48ph) 93 frames, 99% 62, reversals 16; r48's frames on the bench (fin_o) 99% 25, reversals 10.
  Chase before it: 293 frames, 99% 7, 4 reversals (the step-boundary steering pulses).
- Sent as a4_r49_SHIP.exe (unbaked; the owner bakes). What to look at on the phone: the finish
  orbit, a crash, the helicopter, a takedown -- each for back-and-forth; and whether a crash or
  takedown camera now looks choppy (drawn at the game's 11.7 Hz steps by design: if it does, the
  next step is to draw those views one step behind, between two known steps).

## Round 49, phone (r49, sha256 81193beb...): the views fixed; traffic "passed through"

Owner: "They look much better now. However it's almost impossible to hit traffic cars. I could pass
through them and not trigger any crash or hit." Eight telemetry files (r49ph).
- Telemetry: presents median 57 ms (10-90% 46-81; r48 64), render start to present 47 ms; frames by
  mode (tools/telback.py): the finish (mode 1) 67 frames, bearing change 99% 5, reversals 0 (r48ph:
  16); the chase 319 frames, 99% 6, 4 reversals; 0x18 31 frames with the player still at ~400 units a
  step -- a takedown, not a crash (the player keeps its speed), drawn at the step by A4_SCRIPTHOLD as
  designed: the takedown camera is 0x18, the round-49 guess confirmed. The player drawn off the
  game's path (frec2, chase): along 99% 36, across 50% 6, 90% 28, 99% 53 -- the steering the frame
  cannot know. The player's step: 430-490 units (the tuned Veyron; 487 on nitro).
- The game's traffic collision, read: +1FF56 (car against the 16 traffic cars, called 8 times a step
  -- once a car -- measured: EKA2L1_PCTRACE_REGS on +FB12/+1FF56, 1,642 of 1,643 steps exactly 8, no
  sub-steps) skips a car with +0x128 bit 0 (a wreck), then the gate +23728: the two spheres (centres
  +04C, radii +078: player 77, traffic 70-98) overlap at the step's positions, |d| <= rA + rB. A plain
  overlap at each step: two cars that close faster than ~2(rA + rB) = 300-350 units a step can pass
  between steps without the game ever seeing them touch.
- Bench (tools/trafoff.py, tunnel.py; a4_y9 = C49 + recorders + 'FRKT', every traffic car within
  4000 of the player as drawn and as the game has it; a4_s1/s2 + A4_BENCH_SEEK, the autopilot
  steering at the nearest live traffic car, AUTOPILOT_UNSTICK; slim records, jtrun2.sh):
  - the drawn gap between the player and a traffic car near it (< 800) against the game's at the
    frame's moment: y9 50% 2, 90% 11, 99% 83 (max 128, at the hit); s2 50% 1, 90% 6, 99% 34 -- what
    is drawn is where the game has it, within a fraction of the 147-175 the spheres need.
  - meetings with live traffic: overlap at a step 3 -> 3 hits (y9 slot 3, s1/s2 slot 1); overlap
    only between steps 1 -> no hit (y9 slot 8: nearest 161 against 175, closing 325 a step); the
    two stationary cars the autopilot drove through (33 and 67 units) were wrecks (+0x128 bit 0).
  - the bench car is slower than the owner's (140-313 units a step against 430-490) and the seeking
    driver wedges itself after its first hit, so the high-speed case is not reproduced in numbers.
- What it means: at the owner's speed a car closes on traffic at ~340 (same way) to ~580 (oncoming)
  units a step, over the 300-350 window: a pass lands between two steps and the game -- the
  unmodified game's own test -- sees no contact. For a pass aimed anywhere across the car the share
  that falls between steps is ~1 - pi R / 2c: ~25-30% same way, ~55-60% oncoming (a model, not measured).
  At one picture a step you saw the car before and after; the frames between steps now show the
  overlap. The frames did not cause it (the phone build's physics: the r49 flags rebuilt give the
  shipped sha 81193beb; the recorders and the seeker are bench-only defines).
- Guesses (rule 4): the hit rule from 4 bench meetings, not dozens; the share at the owner's speed
  from the model; "almost impossible" may hold more than this (the aim: the player's steering is
  drawn up to ~50 sideways off, 1% of frames).
- Not a repeat (rule 2): round 40/41's traffic ("hard to connect") was the traffic drawn a step
  behind the player (fixed by A4_SPECTRAFFIC); here the drawing is measured right and the miss is
  in the game's own step test.
- The owner, on the question (keep the game's collision or check between steps): "remember that it's
  not only crash from upcoming traffic, but also takedowns or hits from traffic going my same
  direction. ... yes, the idea of all this is to modify the game engine so that it runs at higher
  framerate (at least 30 real fps, not capped by the 11.7 cap) without cheap tricks, under the
  premise that the n95 has enough power to run it, and that the game's original code might not be
  of high quality or pretty much unoptimized for devices like this ... Keep this in mind".

## Direction from round 50: the engine itself at 30+ Hz

The goal restated: the game's own simulation runs faster than its 11.72 Hz step and every frame
is drawn from a real simulated state -- no frames predicted from an 85 ms step (A4_KINEDRAW,
A4_ENGDRAW, the corrector and the camera rules are the "cheap tricks" this replaces). All the
collisions -- oncoming and same-way traffic, takedowns (car against car, +1FDEC) -- are the same
discrete overlap at a step, so they come right with the finer step itself.
- The budget, from the record: the logic 1.5 ms a step on the phone (round 30), so three steps a
  frame are ~4.5 ms; the render call 62 ms (r48; 15 ms of it the game's own transforms and sort
  before the first triangle), presents 57 ms (r49). 30 fps is 33 ms a frame: the render must come
  to about half. The engine rate is cheap; the frame cost is the larger job.
- Plan:
  1. The step at 3x (35.16 Hz, 28.44 ms, a period of 7111 of the game's 21333): every per-step
     quantity in the step's ~127 functions (57 KB, bench trace) rescaled in the game's own code --
     velocities and accelerations, gravity (-9 a step: -3), decays, timers and counters (x3), the
     traffic's lane motion, the camera (the chase's rigid rule and the scripts' keyframe times),
     effects. What is not motion (AI choices, race rules) may stay at the 11.72 Hz cadence by
     running every third step -- named per function in ENGINE.md. Round 44's lesson: a census of
     normal driving missed jumps and turbo; this time every function of the step is read and every
     situation is on the oracle.
  2. The oracle: stock (11.72 Hz) against the 3x build on the bench, the same scripted inputs in
     game time: speed curve, top speed, jump distance and airtime, turbo length, lap time, the AI's
     lap times, traffic speeds, hits. Nothing goes to the phone until they match.
  3. The frame cost to ~30 ms on the modelled N95: profiled per function on the calibrated bench.
  4. A frame per step; the prediction code retired.

## Round 50 (bench): the engine in C, checked against the game in lockstep

Owner: "Sounds like a plan. Go ahead."
- The step's call tree (tools/calltree.py on a PC trace of 31 race steps, a4_tr.exe): the race step
  +141B0 runs, in order: sound and HUD set-up; the traffic +FB12; every car's control (+1F1F8 for the
  others, x13; the player's keys, then +1F1F8 and +1DE78); +2217C on the traffic and cars; the
  collisions (+1FDEC car-car x27.5, +1FF56 car-traffic x7.9, +ED9E); the movement (+1FD5C / +1EA64,
  x7.7); race logic (+13968), the camera (+12DEC -> +5968 -> +4B40), positions and messages. The car
  chain is ~11 KB of Thumb (+1F1F8, +1DE78 and its 12 helpers, +1EA64 with its continuation +1ED4C,
  +226E8, +22832, +2292A, the collision functions).
- Method: angr's decompiler fails on this code (a region-builder KeyError), so tools/lift.py
  translates a function to C instruction by instruction -- registers as variables, N Z C V computed
  where set, the frame an array with the incoming stack arguments at its top, literals read from the
  game's code at run time (relocated), calls through a4_eng_call (renderer/a4_eng.s: eight stack words
  passed, r0:r1 returned, and r2, r3 -- the game's code reads them after some calls: the first B run
  read 0xDEAD0A18, the marker for a clobbered r2/r3). Function ends: the image's BL targets, not the
  next push (+1EA64 continues at +1ED4C, which the push scan took for a function). tools/lift2.py is
  the readable form (base + offset folded: car[0x7D8]) for the rate map. Every a4_engv_XXXXX symbol
  puts a veneer at +XXXXX (injector), only with A4_ENGINE.
- The oracle: A4_LOCKSTEP (one half-step an update), the autopilot in game steps, the Veyron swap,
  CREC records each step (tools/crec.py, every live field of the player car and the race object's
  first 0x100 bytes). Two runs of one build parted at step ~136 (e1/e2: A against A) -- the menus
  before the race draw a varying count of random numbers. A4_BENCH_SEED (bench only, a4_gl.c
  car_record): the C library's random() state -- a 31-word table at 0x50016C, the front pointer at
  0x50014C three words ahead of the rear at 0x500150 (EKA2L1_WATCH) -- set the same at the race's
  first step. Then A against A: 0 of 60 live fields differ over 403 steps (e3).
- e1: +22832 (measure) in C, against the second A run: 0 of 60 over 399 steps.
- e3: the whole car chain in C (25 functions: +1F1F8 +1DE78 +1971E +1B560 +1B9E0 +1BB98 +1BD1C
  +1BDEC +1BE70 +1BF26 +1C0B0 +1C276 +1C350 +1CEC2 +1CDDA +1EA64 +226E8 +22832 +1FD5C +1FD0E +2292A
  +1FDEC +1FF56 +239B6 +22C60) against both A runs: 0 of 60 live fields over 403-409 steps. The
  game's car physics now runs from C, word for word the game's.
- Not covered yet: the opponents' records (CREA, not compared by crec.py), jumps, the turbo, crashes
  -- the autopilot's 35 s of race. Next: the race step +141B0, the traffic +FB12 and the camera in C,
  the comparison over a longer race and the opponents, then the rate map in the C.
- e4/e5 (the race step +141B0, the traffic +FB12/+F1B0/+F60A/+2202C and the camera +12DEC/+5968/
  +4B40/+49C4 added to the C): two crashes, both the lifter's. (1) +141B0 calls +14E60 with BL, but
  +14E60 is a local subroutine inside +141B0's own body (no prologue, it reads the caller's frame at
  [sp, #0x12C]); called as a function it read a foreign stack. Now a BL to a target inside the
  function before its next push is a goto with an internal return marker in lr, and bx lr / pop {pc}
  dispatch on it. (2) The injector's hooks were declared extern char[]: with -fropi a data address
  stays absolute (the call went to 0xC308C). Declared as functions. Also: +5968's mode switch is a
  Thumb byte jump table (cmp; adr; ldrb; adds; add pc) -- lifted to a C switch.
- The oracle moved to the stock-step flag set (E0 = C37nohalf's flags + the recorders, A4_LOCKSTEP=1,
  A4_BENCH_SEED): on the half-step set two runs of one build parted at step ~136-626 in the render's
  own once-a-frame counters (+15C, +158: round 46's), whose frame count depends on host time there.
  Both libc generators are now also pinned on every frame before the race (a4_fps.c): rand() is a
  Park-Miller generator (its first write after a seed of 1 is 16807) with its state at 0x500148,
  apart from random()'s table.
- e6 (E0, 150 s, the whole race, 1,898 aligned steps): the build with all 34 functions in C ran the
  race without a fault. The two runs of the game's own code part at step 405 (A vs C); the C build
  agrees with the second of them for 825 steps (B vs C) -- its differences sit inside the bench's own
  run-to-run spread. The opponents start 1-3 units apart between runs (a set-up source not yet pinned;
  not the C library's generators).
- A4_THIRDSTEP (injector): the step a third, 21333 -> 7111 units (28.44 ms): +52B98's divide magic
  0x49BAA9B5 >> 43 checked exact at every multiple of 7111 below 2^31, +52B9C, +15F28.
- The substep frame (A4_ENG3, bench): the race step +141B0 runs three times a game step at
  A4_THIRDSTEP; ENG state at 0x45D000 (renderer/a4_eng.h: count, substep, keys, per-car remainders).
  renderer/eng/edits.py holds the edits the lifter applies to the C, each with its reason. Substep 0
  is the game's whole step; on substeps 1-2 the race rules (+1433C..+1452C), the traffic's pursuit
  check (+14586) and the player's key queries (+14754: the keys of substep 0 reused) are skipped, and
  after the movement the step ends at +14CB2 with only the camera's placement (+5968) -- race logic,
  HUD and positions stay at 11.72 Hz. The keys routine +25E8 (+167AE) runs on substep 0 only
  (a4_eng_keys); the car recorder records on substep 0. Found on the way: the slow-motion scale
  race+0x84C is written in the race rules from the countdowns race+0x850/+0x854, and race+0x30 is
  the race's step counter.
- t3 (substep frame, no rate edits yet): ran the race without a fault; the cars move three whole
  steps' worth per game step, as expected while no rate is scaled.
- The rate rule, one for the whole map, applied at each store to car state: a counter stores on
  substep 0 only; a rate keeps a third of its change, the remainder carried per car; everything else
  stays as the game has it. 199 stores to car state in the car chain (tools/annot.py) to classify.
- The rate map, first batch (renderer/eng/edits.py, ~100 edits; tools/reach.py lists every function
  the substeps reach statically, 182, with its stores and indirect calls): the car's control and
  dynamics (+1DE78 and its helpers, three more lifted: +1BB1E the brake, +1BC5E the handbrake's rpm,
  +1B954 the drift's decay), the height (+1ED4C), the measure (+22832: velocity per step = 3 x the
  substep's displacement, acceleration likewise), the AI (+1F1F8: choices and the stuck check on the
  whole step, its keys and +1DE78's stack argument replayed, per car), the traffic (+FB12: its place
  on the road comes from two clocks, race+0x378/+0x37C, times the traffic speed G+0x83D8; a substep
  adds its share of the clocks' last step), and +2217C's contact counter. Kinds: T (a rate: the store
  keeps a third of its change, floor-divided, the remainder carried per word in a table keyed by the
  address), G (a counter: it changes on the whole step; a substep reads memory's value back into the
  register), decisions skipped on substeps (the boost's start/chain, the drift's start/end), and a
  car effect call (+1B2D6) at most once per car and effect a game step (eng_c_1b2d6).
- tools/oracle.py: stock against 3x in game time from the race's own step counter race+0x30 -- speed
  (+B4), the engine's target (+7D0), gear, rpm, distance, air, boost, drift.
- r1 (60 s): ran without a fault (faults=1 is the end-of-run kill, as every run). The car rolled
  during the countdown: B4 11..90 from step 54 while stock stands still to step 149. Cause: the
  substeps skipped the race rules' part but set only five of the frame slots it writes; the rest
  (+0xC8, +0xCC, +0xD4, +0x118, +0x120) were stale stack. Now every slot it writes is saved at
  substep 0 (+1452C) and restored on the substeps (+1433C).
- r2 (the frame restore): a fault at step 46, in the race's intro (race+0x30 still 0): +4B8E4 (free)
  read its allocator through 0xFFFFFFE7, called from +5205A (a list's node removal) under the camera.
  No guest store wrote that value (EKA2L1_WATCH on the global and on the code literal: nothing at the
  time). The substeps placed the camera with fixed arguments (+5968: the camera, the track, the player,
  K, stack 0, 0, 0), but the intro's flyover is placed by +12DEC with others (the stack words carry the
  mode: race+0x928 x race+0x92C x 15, slow motion race+0x834, another car for a takedown view). Now the
  substeps make again the whole step's last placement, its arguments saved at each of +12DEC's five
  +5968 calls (eng_cam_save), and place nothing when the whole step placed nothing.
- Also gated: the pursuers' own AI (+1D20A target, +1C48E reaction: cars with +C3C), the wreck path's
  counters (+2292A: +034 frame, +048 step).
- r3 (camera replay): no fault in 60 s, but the car still left the grid ~90 steps early (B4 20 at
  step 61; stock 149). A per-substep record (A4_ENGDBG, 'ENGS': path bits set at chosen addresses)
  showed why: in the intro and the countdown the whole step never enters the car loop -- the race
  rules call +14E60, which is not a small local routine but the step's own tail (keys, race logic
  +13968, the camera +12DEC ...), and it returns from +141B0; the car loop runs only once the race
  is on. The substeps jumped from the rules straight to the traffic and the cars, so the player car
  was driven (by the AI, the intro's way) on two thirds of the steps. Now the whole step marks
  reaching the car loop (+14672) and the substeps run only then. Also: the substep camera now makes
  the placement again with the camera's own state put back afterwards (its mode, timers, focal
  length, script) -- only the pose (cam+0x10..+0x2C) is the substep's -- and only for the views
  placed from the car (0-7, 0x2A); the scripted views (fly-in, crash, takedowns) wait for the step.
- Measured on the way: +B28 bit 13 is +1ED4C's "moving backwards" (velocity . the segment's
  direction <= 0), set at rest; the whole step's tail clears it, so a record after a substep sees it.
- r5 (substeps only when the whole step ran the cars): the start is now exact -- both hold the car
  48 steps of countdown (rpm revving from record 101, moving at 149). Then (tools/oracle.py, 41 s):
  rpm climbs 8 a step in first gear and 16 in second in both, the speed's climb 80 -> 140 takes 33
  steps stock and 32 at 3x, the grip comes back +0xD55 a step. The launch is softer at 3x (the GO
  step takes rpm 631 -> 242 stock, -> 119 at 3x: B4 21 against 35, ~10 steps behind until second
  gear). From 15 s the runs part: the autopilot holds only the throttle, the cars meet different
  walls, opponents and traffic. Stock's wall hits cost 26 B4 on average (some 60+, with a downshift:
  one step's slip penalty, rpm -= 7DC x 8, drives rpm below 0), the 3x car's 7 -- but its biggest
  hits were opponents (contact bits 0x49/0x4A, grip reset to 0x1000), not walls. Not yet a like
  for like comparison of hits; to do with a longer race and the opponents' records.
- Logic cost (host, uncalibrated): 1.4 ms a substep against 2.1 ms a stock step, ~2x a game step;
  the render call is the frame's cost either way (31-60 ms a call on this host).
- Also third: +1FD0E's kinematic move (pos += +6B0) and its A34 counter gated.
- r6 (150 s, the full race): at step 132 a contact with an opponent tripled the player's velocity
  each step (B4 154 -> 516 -> 1468), then a wreck sequence flung it across the map (segments
  jumping, B4 in the millions) and it stood ~20 s. Cause: +239B6, the car-car response, works on
  each car's displacement so far (position - +0A8, the previous position) as the step's velocity,
  and gives the new velocity by setting +0A8 = position - the new displacement. At 3x it read a
  substep's displacement (a third: its thresholds, its fixed 20-unit nudges and the momentum split
  off scale) and wrote a step's, which the substep measure then tripled. Now conjugated by the step:
  the two displacements x 3 on reading (+23A54, +23A5E), the written +0A8 back to a third of its
  displacement (+23D16, +23D56). The road-edge constraint (+23E72) only projects the position, so
  the measure sees it right.
- r7 (150 s, the response conjugated): the player's race ran clean (the jump: 16 steps of air
  stock, 15 at 3x); lap 1 in 710 steps against stock's 830, mean B4 251 against 208. But an
  opponent (CREA) blew up at its first wreck (step 326 of its record: B4 640, then positions
  jumping 20,000-50,000 units a step): the wreck path +2292A integrates the car's own velocity
  (+018 +- the path's pull), which the substep measure scales. Now the whole wreck sequence
  (+1DE78's branch for +034 != -1) runs on the whole step only, the wreck's movement (+1EA64)
  is skipped on the substeps and measured at the game's scale on the whole step (ENG->meas1); a
  wreck moves at 11.72 Hz (a later job: frames for it). Also: the car thrown over a traffic car
  (+1FF56, +20154) takes the traffic's substep, not its step.
- r8 (150 s, wrecks on the whole step): no blow-up in the player's or the opponent's record; the
  opponent's wreck now tumbles to rest in ~30 steps (stock-like) and respawns. Player: lap 1 in 748
  steps (stock 830), top speed 357 (stock 333), the jump 15 steps of air (stock 16), 22 contacts
  with cars (stock 4: at 3x fewer pass through). Opponent: its first 24 steps match stock (rpm
  +16 a step, B4 3, 6, 10 ... against 3, 10, 13 one step early), but after its wreck it drives
  85 s at B4 ~112 cycling gears 1 -> 2 -> 1 every 4 steps (rpm 452 in first, shift, 7C4 3 2 1, back
  to first) with no throttle, where stock's opponent is at 160-260 in gears 2-5. The 3x player is
  ~10% quicker over the lap; to find: whether its wall hits cost less than stock's (stock 26 B4 a
  hit on average, 3x 11), and why the AI stops throttling. A debug run with the opponent's
  per-substep record ('ENGA': its cached AI keys) is running.
- r8d (the opponent's per-substep record): after its respawn the opponent's AI keys are steering
  only (0x1/0x2) three steps in four and throttle+steer (0x5/0x6) the fourth -- a speed governor:
  its target (1F1F8, +1FA0E: (B44 + max(the player's B4, a random draw) + B48) x the curve's factor
  >> 12, B48 counting up a step) sits near its speed, so it pulses the throttle; each pulse shifts
  it to second (rpm 457 > first's top), the shift delay 7C4 (3 steps) passes without throttle,
  coasting takes rpm below 0 and it drops back to first. Not yet known whether stock's AI does the
  same after a respawn (stock's recorded opponent never wrecked). Open.
- State at the end of this round: the 3x engine runs whole races on the bench (150 s) with no
  fault and no blow-up; per-step rates match stock (rpm, grip, in-gear acceleration, the jump's
  airtime, the countdown exactly). Open before any phone build: the player's lap ~10% quicker
  (wall hits cheaper), the opponent after a respawn, a wreck moving at 11.72 Hz, the scripted
  camera views at 11.72 Hz, the frame cost on the calibrated N95 model (plan step 3).
- renderer/eng/funcs.txt: the 38 functions the engine lifts (tools/lift.py's argument list).
- Wall hits, read closer (r8d's per-substep records against stock's): stock's big losses come at
  hits where the car meets the wall at a wide angle -- the first step turns the heading 49 (the
  wall's push, half the angle, clamped 20..64), the next 24, 20, 20, 20, and the velocity left off
  the heading gives a slip of -13/-14 two steps running, each a rpm -= 7DC x 8 (-384) penalty, the
  second taking rpm below 0: a downshift (230 -> 164). The 3x car's hits in r8d all came at narrow
  angles (first substep -7, i.e. ~21 a step; slip never below -3), scraping along the wall. A like
  for like test needs both engines at one state before a hit: tools/mkinject.py + A4_BENCH_INJECT
  (bench, car_record) write stock's player car at chosen steps into a build (the vtable, the owner
  block and the 86 words that hold addresses stay the live car's). The stock reference rebuilt with
  the same flags is byte-identical to e6A's (a4_eqA.exe).
- Next run (inj): stock and 3x, each with stock's car injected 4 steps before six of stock's hits
  (record indices 317, 353, 438, 543, 582, 613; hits at 321, 357, 442, 547, 586, 617).
- inj (stock + inject, crec_injA): reproduces e6A exactly through all six injections (the harness
  is sound: injecting a run's own state changes nothing).
- inj3 (3x + stock's car injected, crec_inj3, tools/injcmp.py): both engines from one state. At
  317 the paths agree within 1-2 units to the hit at 321; there stock turns the heading +49 (3x
  +30), 7FC 1024 (3x 341), slip -14, -13 (3x -14, -8, -3): stock pays two -384 rpm penalties,
  rpm 688 -> 304 and a downshift, B4 221 -> 192 -> 164; 3x 554 -> 416 -> 400, no downshift, B4
  216 -> 208 -> 210. The later contact steps match (+24/+28, then +20 a step: the clamp's floor).
  353 the same pattern (stock B4 197 -> 129 in 3 steps, 3x 197 -> 175). At 438 the 3x car steers
  (7FC -536 .. -4096) from the step after the injection where stock's does not (the autopilot
  sends '5' only, so this is the game's; state outside the car -- the race, the traffic -- is
  not injected: a limit of the harness) and misses stock's upshift at 440 by 2 rpm.
  Read: the first contact step's turn is the difference. The push is half the angle met; three
  thirds, each on the angle the last left, add to less than the one whole push. 7FC's push
  (+-0x400) is thirded too and the steering's return snaps it to 0 every substep, so it holds
  341 where stock holds 1024 (a guess that this matters less: the later steps' turns agree).
- r9k (the kick): the substep where +B18 leaves 0 marks the car (eng_kick_mark at 1F03A/1F06C);
  its heading push (1F132/1F13A) is the whole push, the step's later substeps add none, later
  steps thirded as before (eng_kick, a4_eng.h). Run: inj3k, the same six injections.
- inj3k (r9k, the kick): the first contact step's turn now matches (heading 163 both at 321; 396
  both at 357), but the speed loss does not: at 321 stock B4 221 -> 192 -> 164 with a downshift,
  3x 216 -> 219 -> 219 (rpm 555 -> 539, one penalty's worth); 357 stock 190 -> 129, 3x 182 -> 175;
  547 stock 256 -> 228 (downshift), 3x 256 -> 256. (At 438 and 582 the 3x run had drifted from
  stock before the hit -- the 3x car stays in gear 3 at 586 where stock stays in 2 -- not a like
  for like there.)
- inj3kd (r9k with the per-substep record): why. Stock's control sets the target velocity
  (+1DE78 at +1E56E: +680 = +704 x +7D0 / 4096) from +704, the heading's direction, which the
  control updates only after the target, from the heading +678 the last movement left: the target
  follows +678 two steps late. At 321 the wall turns +678 114 -> 163 (8.6 degrees); at 322 stock's
  +704 is dir(163) but +680 still dir(114) (-159.9 degrees, 17.5 into the wall, which runs at
  -142.4): the wall clips the move to 201 x cos 17.5 = 191 (B4 192), below the engine's 202, so
  the slip check (+1BB98: dot(+010, +704) >> 12 - +7D0 < -10) fires twice (-14 at 321, -13 at 322),
  rpm 688 -> 304 -> below 0, a downshift. At 3x the same chain is late by substeps: the target
  points into the wall for one substep, |v| 214 -> 211 against 7D0 213, slip -18, -12, then -4:
  two thirds of one penalty. The game's handling has a lag of 1.5 steps (85-170 ms) from heading
  to motion; at 3x it shrank to 28-57 ms, and a wall's cost with it.
- r9h: the target reads +704 as it was three substeps (one game step) ago, per car
  (eng_hlag, a4_eng.h; edit at +1E57A) -- the game's own lag in game time, the rest of the chain
  as before. A guess to check: that this is the only lag that matters (the slip check and the
  movement read +704 and +678 current, as stock's do within its step). Run: inj3h.
- inj3h (r9h, the target's +704 three substeps late): partly. At 317 rpm 688 -> 544 -> 406
  (one stock penalty's worth, r9k's 555 -> 539), B4 215 -> 210 -> 208, no downshift (stock 221 ->
  192 -> 164). 353: B4 199 -> 175 (stock 197 -> 129). Two things wrong in it: (1) the slip check
  reads the same +704 as the target and was left at one substep's lag, so the first penalty --
  stock's slip -14 is the move along the wall against the old heading -- lasts one substep; (2)
  the lag was one substep too long: the order (watched: EKA2L1_WATCH, value-filtered, on the
  player's +704 -- +522F0, matrix x vector, called from +1971E) is: control = slip check (+1BB98)
  and target (+1E56E) with +704 as left, relax, move (+1E8C0), then +1971E makes the matrix +6D4,
  +704 and +710 from +678/+674/+67C; the movement (+1EA64) turns +678 after it. A heading change
  in a step's movement moves the car from the step after next: a lag of one step, at 3x of one
  substep. To match: two more substeps, on every reader.
- r9a: +1971E reads its three angles two substeps late (eng_alag, edits at +1972E/+19736/
  +1973E; the target-only lag removed). The matrix it makes is the drawn car's and the collision's
  too: both now see the heading with the game's lag, as stock's do. Injection resets the rings
  (ENG->flush). Run: inj3a.
- inj3a (r9a): 438 is now like for like to the hit (the 3x car no longer steers on its own after
  the injection -- that came from the heading lag's mismatch -- and upshifts at 440 with stock).
  At the hit stock rpm 0 -> 689 (down to 3) -> 305 -> down to 2, B4 211 -> 205 -> 193 -> 164;
  3x down to 3 a step early, 689 -> 305 -> 166 -> 150, B4 206 -> 201 -> 190 -> 190: about 1.4 of
  stock's 2 penalties (r9k: 0.3). 543 the same (3x rpm 560 -> 277: ~1.3 penalties; stock 2 and a
  downshift). 317 and 353 are not like for like: the 3x run had a takedown just before (the player
  car's keys get bit 0x100 and +B20/+B24 count: the race drives the player car with the AI,
  +1F1F8, for ~20 steps), a race state outside the car the injection does not carry; stock's run
  never has one on this path (r8: three, at 367, 638, 727 -- after car contacts stock's 85 ms step
  passes through). The rest of the wall's cost: stock's second penalty is the slip of the step
  after the into-the-wall step, held for a whole step; at 3x the target catches up a substep later
  and the slip is under the threshold after one substep. A sampling effect of the 85 ms step; not
  chased further until a whole race says whether it matters.
- r9 (= r9a without the injection): a whole race against e6A, 300 s. Run.
- r9 (whole race, 300 s, against e6A): worse. Lap 1 in 633 steps (stock 830, r8 748), top speed
  504 (stock 333; 9 steps over 360: spikes), gear 5 at 364 (stock 1008), distance at 50 s +43%.
  Wall contacts in 1300 steps: stock 47 (198 steps in contact), r8 65 (184), r9 64 (213); the
  player driven by the AI after takedowns: stock 0 steps, r8 71, r9 41. Read: the bench's car
  only holds '5' and the walls steer it round the track, so the lap is set by the wall's cost;
  stock pays it on most hits and never leaves gear 4 for 700 steps. Also found (there since r8,
  not new): the launch. Countdown identical to record 148 (rpm 631 -> 210 at the go); at 149
  stock's rpm 242, B4 35, 3x's 119, B4 21 (B4 >= 40 at step 1 against 13). The slip check runs
  every substep and at the launch reads the velocity a third of the way to its target (slip -20):
  a penalty stock never has. The same cause as the wall's second penalty: the check is a decision
  the game takes on its step's state, and substeps show it states between steps.
- r10: the slip check (+1BB98) on the whole step only, with the whole penalty, on the move since
  the last whole step (eng_slip_step: the car's position at each whole step, per car; edits at
  +1BB9A, +1BBD8, +1BBE0; +1BC06 no longer thirded). Run: r10, a whole race.
- r10 (whole race): the launch step now matches (rpm 242 at 149) but the next whole step's check
  fires (step move 22 against 7D0 46: slip -24, rpm 242 -> 11) and the car crawls; it wrecks at
  245 and sits at rest to the end (not looked into: the next build changes the cause). Two
  findings from the launch's substeps (inj3kd): (1) +7D0 = f(rpm, gear) (+1C0B0) made before the
  step's rpm rises: stock's lags its rpm by a step, 3x's by a substep -- at the launch 46 against
  stock's 35; the step's move (an average over a speed rising from 0) is not stock's move (the
  speed at the step's end). (2) The real fault: the velocity's change (+1E5CA..+1E684: d = target
  - velocity, |d| clamped to T/16 a step, T from the grip) was thirded linearly since r1: a
  substep moves a third of the way, so a step covers 1 - (2/3)^3 = 70% of what stock covers in one
  step (the launch: v 13 of a target 37 after the first substep; stock reaches its target in one
  step whenever it is within the traction). The car lagged its target everywhere it changes fast:
  the launch, a wall, a car contact.
- r11: the velocity's change reaches the target, clamped to T/48 a substep (edit at +1E684;
  eng_udiv, the build has no divide); the slip check once a step on +010 with the whole penalty
  (the step-move version dropped). Run: r11, a whole race.
- r11 (whole race): the launch matches stock -- B4, rpm and the shifts identical to step 54 (rpm
  32, 128, 224, 320 at the same steps), distance +0.4% at 10 s, -1.4% at 20 s. Lap 1 in 719 steps
  (stock 830), top speed 405 (stock 333), gear 4 at 175 (stock 291). The runs are within 10 units
  to the first wall scrape (204-210) and turn identically through it (heading 659 -> 626 -> 606 ->
  586 -> 566 -> 546, both); at 210 stock's +B18 reaches 0 at the step's end and the contact is
  over the next step; 3x counts +B18 on substep 0 (the gate), so its substeps 1-2 see 0 with the
  car still against the wall and re-arm it: a second push, -7 -7, heading 532 against 546 -- and
  from there the paths part (other walls, a car contact at 270 stock does not have). After that
  no step-for-step comparison holds; the lap and speed figures are the chaos of two paths.
- r12: +B18's countdown on the step's last substep (on 0 for a wreck). Run: r12, a whole race.
- r12 (whole race): the scrape at 206-210 now ends as stock's (heading 546 and +B18 0 at 210,
  both). At 211 stock's car is off the wall by the step's end (D4 0800, no push); the 3x car is
  still against it at substep 0 (a third of the way along), +B18 is 0, it re-arms, and the push's
  floor (clamp 20..64) turns it 20: 526 against 546, and the paths part from there. tools/part.py
  (new): prints two runs side by side up to where they part.
- r13: the contact's start (+B18 from 0, 1F038/1F06A) only on the step's last substep (0 for a
  wreck): the game decides it on its step's end position; the contact's physics -- the car held
  out of the wall, the speed it loses -- every substep as before. Run: r13, a whole race.
- r13 (whole race): the scrape at 206-211 now matches stock step for step to 228 (heading 546
  both); the paths still drift (40 units at 224) from an rpm offset of 21 taken at the contact's
  first step (the 3x car finds the contact a substep or two into the step and the contact's rpm
  rule applies from there; stock applies it from the next step: within one step of resolution).
  Lap 1 in 621 steps (stock 830). Not a measure of the engine: the bench car only holds '5', the
  walls steer it, and two paths a few tens of units apart meet different walls and different
  traffic. tools/hits.py (new) prices each wall contact instead (turn over its first two steps,
  B4 lost after six, downshift), by turn:
      run     hits | turn 40-80: n cost down | turn 80-200: n cost down
      e6A      82  |          67    8   22%  |            12   54   92%
      r8       78  |          15   18   33%  |             2   62  100%   (61 at a turn < 40: cost -1)
      r11      69  |          51    6   22%  |             3   28   67%
      r13      74  |          65    9   23%  |             3   46   67%
  The common hit now costs what stock's does; hits match one for one to 292 (206/206, 236/237,
  248, 257/255, 260/261, 270, 290/292). Different still: wide hits (turn >= 80, ~50 B4 and nearly
  always a downshift) -- stock 12, every 3x run 2-6. Not known whether that is the engine or the
  paths. And the 3x car meets traffic 10-17 times a race to stock's 4 (stock's step passes through
  it -- the point of the job), each contact throwing its speed over the engine's (B4 327 at 7D0
  234 at 334): the car-car response's size is not yet checked against stock's (stock has too few
  contacts on this path to compare).
- Frame cost (plan step 3): s13 (stock) and t13 (the 3x engine, real time) on the calibrated N95
  model (TIMESCALE 4, CPUCAP 330, CPUMODEL, GPUMODEL), 120 s each. Run.
- The opponent (CREA=1, r13): it parts from stock's at ~186 (the AI's steering pulses, 7FC ~400
  against stock's ~360), and from 191 cycles gears 1 -> 2 -> 1 every 4 steps -- the "governor"
  logged at r8d, here before any respawn. Read in the records (raw 185-199): both upshift at 188
  (+7C4, the shift delay, = +7C8 = 3) and the AI lets off the throttle for three steps (keys 1, 1,
  1: steering only). +1BB98 skips all engine drag while +7C4 != 0 and counts it down. Stock's
  delay holds through 191 (the check at 191's start reads 1, so no drag that step) and the throttle
  is back at 192: rpm 0, 16, 32. At 3x the count is gated to substep 0: at 191 substep 0 takes the
  last count and substeps 1-2 coast, rpm goes below 0 and the car drops to first (rpm 623); with
  the throttle back it shifts up again, delay, coast, down: every 4 steps, the rest of the race.
  The same ordering fault as +B18's: a countdown that the substeps read is counted at the game's
  step's start instead of its end.
- r14: a gate kind for that (lift.py 'gate': 'end', edits.py GE(): stored on the car's last
  substep of the step -- 2, or 0 for a wreck -- ENG_SUB_END in a4_eng.h): +7C4 (1BC5A), +AE0 the
  boost's countdown (1BEE0), +AEC (1DF9E, rpm -8 a step while it runs), +B18 (1F144/1F14E, as r12)
  and the contact's start (1F038/1F06A, as r13). Other countdowns left on substep 0 until a record
  says otherwise (A7C, A68, A6C, AF0).
- Frame cost (plan step 3), s13 (stock, real time) and t13 (the r13 engine at 3x, real time) on
  the calibrated N95 model (TIMESCALE 4, CPUCAP 330, CPUMODEL 300:60:2:4:5:160, GPUMODEL ...;
  the host kept up: behind by 0.0%), 120 s holding '5', tools/steptime.py:
      build  ms a render  3D     comp   2D    present  logic                  render call +12992
      s13    84-91        24-38  13-22  8-11  3        1.1 ms a step, 1/frame  54.7 ms
      t13    55-84        17-34  12-17  8-11  3        1.0 ms a substep, 2.2   49.0 ms
                                                       a frame (2.7 substeps)
  The engine at 3x costs ~1 ms a substep of modelled N95 time: nothing. The frame is the render's:
  ~50 ms a render call (3D + composite + the 2D HUD + present), so the 3x build runs 12-18 frames a
  second, a frame every 2-3 substeps. 30 fps needs the render call near 30 ms: the renderer is the
  work of plan step 3, not the engine. (s13's first attempt faulted: built without
  A4_SCRATCH_SIZE=0x5F000, the telemetry's blocks past its scratch.)
- r14 (whole race): the opponent's gear cycling is gone (lap 1 in 700 steps; r13 1078, stock 827).
  But the player's launch is off again (distance -6.5% at 10 s; r13 -0.6%): +7C4 sits at 10 from
  the countdown and counts down only after the start (stock and r13: 0). In the countdown the game
  runs the car's control outside the race step's car loop (ENG->loop clear), the substeps return
  at once, and a store on substep 2 never comes. ENG_SUB_END now takes substep 0 as the step's end
  when the loop flag is clear, as for a wreck.
- r15: that. Run: r15, a whole race.
- r15 (whole race, the best so far): the player's distance against stock -0.1% at 10 s, -1.0% at
  20 s, -1.0% at 30 s, +4.3% at 40, +20.8% at 50; the runs part at ~250 (r13: ~210). Lap 1 in 732
  (stock 830). Wall hits (tools/hits.py): 40-80 turn 51 hits, cost 10, 24% downshift (stock 67, 8,
  22%); wide hits 6, cost 55, 100% (stock 12, 54, 92%) -- each kind of hit now costs what stock's
  does; there are fewer wide ones (paths, or not: open). Opponent: lap 1 in 770 (stock 827, r13
  1078), -7.9% at 10 s (its launch: not looked at), one wreck at 382 (stock none), 15 car
  contacts (stock 8).
- State of plan step 2 (the oracle): per-step rates, the launch, a scrape and the cost of each kind
  of wall hit match stock; whole-race figures part after ~20 s because the bench car is steered by
  the walls. Open: the opponent's launch and wreck, the car-car response's size, wreck and scripted
  camera motion at 11.72 Hz. Plan step 3 is the renderer (~50 ms a render call on the N95 model).
- Plan step 3, the renderer: where the time goes.
  - Exact instruction profile (EKA2L1_PCPROF, the r15 engine real time = t15, CPUCAP 85, 2 G
    instructions of race; tools/profreport.py, the blob named from A4_SYMFILE): our blob 43.3%
    (a4_gl_flush -- the composite -- 18.4, the mesh list's lean_tail 5.9, a4_mesh_insert 5.5,
    mesh_draw 3.2, batch_draw 1.8, mesh_scr 1.7, mesh_fallback 1.5, draw_prim_take 1.3, the
    engine's a4_eng_call 1.7), the game 33.2% (the sprite painter's page +29000 15.3 -- the HUD,
    +34000 4.9, +52000 blends 3.3, +31000 2.2, +30000 2.1), ngibackbuffer +CCB8 14.1% (the
    runtime's 565 -> 32-bit conversion of every present).
  - Instructions misrank it: in t13's window (capidx 80, 80 ms a render) the composite's 11.8 ms
    is 9.6 ms of read (the model's tile render 3.2-3.6 and readback 3.7 ms, GPUMODEL stats) and
    ~2.2 ms of blit loop, for its 18% of instructions. GPU charges a busy frame: draws 6.3-9.5
    ms (270-420 draws), other calls and binds 4.7-6.9, tile render 3.2-3.6, readback 3.7, post
    2.3: ~20-26 ms of a 50-80 ms render.
  - A cycle-weighted profile is the instrument (emulator: EKA2L1_PCPROF_CYCLES=1, the model's
    cycles credited per block, EMU_PATCHES.md). Run: pct15, t15 on the calibrated model.
- Cycle-weighted profile (pct15: t15 on the calibrated model, 1.5 G instructions of race after 8 G,
  EKA2L1_PCPROF_CYCLES; GPU charges not in it): the game 49.0% -- the HUD sprite painter +29736
  19.3% (one function, 109 blocks), +34000 8.8% (+345E4 3.0, the mesh routine +349F8 2.8),
  +52000 5.5% (+522A4 4.0: 565 blends), +3115C 2.8, +3015E 2.7 (the billboard blit) --; our blob
  30.6% (the composite 11.5, lean_tail 4.4, a4_mesh_insert 3.9, batch_draw 2.3, mesh_draw 2.2,
  mesh_scr 1.2, draw_prim_take 1.1, the engine 1.3); ngibackbuffer +CCB8 11.2%; irobex.dll 2.9%
  (a thread polling: not looked at). The painter is a generic per-pixel blitter (per pixel: the
  sprite's format flags re-read, 4/8/16-bit source, the palette entry's RGB888 -> 565 each time,
  a 4-bit alpha plane, five effect modes) in Thumb at the model's CPI 3.0.
- The capture header's MESH words (t13 capidx 80): the list made 209 of a render's 299 draw
  calls (1,244 triangles, ~6 a call), 10.9 ms of a render drawing it on the model; the table ~90.
- A4_MESHZ (a4_gl.c mesh_draw): the list's run grouping also passes a run whose box meets the
  face's when the order cannot change colour or depth -- both depth-tested (texz) with ZW ranges
  apart, a tested face wholly farther than a painted run, a painted face over a tested run wholly
  farther; two painted ones keep their order. Apart = by 1/256 of ZW, many 24-bit steps (on only
  with zbits >= 24; the phone's depth bits are a capture word, depth_bits). struct mrun's key cut
  to 16 bits for the range (same 20 bytes). Not yet checked against the picture.
- The painter lifted (tools/lift.py, +29736 in renderer/eng/funcs.txt): exact C compiled to ARM.
  Its entry carried the HUD capture detour (+2973A, a4_hud_tramp -> a4_gl_hud, the end of the 3D
  pass); edits.py ALWAYS (applied in every build that lifts) puts that call in the lifted code and
  the injector leaves a capture hook out when its function is lifted.
- Exactness, A4_PAINTCHECK (a4_eng.c; a4_paint_orig in a4_eng.s replays the game's prologue and
  goes on at +29740): every painter call run twice on the same input -- the game's then the
  lifted -- and the sprite's rectangle and clip words compared (census words 115-121). PC1 (stock
  + the lifted painter, lockstep, 100 s, menus and race): 29,842 calls compared, 0 differing
  pixels, 0 clip words; 356 calls too large for the buffers (full-screen menu sprites, all in the
  menus) not compared. The check tested: PCM (a mutant flipping one pixel a call) -- 19,811 of
  19,811 calls flagged. (A whole-run comparison does not do: S0 vs P1 lockstep parted at the
  opponent's grid slot from the countdown -- the menus run on the wall clock and the run's
  timing changes what the seed meets -- so only an in-run comparison is a test.)
- t16 (the r15 engine + the lifted painter + A4_MESHZ, real time) on the calibrated model. Run.
- t16 (r15 engine + lifted painter + A4_MESHZ, real time, calibrated model, 120 s): renders and
  draws right (two captures looked at: city, cars, the helicopter, the whole HUD; no depth fault).
  Whole-race aggregates against t13 (seg0, 1,817 renders each; the two races differ in content --
  t16 drew 1,784 triangles a render to t13's 1,427, so totals do not compare):
      t13  64.2 ms a render | 3D 23.5 comp 11.5 (read 8.8) 2D 8.3 pres 2.5 | calls 194, tris 1427
      t16  65.8 ms a render | 3D 25.4 comp 11.7 (read 8.7) 2D 5.4 pres 0.3 | calls 199, tris 1784
  What compares: the HUD's 2D phase 8.3 -> 5.4 ms and the present phase 2.5 -> 0.3 (painter calls
  inside it, a guess): ~5 ms a render from the painter in ARM C; the list 5.8 -> 13.8 triangles a
  draw call (A4_MESHZ); the GPU model's busy frames 155-167 draws (t13 273-417): draw + other
  charges ~7-7.5 ms a frame (t13 11-16.5).
- Where a render goes now (t16, ~66 ms, ~2.6 substeps): 3D 25 (the game's 3D CPU, our hook and
  list code, ~7 ms of GL charges), composite 12 (tile render 3.2 + readback 3.7 fixed, the blit),
  2D 5, rest 23 (logic ~3, before the first triangle ~10, the runtime's 565 -> 32 convert, the
  post 2.3). 30 fps (33 ms) needs about half of every part: the remaining Thumb game code lifted
  (+345E4, +349F8 with the mesh insert, +3115C, +522A4, +3015E with the billboard hook, ~20% of the
  cycles), our hook and list code tightened, the painter's per-pixel invariants hoisted, and a
  present without the 565 round trip (the runtime's convert, 11% of the cycles, and the readback).
- The owner's direction after t16: the code-level cuts, the game's Thumb taken to ARM C as far as it
  pays. Both sets are native to the ARM1136; ARM's gain is fewer, cheaper instructions (16 registers
  instead of 8, conditional execution, the barrel shifter, a modern compiler tuned for the core). A
  guess named: the model's Thumb CPI 3.0 against ARM's 1.6 is a fit of the total to the phone's
  telemetry (round 49: "the split is open"), so a lifted function's gain on the model may overstate the
  phone's; the instruction counts are the part that holds on any split.
- tools/lift.py, second form (the same instruction-for-instruction C, compiled better):
  - a call between two lifted functions is a direct C call (clang inlines the small ones) instead of
    a4_eng_call into the game's entry and back through its veneer; ENGD_XXXXX keeps a build that lifts
    a function without its veneer calling the game's entry as before;
  - the callee's r2, r3 come back through a pointer (engi_XXXXX's o23), exact for a caller that reads
    them after the call;
  - a function marked XXXXX:i in funcs.txt is lifted for direct calls only, no veneer: the game's own
    callers keep the Thumb (a 17-instruction leaf behind a veneer would cost more than it saves);
  - memory through plain may-alias accessors (L32 ... in a4_eng.h) instead of volatile: each access
    keeps its width and its order against every store, but clang may schedule, combine and drop the
    redundant ones; the function's own frame through a pointer-typed stack (spp, S32 ...) so that a
    frame whose address never leaves the function lives in registers;
  - a compare and the branch right after it as one C comparison (the flag words still computed for
    any later reader, dead otherwise);
  - an indirect call with a known lifted target compared first (+349BC: the face's depth key +3121E);
  - the injector's mesh hook (+34ADC, A4_MESH) a plain C call in the lifted mesh routine.
  injector.py adds -fno-delete-null-pointer-checks (the lifted code dereferences what the game's does).
- The 3D front end lifted (funcs.txt): the mesh routine +349F8 and the ordering-table insert +349B0
  with veneers; inline only the vertex transform and outcodes +345E4, its three dot products
  +523A2/C4/E6, the back-face test +3118E and the face's depth key +3121E. pct15 put them at ~13.5% of
  the race's cycles.
- Exactness, A4_LIFTCHECK (a4_eng.c, a4_orig_349F8/349B0 in a4_eng.s; census words 100-111; needs
  A4_SCRATCH_SIZE=0x7F000, buffers 0x460000-0x480000): the mesh routine run twice per call on the same
  input -- the camera's first word, the vertices, the faces saved; the game's run (its inserts listed at
  a4_mesh_insert, not made), its result kept, all put back, the lifted run (listed too), memory and the
  two insert lists compared, then the inserts made. The insert: run once, the slot found by the face it
  then holds, put back, the lifted run, compared (one call in 16: the slot search reads the table).
  LC1 (all at every call) was too slow to reach the race (76 records, all countdown). LC2 (stock +
  every lifted function, lockstep, 100 s; also A4_PAINTCHECK on the new lifter's painter): mesh routine
  35,690 calls compared, 0 memory differences, 0 insert-list differences (r0 differed on 29,177: no
  caller reads it -- the lifted one keeps r0 across the mesh hook, the game's has the hook's); insert
  28,750 compared (431,260 left to the lifted alone), 0 differences; painter 19,003 compared, 352 too
  large, 0 differences. Menus and race (615 race records).
- The painter by hand, A4_PAINTFAST (a4_eng.c a4_paint_fast): the lifted painter's behaviour exactly,
  with what the game re-reads for every pixel read once a call (format flags, the palette bank's entry,
  its colour key and colour table, the alpha levels' global scale, the mode, the blend's masks) and a
  paletted sprite's colours in a table filled as the walk meets each index (never past what the game
  reads). Mode 2 (three divisions a channel) goes to the lifted painter whole. Review before the run
  caught two faults of the first draft: a table built over all 256 entries (reads past a short colour
  table, a fault the game never risks) and signed walk counters (overflow is undefined in C where the
  game's walk wraps). Run: LC3 (A4_PAINTCHECK on it) and LCM (both checks' mutants: one vertex bit, one
  insert flag bit, one pixel a call -- each must be flagged on every call).
- LC3 (stock + every lifted function + the hand painter, lockstep 100 s): mesh routine 44,078 calls,
  insert 29,728 (1 in 16), painter 19,138 compared: 0 differences each. LCM (both checks' mutants): every
  compared call flagged -- 33,772 of 33,772 mesh calls (one byte each), 30,460 of 30,460 inserts, 18,099
  of 18,099 painter calls (one pixel each): the checks can fail.
- A fault caught by review: the hand painter's colour table (1 KB, a 1.2 KB frame) on a thread whose
  stack is 20 KB (the exe's header: 0x5000). Now 16-bit entries and a bit an index (frame 736 bytes).
  The lifter's frames are as before (+24 bytes typical, -fstack-usage).
- tools/liftfuzz.py: each lifted function against the game's own offline, on unicorn's ARM1136 -- both
  images at their link address (code 0x8000, data 0x400000), the same random memory (a 256 KB arena of
  pointers into itself, small numbers, 16-bit values, noise; registers and stack words likewise), the
  game's function from the untouched image, the lifted one (eng_XXXXX) from the injected one. Compared:
  r0, r1 and every byte of the arena, the data and bss (the scratch with it) and the stack words. A case
  where the game faults is not a case; one where only the lifted one faults is a failure. Also counts
  the instructions each ran (Thumb by halfword pairs): the gain that holds on any CPI split.
- What it found, first pass: every valid case of every function agreed -- but the hand painter faulted
  alone on 214 of 555 cases: it read the object's effect mode (and the sprite's source pointers) before
  the clip, where the game, clipped away, reads nothing. A fully clipped call with a bad object pointer
  is harmless in the game and was a fault in mine. Fixed: it reads exactly what the game's loop reads,
  when it reads it (1,078 then 1,439 cases agree, 0 differ).
- And the instruction counts: on short calls many lifted engine functions ran 2-6x the game's
  instructions (+22C60 0.17x, +1BDEC 0.24x). The calls: +22C60 calls three getters of three instructions
  five times, each through a4_eng_call (~35 instructions: eight stack words copied each way). So:
- tools/lift.py: small leaves inline. A game function a lifted one calls, with no calls of its own, no
  jump through a register but its return, at most 64 instructions, no edit and no injector patch inside,
  is lifted too, inline only (A4_NOLEAF turns it off): 40 leaves (getters, small math). Fuzz again:
  0 differences over all 85 functions (38 engine, 7 3D front end, the painter, 40 leaves), and the
  short-call ratios up (+1BB98 0.40 -> 1.03x, +1C276 0.27 -> 0.96x, +1BDEC 0.24 -> 0.76x); functions that
  call bigger game functions still pay a4_eng_call (+1F1F8 0.37x, +22C60 0.17x): a call without the
  eight-word copy, for callees that read no stack argument, is next.
- On the race's own data (LC5, the in-run checks run both versions on the same inputs: EKA2L1_PCPROF,
  instructions, 5.5 G after 3 G, tools/profsym.py): the 3D front end 255.0 M (game) against 222.5 M
  (lifted): 1.15x fewer instructions; the hand painter 343.7 M against 253.0 M: only 1.36x. So the
  measured gain of a mechanical lift is ~1.2x in instructions; the rest of what the model shows is its
  Thumb/ARM CPI split -- the guess named above. Real gains need fewer instructions, i.e. rewritten loops.
- The hand painter's loop was not specialised (clang kept every format test and a dozen stack reloads
  in the pixel loop: its hot blocks were the loop tail and the flag tests). Rewritten: one walk per
  format (paletted 8 or 4-bit, 16-bit; alpha plane or not; an effect or none), each a constant where it
  is instantiated, the invariants in registers. Fuzz: 1,439 cases agree, 0 differ. Run: LC6 (the
  checks and the instruction profile).
- LC6 (the specialised painter under the checks, with the instruction profile): 21,326 painter calls,
  35,707 mesh calls, 32,964 inserts compared, 0 differences. On the race's own data the hand painter now
  runs 2.37x fewer instructions than the game's (353.7 M -> 149.3 M; LC5's unspecialised draft 1.36x);
  the 3D front end 1.15x (270.8 M -> 235.0 M).
- tools/lift.py: a call without the eight-word copy (a4_eng_call0, a4_eng.s) for a game callee that
  reads none of its caller's stack words -- its flow walked with the stack's depth; a load or store at or
  past the entry depth, a tail call, the stack's address taken, an indirect or ARM call, or a call to a
  function that reads its caller's words (a thin frame could reach this one's caller's: conservative)
  each say it reads. 232 of 475 call sites. Fuzz again: all 87 functions, every valid case agrees.
- t17 (the r15 engine at 3x + everything above: the new lifter, the 3D front end, the leaves, call0, the
  specialised hand painter; A4_MESHZ; real time on the calibrated model, 120 s). Whole race against t16:
      t16  render call 52.9 ms, before the first triangle 10.3, logic 2.5 ms a span, 1.1 ms a step
      t17  render call 43.9 ms, before the first triangle  7.4, logic 1.5 ms a span, 0.8 ms a step
  The last eight capture windows: t17 44-54 ms a frame (19-23 fps; 3D 17-22, composite 12.5-15.4, 2D
  2.1-2.6, present 2.3-3.0, rest 9.6-11.7); t16 63-94 ms (11-16 fps; 3D 22-38, 2D 5.3-6.3, rest 20-30).
  Different stretches of race (the content differs run to run), but every phase moved the same way.
  On the model this is ~21 fps against ~13. A guess named again: the model's Thumb CPI; the measured
  instruction ratios above are the part that holds on the phone whatever the split.
- t17 in pictures (two captures decoded): the tunnel, the car, sparks off the wall, the whole HUD
  (position, lap, minimap, WANTED, speed, nitro) drawn right.
- Cycle profile pct17 (t17, the calibrated model, 1.5 G instructions after 8 G; the stretch is the
  tunnel): our own renderer C is now the largest -- the mesh list (a4_mesh_insert 9.6%, mesh_draw 6.0,
  mesh_fallback 4.7, lean_tail 3.8, mesh_scr 2.5, batch_draw 2.1) ~30%, the composite 10.3; the lifted
  3D front end 12.6 (the transform 7.3); ngibackbuffer's 565 -> 32-bit convert 10.0; the game's Thumb:
  +3041E 6.7 (see below), +CE8C 3.7, +ED9E 1.0, and a long tail; irobex 2.6 (a bench thread).
  The hottest loads in mesh_take and mesh_fallback are reads of the game's 128 KB reciprocal tables
  (a miss each, 60 cycles on the model), done again for every face that shares a corner.
- "+3015E 6.8%" was a pixel loop at +3041E inside it: the billboard blit's mode 2 (added to the frame,
  each 565 channel saturating -- the 0x20 / 0x800 / 0x10000 carries), in software for the billboards
  drawn after the frame's composite (in the tunnel: the lights), ~30 Thumb instructions a pixel and
  most of them stack traffic.
- +3015E lifted (funcs.txt), with two ALWAYS edits: the capture hook +30252 (a4_bbq_tramp) as a C call
  after the two loads it displaces -- a4_gl_bb(r0..r7, the blit's own sp), 1 = a GPU quad, to the
  epilogue -- and the mode 2 row (+30412..+30484) by hand (a4_eng.h bb_row2: every register and frame
  word the game's walk leaves that is read again set as it sets them).
- A fault caught by review on the way: a4_gl_bb reads the blit's pushed return address to know a
  TBillboard's draw (lr at f+0x8C == +31364), and a lifted function pushed a dummy lr. Now the veneer
  entry passes the real one (__builtin_return_address) and a direct call passes the game's own return
  address (lift.py: engi_XXXXX's lr0, engr_XXXXX). Fuzz: +3015E 346 cases agree, the others again 0 differ.
- A4_BBCHECK (bench): bb_row2 runs on a row, its result kept, the row put back, the lifted game walk
  runs on it, and +30486 compares (census 100-104). BC1 (lockstep 100 s): 69,647 rows compared, 0
  pixels and 0 states differing; painter 32,371 calls, 0. BCM (a pixel a row wrong): 58,024 of 58,024
  rows flagged.
- A4_MESHMEMO (a4_gl.c): a corner's screen point and perspective term (mesh_scr, persp_rt) kept per
  frame, keyed by its exact view x' y' z' -- pure functions of those and of the frame's projection,
  which is fixed while the list is gathered -- 128 entries of 32 bytes at 0x452000 (free in a 3x
  build); mesh_fallback on the kept corners. A4_MEMOCHECK works each out again and counts differences
  (census 100-103). Run: MC1, and MCM (a mutant: one coordinate wrong a face).
- MC1 (A4_MESHMEMO + A4_MEMOCHECK, lockstep 100 s): the memo is NOT exact as keyed. Of ~1.4 M faces'
  boxes 25,712 had a corner whose kept screen point differed from the one worked out again, and 531 of
  the perspective class's fallback verdicts differed (4.0 M corners found kept). The guess that failed:
  "the frame's projection is fixed while the list is gathered" -- gctx, F and the centre are, but the
  game's tables behind them change between meshes within a frame (a guess in turn: the speed-dependent
  field of view rebuilt between them). Next: kept per call of the mesh routine (a generation the lifted
  +349F8 bumps at its entry), where nothing changes them; then MC2.
- MC2 (the memo kept per call of the mesh routine): still 16,562 boxes and 220 verdicts differing. The
  cause was mine, not the game's: mesh_vtx returned a pointer into the table, and when two corners of
  one face hash to one slot the second lookup overwrote the first's entry while its pointer was in use.
  So MC1's "the tables change between meshes" was a wrong explanation of a right measurement (logged
  above as a guess; struck here). Fixed: a corner's numbers copied out (struct mpt), the perspective
  term taken from the entry only while it still holds that corner. Kept per call (exact by argument:
  nothing in a call of the mesh routine writes the projection's tables). Run: MC3.
- MCM (the old memo with a mutant, one coordinate wrong a face): 1,454,627 boxes flagged of as many:
  the memo check can fail.
- +CE8C (a mesh instance's set-up, 3.7% of pct17) lifted (funcs.txt), its two unpack loops by hand
  (a4_eng.h ce_verts: three int16 to words a vertex; ce_faces: ten bytes to a 52-byte face) as ALWAYS
  edits at their guards -- every register they leave is written again before a read, so only memory
  is theirs. A4_CECHECK runs each beside the lifted loop on the same arrays. CC1 (lockstep 100 s):
  81,534 vertex loops and 81,534 face loops compared, 0 differing; CCM (a mutant): 83,172 of 83,172 of
  each flagged. Fuzz: +CE8C 2,009 cases agree.
- A4_FASTXF: the transform +345E4 by hand (a4_eng.c eng_h_345e4, routed from the lifted mesh routine
  as the engine's CALLMAP routes): the matrix and the camera's four planes read once a call, under a
  guard that the vertices reach neither (else the lifted one). The fuzz caught a fault in the first
  draft: when every vertex shares an outcode bit the mesh routine returns at once, and its r1 is what
  the transform left -- the draft returned 0. Now r1-r3 as the game's last vertex leaves them (a
  rejected vertex: the far limit, m8, m2 x + m5 y; an accepted one: 8 or the fourth plane's sum, x,
  2^18). Fuzz: +349F8 with it 271 cases agree, 0 differ (1.46x fewer instructions than the game's).
  Run: LX1 (A4_LIFTCHECK over the mesh routine with the hand transform in it).
- MC3 (the memo, aliasing fixed, kept per call): 2,114,152 fallback verdicts and every box's corners
  compared, 0 differing (5.18 M corners found kept). The memo is exact.
- LX1 (A4_LIFTCHECK with the hand transform): 3,937 mesh-routine calls compared, 0 differing; 40,457
  inserts, 0. Few calls: the lifted +CE8C calls the mesh routine directly, past the checker on its
  veneer. lift.py: A4_NODIRECT_XXXXX makes lifted callers go through the game's entry (the veneer);
  A4_LIFTCHECK now requires it for +349F8 and +349B0. Run: LX2.
- t18 (t17 + the billboard and +CE8C lifts with their hand loops + A4_MESHMEMO + A4_FASTXF, real time,
  calibrated model, 120 s): render call 45.0 ms a call over the race (t17 43.9), windows 45.6-50.3 ms a
  frame (t17 44-54). No gain visible -- but the two runs drove different stretches (2,109 renders
  against 2,155; content moves the cost +-5%), too coarse for a few percent. A fair A/B: the same
  configurations in lockstep (the same seed: the same scene at the same render), L16 (t17's lifted
  set), L17 (+ the billboard and +CE8C), L18 (+ the memo and the hand transform). Running.
- rules.py: titles.py --check FAILS (GAMES.md's generated tables stale); nothing in this round touched
  a game.h or a shim, so it predates it -- not looked at here.
- LX2 (A4_LIFTCHECK at the veneers, A4_NODIRECT_349F8/349B0, the hand transform inside the mesh
  routine; with A4_PAINTCHECK): 49,251 mesh-routine calls compared, 0 bytes and 0 insert lists
  differing; 31,053 inserts, 0; 20,490 painter calls, 0. The hand transform is exact in-run.
- The composite's BGRA loop (a4_gl.c composite_rows, A4_SLOWBLIT off): the compiler rebuilt its three
  masks every pixel pair (short of registers) and walked by index -- 24 instructions a pair on the
  common path. Masks pinned in registers, the walk by pointer, the both-drawn case hinted: 21. An
  unrolled draft (two pairs a turn) spilled the masks to the stack: worse, dropped. A4_BLITCHECK (the
  general loop's answer for the same strip) is the check.
- The lockstep A/B (calibrated model, 120 s each, A4_LOCKSTEP=1 paces one game step a render, so these
  are not comparable with the real-time t-runs):
      L16 (t17's lifted set)                          render call 50.8 ms (1,922 calls), 2,393 posts
      L17 (+ the billboard, +CE8C, their hand loops)  50.3 ms (1,944), 2,414 posts
      L18 (+ A4_MESHMEMO + A4_FASTXF)                 48.0 ms (2,037), 2,529 posts
  L18 ~5.5% faster than L16 over the race; L16 against L17 within noise (the billboard's software path
  is rare outside the tunnels). The per-capture rows differ between the runs at the same index (the
  runs drift apart: the opponent's slot, and what follows from it), so a few-percent change is at the
  edge of what this A/B resolves; the in-run instruction counts are the firmer measure.
- Where it stands (the calibrated model): t16 11-16 fps in its windows, t17 19-23, t18/L18 ~5% more.
  30 fps is a 33 ms frame; the frame is ~45 ms. What is left, by pct17: our mesh-list C (~25%), the
  composite (~10%, most of it the modelled readback), ngibackbuffer's convert (10%, not ours), the 3D
  front end (now ~9%), the engine and a long tail of game code.
- Guesses named (rule 4): (1) the model's Thumb CPI 3.0 against ARM 1.6 is fitted, not measured --
  measured instruction ratios: a mechanical lift 1.15-1.2x, hand-written loops 1.46-2.4x; (2) the
  phone's instruction cache is 16 KB (round 22's note) and lifted ARM is about twice the Thumb it
  replaces -- the bench models no I-cache; (3) the composite's loop change is not yet checked in-run
  (A4_BLITCHECK). Only a phone round settles (1) and (2).
- BL1 (A4_BLITCHECK, the composite's loop as changed, lockstep 60 s): 9,660 strips compared against the
  general loop, 0 pixels differing. BLM (a mutant flipping a drawn pixel a row): 143,583 pixels flagged
  in 10,350 strips. The change is exact.
- Run: pct19, the cycle profile of the current build (t18's flags with the composite change), for the
  next cuts.
- pct19 (cycle profile, calibrated model, t18's flags + the composite change; 1.5e9 instructions after
  8e9 skipped), share of modelled cycles: ngibackbuffer +CCB8 (the runtime's 565 -> 32 convert) 13.9%,
  a4_gl_flush (the composite) 12.4%, engi_349F8 (with the hand transform) 8.56%, a4_mesh_insert 6.19%,
  the painter 5.56%, lean_tail 5.34%, mesh_draw 4.22%, mesh_vtx 3.75%, batch_draw 2.91%, bb_row2 1.72%,
  mesh_fallback_m 1.70%, engi_349B0 1.65%, engi_0CE8C 1.62%; irobex 3.6% (a bench artifact). The
  largest single item is not ours: next, how the runtime's convert is reached and whether our side can
  spare it.
- The convert, read (ngibackbuffer +CB70, the converter class whose vtable is at image +24158; the
  present at +34A4 calls `[bb+0xC]->vtable[2](conv, dst [bb+0xB8], src [bb+0x30])` when the DSA
  region is the whole window): 20 instructions a pixel pair -- it loads each source word twice and
  stores a pixel at a time. pct19's blocks: 72,271 rows converted in the window, ~301 converts,
  43.5 cycles a pair (32 of instructions, 7.5 of read misses, 4 of write misses): ~1.67M cycles a
  convert, ~5 ms on the model, plus the post's 2.3 ms.
- GL call census (emulator: EKA2L1_GPUMODEL_CALLS=1, new -- per ordinal, and the sets that set what
  was already set; gc19, t19 real time on the calibrated model, 120 s): 659-1,000 calls a frame, of
  them 20-32 redundant (3%): binds 148-226 (4-10 redundant), draws 160-268 (DrawArrays 84-152 the
  table, DrawElements 76-118 the list), enable/disable/depth-mask ~90 each, LoadMatrixx 59-96,
  Color4x 21-60, BlendFunc 16-21 (11-16 redundant). A state cache would save ~0.2 ms a frame: not
  the lever. The GL side is draws (one texture each) and the kind switches between them.
- The two whole-screen pixel loops in ARMv6 (renderer/a4_px.s, -DA4_PX6; the N95's ARM1136 is an
  ARMv6 core, the blob is built for ARMv5TE): PKHBT/PKHTB pack a halfword of each of two registers
  into one, so two pixels go through each mask, shift and OR together.
  - a4_comp_row, the composite's N95 case (BGRA, alpha bit 31 the drawn-mask): 22 instructions for
    four drawn pixels against the C loop's 42 (21 a pair); halfword stores when any of the four is
    not drawn. pxfuzz (unicorn ARM1136, 400 random rows against the C loop's rule): 0 differing;
    a mutant (the G mask one bit short) 398 of 400 flagged.
  - a4_cvt565_run, the runtime's 565 -> 32 row: 16 instructions for four pixels against +CB70's 40.
    pxfuzz against the runtime's own loop (+CCB8..+CD04 run from its code): 400 rows, 0 differing;
    a mutant (G placed a bit low) 400 of 400.
- The runtime's converter, ours (cvt_fast / cvt_install, a4_gl.c, with A4_PX6; A4_PX6_NOCVT leaves
  it out). The probe (cvp: registers and memory at the game's present call +1EA, the runtime's
  present +34A4 and +CB70; 20 s of race): the game's display object (r4[0x20]) is the game's own
  class, not the runtime's; the runtime's back-buffer object bb sits 0xC0 before the game's 565
  buffer (bb[0] its vtable at image +23B8C, bb[8] the pixels, bb[9]/bb[10] 320x240), bb[3] the
  converter, whose [1] is bb (it is the converter's X: source stride bb[0x10] = 160 words, size
  bb[9]/bb[10], the screen object bb[4] with its TRect at +0x28 and target stride at +0xB4); offsets
  conv[2]/conv[3] 0; target 0xC9200020 (the DSB's buffer). 796 runtime presents to 2,405 game
  presents in the probe, menus included (the counter covers the session).
  - Install, at every present: bb = buffer - 0xC0 only when that is on the buffer's own page
    (nothing read off it otherwise), then bb[8], bb[9], bb[10], conv[1] == bb, and +CB70's 112
    words hashed (0x31323FE4) -- each link checked before it is followed; a failure leaves the
    runtime alone for the session and says which (0x412100 [0]: 1 installed, 2-6 the failed step).
    Our vtable is a copy of the converter's (its offset and typeinfo words too) with [2] = cvt_fast.
  - cvt_fast does +CB70's set-up exactly (the offsets, the size clipped to the screen rect's, the
    row strides) and converts with a4_cvt565_run -- one run when the rows are contiguous (they are:
    160 words / 320 words) -- and hands anything else (odd or zero-modulo-4 widths, unaligned,
    oversized, overlapping source/target or objects in the target) back to +CB70 itself.
  - cvtfuzz (unicorn: our image's cvt_fast against the DLL's +CB70 from its code, TRect::Size
    emulated, random objects, offsets, strides, sizes, alignments, overlaps; whole memory compared):
    285 agree, 0 differ, 169 handed back, 146 not cases (+CB70 itself faults or runs away -- odd
    widths, negative heights). A mutant (x offset scaled by 8, not 4): 8 differ.
  - In-run check A4_CVTCHECK: +CB70's own answer goes to the screen and each row of ours, made into
    a spare row, is compared with it (census [93] words differing, [94] converts); [91] fast, [92]
    handed back; A4_CVTCHECK_MUTANT flips a word a convert. Runs: CV1 (the check and A4_BLITCHECK),
    CVM (both mutants).
- CV1, first build: installed ([90] 1), the composite's ARMv6 row exact in-run (A4_BLITCHECK: 12,420
  strips, 0 pixels differing) -- but every convert handed back to the runtime ([91] 0, [92] 827). The
  probe of the converter's screen object (cvq: [bb+0x10] dumped at +CB70) says why: +CB70's size call
  (its import stub +5EC8, ordinal 387) is WS32's RWindowBase::Size() on the window at +0x28 (its
  handle and its session's buffer there, SYMBIAN.md's test), not TRect::Size as I had guessed and the
  fuzz had modelled for both sides alike. Corrected: cvt_fast calls the runtime's own stub (found
  from +CB70's address, inside the hashed words; the stub's ldr pc checked at install), the same
  calls in the same order as +CB70 (width, then height, each again when the window's is smaller).
  cvtfuzz with the stub returning a size from the window object and the calls compared too: 386
  agree, 0 differ, 226 handed back, 188 not cases.
- A slip: CVM was still running (a backgrounded second command) when the cvq probe started; the
  probe's telerun killed it. Not two emulators for long, but CVM is rerun clean; the runs since go
  through one strictly sequential loop.
- CV1 (the corrected build: A4_PX6 + A4_CVTCHECK + A4_BLITCHECK, calibrated model, 60 s of race):
  installed once ([90] 1); 735 converts, all on the fast path ([91] 735, [92] 0 handed back), each
  compared row for row with +CB70's own answer: 0 words differing ([93] 0, [94] 735). The composite's
  ARMv6 row: 11,027 strips against the general loop, 0 pixels differing. Run: CVM (both mutants).
- The mesh list's back-face determinant (a4_mesh_insert, 2.2% of the race's modelled cycles in pct19,
  its two hottest blocks): every product was 64-bit (6 SMULLs and three 64x64 multiplies). With the
  nine coordinates int16 (MESH_FITS, all checked before) each 2x2 minor is within 2^31 - 32768 and
  exact in 32 bits; only the three terms need 64 bits: 6 MULs, an SMULL and two SMLALs. Checked on
  the host against the all-64-bit form: 2e8 random cases and 4.0e7 corners of the int16 cube, 0
  differing. Unconditional (exact by construction and by that check).
- CVM (both mutants: a word flipped in each convert's spare row, a pixel flipped a composite row):
  781 of 781 converts flagged (one word each: [93] 781), the composite check 166,436 pixels in 11,730
  strips. Both checks bite; A4_PX6 is exact on the race. Runs: RS1 (which rows the composite reads
  back carry a GPU pixel), then the lockstep A/B L18b (t18's flags, today's tree) against L19 (+ A4_PX6).
- RS1 (bench census A4_ROWSTAT: which rows the composite reads back hold a GPU-drawn pixel; t18 +
  A4_PX6, 60 s): 248,192 rows read, 5,344 (2.2%) with none -- all before the race (the count stops
  growing); a frame's first drawn row 5 on average, its last 239. The GPU draws the sky now, so a list
  frame's "all rows" is the truth: a tighter readback extent saves nothing. Dropped.
- mesh_draw (4.2% in pct19), two of its hot blocks: the run grouping's A4_MESHZ test read each face's
  three corner depths out of MESH_V (36 bytes a face, 45 KB for a full list: past the 32 KB data
  cache), and the counting sort cleared and summed all 4,096 buckets every list. Now mesh_take keeps
  each face's ZW range (uint16 min/max: ZK..32768, exact) in MESH_Z, 4 bytes a face, and the list's
  lowest/highest bucket; the sort runs over that range only (the same order). Room: MESH_NR, the runs
  held before a draw, 1024 -> 512 (a frame's list makes at most ~210 draw calls benched). In-run check
  A4_LISTCHECK (census [74] faces whose place differs from the full sort's, [75] ranges differing from
  the corners', [76] lists) and its mutant: runs LS1, LSM; RS2 the list's texture keys a frame against
  its draw calls (how far merging could go).
- Lockstep A/B (calibrated model, 120 s of race, A4_LOCKSTEP=1 + A4_BENCH_SEED: one game step a
  render, so renders in the fixed window are the throughput):
      L18b (t18's flags, today's tree)     1,991 renders, render call 48.9 ms, composite 14.3-15.9 ms a render
      L19  (+ A4_PX6: both ARMv6 loops)    2,130 renders, render call 47.3 ms, composite 10.8-11.5 ms a render
  7.0% more frames in the same time (the frame ~6.5% shorter); the convert is outside the render call
  (the present), so the render call shows only the composite's share. The windows' rows are not
  comparable run to run (L19's last windows sat in a heavier stretch: 383-390 draws a frame against
  206-212); the totals are.
- LS1 (A4_LISTCHECK, t18 + A4_PX6 + the determinant and list changes, 60 s): 1,011 lists, 0 faces out of
  place against the full 4,096-bucket sort, 0 kept ZW ranges differing from the corners'. LSM (the
  mutant: two places swapped and a range bit flipped a list): 2,070 places and 1,035 ranges flagged.
- RS2 (census of the list, 60 s): 1,011 list frames, 26.9 texture keys (texture x texz) a frame, 90.8
  draw calls, 997 faces -- 3.4 calls a key. A run's one box (the union of its faces') soon meets
  everything later in table order, so a face of the run's texture behind another run cannot join it.
  Upper bound: one call a key, ~64 fewer draws a frame (~1.3 ms of the GPU model's draw charge, and
  the binds). RS3 simulates, in-run, the same grouping with a per-run mask of the 16-pixel tiles its
  faces' boxes touch as well as its box (both conservative, so a pass either allows keeps the picture):
  census [77] its runs, [78] the same with no limit on the runs scanned back.
- RS3 (the tile-mask grouping simulated in-run, 60 s, 988 list frames): 90,684 runs against the real
  91,340 (-0.7%); with no limit on the runs scanned back 90,639. The list's splits are real overlaps
  in table order (a face of one texture behind a run of another, at depths the MESHZ rule cannot pass),
  not the box's coarseness. Finer overlap tests do not pay; dropped.
- RS4 (the same simulation for the table path's batches, 60 s): 75,031 runs against 74,320 with tile
  masks (-0.9%), 27,480 distinct keys summed over the batches (2.7 runs a key). The same answer as the
  list's: the splits are real overlaps in painter's order. The simulations removed again (the key
  census kept under A4_ROWSTAT). Draw calls stay where they are unless a class's order can be
  proven free, which a depth test cannot do for painted faces.
- For a phone round (rule 4: the bench cannot settle these): A4_ABCYCLE11 steps AB_PX6 (4096: the ARMv6
  loops and the runtime's convert ours) x AB_PXPLD (8192: the same loops with a PLD of the source a few
  lines ahead) one pair a window -- slot 0 the C loops and the runtime's convert, 1 ARMv6, 2 the C loops
  again (the prefetch has no loop to go in: a second baseline, the noise floor), 3 ARMv6 + PLD. The
  aggregates' format word 'AG27'; tools/r50sum.py reads them. In a baseline slot the converter hands
  back before any work of its own (its window-size calls would otherwise be doubled: census [95]).
  The PLD variants: pxfuzz 400 + 400 cases, 0 bad (PLD is a hint; it never faults past a buffer's end).
  Guesses this round would settle: (1) the model's Thumb/ARM CPI split (the lifted code's gain);
  (2) the 16 KB instruction cache (the bench has none); (3) whether a PLD hides the streaming loops'
  read misses on the ARM1136 (the model credits none); (4) whether the phone's runtime is the build the
  converter is checked against (the hash; census [90] says whether it was taken).
  Bench run of the A/B build: AB1.
- AB1 (the A/B build on the bench: real time, calibrated model, 120 s, slots interleaved a window each):
      slot 0  C loops, the runtime's convert   555 renders  54.1 ms a render  comp 11.5 (read 8.5)
      slot 2  the same again (the noise floor)  552           55.0             11.6 (8.6)
      slot 1  ARMv6 loops, the convert ours     552           50.3              9.2 (7.6)
      slot 3  ARMv6 + PLD                       552           48.3              9.3 (7.7)
  The ARMv6 loops ~5 ms a render (~9.5%) on the model in real time; slots 1 and 3 run the same code here
  (the model has no PLD), so their 2 ms apart is the noise -- a phone difference under ~2 ms would not
  show. Converter: installed once, 1,169 fast, 1,149 handed back in the baseline slots (as designed), 0
  otherwise. The A/B build is ready for a phone round on r49's flags (the 3x engine is not phone-ready).
- pct20 (cycle profile of P2: t18's flags + A4_PX6 + the list changes; 1.5e9 instructions after 8e9):
  our blob 71.2%, the game's Thumb 23.8%, ROM 4.9% (the runtime's convert gone from the list). Ours:
  engi_349F8 9.67% (its vertex transform 5.5, the per-face outcode test 4.2), a4_cvt565_run 8.75%,
  a4_comp_row 8.11%, the painter 6.61%, a4_mesh_insert 6.42%, lean_tail 5.99%, mesh_vtx 4.42%,
  mesh_draw 3.59% (4.22 in pct19), batch_draw 3.24%, mesh_fallback_m 2.04%, +CE8C 1.91%, +349B0
  1.78%, bb_row2 1.77%, draw_prim_take 1.44%, tex_index 1.11%. The two pixel rows are now about half
  instructions and half the model's read misses (150 KB and 300 KB streamed a frame) -- what a PLD
  would hide on the ARM1136, if it does (the phone A/B's slot 3).
- The same slip again: gc20 (the census with reverted sets) was still running when RS5 started; each
  telerun's pkill cut the other short. What each gathered before the cut stands (gc20's census lines,
  RS5's 276 race frames). Every bench run now goes through scratchpad benchgo.sh, which waits until no
  emulator and no bench script is alive.
- gc20 (census with reverted sets: a state changed and changed back with no draw between, both calls
  counted): 741-820 calls a frame, 7 redundant, ~100 reverted -- glDepthMask 45-50 of its 49-54 calls,
  glEnable/glDisable 53-58: a kind's leave and the next kind's enter turning the depth test and mask
  off and on again. A lazy state (renderer: gl_enable/gl_disable/gl_depthmask record, lz_flush tells GL
  at the next draw or clear -- the only calls these states act on; every draw and clear goes through
  those wrappers, checked by grep; A4_EAGERGL keeps the old behaviour for the A/B) removes them: ~0.3 ms
  a frame on the GPU model. Check: a seeded lockstep pair, eager against lazy, frames compared.
- RS5 (cut short, 276 race frames): 19,500 pixels a frame (25%) with no opaque GPU class under them
  -- the backdrop software keeps. The blended classes (KIND_ADD, KIND_ALPHA: lean_tail returns 0 for
  them) are drawn twice, by the GPU and by the game's span (+33EA8 is the alpha-mapped boards' span,
  3.2% of pct20), because over that backdrop only software's copy shows; over the other 75% the
  composite overwrites software's copy. Which pixels are backdrop is known only after the GPU frame.
- benchgo.sh's first version waited on itself (its pgrep -f matched its own arguments); it now matches
  only scripts being run ("bash .../telerun4.sh"). The first eager/lazy pair (LZE/LZL, 25 s): the two
  races identical step for step (aligncheck: 62 of 62 records), but the captures are keyed to presents
  since boot (the menus run on the wall clock), so equal capture indices were not the same moment
  (31-39 K of 77 K pixels apart). A4_TELSTEP (bench, a lockstep build with the car recorder): a capture
  at every TEL_EVERY-th race step instead -- the same moments of the same race in both builds. Rerun.
- LZE/LZL again with step-keyed captures (40 s): the two races identical to step 249, where they part
  (a car word, +AC0: the run-to-run spread); the frames at steps 200-240 1,199-3,670 pixels apart, all
  in the 3D view -- the opponents ahead and their position markers, and the exhaust sparks; road,
  scenery, HUD and the player's car identical (lz_cmp22.png). Control, the eager build against itself
  (LZE2): 686-3,273 pixels apart at the same steps, parting at the same step 249. The lazy state's
  differences are inside the bench's own spread; with every draw and clear flushing, it is taken as
  exact. Default on (A4_EAGERGL restores the old calls). gc21: the census on it.
- gc21 (the census on the lazy build, 60 s): reverted sets 2.5 a frame (from ~100); redundant 25-34
  (binds 8-10, blend functions 22 -- left: the bind and blend calls are not cached). glDepthMask no
  longer among the busiest calls.
- Where the round stands (calibrated model): the ARMv6 pixel loops ~5 ms of a ~54 ms real-time render
  in interleaved windows (AB1), 7% more frames in the lockstep window (L19); the list's determinant,
  depth ranges and sort, and the lazy state, each ~0.3-1% (not separately timed: under the bench's
  run-to-run spread). Tried and dropped, with numbers: a tighter readback extent (the GPU draws every
  row), finer draw grouping (under 1% of draws), a GL state cache for repeated values (3% of calls),
  20 more lifted functions (the random fuzz cannot reach them; random fuzz is no verification for them).
  Open, each ~1-2%: the blended classes' software copy where opaque list faces cover them, the
  painter's loop spills, the Thumb long tail (needs real-input verification). Open guesses for a phone
  round (A4_ABCYCLE11 build ready on the bench; r49's flags, the 3x engine not phone-ready): the
  Thumb/ARM CPI split, the instruction cache, PLD on the streaming loops, the runtime build's hash.

## Round 50 candidate (r50): r49's build with the ARMv6 loops stepped per window

- Not a repeat: no phone round has run the ARMv6 loops, the converter swap, the list changes or the lazy
  GL state, nor any A/B of a renderer loop inside one race (rounds 23 and 24 stepped other bits: the
  hook's prefetch and the readback strips, the atlas). Gameplay is r49's flags unchanged (C49.flags:
  the half-step frames, A4_ABMODE 3118 with AB_HSPEC) -- r49's "traffic passes through" stays; this
  round measures, it does not fix.
- Build: C49.flags + A4_PX6 + A4_ABCYCLE11 (C50.flags), A4_HALFSTEP=1, A4_SCRATCH_SIZE 0x5F000 -- r49b's
  recipe. The A/B keeps every bit of A4_ABMODE and steps only AB_PX6 x AB_PXPLD (a first draft
  hard-coded the t-series base 1070 and would have turned the half-step frames off: caught reading
  the compiled diff, fixed). Bench-only flags: none (checked by name).
- Rule 4 (b), read back: the sources preprocessed with C49.flags at r49's commit 2762693 and with
  C50.flags now, diffed: a4_tel.c the slot, the format word 'AG27' and the mode stepping; a4_eng.c
  nothing; a4_gl.c the lazy state (wrappers), the struct mrun (kslot 16-bit, zlo/zhi; 20 bytes as
  before), MESH_NR 512 and MESH_Z, the list's bucket range, the determinant, the window's mode taken
  from A4_ABMODE, the composite's two loops behind the mode bit, cvt_fast and cvt_install. Each line of
  that diff read. The read-back found an absence that could not have been read: a refused swap left
  no reason in a captured word -- now census [89] (the step that failed) and [88] (the hash a
  different runtime's convert gave); r50sum.py prints them.
- Rule 4 (a), the guesses this round settles, each named: the model's Thumb/ARM CPI split and its lack
  of an instruction cache (the slots' difference against AB1's 5 ms); whether PLD hides the streaming
  loops' misses (slot 3 against 1; the bench cannot); whether the phone's ngibackbuffer is the build
  the swap checks for (census [90]/[89]).
- Rule 4 (c), states the bench has not entered: the telemetry failing to open its files (then
  a4_tel never steps the mode: every window stays slot 0, the old code -- safe, the A/B lost, and the
  missing files say so); a window smaller than the buffer (the N95's slide closed, if the
  game draws then: the buffer stays 320x240, so the swap is taken and clips to the window's size as
  +CB70 does, rows then not contiguous -- the fuzz covered clipped sizes and strides; on the bench the
  window always matched); a DSA abort and restart (a call, the screen saver): the swap is checked
  again at every present and a converter object made anew is taken again after the hash; the
  runtime's convert called from a menu after a race (taken by the swap, either slot exact).
- Candidate: sha256 7154085238e1672244f1811d40e18ea5223b27a79837125cccf6537b2b7d54f7, 816,564 bytes;
  +3A15C B510 0004; no 0xDEADBEEF. (A first build, 2c200e9b, 20 bytes smaller, before the refusal
  words; its battery was stopped at its first step and rerun.) Recorder twin a4_y9 (the same flags +
  A4_CARREC/A4_YREC/A4_AUTOPILOT). Battery r50bat.sh (r49b's, the names changed, r50sum and the finish
  on the exact binary added): running.
- Battery on 71540852 (r50bat.out): tele (r50t) installs 1, fast 920, baseline-slot 919, no refusal; the
  recorder twin normal; crash-steer, the finish (r50f, r50f2), marks and Veyron as r49b's; faults 1
  each. The menu run (r50mn) did not pass: all 19 screens one frame, a race on a grass track held at
  "READY?" at 0 km/h (only the emulator's fps text differs), framestat 3575, the 3D renders running
  (23 a window, the slots stepping) -- and the converter census says refusal 2 after its install.
  Rerun (r50mn2): the same stall on the same track. r49bmn (Nov 6, 14:33) raced a city track: the
  game's save (C:\Private\20007b39\2001092b\Asphalt4) was written at Nov 6 23:08, after it, so the
  menu route now lands on another race. Control: r49b's exact binary through the same route (r49bmn2),
  running. The save as it is now kept in the scratchpad (save_asphalt4_1106.bin).
- Refusal 2 is a bug of ours, whatever the stall turns out to be: A4_GL_BB is the frame's 3D target,
  and the garage's car renders offscreen, so "bb[8..10] not the buffer" means "not the screen this
  frame", not "another runtime" -- and it latched. Had the garage come before the first race frame
  the swap would never have been taken, and rule 4 (c)'s "a converter made anew is taken again" was
  false after it. Now: no latch, counted in census [87] (r50sum prints it), tried again at the next
  present (the read is on buf's own page, as before). Candidate r50b: c445ce58..., 816,576 bytes.
- Correction: r50mn's race is the same as r49bmn's (Los Angeles: the grass start, the city later), held
  at the countdown's "READY?". Not the save: r49b's binary through the same route on today's save
  (r49bmn2, r49bp) counts down and races. The menu run is the battery's only uncapped race (the rest
  run under EKA2L1_CPUCAP); r50 stalls there in every run (r50mn, r50mn2, r50p: the menus, the
  intro's flyover, then READY? for good, the pause key unanswered), while the logic steps go on
  (h[61] 25-37 a window, as r49b's).
- A rebuild of r50b without A4_HALFSTEP/A4_SCRATCH_SIZE (my slip, not the recipe) faulted at boot on
  the camera-script flag 0x447060 (scratch too small) -- discarded. The recipe checked: the previous
  commit's source with it rebuilds 71540852 byte for byte; r50b with it: 91183e41.
- Bisect (racemenu2.sh: a screenshot at each menu step and three after; r49's flags on each commit;
  r49b's commit rebuilds 81193beb exactly and its hooks match HEAD's line for line): 75df72c, ab870c4,
  0921983 count down and race; 162b0e5 (= HEAD's binary for r49's flags) stalls. r50b with A4_EAGERGL
  counts down and races. The lazy GL state (162b0e5) is the cause; how a GL call order holds the
  race's countdown is not known yet.
- Not the game: the stalled run's own captures show it running -- r50p's framebuffers a race downtown
  at 170-199 km/h, r50mn's the garage (where its converter census met the garage's offscreen target).
  Only the bench window froze. The emulator's screen draws (the DSA post over the screen texture)
  set clipping and nothing else, so they drew with the GLES context's leftover state; the lazy
  state leaves GL as the frame's last draw had it -- depth test on -- where the eager calls ended
  with it off, and every post after failed the depth test. A device never lets a GLES context touch
  the screen's composition. Fixed in the emulator (EMU_PATCHES.md "The screen draws inherit a GLES
  context's state"); the lazy state stays. r50b on the fixed emulator, the battery's menu route
  (r50bfix): the countdown, the race, the pause menu and every row, confirm, the main menu, the
  garage -- as r49bmn. Every earlier bench run's game-side captures were unaffected (they are the
  game's buffer, not the window); only racemenu's screenshots read the window.
- r50b: the battery reruns on 91183e41 with the fixed emulator, the recorder twin rebuilt from the
  same source.
- Read again before the battery: cvt_install read the page at A4_GL_BB at every present, 2D ones too,
  where after the garage A4_GL_BB is the garage's offscreen buffer, which the game may have freed. r50
  read it the same way until a refusal latched; without the latch it would read it for good. Now
  called only in a frame that drew 3D (had), whose first triangle set A4_GL_BB that frame. The r50b
  battery (91183e41) was stopped in its first run and the archives restored (cmp ok). Candidate r50c:
  7ed1371c93356251b14b0dfcad24577e2306ddda3859a56b11eafdec1fc348a5, 816,560 bytes; against r50 only
  a4_gl_frame differs (cvt_install inlined: the step-2 count, the had gate); no 0xDEADBEEF; bench-only
  flags none. Twin a4_y9c (+ A4_CARREC/A4_YREC/A4_AUTOPILOT; the recipe rebuilds r50's a4_y9 byte for
  byte from r50's source). Battery r50cbat.sh, on the fixed emulator: running.
- Battery r50cbat.sh on 7ed1371c (fixed emulator; r50bat's runs, the names changed): ship check
  816,560 bytes, +3A15C b510 0004, no 0xDEADBEEF; faults 1 in every run; archives restored (cmp ok).
  - Twin y9c (recorder, 120 s, model): all cars x-z 99% 24 max 106, camera 99% 45 max 90, player
    bearing reversals 0 -- as r50's y9 (27/116, 43/99, 0).
  - Tele r50ct (model, 120 s): framestat 2176; slots C 66.5 / ARMv6 63.6 / C(2) 69.6 / ARMv6+PLD 63.6 ms a
    render (r50: 70.5 / 62.8 / 65.5 / 65.1); converter installs 1, fast 1,050, handed back 0, other 3D
    targets 0, no refusal; car heading reversals 0; camera yaw reversals 6 (r50 4, r49b 3); drawn car
    off the game's path x-z 99% 39 max 51 (r50 37/66). Two differences under study: the present
    (convert + post) 2.5-3.0 ms a render against r50's 0.2-1.2 and r49b's 0.1-0.7, in every race
    window (the race's 69 presents a window unchanged); and 168 presents with no 3D, all before the
    race (menus, loading: the counter covers the session). At cap 85 without the GPU model the
    present is 0.8-0.9 ms (r50cf2), so it is the model run only. A/B queued: r50b (no had gate) and
    r49b, the same model run on the fixed emulator.
  - Crash-steer r50cc2: framestat 2517; drawn off the path x-z 99% 6 max 8; camera yaw and car heading
    reversals 0; bearing reversals 5 (max change 11/2048), r50 0, r49b 1.
  - Finish, twin (r50cf): reached the finish camera (mode 1: 162 frames, bearing reversals 0; the camera
    yaw's two reversals at the cut into it, as r49bf2's). Finish, exact binary, key held (r50cf2):
    framestat 5023; finish camera 166 frames, reversals 0; crash camera (0x17) 147 frames; converter
    fast 4,182, handed back 0; seg1 slots C 72.0 / ARMv6 62.4 / C(2) 71.0 / ARMv6+PLD 61.4 ms (cap 85).
  - Menu r50cmn: the pause menu and every row, restart, confirm, the main menu, career, the garage and
    the car's page (sheet_r50cmn.png) -- as r49bmn. Converter: installs 1, fast 4,365, other 3D targets
    805 (the garage's offscreen car), no refusal: taken once and kept through the garage.
  - Marks r50ck (framestat 1249) and on the Veyron r50ckv (1184): the race drawn downtown, 160-320
    km/h. (r50's mark shots were frozen at READY? too -- the emulator's freeze; their framestat was
    read, not the pictures.)
  - Veyron autopilot r50cv: framestat 2109; drawn off the path x-z 99% 55 max 86 (r50 17/18, r49 27/54,
    r48 60/75: the autopilot's course differs run to run); car heading reversals 0.
- The present-time A/B (the same model run, fixed emulator): r50b (no had gate) 0.0-0.5 ms a render, 1
  render-less present; r49b's own binary (81193beb: no ARMv6 code, no converter swap, no had gate)
  2.4-3.0 ms, 62 render-less presents before the race; r50c again (r50cT) 2.5-3.0 ms, 89. Not the build:
  a state a run is in or not, from the menus on (the render-less presents before the race come with
  it), seen in 3 of 4 model runs on the fixed emulator and in none before it. 2.3 ms of it is the
  model's post charge (GPUMODEL's last field) falling inside the present's bracket. Why the runs split
  is a bench question, open; until it is settled, present times compare only between runs in the same
  state, and the slots' A/B inside one run is unaffected (every slot carries it).
- Round 50c goes out: 7ed1371c, unbaked (the owner bakes).

## Round 50c, phone (sha256 7ed1371c...): the ARMv6 loops stepped per window

Owner: "I didn't notice anything out of the ordinary." Eight telemetry files (phone50c; captures 225-232,
5,346 race renders in the aggregates over 116 windows, the four slots 598-746 renders each).
- The converter swap on the phone: installs 1, fast 3,677, handed back 0, baseline-slot converts 4,168,
  other 3D targets 0, no refusal -- the phone's ngibackbuffer is the build the hash checks for (rule
  4 (a), settled); one convert a real present (3,677 + 4,168 = 7,845 of 7,846).
- Per slot, whole race (ms a render | 3D, composite (readback), 2D, present):
  seg0 C 72.9 | 26.0 10.5 (6.6) 6.3 2.1; ARMv6 82.0 | 30.8 8.8 (6.8) 6.2 2.3; C(2) 73.0 | 29.2 10.6 (6.7)
  6.1 1.9; ARMv6+PLD 67.1 | 26.1 8.7 (6.7) 6.6 2.0. seg1 C 71.8 | 26.8 10.7 (6.8) 5.8 1.9; ARMv6 69.3 |
  27.2 8.8 (6.8) 5.4 1.9; C(2) 74.3 | 29.2 10.6 (6.8) 6.0 2.0; ARMv6+PLD 72.2 | 29.7 8.9 (6.8) 6.2 2.0.
  - The composite: 10.5-10.7 ms with the C loop, 8.7-8.9 with the ARMv6 one, the readback 6.6-6.8 in
    both: the loop itself ~3.9 -> ~2.0 ms, 1.8 ms a render -- the one gain the phone shows, 2.5% of
    the render. The model (AB1) put the loops at ~5 ms of ~54 (9.5%): it overweights them.
  - The present (the runtime's convert and the post): 1.9-2.3 ms in every slot, the swap's or not
    (r49 phone 2.3). The model's ~5 ms convert (pct19, 13.9%) is not on the phone: the whole present,
    post included, is ~2 ms. The convert swap saves nothing measurable there.
  - PLD: slot 3 against 1, the composite 8.7 vs 8.8 and 8.9 vs 8.8 -- nothing (rule 4 (a), settled).
  - Whole render, all slots: 72.7 ms (5,346 renders) against r49 phone's 72.7 (2,139): the list's
    determinant, depth ranges, sort range and the lazy GL state (all slots) show nothing either at
    this resolution; the 3D phase's slot-to-slot spread (26-31 ms, scene content) is larger than any
    of them. Presents median 55 ms (10-90% 43-85), render start to present 46 ms (r49: 57, 47).
  - The render's parts on the phone: 3D 28.1, composite 9.7 (readback 6.8), 2D 6.0, present 2.0 --
    45.8 of 72.7 ms; the other ~27 ms is outside these phases (the game's step and its own work).
- Frames: drawn speed against its neighbours quartiles 0.99-1.01 (render-timed), 0.98-1.02 (present-
  timed); car heading one-frame reversals 1 in 417 frames; camera yaw 11 (r49ph: chase 4 in 319).
  Drop frames in the race 10, half-step skips 20; held frames 0.
- What it settles: the pixel loops were worth ~2 ms, not ~10; the convert is not a cost on the phone.
  The model's split (Thumb/ARM CPI, no instruction cache) puts too much on the pixel work: recalibrate
  on these slots before trusting it for the next cut. The time is in the 3D phase (~28 ms) and the
  ~27 ms outside the measured phases.
- Correction to the above: the present column brackets DoPresent's present call (+1EA); the convert and
  the post run in the framework's post (+6AC, [r4+0x3C]->vtbl[0x34]), outside it, on the phone and the
  bench alike (the bench's runs that show ~2 ms there carry the model's post charge, the bimodal
  state above). So the convert's saving is in "rest", where the phone's slots read C 26.6 / ARMv6 27.1
  ms (seg1 alone 26.6 against 25.7): 0-1 ms, the scene noise +-1.5 -- not the model's ~3.

## The model recalibrated against round 50c's phone slots

Per-phase means over the whole race (AG27 aggregates, seg0+seg1; phone: 5,346 renders), the C slots
and the ARMv6 slots apart where they differ; "rest" = present + everything outside the phases. Model
runs repeat within 2-3% a phase (r50ct and r50cT, the same binary and settings). One parameter moved
a run (r50c 7ed1371c, tele 120 s at TIMESCALE 4, CPUCAP 330; calrun.sh, calfit.py, aggph.py):

  phase     phone  model | d/dThumbCPI d/dArmCPI d/dRmiss d/dWmiss (ms a render per unit)
  3D        28.14  24.69 |    2.66       6.76     0.0512   0.2876
  read       6.76   8.13 |    0.02       1.06     0.0030   0.0571
  loop C     3.83   3.12 |   -0.04       0.92    -0.0015   0.0330
  loop V6    2.03   1.66 |   -0.01       0.73    -0.0004   0.0262
  2D         6.04   9.20 |    2.63       1.08     0.0041   0.0214
  rest C    28.57  22.48 |    2.28       7.38     0.0313   0.4801
  rest V6   29.16  19.98 |    1.61       5.22     0.0326   0.5444

(kT: Thumb CPI 2.0, kA: ARM CPI 1.1, kR: read miss 120, kW: write miss 20; base 300:60:2:4:5:160.)
- What they say: 2D is the game's Thumb code (2.6 ms a CPI unit, nothing else moves it); 3D moves
  with read misses (the game's front end and our list over the scene's data) as much as with either
  CPI; "rest" is write-bound -- 0.48-0.54 ms a cycle of write miss, the background fills before the
  first triangle and the convert's 300 KB (round 49's c4 saw the first of it); the readback moves
  with the CPU only through its strip handling. At write miss 20 the ARMv6 slots' rest gains more
  than the C slots' (0.54 against 0.48 a cycle): the convert becomes store-bound and its ARMv6
  version's saving shrinks toward the phone's.
- Least-squares fit (relative errors, a grid over sane ranges): Thumb CPI 1.75, ARM CPI 1.35, read
  miss 85 cycles, write miss 21 cycles a store -- the fitted phases against the phone: 3D -6%, loop C
  -8%, loop V6 -2%, 2D +2%, rest C -3%, rest V6 -5%, read +33%. The read is the GPU model's copy:
  its per-byte charge 12 -> 5 ns closes it (-2.2 ms over the frame's ~307 KB of strips). Write miss
  21 cycles a missed store is a stand-in for the memory's write bandwidth (the model has no write-
  buffer drain), and absorbs what the bench lacks (the phone's audio mixing) -- named, not measured.
- Validation run kV1 (CPUMODEL 175:85:21:4:5:135, GPUMODEL 3,20,150,8,2500,400,5,30,2300): running.
- Validation kV1 (r50c, CPUMODEL 175:85:21:4:5:135, GPUMODEL 3,20,150,8,2500,400,5,30,2300, 120 s): whole
  race 72.5 ms a render (phone 72.7); 3D 27.5 (28.1), composite 9.7 (9.7), readback 6.8 (6.8), loop C
  3.5 (3.8) / ARMv6 2.2 (2.0), 2D 6.6 (6.0), present + rest 28.7 (28.9); the ARMv6 slots' rest saving
  0.9 ms (phone 0-1; the old model ~3). Host behind the model 0.1-0.2%. The frame-record medians
  differ (render start to present 61 against 46, presents 73 against 55) because they cover different
  stretches: the phone's 417 records are its second race's last ~25 s with the finish, whose last 8
  windows average 55 ms a render against its whole race's 72.7; the bench's are mid-race.
- At the fitted settings, where a race render's ~72 ms goes (sensitivity x setting, linearised at the
  old point, so approximate): ARM-state instructions ~22 ms (3D 9, rest 10 -- the runtime's convert,
  the ROM -- 2D 1.5, loops 2); missed stores ~17 (rest 10: the fills before the first triangle and the
  convert's 300 KB; 3D 6); Thumb ~13 (3D 4.7, rest 4, 2D 4.6); read misses ~7.5 (3D 4.4); the GPU
  model the rest (the readback's tile render and copy ~6, draws and binds). The weakest number is the
  store cost: 21 cycles a missed store also absorbs whatever "rest" the bench lacks (audio mixing), so
  store-heavy code may read heavier than it is. Settled on the phone by a store-only A/B (the same
  instructions, fewer stores: e.g. the back buffer's fill before the first triangle skipped in
  alternate windows where the composite covers it).
- The bench's model from here: CPUMODEL 175:85:21:4:5:135, GPUMODEL 3,20,150,8,2500,400,5,30,2300
  (EMU_PATCHES.md "A modelled N95 for the bench" updated; scratchpad model_r50cal.env). Results on the
  old settings (t-runs, AB1, L-runs, pct profiles) are not comparable with runs on these.

## Round 51 (bench): cuts on the recalibrated model

- Cycle profile pcr50c (r50c on the new model, 1.5e9 instructions after 8e9, EKA2L1_PCPROF_CYCLES):
  the game's HUD painter +29736 10.1%; euser memset 9.7%; the 565 -> 32 convert 16% (ngibackbuffer
  +CCB8 8.9% in the C slots, a4_cvt565_run 4.0 + _pld 3.3 in the ARMv6 ones); the composite ~10%
  (a4_gl_flush 6.2, a4_comp_row 2.3 + _pld 1.9); our 3D path ~17% (lean_tail 5.6, a4_mesh_insert 5.1,
  batch_draw 4.2, mesh_draw 2.3); euser memmove 3.8%; the billboard blit +3015E 2.6, the mesh routine
  +349F8 2.5, +3118E 2.1, +523E6 2.0. (profreport.py read ROM images from RM-409's z: drive; the
  bench runs RM-320, and euser showed as "irobex" -- the "irobex polling" of pct15/pct19 was euser's
  memset all along. A4_ROMDEV=rm-320 now picks the drive; euser ordinals 1954/1955 memmove/memset,
  read in the code: +55EC an 8-register STM fill.)
- The fill (EKA2L1_PCTRACE_REGS inside memset, +5600, with a4_gl_frame as the frame marker): one call a
  render, 307,200 bytes at 0x20C3BB0, from +2DF0C -- +2DEF4 zero-fills the rasterizer's per-pixel 1/w
  buffer (ctx+0x40100: +0x18 the buffer, +0x10/+0x14 width and height), called by the begin-frame
  +31C70 (at +31CC0) and once by the allocator +2DF66 (+2DFCE). Round 8's reading: only the
  perspective span +333F4 tests and writes it. Checked now (zw2: EKA2L1_WATCH over the 300 KB armed by
  the race's first clear, EKA2L1_RWATCH limited to the game's code): writers the clear and +3384C,
  +33948, +338AE; readers +33826, +33926, +3388C -- all inside +333F4. On the model the clear is 76,800
  missed stores, ~5 ms a render.
- A4_ZSKIP: +31CC0's bl becomes a BLX to a4_zclr_tramp (a4_tramp.s; the call site checked a BL to
  +2DEF4 before it is replaced), which asks a4_zclr_skip (a4_gl.c): skip while glr.zclean is this
  context (the buffer all zero since its last clear) and GL is up; otherwise the game's clear runs and
  zclean = the context. a4_gl_draw_prim sets zclean = 0 whenever it leaves a perspective triangle
  ((flags & 3) == 1) to software -- the only way +333F4 runs. glr is bss-zeroed: no context is known
  clean at the start, so the first begin-frame clears. Census [79] skipped, [99] cleared. Bench check
  A4_ZCHECK (each skipped buffer scanned: [93] found a word set, [94] checked) and its mutant (one
  word set in every 64th checked buffer). Read back: the BLX, the trampoline's word (+2DEF5, Thumb),
  the decision's fields (glr.up 0x401000, zclean 0x402D18, census 0x412E3C/[99]). z1 816,740 bytes.
  Runs queued (zbat.sh): the check over a race and the menus with the garage, the mutant, and z1 on
  the new model against kV1.
- Z checks: z1c over a 60 s race (uncapped): 1,079 clears skipped, each buffer scanned: 0 with a word
  set; 41 clears run (frames after a perspective triangle went to software). z1m (the mutant, one word
  set in every 64th checked buffer): 17 of 1,102 flagged -- the check can fail.
- The hand painter (A4_PAINTFAST, round 50's t-series) into the phone flags without the 3x engine:
  A4_ENGINE + A4_ENG_29736 + A4_PAINTFAST. lifted.c compiled every engine function's global entry
  (eng_XXXXX), which kept all 91 lifted functions and their 3x edits (undeclared without A4_ENG3).
  Now each global entry exists only with A4_ENG3, A4_ENG_ALL or its own A4_ENG_XXXXX (lift.py's
  template and lifted.c alike), and without A4_ENG3 the engine's state and helpers are declared and
  never defined (a4_eng.h): a reached engine function would fail the link, not run. The painter-only
  object: 7 KB (a4_engv_29736, the lifted fallback eng_29736, the hand painter); the t18 flag set still
  builds. z2 = C50 + A4_ZSKIP + the hand painter, 823,752 bytes; its check builds z2pc/z2pm carry
  A4_PAINTCHECK (+ _MUTANT) with A4_PAINTFAST, as LC6 did. Runs queued (pbat.sh).
- z1c over the menus with the garage (the battery's route): 2,047 clears skipped and scanned, 0 with a word
  set, 40 run; the screens as r49bmn (the race, the pause menu and every row, confirm, the main menu,
  the garage and the car's page: sheet_zcmn.png).
- z1 on the new model against kV1 (r50c), 120 s each: 72.5 -> 67.5 ms a render (-5.0, -6.9%); rest
  28.2 -> 24.9, 3D 27.5 -> 25.9, the rest of the phases unchanged; 1,793 clears skipped, 39 run. The
  model's figure for 76,800 missed stores (~5 ms) -- which a phone round would test: the store-only
  A/B the recalibration named.
- The hand painter in the phone flags (z2 = C50 + A4_ZSKIP + A4_ENGINE + A4_ENG_29736 + A4_PAINTFAST):
  z2pc (A4_PAINTCHECK with the hand painter, as LC6) over a 60 s race: 28,556 calls compared, 0 differing
  (pixels 0, clip words 0), 451 too large for the check's buffers; over the menus and the garage:
  359,996 compared, 0 differing, 829 not compared. z2pm (one pixel flipped a call): 21,336 of 21,336
  flagged. z2 on the new model, 120 s: 64.0 ms a render (z1 67.5, r50c 72.5): 2D 6.5 -> 3.4.
  The two cuts together: -8.5 ms, -12%.
- z3 = z2 + the lifted 3D front end (A4_ENG_349F8/349B0/0CE8C/3015E, A4_FASTXF, A4_MESHZ; A4_MESHMEMO left
  out: its table is the half-step machinery's scratch): links without any engine function (the guards
  hold); on the new model 63.6 ms a render against z2's 64.0 -- inside the run-to-run spread. Not worth
  its risk on this model (round 50's LC5: a mechanical lift is ~1.15x in instructions, and the new
  model's Thumb/ARM CPIs are 1.75/1.35). Dropped.
- Dual profile of z2 (instructions pz2i and modelled cycles pz2c, 1.5e9 each after 8e9; dualprof.py),
  cycles / instructions / cycles an instruction: ngibackbuffer 10.2% / 9.3% / 3.6; a4_gl_flush (the C
  composite, half the windows) 7.1 / 11.8 / 1.9; a4_mesh_insert 6.8 / 4.5 / 4.9; lean_tail 6.5 / 8.7 /
  2.4; the hand painter 5.2 / 9.3 / 1.8; batch_draw 4.8 / 2.7 / 5.8; a4_cvt565_run(+pld) 8.2 / 3.9;
  memmove 4.3 / 0.4 / 37 (mean 3.26). The store-bound blocks: a4_mesh_insert +394 (the face's 18
  halfword stores into the list's record, 2.7% of cycles) and batch_draw +360 (the table path's
  vertex copy into the draw buffer, 3.2%).
- The model's store cost against a phone figure already on file: round 20 measured that copy on the
  phone at ~0.74 us a triangle (~0.9 ms a render for ~120 KB); the new model charges ~3 ms for it. So
  21 cycles a missed store is ~3x too dear for streaming copies on the phone, and the fit's extra
  phone time in "rest" is likelier work the bench lacks (audio mixing) than store cost. The 1/w clear
  skip (-5.0 ms on the model) is then nearer ~1.5-2 ms on the phone -- which a phone A/B of the skip
  alone settles, and with it the store cost every store cut is ranked by.
- A4_ZSKIP/painter/z3 aside, two more in this round: the list's record as 9 words (two STMs) instead
  of 18 halfword stores (a4_mesh_insert; A4_VPACKCHECK reads the halfwords back, census [74] records
  that differ, [75] checked; its mutant flips one in 256). A4_ABCYCLE12: AB_ZSKIP (16384) alternated
  per window over the build's A4_ABMODE ('AG28', slot 0 the clear run, 1 skipped); r51sum.py. The
  ARMv6 loops always on (A4_PX6 without A4_ABCYCLE11: the phone settled them, PLD dropped).
- Candidate r51 = C50 - A4_ABCYCLE11 + A4_ZSKIP + A4_ABCYCLE12 + A4_ENGINE + A4_ENG_29736 + A4_PAINTFAST
  (C51.flags), A4_HALFSTEP=1, A4_SCRATCH_SIZE 0x5F000: sha256 5fdb6663d8c08d072a64941e992b8edf5a79c496
  6191e8b77853d0ea8bb660ee, 823,372 bytes; no 0xDEADBEEF; bench-only flags none. Twin a4_y51 (+ the
  recorders). Battery r51bat.sh on the new model, after the pack checks (v5c, v5m): running.
- Pack checks (C51 + A4_VPACKCHECK + A4_ZCHECK, 60 s race): 1,657,402 records read back, 0 differing;
  the 1/w clear (ABCYCLE12 halves): 644 skipped and scanned, 0 with a word set, 705 run (the skip-off
  windows count as run). Mutant v5m (every 256th record flipped): 4,065 of 1,040,637 flagged. Both runs'
  faults=1 is the known ngilightserver boot fault (reading 0x34 in its Main; on file in every log).
- The skip's one assumption, read statically (a4code.bin, every Thumb BL): +333F4 is called from
  +2FB24, +2FB74, +2FBAE -- all inside the textured rasterizer +2F6FC; +2F6FC from +300EE only, the
  dispatcher +300AE's branch for flags bit 1 clear and bit 0 set (bit 1 is tested first: flags 3 goes
  to the flat +2FBDC). So (flags & 3) == 1, the mark in a4_gl_draw_prim, is exactly when the span can
  run; the hook is the dispatcher's entry, every caller (+347E4, +34858, +35DC2, +35F9A) passes it, and
  the mark sits after the take on every path. +2DEF4's other caller is the allocator (+2DFCE): a new
  buffer is cleared there, so a context made again at the same address is still clean. No function
  pointer to +333F4 seen; zw2's watch agrees.
- The painter check turned away 1,280 calls (451 race, 829 menus) larger than its 20 KB buffers.
  A4_PAINTCHECK_BIG: the two copies a whole 320x240 surface each, past the phone scratch (0x460000,
  0x486000; A4_SCRATCH_SIZE 0xAB000). pbc (C51 + PAINTCHECK + _BIG) over the race and the menus, pbm
  (+ _MUTANT): queued after the battery (pball.sh).
- Battery r51 (r51bat.sh on 5fdb6663, new model where marked): tele (TS 4, the model) framestat 2405,
  faults 1. AG28 per slot: the clear run 64.3 ms a render (966 renders, 42 windows; 3D 26.8, comp 8.5,
  read 6.6, 2D 3.3, pres 2.3), skipped 58.9 ms (989, 43; 3D 24.3, comp 8.6, 2D 3.4) -- the skip -5.4 ms
  on the model, as z1 (-5.0). Against r50c on the same model (kV1 72.5): -8.2 ms with the clear run,
  -13.6 (-19%) with it skipped. Converter installed once, 2,025 fast converts, none handed back; 1/w
  clear skipped 989, run 1,027; presents 2,026 real (49 with no render since the last), held back 0;
  drop frames 1. Chase framing as r50c (distance median 295, bearing 99% 6 max 10, reversals 6 -- r50c
  max 9, 6; car heading reversals 0). The drawn car off the game's path in height: 99% 19, max 66 (r50c's
  run 99% 5 max 8) -- seven frames, four of them one jump (the steps' y 971 -> 1005 -> 976 -> 949: the
  frames along the velocity run past the apex), the landing tail on file since r49's recorder run (99%
  15, max 95); x-z 99% 36 max 42 (r50c 39, 51). Gameplay code is r50c's (the compiled diff above).
- crash-steer (cap 85) 3,046 framestat, faults 1: reversals 0 both, height max 1, x-z max 8. Race to the
  finish (twin, cap 85) and with the exact binary and key 5 held: both reach the finish camera (0x17),
  reversals 0-1 as r50c. Menus with the garage (18 shots): the race, the pause menu and every row,
  confirm, the main menu and the garage as r50c's (sheet_r51mn.png). Mark and mark on the Veyron: run.
  Veyron autopilot (cap 85): chase 317 frames, bearing 99% 6, camera yaw reversals 3 (r50cv 7), height
  max 3, x-z max 32 (r50cv 86). Archive restored; shipchk: +3A15C B510 0004, no 0xDEADBEEF.
- The recorder twin a4_y51 (TS 4, the model) recorded nothing and wrote no telemetry: its race never
  ran, though the same binary raced to the finish at cap 85 (r51f) -- a bench-run state, not yet
  known. Rerun with a screenshot at each menu key and every 10 s (jtruns.sh / telerun4s.sh): queued.
- The painter with the big buffers (pbc), 60 s race: 37,072 calls compared, 71 not (past 153,600 bytes
  or no surface), 0 differing (0 pixels, 0 clip words).
- pbc over the menus with the garage: 498,657 calls compared, 0 not, 0 differing. pbm (one pixel flipped
  a call), 40 s race: 28,180 of 28,180 flagged. The painter is now compared on every call it takes but
  71 (no surface, or past a 320x240 surface's bytes).
- The twin's empty run, found (yd51: a screenshot at each menu key and every 10 s): under the new model
  the 185-post threshold trips at the Gameloft logo, not the menu, so the six Returns end one screen
  early -- at the race's "PRESS ANY KEY" -- and a NOKEY5 run (the autopilot drives) presses nothing more;
  r51t held key 5, which cleared it, and r51f's cap-85 timing lined up. A bench-script state, not the
  build. telerun4.sh (and telerun4s.sh): a NOKEY5 run taps 5 once after the Returns (the autopilot
  overwrites the keys at every step). The twin rerun (y51b): running.
- Twin y51b (the fixed script, TS 4, the model): recorded (88.3 MB), framestat 2,420. All cars off the
  game's path x-z 99% 26 max 107, height 99% 17 max 81, heading 99% 12 max 105 (r50c's y9c: 24/106,
  12/76, 13/104); camera (chase 1,388 frames) x-z 99% 58 max 101, yaw 99% 36 max 57 (y9c 45/90, 27/53);
  frames bearing reversals 0 (y9c 0); the game's own steps 2 (y9c 0); finish camera 92 steps, as y9c.
  Race-to-race spread of a real-time run: gameplay code unchanged. The battery is complete.
- Round 51 to the owner: r51 = 5fdb6663 (unbaked). Not a repeat: no phone round has run the 1/w clear
  skip, the hand painter, the packed list records, or an A/B that changes only stores (rounds 23/24
  stepped the hook's prefetch, the strips and the atlas; 50c the pixel loops, which change loads and
  instructions too). Rule 4 (a): the store cost (the skip's phone figure, the round's question); the
  painter's large calls and the span's callers, settled on the bench above. (b): the compiled diff
  against r50c read line by line; the painter's source by its call-for-call checks, not reread. (c):
  focus loss mid-race (GL down: the mark still runs at every hooked call, the skip needs GL up); a
  track or night race with more perspective triangles left to software (each one marks, the next
  frame clears); the telemetry ring wrapping (r51f, 330 s, wrapped).
- Owner: "send it". Sent unbaked (asphalt4_ngi_r51.exe, 5fdb6663..., 823,372 bytes). Expected, written
  before the result: nothing visibly different from r50c; on the phone ~67-69 ms a render against r50c's
  72.7 (the painter's 2D phase ~6 -> ~3 ms; the skip ~1.5-2 ms if a store costs a third of the model's,
  ~5 ms if the model is right); the skip's slots apart by that amount.

## Round 51, phone (sha256 5fdb6663...): the 1/w clear skip per window, the hand painter, packed records

Owner: "I barely noticed an improvement in framerate in certain places. Nothing else." Eight telemetry
files (phone51; captures 104-111; 2,557 race renders in the aggregates over 111 windows).
- Per slot (ms a render | 3D, composite (readback), 2D, present): the clear run 68.7 | 29.4 8.8 (6.7) 3.1
  2.4 (1,265 renders, 55 windows); skipped 67.6 | 29.4 8.8 (6.7) 3.2 2.3 (1,292, 56). The skip: -1.1 ms
  a render on the phone against -5.4 on the model, all of it outside the timed phases (3D, composite, 2D
  and present the same to 0.1). 1/w clear skipped 1,292, run 1,320; the converter installed once, 3,195
  fast, none handed back.
- The store cost, measured: 76,800 words (the 8-register STM fill of 307,200 bytes) in ~1.1 ms -- ~4.8
  cycles a word at 332 MHz, ~280 MB/s, against the model's 21 a missed store word (~4.4x dear). With
  round 20's copy (~3x) the second phone figure the same way. Rule 4 (a), settled: streaming stores are
  cheap on the phone; the model's W is to be refit, and every cut ranked by stores re-ranked.
- Whole render, both slots: 68.1 ms (r50c 72.7, r49 72.7): -4.6 (-6%). The hand painter: 2D 6.0 -> 3.1
  (the model said 6.5 -> 3.4: right for instruction-bound work). Composite 9.7 (r50c, half its windows
  in C) -> 8.8 (ARMv6 always, as 50c's ARMv6 slots). 3D 28.1 -> 29.4 and present 2.0 -> 2.4: scene and
  route (r50c's race had a crash sequence, 0x18; this one none). Outside the phases 26.9 -> 24.4.
- Frame records (the last 417 renders): render to render median 52 ms (10-90% 42-75), mean 56.3 -> 17.8
  renders a second; r50c's 54 (43-84), 60.4 -> 16.6. +7%, the owner's "barely noticed".
- Presents 3,196 real (617 with no render since the last), held back 0; drop frames 3 (r50c 10). Camera:
  chase yaw reversals 6 in 417 (r50c 11), car heading reversals 1 (1). Nothing seen by the owner; no
  pattern every 1-2 s.

## Round 52 (bench): the model refit on the phone's store cost

Owner: "yes, go ahead" (refit the model with the measured store cost, re-rank the cuts).
- The model's charge for the 1/w fill: an 8-register STM is 3 cycles past issue plus W a missed word
  (cpumodel.h: stores do not allocate), ~0.55 + W a word; the phone's ~4.75 a word puts W at ~4.
- calfit.py with W pinned (calfit2.py, r50c's sensitivities and phone phases): W 4 leaves the time
  outside the phases ~5 ms short (rest V6 -17%) and drives the read miss to the grid's edge (160) --
  W 21 had been standing in for something else. With a stretch of the CPU phases (a share of the CPU
  the bench lacks) as a fifth parameter: W 4, Thumb 1.15, ARM 1.30, R 140, the CPU phases x1.38 (a cap
  of ~239), rms 6.2% (W 21's fit 13.3%); 3D +10%, rest -6/-10%. A 28% share is large; the likeliest
  missing work is the game's audio: ngiaudio mixes in software on the phone, and on the bench every
  cubeb stream fails ("Error trying to initialize cubeb stream!": no device), so the guest never mixes.
- Emulator: EKA2L1_AUDIO_NULL=1 (a paced sink, nothing written) and the wavcap pump paced on the guest
  clock (x EKA2L1_TIMESCALE) -- EMU_PATCHES.md. au1: r51 on the round-51 model with the sink, against
  r51t without: running.
- au1 (r51, the round-51 model, EKA2L1_AUDIO_NULL), 120 s: clear 66.5 ms a render, skip 61.4 (r51t without
  audio 64.3, 58.9): the guest's audio +2.2/+2.5 ms a render, all outside the timed phases (3D 26.2/25.0,
  comp 8.6/8.7, 2D 3.5). au2 (the capture variant, 40 s race): the game's streams are sound, not silence
  -- 16 kHz mono 51.8 s and 67.9 s (rms ~3,400 and ~1,250 in the race), 24 kHz stereo 2.5 s (a jingle) --
  and each as long as the run's guest time (~70 s of a ~280 s host run at TS 4): paced on the guest clock.
  On this model the audio is ~2.4 ms of the ~5 ms the phone's time outside the phases exceeds the model's.
- The fit moved to r51's phone slots, with the audio on (calfit51.py; the skip's difference outside the
  phases, 1.2 ms on the phone, its own equation). Sensitivities from au1: aT (Thumb 1.15), aA (ARM 1.00),
  aR (read miss 140), aW (write miss 4) -- calrun51.sh: running.
- calrun51 (r51, audio on, from au1 at Thumb 1.75, ARM 1.35, R 85, W 21; 120 s each): the one-at-a-time
  sensitivities are too noisy to fit four parameters (each run a different real-time race, ~0.5-1 ms a
  phase): ARM CPI moved the clear slot's outside time 3.6 ms per unit, though the fill is ~29k ARM
  instructions (0.1 ms). The four-parameter fits went to the grid's edges (R 175-200, Thumb 4).
- W from the fill itself instead: the model charges it 9,600 STMs x (3 + 3 x ARM CPI) + 76,800 x W cycles
  -- 5.1 ms at W 21, as the model runs measured (r51t 5.4, au1 5.1). The phone's 1.22 ms (405k cycles)
  gives W 4.4: W = 4, settled.
- aW (W 4, the rest as the round-51 model): 50.1/49.4 ms a render against the phone's 68.8/67.5. Short by
  phase: 3D -28%, outside -30%/-30% (the game's Thumb code and the GL driver), 2D -12%, the composite
  loop -21% (our ARM code). W 21 had been carrying the rest of the phone's time. On the phone the GL
  driver is ARM code inside the game's process; on the bench it is the GPU model's per-call charges
  (~7 ms of the 3D phase at 3/20/150/8/400), which were never fitted.
- calrun52: at W 4, the base again (bB) and larger moves -- Thumb 3.0, ARM 2.5, R 170, and the GL
  driver's per-call/draw/vertex/bind/triangle charges doubled (bG): running.
- calrun52 (W 4, audio on; the base repeated within 0.2 ms a phase this time): d/dGL (the GL driver's
  per-call charges x2) 8.05 ms in 3D, 0.7 in the readback, ~0.5 outside -- the GL model is ~8 ms of the
  3D phase. Fit (Thumb, ARM, R, GL): 1.40, 1.85, R 200 (the edge), GL x1.2, rms 6.9%, and badly
  determined (fits within 1.5x the best error: Thumb 1.0-3.7, ARM 1.35-2.2, R 135-200): the CPIs and R
  stretch nearly every phase alike, and six phase totals cannot tell them apart.
- Where the phone's time is, from the step timer (steptime.py; header and STEPB): the game's logic step
  ~0.8 ms (r50c the same), 1.2 ms a span -- not the time outside the phases. The render call +12992:
  58.4 ms a call on the phone, of it 15.4 ms BEFORE the first triangle (r50c 15.4/15.6) -- against 8.5-8.8
  on the W-4 model (aW, bB), 10.4 on the round-51 model (r51t). That stretch, and the 3D phase (29.4
  against 21.2), are the model's two big shortfalls.
- Emulator: EKA2L1_PCPROF_ON/OFF, a gate for the exact profiler (EMU_PATCHES.md). gprof.sh: r51 on the
  W-4 model with audio, gated from +12992 to a4_gl_draw_prim, in instructions and in cycles: running.
- gprof (r51, the W-4 model with audio, gated +12992 -> a4_gl_draw_prim; 606 openings, 1.32 M instructions
  each): the stretch before the first triangle is the mesh list being gathered, not the game's logic --
  a4_mesh_insert 2.08 ms (CPI 1.76), the mesh routine +349F8 1.43 (CPI 5.2), +3118E 0.98, +523E6 0.98
  (CPI 8.7: the projection's table reads), +345E4 (the transform) 0.96, mesh_scr 0.74, memmove (the sky)
  0.70, mesh_fallback 0.54, +CE8C 0.44, memset (the 1/w clear, half the windows) 0.39, +523A2/C4 0.55;
  modelled 3.76 M cycles a stretch, more than the run's own telemetry (8.4-9.8 ms: a gap not run down --
  the gate stays open through a render with no triangle, 1.6% of them, which is not enough). Its
  memory-bound game code (the mesh routine and the projection helpers) is where the phone's 15.4 ms
  against the model's ~9 lies -- random reads of a 128 KB table, which a single read-miss price cannot
  hold together with the composite's streaming reads (2.05 ms on the phone for 9,600 lines: ~43 cycles
  a line if all miss, against R 140-200 that the fits wanted).

## Round 53 (bench): the lifted 3D front end and the corner memo, a window each

Owner: "yes, go ahead" (round 52's refit and the cuts after it). The model cannot rank this cut: it turns on
what a phone charges for the game's Thumb and its random table reads against our ARM -- the fit's
unresolved part. So it goes to the phone as an A/B inside one race, as round 51's skip did.
- A4_ABCYCLE13 (AB_FRONT 32768, 'AG29', slot 0 the game's front end, 1 the lifted one with the memo): the
  veneers at +349F8 and +349B0 call a4_engv_349F8/349B0 (a4_eng.c), which run the lifted functions
  (eng_349F8 with A4_FASTXF's hand transform, the projection helpers inline; eng_349B0) when the window's
  bit is set, else the game's own past their veneers (a4_orig_349F8/349B0, as A4_LIFTCHECK runs them).
  A4_NODIRECT_349F8/349B0: lifted callers come through the veneers. The corner memo (A4_MESHMEMO) is used
  only in the lifted windows (MEMO_ON: the game's routine bumps no generation; a corner kept under the
  last lifted call would pass for one of its). A4_ZSKIP always on (round 51 settled it), A4_ABCYCLE12 out.
- The memo's home: 0x451FFC-0x453000 belongs to the half-step machinery; a half-step build keeps it at
  0x460000-0x461004 (renderer/a4_memo.h, shared by a4_gl.c and a4_eng.h's MESHMEMO_BUMP), past the phone
  scratch, with A4_SCRATCH_SIZE 0x62000 (the injector refuses less: tested, 0x5F000 asserts) and a4_gl.c
  refusing the bench checks that keep buffers there (LIFTCHECK, CECHECK, BBCHECK, PAINTCHECK_BIG).
- r51sum.py reads AG29. Candidate r53 = C51 - A4_ABCYCLE12 + A4_ENG_349F8 A4_ENG_349B0 A4_NODIRECT_349F8
  A4_NODIRECT_349B0 A4_FASTXF A4_MESHMEMO A4_ABCYCLE13 (C53.flags), A4_HALFSTEP=1, A4_SCRATCH_SIZE 0x62000:
  sha256 8b0759f6ce3080aed30b572ee98e1ba48efb016d11a491a9cfb3ffa5216b1097, 827,844 bytes, no 0xDEADBEEF;
  veneers at +29736, +349B0, +349F8. Twin a4_y53 (+ the recorders, rec pc 700BA754).
- Checks queued (c53all.sh): lc53 (A4_LIFTCHECK on the r53 flags without the memo and the A/B, scratch
  0x7F000) and its mutant lm53; mc53 (the candidate + A4_MEMOCHECK, race and menus) and its mutant mm53;
  r53m, the candidate on the W-4 model with audio (model_r53.env, the reference from here). Then the
  battery r53bat.sh (r51's, the exact binary, the W-4 model with audio).
- Read back (rule 4 b): the sources preprocessed with C51 at 5fb6624 and C53 now, diffed: a4_gl.c the memo
  (its table at 0x460000, the generation at 0x461000, the three uses behind `memo`, the mode word's bit
  15), AGG_SLOT bit 15, the first prim's mode with ~32768, and a4_zclr_skip's condition without the
  per-window bit (the skip always on); a4_tel.c the slot, 'AG29' and the stepping; a4_tramp.s, a4_eng.s,
  a4_px.s unchanged. The engine object: the painter, eng_349F8/engi_349F8 (1.7 KB, the hand transform
  inline), eng_349B0, the two switches; a4_engv_349F8 compiled reads 0x412C00 and tests 32768. What the
  lifted mesh routine calls: +3118E as the game's Thumb (a4_eng_call0: +3118E is not lifted), the insert
  through its veneer, a4_mesh_insert; the hand transform replaces +345E4 and with it the game's three
  projection helpers (+523E6/A2/C4). A bias of the A/B named: the game's windows pay the veneer, the
  switch and a4_orig_349F8 on each mesh-routine call (~40 instructions; ~0.05 ms a frame by the call
  count), which r51's game front end did not.
- Checks (c53all.sh): lc53 (A4_LIFTCHECK at the veneers, 60 s race): 30,857 mesh-routine calls compared, 652
  not, 0 with memory differing, 0 insert lists differing; 35,104 inserts compared, 0 differing. lm53 (the
  mutant): 22,887 of 22,887 calls and 21,895 of 21,895 inserts flagged. mc53 (A4_MEMOCHECK in the lifted
  windows): race 578,014 faces, 0 fallback verdicts and 0 screen boxes differing (1,775,867 corners found
  kept); menus 211,044 faces, 0 and 0. mm53 (one coordinate wrong a face): 437,135 boxes flagged.
- lc53's r0 ([105]: 24,176 of 30,857 calls returned a different r0) -- round 50's "no caller reads it",
  now read: the mesh routine returns what its last callee left in r0 (the insert's, or the transform's
  on an early exit): a void function. Its two callers (+0CF66 in +CE8C, +34B12 in +34AEA) tail-return
  it; +CE8C's caller (+0D2F6) loads r0 at once; +34AEA's thirteen callers load or move r0 before any
  read, or (+32C8C, +514D0: virtual methods, +514D0 in a table at +B06D0) tail-return it in turn -- each
  a void function's chain, whose callers the compiler never reads r0 after. Settled.
- r53m (the candidate, the W-4 model with audio, 120 s): the game's front end 50.5 ms a render, the
  lifted one with the memo 50.1 (-0.4); 3D 20.9/20.3. Battery r53t (same model): 51.5/51.2 (-0.3). On
  the model the cut is small -- a gated profile of the candidate's pre-triangle stretch, both window
  types in one run (gprof53.sh), to see where the lifted side's time goes: running.
- Battery r53 (r53bat.sh on 8b0759f6, the W-4 model with audio where marked): twin y53 recorded (88.3 MB):
  all cars off the path x-z 99% 32 max 112, height 99% 6 max 37, heading 99% 12 max 89; camera x-z 99%
  34 max 83, yaw 99% 33 max 442 (one frame: the yaw of 442/2048 with "camera yaw less the drawn heading
  99% 925" -- a camera mode change on the route, steps mode 0x17's finish camera is 96 steps as y51b's
  92); frames bearing reversals 0. Tele framestat 2,846: chase distance 295, bearing 99% 5 max 7,
  reversals 2; height off the path 99% 5 max 9; presents 2,765, held back 0; drop frames 1. Crash-steer
  reversals 0, height max 1. Both finish runs reach the finish camera, reversals 0-2. Menus 18 shots;
  marks run; Veyron autopilot bearing 99% 4, height max 6, x-z max 110 (r51v 32, r50cv 86: a crash,
  mode 0x18 66 frames). Archive restored; +3A15C B510 0004, no 0xDEADBEEF.
- gprof53 v1 (the first draft, 8b0759f6; gated pre-triangle profile, both window types in one run): the
  stretch 11.43 ms against r51's 11.38 -- with the 1/w clear skipped always (memset -0.38), so the front
  end lost ~0.4. Per window: the lifted mesh routine (engi_349F8, the hand transform inside) 247 k
  instructions, 3.0 ms (CPI 4.0), against the game's +349F8 + +345E4 + +523E6/A2/C4 ~325 k, 3.6 ms: ~-0.6..
  -1.2. The memo: mesh_vtx 233 k instructions, 1.5 ms a lifted window, against the game windows' mesh_scr +
  mesh_fallback 1.35 ms -- MORE instructions than the projection it keeps (hashing, compares, the copy),
  so it can win only where the projection's table reads miss (the model's mesh_scr CPI 1.8: they mostly
  hit). The insert's switch: a4_engv_349B0 + a4_eng_call + a4_orig_349B0 ~0.34 ms a stretch in BOTH
  window types (every insert reaches +349B0 through a4_mesh_insert, so through the new veneer).
- Reworked: the insert not lifted (A4_ENG_349B0 and its NODIRECT out: both window types call the game's
  +349B0 from a4_mesh_insert as r51 does; A4_LIFTCHECK checks the insert only where it is lifted); and
  four slots (AB_FRONT 32768 x AB_MEMO 65536, as round 50c's: 0 the game's, 1 lifted, 2 the game's again,
  3 lifted with the memo -- MEMO_ON needs both bits): the memo gets its own answer on the phone, apart from
  the lift. Candidate r53 = eaedb84641f65e5a5141cdf9fe8ec5fb104f85f721c66c9ffedbb4d13ab56fc5, 826,896 bytes,
  no 0xDEADBEEF; veneers at +29736 and +349F8 only. Checks, r53m and the gated profile again (c53all.sh,
  gprof53.sh): running; then the battery.
- Reworked candidate eaedb846, checks: lc53 (the lifted mesh routine at its veneer, 60 s race) 56,767 calls
  compared, 1,122 not, 0 with memory or insert lists differing (r0 43,942: void, above); lm53 40,952 of
  40,952 flagged. mc53 (the memo, slot 3 windows) race 284,802 faces, menus 94,754: 0 verdicts and 0 boxes
  differing; mm53 212,843 boxes flagged.
- r53m (the W-4 model with audio, 120 s, 25 windows a slot): game 51.4, lifted 50.3, game (2) 49.7,
  lifted+memo 49.3 ms a render -- the two game slots 1.7 ms apart: a 120 s bench run does not resolve
  the cut. gprof53 v2 (gated, all four slots): the stretch 10.96 ms (r51 11.38, v1 11.43); the switch's
  own cost 0.02 ms (a4_engv_349F8 + a4_eng_call + a4_orig_349F8). Per window, from the profile: lifted
  -1.15 ms (engi_349F8 3.0 against the game's mesh routine, transform and projection helpers 4.15); the
  memo +0.9 ms over the lifted window (mesh_vtx + mesh_fallback_m 2.1 against mesh_scr + mesh_fallback
  1.2). The model's call: the lift a small gain, the memo a loss -- unless the phone's projection-table
  reads miss far more than the model's (mesh_scr CPI 1.8 here), which is what slot 3 against 1 measures.
- Battery r53bat.sh on eaedb846: running.
- Battery r53 on eaedb846 (r53bat.sh, the W-4 model with audio where marked): twin y53 recorded (88.3 MB):
  all cars x-z 99% 24 max 99, height 99% 9 max 61, heading 99% 13 max 87; camera x-z 99% 37 max 64, yaw
  99% 25 max 45; frames reversals 0; finish camera 97 steps. Tele framestat 2,854: AG29 game 51.6, lifted
  49.3, game (2) 50.0, lifted+memo 50.2 ms a render; chase bearing 99% 6 max 9, reversals 1; height off
  the path max 19; presents 2,958, held 0; drop frames 0. Crash-steer reversals 0. Menus 18 shots, marks
  run, the Veyron (bearing 99% 2, height max 4, x-z max 44). +3A15C B510 0004, no 0xDEADBEEF.
- Two finish runs differ from r51's. r53f2 (the exact binary, key 5 held, cap 85): its last captures are
  a second race segment (664 spans) -- the race finished and another began; r51f2's finish fell in its
  last captures. r53f (the twin, no key, the autopilot, cap 85): every frame of all eight captures at one
  position, (22729, -23087), mode 2, with game steps running -- not the finish (r51f and r50cf cross it
  near (19-24k, 44-46k)): the car stands still mid-route. Under CPUCAP=85 (instructions, no cycle model)
  r53's fewer instructions make the race run faster (7,937 steps in the run against r51f's 6,285), so the
  runs see different moments; but a standing car is a question. r53fs (the same run with a screenshot at
  each key and every 10 s) and r51fs (y51, the same script: the build against the script's NOKEY5 tap,
  new this round): running.
- The finish runs, settled: r53fs (the same y53 run again, screenshots every 10 s) reached the finish (its
  last frames in the finish view at (17807, 46936)). r51fs (r51's twin y51, the same script) stood still
  from ~h240 to the end at (-4821, -7893): on lap 3, 0 km/h, the car facing the camera -- the autopilot
  holds accelerate and never steers, so a crash that leaves it facing a wall leaves it there
  (sheet_r51fs.png). A route state of the autopilot run, in both builds; not the build. (r53f2's second
  segment: its race finished early -- this build plays faster under an instruction cap.)
- Read back (rule 4 b), the reworked candidate: the compiled diff against r51 -- the aggregates' slot
  (bits 15-16, mask 3), the memo gated on both bits (0x412C00 & 0x18000 == 0x18000), its table at 0x460000
  and generation at 0x461000, the first prim's mode with ~(32768|65536), a4_tel.c's stepping through 0..3
  and 'AG29', a4_zclr_skip's per-window bit gone; the engine object the painter, eng_349F8 and the switch
  a4_engv_349F8 (a4_ab_l349F8, the lifter's renamed entry, unused); no +349B0 code.
- Round 53 to the owner: r53 = eaedb846 (unbaked). Not a repeat: no phone round has run lifted game code
  in place of the game's own (the painter that shipped in r51 is a hand-written one) nor the corner memo,
  nor an A/B of two changes crossed in one race (50c stepped the pixel loops and their prefetch, which
  are one loop each). Rule 4 (a): the phone's price of the game's Thumb and the projection table's reads
  against our ARM (the round's question; the model, with no instruction cache and one read price, cannot
  give it); the trampoline into the game's routine and the lifted routine itself, settled by lc53
  (56,767 calls through the same trampoline, 0 differing); the bss 12 KB larger (0x62000), entered on
  the bench by every run. (b): above. (c): focus loss mid-race (the routine's choice is per call, from
  the mode word; nothing persists across windows but the memo's table, keyed by its generation, which
  the game's routine never bumps -- and the memo is used only when the lifted routine ran this call);
  other tracks and night races (more faces in view: the memo's 128 entries collide more, a cost, not a
  fault -- each hit compares the exact coordinates).
- Owner: "send it". Sent unbaked (asphalt4_ngi_r53.exe, eaedb846..., 826,896 bytes). Expected, written
  before the result: nothing visibly different from r51; the whole render about as r51's (~68 ms; the
  1/w skip always on, ~-0.5 against r51's mix). Per slot, the model's call: lifted ~-1.2 ms against the
  game's, the memo ~+0.9 over lifted. If the phone's pre-triangle shortfall (15.4 ms against the model's
  ~9) is the game's Thumb or the projection table's misses, lifted gains more than 1.2 and/or the memo
  turns to a gain; if neither moves beyond the slots' spread (~1.5 ms), the shortfall is elsewhere
  (the instruction cache on code we did not touch, or the GL driver).

## Round 53, phone (sha256 eaedb846...): the lifted mesh routine x the corner memo, a window each

Owner: "this is the very first time where I see the game running truly smooth, even during traditionally heavy
scenes ... 2nd batch felt the same. There was a particularly slow crashing scene but other than that, it felt
closer to what I envisioned." Two races, eight files each (phone53a: captures 109-116, 2,668 race renders over
116 windows; phone53b: 116-123, 2,829 over 122).
- Whole race (all slots): 65.4 and 65.7 ms a render -- r51 68.1, r50c 72.7 (-3.7%, -10%). 3D 28.0/28.1 (r51
  29.4), outside the phases 25.4/25.6 (r51 26.8), composite 8.8/8.9, 2D 3.2/3.1; before the first triangle
  13.7/14.5 ms (r51 15.4). Frames (the last 417 of each race): intervals median 52/55 ms, 10-90% 41-77 and
  44-70, 99% 119/115 (r51 42-75, 124; r50c 43-84, 126) -- the slow tail shorter, the owner's "smooth".
- Per slot, both races (59-60 windows each): the game's 64.9 and 67.7 ms (outside 25.1, 26.3 -- the two
  identical slots 2.8 ms apart whole, 1.2 outside: the resolution); lifted 64.2 (outside 24.6); lifted+memo
  65.5 (outside 26.0). The lift ~-1 ms outside against the game's slots (the model: -1.15); the memo
  +1.4 over the lift (the model: +0.9). Within the spread, each; both with the model's sign and size.
  Rule 4 (a), the round's question: the phone's pre-triangle shortfall against the model is NOT the
  game's Thumb or the projection table's reads -- had it been, the lift would have gained more than the
  model's figure and the memo would have won. The model ranks these two as the phone does. The shortfall
  is elsewhere (code we did not touch, the instruction cache, the GL driver).
- The crash: batch A's crash camera (0x18, 62 frames) ran at interval median 43 ms, 90% 63, max 119 --
  quicker than r50c's crash (48, 81, 150). Its slowness is the game's own slow motion in the crash view,
  not the frame rate.
- Batch B's last windows (116-121): the present phase 0.0-1.0 ms against the usual 2.1-3.4, with the
  presents a window as ever (69) -- around the finish view (mode 1 in its last captures). Not run down.
- Converter installed once a race, none handed back; 1/w clear skipped 2,689/2,850, run 34; drop frames
  5/4 (r51 3); held back 0.
- Next: the lift always on, the memo out. Where the time is now (per the gated profile, the model):
  our own list insert a4_mesh_insert (~430 k instructions a frame, ~2.2 ms), the game's +3118E (131 k), and
  the 3D phase, whose phone/model gap (28 against ~21 ms) sits where the GL model's charges are (~8 ms of
  the model's 3D): the number of draw calls a frame (~170-230).

## Round 54 (bench): the r53 baseline and the next cut

Owner: "continue".
- The GPU-presents design reconsidered (the largest block left on the phone: readback + composite +
  present ~11 ms a render): settled against in the early rounds on the phone -- the game's runtime posts
  its frame by direct screen access, and an EGL window surface of ours never showed over it (round 1's
  video: no magenta sentinel anywhere, the DSA's frame and our window fighting at ~1.2 s); the pixmap
  surface probe was refused on the N95. The readback composite into the game's own buffer came out of
  that. Reopening it means taking the screen from the runtime's DSA session (cancelling it, owning the
  window, the focus and redraw paths) -- a project, not a cut; put to the owner separately.
- r54b = C53 - A4_MESHMEMO - A4_ABCYCLE13 (C54.flags: the lifted mesh routine always, A4_NODIRECT_349F8,
  A4_FASTXF; the 1/w skip always), A4_SCRATCH_SIZE 0x5F000: f9254eec..., 825,196 bytes. Whole-frame dual
  profile on the W-4 model with audio (dprof54.sh, 1.5e9 instructions after 6e9): running.
- Profile of r54b (dprof54: the whole frame, 1.5e9 instructions after 6e9, the W-4 model with audio; the
  model's render 48.5 ms): cycles % / instructions % / CPI -- engi_349F8 (the lifted mesh routine) 10.5 /
  6.4 / 4.0; a4_cvt565_run 9.4 / 6.0 / 3.8; a4_mesh_insert 7.3 / 10.5 / 1.7; a4_comp_row 6.6 / 8.5 / 1.9;
  lean_tail 5.2 / 7.2; the painter 4.7 / 6.8; +3015E 4.4; mesh_draw 3.9; batch_draw 3.5 (CPI 3.8);
  +33EA8 3.4; mesh_fallback 3.4 / 3.8; +CE8C 2.8; memmove 2.7 (CPI 18); +3118E 2.4; mesh_scr 2.3 / 3.1.
  Our code ~28% of the frame; the list insert with its two helpers ~13% (~6 ms on the model).
- The insert's cost: ~584 k calls in the window (its entry block, 9 instructions, credited 5.26 M), ~285
  instructions a call in a4_mesh_insert, ~80 in mesh_scr, ~97 in mesh_fallback -- ~460 a face, ~2,400
  faces a frame. Its hot blocks: the determinant (+33C, values reloaded from the stack), the record's
  stores (+39C: spilled values), the box and depth range (+60C). Two causes read in the code: the
  perspective class's corners projected twice (mesh_fallback's three mesh_sy, the box's three mesh_scr;
  a corner's screen y does not depend on its x) and every field of the list's state through a volatile
  pointer (struct mesh, struct glr: each to memory and back).
- A4_TAKE2 (a4_gl.c): the insert again -- each corner projected once, the fallback on the box's screen
  y's, the centre and focal read once a face, the list and GL state through plain pointers; the same
  side effects in the same order (the fallback's count before the coordinate checks, the back-face
  count before the record). The old one is mesh_take_ref. A4_TAKECHECK (a4_mesh_insert ->
  mesh_take_check): every face through both, the list's state put back between, the return, the
  counters and the slot's every word compared (census [100] faces, [101] return/counters differing, [102]
  slots differing, [103] slots compared); its mutant flips a record bit every 256th face. Builds: t54
  (C54 + A4_TAKE2, 824,628 bytes), t54c, t54m. Runs (t54all.sh): the check over a race and the menus,
  the mutant, an instruction profile of t54: running.
- t54 v1 (each corner projected once, plain pointers): t54c over a race 2,050,179 faces, 0 returns or
  counters differing, 0 of 1,200,893 slots; menus 1,400,527 faces, 0 of 682,607; t54m 3,033 slots flagged
  (one in 256). Instructions a call (pt54i, the entry block's count / 9): 446 -> 413 (-7%) -- the new
  one projected every corner before the back-face test, so the 0x4000 class's back faces, which the old
  one never projected, were projected and thrown away.
- t54 v2: the coordinate checks and the back-face test before any projection (their results do not depend
  on the fallback's, which only picks the depth class); mesh_scr's body inlined (mesh_scr_i). The only
  difference left is fellback, a count nothing reads (grep: written, never read), now of the faces kept;
  the check no longer compares it. t54c race: 1,758,056 faces, 0 differing, 0 of 1,045,395 slots; t54m
  3,054 flagged. 446 -> 314 instructions a call (-30%): ~0.32 M instructions a frame at ~2,400 faces, ~1.6
  ms on the model. What is left is mostly spill traffic (two dozen live values; the hot blocks load and
  store sp-relative) -- hand assembly could halve it again.
- Our code by instructions (pt54i): the insert 17.4%, a4_comp_row 14.9%, lean_tail 12.2%, the painter
  11.8%, a4_cvt565_run 11.6%, engi_349F8 9.7%, mesh_draw 7.6%, batch_draw 3.8%, draw_prim_take 2.8%.
  lean_tail (the table path, round 21's): ~255 k calls in the window, ~1,200 triangles a frame, ~420
  instructions a triangle -- the insert's pattern again (volatile GL state, the texture lookup and the
  list-cover test per triangle).
- Next safe cut, the blended classes' second drawing: KIND_ADD/KIND_ALPHA triangles go to the GPU and to
  the game's span (lean_tail returns 0), because over the backdrop (dst alpha 0) only software's copy
  shows. The sky routine +35FDA (disassembled): r4 the framebuffer; a top fill (stm, row 0's middle
  pixel) to the band's first row, the band a memmove a row (+36120, or +3614E/+3615A where it wraps),
  then, when its stacked flag [sp, #0x70] is set, a bottom fill to the end of the screen. All its band
  paths join at +3616A. The question for a cut: below the band, is every pixel a blended triangle
  covers drawn by an opaque GPU class? Then software's copy there is always overwritten.
- A4_SKYCEN (bench, with A4_ROWSTAT): a4_sky_tramp (a4_tramp.s) at +3616A does the two instructions it
  replaces and notes r4 and the framebuffer; lean_tail notes each blended triangle, and the boxes of
  those starting at or below the band in a bitmap (0x460000, A4_SCRATCH_SIZE 0x63000, the injector
  checks); the composite's row pass counts uncovered pixels above and below the band and uncovered
  pixels inside the candidates' boxes (what a skip could lose; a box over-covers its triangle).
  Census 0x412D00 [40..57] (a4_gl.c). Build s54 = C54 + A4_TAKE2 + A4_ROWSTAT + A4_SKYCEN; runs: a race
  (60 s) and the menus (s54.sh): running.
- s54 (the census, race 60 s / menus 30 s): the band's first row below it is row 171 on average (max 190)
  -- the backdrop is most of the screen, the ground below; 1,012 race frames all with a band. Pixels no
  opaque GPU class covered: 13,602 a frame above the band row, 13.3 at or below it (in 301 frames).
  Blended triangles 29,081 (28.7 a frame); 6,404 (22%, 49% of their box area) start at or below the
  band. Candidate-box pixels left uncovered (what the skip could lose; a box over-covers its triangle):
  373 in 21 frames. Menus: band row 171.2, 39% of blended triangles below it, 152 pixels in 8 frames.
  A4_SKYSKIP (the skip, kept in the tree, built as k54/k54c) is not exact and takes a fifth of the
  triangles: not a candidate.
- The exact form of the same cut: the backdrop drawn by the GPU. If the sky routine's top fill, band and
  bottom fill go to the GPU at the frame's start (the band 1:1 from the sky image as a texture, the fills
  flat), every pixel carries an opaque GPU colour, and software's copies of the blended classes and of
  the additive billboards (+3015E, the scaled sprite blit, 4.4% of r54b's cycles) are overwritten
  everywhere -- they and the software sky (memmove 2.7%, +35FDA 0.6%) can all be left out: ~12% of the
  model's frame (+33EA8 3.4, +3015E 4.4, +2E56C/+33D18/+2F358 ~0.8, memmove + +35FDA 3.3). The game's
  math stays its own: tramps at the band's memmoves and the two fill loops record what they would write.
  What it changes: a KIND_ALPHA board over the sky is the GPU's blend (premultiplied, 8 bits) instead of
  the span's -- the deviation it already has over the other 75% of the screen. To settle first (s54b):
  the image's size (a texture is at most 1024 wide), how often the bottom fill is set (without it the
  rows below the band are the last frame's), and whether anything opaque is still left to software in a
  GPU frame (it would need the backdrop in software).
- s54c (census v2 fixed; race 40 s, menus 30 s): one sky image, 512 x 128 (565), the same pointer all
  run; the bottom fill set in 125 race frames of 690 (18%; menus 124 of 492). Non-blended triangles left
  to software: none in either run (s54b and s54c). So the rows below the band keep the last frame's
  pixels in most frames, and the GPU's backdrop covers rows [0, top fill + band) only.
- A4_GPUSKY (a4_gl.c, before draw_prim_take; a4_tramp.s; the injector checks the five sites): the band's
  three BLXs to the memmove veneer (+36120, +3614E, +3615A), the top fill's end (+3609C) and the bottom
  fill's colour (+3619C) through tramps; a frame's first store decides (GL up, no GPU frame begun, the
  surface the last frame composited into, a drawn-mask, a 320 x 240 screen, an image <= 1024 a side,
  even width), then each store is recorded and skipped. At the frame's first triangle, after the clear and
  before the mesh list: the image as a 565 texture (uploaded when its pointer, size or a 64-texel sample
  changes), the band's runs as 1:1 quads (texel edges on pixel edges, NEAREST), the fills as flat quads,
  drawn as their own batch. A layout it does not expect replays the recorded stores in software and the
  frame is software's; so does anything that would draw into the surface before a GPU frame began (the
  hook leaving a triangle, a billboard, a fill, the flush). Blended triangles and additive billboards
  (modes 2/3) whose rows lie within the backdrop's rows are the GPU's alone; ones reaching below keep
  software's copy (the old rule). In a GPU-sky frame a triangle software would draw alone forces the
  composite first. Census [67] GPU-backdrop frames, [68] software ones, [69] forced composites.
  A4_SKYCHECK (bench): the routine's stores also run; after the backdrop is drawn the GPU frame is read
  back and compared pixel for pixel with them ([50..54]); the surface's rows are hashed then and again
  at the composite ([55] rows software changed in between: they would be lost); mutant flips one pixel
  before each test every 64th frame. Builds g54 (C54 + TAKE2 + GPUSKY), g54c, g54m: running.
- g54c (the check, race 60 s): 1,057 frames with the GPU's backdrop, 40 left to software (the decision's
  reasons: the first frames, the race's start), 0 composites forced; the backdrop read back against the
  routine's own stores: 0 pixels differing in 1,057 frames, 0 backdrop pixels without the drawn mask.
  Menus 30 s: 526 frames, 0 differing. g54m (mutant): 10 of 10 flipped pixels found. The row test as first
  written counted the rows of the copies kept by design (blended triangles reaching below the backdrop)
  -- 102,192 rows in 861 frames, not separable; v2 masks those rows (g54c2/g54m2), then the W-4 model A/B
  t54 against g54b (gperf54.sh): running.
- A4_ABCYCLE14 (AB_GPUSKY 131072, 'AG30', r51sum.py NAME30): the sky alternates per window, for a phone
  A/B if the round wants one (A4_ABMODE 134190u = 3118 + AB_GPUSKY). Builds: ab54.
- g54c2 (v2 row test, race 60 s): the backdrop 0 pixels differing in 1,058 frames (menus 550: 0); g54m2
  12 of 12 flips found. But rows software changed between the first triangle and the composite, with
  the copies kept by design left out: 76,204 in 803 frames (~95 a frame; menus 28,888 in 311). An
  unaccounted writer during the 3D pass; whatever it writes into the backdrop's rows would be lost.
  Not run down yet.
- gperf54 (W-4 model with audio, 150 s each, separate runs): t54 47.0 ms a render (3D 19.1, comp 7.1),
  g54b 49.8 (3D 23.0, comp 7.2). The GL model's own line shows the sky texture uploaded every few frames
  (10-34 KB a frame on average, 0.3-1.0 ms of upload charges; t54 0): the image changes, or the 64-texel
  sample misfires. The rest of the 3D gap may be the two runs' scenes; an in-run A/B (ab54) is the
  measurement. Diagnosis builds: g54d (+ census [58] uploads, [59] of them the same image changed, [62]
  the image, [63] the surface) with a write watch on the image; g54t (A4_SKYTRACE: in every 16th checked
  frame the backdrop's rows re-hashed at every hook, so a change is pinned between two hooks): running.
- g54d (race 40 s, census): one sky upload in the whole run (the image's pixels never change after its
  load: the write watch on it shows only the loader +362EC/+36574 and +35FAC, which ORs 0x20 into every
  texel once at load). The model runs' extra GL cost is not the sky: over the whole run g54b made 220
  draws a frame against t54's 172 and uploaded 22.9 KB a frame against 1.8 -- not explained yet; the
  in-run A/B (ab54) is queued.
- g54t (A4_SKYTRACE, race 90 s): 21 intervals with backdrop rows changed by software, 14 between the draw
  hook's exit (the first: a shadow, flags 0x20, r 1 -- the GPU's alone) and the flush (the HUD's first
  call), 7 after a billboard; the first one 96 rows from row 19. So something the game draws after its
  last triangle and before the HUD, through none of our hooks, covers most of the backdrop's rows. In
  the old design it showed only where the backdrop showed (the composite overwrote it everywhere
  else); with the GPU's backdrop it would show nowhere. A write watch on screen rows 30-31 (g54w)
  names it: running.
- g54w (a write watch on screen rows 30-31, each writer once): in the race, the sky's top fill (+360A2,
  the software-sky frames), a4_span_transparent, the perspective span +333F4 (two PCs), the alpha span
  +33EA8, text (+2C524, the HUD), and the composite. No new unhooked writer by name; the perspective and
  transparent spans run only for triangles left to software, of which the GPU-sky frames have none at
  the hook ([69] 0) -- so either they run on a path that does not pass draw_primitive's hook, or the
  changes are not at rows 30-31. g54t2 records the changes' shape (32-pixel segments: the first event's
  box, the union) to tell; then the in-run model A/B ab54: running.
- g54t2 (the changes' shape): the first event x 32-319, rows 19-136, 166 of its 32-pixel segments; over
  the run rows 0-174 across the whole width (19 intervals with changes in 16 traced frames, 13 between
  the last triangle -- a shadow, the GPU's alone -- and the flush, 6 after a billboard). Something the
  game draws over most of the screen between its last triangle and the HUD, through none of our hooks,
  and the row-30 watch did not name a new writer for it. Not run down.
- ab54 (the in-run A/B, W-4 model with audio, 150 s, 64 windows a slot): sky in software 49.4 ms a render
  (3D 22.2, comp 7.2), the GPU's backdrop 49.8 (3D 22.8, comp 7.2) -- no gain. Why the estimate was
  wrong: the composite now writes every backdrop row the memmove wrote (the same stores, moved), and
  without the bottom fill (82% of race frames) the blended copies that reach below the band stay. With
  the unexplained writer on top, the GPU backdrop is shelved: A4_GPUSKY, A4_SKYCHECK, A4_SKYTRACE,
  A4_ABCYCLE14 stay in the tree, in no candidate. What the work established: the game's sky can be
  drawn on the GPU pixel-exact (0 of 1,583 frames differing), and the software sky costs about what
  the composite would spend on the same pixels.
- Next (our code, by instructions in pt54i): lean_tail ~420 instructions a triangle (~1,200 a frame),
  the insert's pattern (volatile GL state, the texture lookup and the list-cover test per triangle) --
  the same treatment as TAKE2, with a per-triangle check against the old path.
- The lean path without volatile (A4_LEAN2, its GL state through a plain pointer): lean_tail 2,256 ->
  2,240 bytes, the rest unchanged -- the volatile is not where its ~420 instructions a triangle go (three
  vertex records, the descriptor, the texture check, the conversion, the list-cover test, the 20-odd
  words it writes). Reverted. A full rewrite of it is worth ~0.4 ms on the model at most.
- Where the round stands: one safe cut ready, A4_TAKE2 (exact; the insert -30% instructions, ~1.6 ms of
  the model's ~48). Tried and dropped this round, with numbers: the skip below the band (not exact), the
  GPU backdrop (exact, no gain), the lean path's volatile (no change). Round 24/25's atlas already showed
  fewer draw calls are no gain on the phone. What is left of our own code is small cuts of <1 ms each;
  the large blocks are the readback, composite and present (~11 ms a render on the phone) and the game's
  own front end -- the first is the screen take-over the owner deferred. Put to the owner.

## Round 55 (bench): taking the screen -- the design from what the runtime does

Owner: "go with 2" (the screen take-over, the only lever left worth ~10 ms).
- What the record already says (rounds 1-2, phase 1): a GL window of our own in front of the game never
  showed on the phone (no magenta in any frame, a fight at ~1.2 s); the redraw-window/cone handle dilemma
  (WSERV 4/6, CONE 46) was solved on the bench only by a backed-up window, which the phone did not present.
  The runtime posts through a DOUBLE-BUFFERED CDirectScreenBitmap (scdv: Create(rect, EDoubleBuffer),
  BeginUpdate/EndUpdate with the driver's own transfer; round 29 notes), so an EGL swap competes with the
  DSB's flips -- the likeliest reading of round 1.
- The runtime's own GL path (ngiOpenGLControl.dll, read here; source rank 1): it dlopens libGLES_CM and
  resolves ~45 EGL/GL entries BY ORDINAL (eglCreateWindowSurface = 6 first); it takes the window from a
  control through a virtual call (slot 0x24), SetExtent()s it (ws32 277) and makes the EGL window surface
  on it; around that it calls runtime export 40 -- which stores its caller as the display's one owner and
  refuses with an NGi error if another holds it -- and export 41 to give it back. So the runtime arbitrates
  one display client at a time; a GL title holds the display instead of the back buffer. The NGi API
  reference (archived CHM) is not reachable from here (web.archive.org blocked by the egress policy).
- Two routes:
  A (direct present): at the present, glReadPixels the GPU frame straight into the DSB's hardware buffer
  (cvt_fast already receives it: 320 x 240 XRGB words, its stride, the window's offset), the GPU frame
  rendered flipped so its rows come out top-down, BGRA read = XRGB words on the N95. The 565 buffer holds
  only the 2D layer (cleared to a key at the frame's start) and goes on top: the painter's translucent
  pixels over the GPU's area replayed against the read pixels in order, then the opaque 2D pixels copied.
  Gone: the composite pass, the full 565->32 convert, the software sky (A4_GPUSKY, exact), every software
  copy of the blended classes and billboards; the readback overlaps the 2D phase. Estimate ~5-6 ms a
  render on the phone. Mostly benchable (the bench runs the same convert path).
  B (EGL swap): A's 2D layer drawn on the GPU (the 18 painter sprites as quads, the text, the minimap) and
  the DSB stopped, the runtime's display owned (export 40) and an EGL window surface swapped: removes the
  readback too (~10 ms) -- several phone rounds, and the device's EGL present is unproven.
  A is B's first half (the GPU sky, the isolated 2D layer, the per-frame fallback) and is done first.
- A's open questions, for the bench: (1) every writer of the 565 buffer between the frame's 3D and its
  present and whether each blends with what is under it (the painter does, 16 alpha levels; text, the
  minimap +26E28/+2B5A0, the unexplained writer found in round 54's trace); (2) the bench's read format
  (the N95's is BGRA/UNSIGNED_BYTE, round 10); (3) the frames A cannot take (a triangle or billboard left
  to software, holes below a band without its bottom fill: filled with the fill colour the routine would
  have used -- stock shows the last frame's pixel there) fall back to the current path for that frame.
- wr55 (t54, race 60 s, a write watch on the whole 565 buffer armed by the first composite, each writer
  once): after the race's first composite the buffer is written by the composite and the painter (ours),
  the sky (+360A2/+361A4, euser memmove), the billboard blit +30472, the blended spans (+33DC0, +33F66), and
  the game's 2D families: +10A96/+10AD4/+10B1A, the minimap +26E5A..+2713E, a shape family +2B896..+2BC1E,
  fills +2AB84, text and the speedometer +2BE72..+2D136. Read at each store (capstone, a4code.bin): the
  minimap +26E5A loads the pixel and stores its average with a colour (a 50% blend); +270C2 and +2BC1E
  (via +52E5C) alpha-blend with it; +2C524 adds to it saturating per channel; +2D09E stores (3x + y) / 4;
  +10A96 and +2B896 store opaquely. The painter itself blends 16 alpha levels.
- So the game's HUD reads the 3D under it: the 3D must be in the 565 buffer before the HUD draws -- what
  the readback and composite at the HUD's first call are for. Route A's isolated 2D layer (the buffer
  cleared to a key, the HUD laid over a frame read straight into the DSB) would blend those pixels with
  the key: not viable. Route B needs the same pixels on the GPU, so every one of those game routines
  redrawn there (the painter's 18 sprites, text, the speedometer, the minimap, fills) -- or a readback
  anyway. Either way the readback the take-over was meant to remove is required by the game's own 2D,
  unless its whole HUD moves to the GPU.
- Found on the way, for later: glCullFace = libGLES_CM 46, glFrontFace = 64 (epoc9.def's list, which
  agrees with every ordinal the phone already resolves: 1, 6, 22, 28, 30); the bench answers the read
  format query as the N95 (BGRA/UNSIGNED_BYTE, emulated).

## Round 55, route B (owner: "go with 1. Let's take the gamble. Adjust the bench and emulator to reduce the quantity of rounds on hardware")

What the N95's own binaries say (source rank 1, the RM-320 ROM; tools: romimg.py, scratchpad romdis.py --
every BL/stub target named lib:ordinal across the ROM's export tables):
- **How the N95 puts an EGL window on the panel.** The PowerVR stack is libgles_cm, libimgegl (EGL),
  libpvrtechviewwsegl (the window-system layer), pvr2d, pvrsrv, pvromaplcd. The WSEGL layer's import stubs:
  ws32 233/234 (CDirectScreenAccess::NewL/StartL), 387 (RWindowBase::Size), 68/70/76, 184/185, 446; cone 182
  (CCoeEnv::Static: the app's own session); fbscli and bitgdi; pvr2d 1-19 (the 2D blitter). So a window surface
  is posted by a DSA session of the EGL layer's own on the window, a pvr2d blit into the screen and the screen
  device's update -- a direct-screen client like the runtime itself.
- **The runtime's DSB on the N95** (ROM scdv, vtable 8087ECD0): [3] Create (+0x0C; flags & 3 must be 1 or 2,
  queries HAL 0x54/0x4F/0x50/0x4E and a display-driver object at [this+4]), [4] BeginUpdate (+0x10), [5]/[6]
  EndUpdate (+0x14/+0x18), [7] Close (+0x1C: the driver's +0xFC, its session ended). The kernel driver's flips
  cannot be read from user code; the DSB being open is the likeliest reason round 1's GL window never showed.
- **The back buffer object** (ngibackbuffer, ARM code; the X10 cvt_fast already reaches): +0x1C its
  MDirectScreenAccess (the DSA observer), +0x24 the display (vt+0x28/+0x2C: session, screen device), +0x28 its
  RWindow (embedded: the DSA window), +0x54 the CDirectScreenAccess, +0x68 the CDirectScreenBitmap, +0x31 the
  display-owned flag; it takes the display with runtime export 40 and gives it back with 41 (+28C/+2B8).
- **EKA2L1's DSB** (src/patch/scdv/src/cdsb.cpp, swapped in on 3.1+): one buffer, the screen; EndUpdate posts
  through dispatch update_screen at once; Close does nothing. update_screen draws the posted buffer over the
  composed screen; an EGL window is composited by the window server on its redraws.

The plan, in order (each step benched before the next; the phone sees one probe, then the candidate):
1. Probe build (bench first): in races, windows cycling three variants -- (v1) the runtime's presents skipped,
   its DSB closed, our frame swapped through an EGL window surface on the runtime's own window (X10+0x28, the
   app's session); (v2) as v1 with the runtime's DSA cancelled as well (the configuration a GL title has: one
   window, the EGL layer's DSA only); (v0) normal. Each variant marks its frames (a coloured bar and digit) so a
   video or the owner's eye names it; telemetry carries every call's return, EGL errors and the swap's time.
   Giving the screen back (DSB Create with the rect it had, DSA StartL) is checked at every variant change.
2. The bench made to show what the phone would: a DSB open holds the panel (wserv's composites and EGL
   posts not shown while it is open); Close releases it (EMU switch, EMU_PATCHES.md).
3. With the probe's answer: the HUD on the GPU (the painter's 18 sprites as quads, text, speedometer,
   minimap, fills -- each a GL blend the routine's own maths defines), all benchable pixel by pixel against
   the software HUD, then the candidate.

### Round 55, route B: the runtime never uses its DSB (bench trace + its own constructor)

- **The bench first** (EKA2L1_PCTRACE over ngibackbuffer 0x71004000-0x71006000, PCMEM at the lock
  +4EA0; t54, 10 s of race): the back buffer's DSB object ([X10+0x68], the emulator's stub, vtable
  0x84305F18) is never Created -- [X10+0xAC] stays 0 over 2,817 locks. Every lock runs +4EA0 -> +4F70
  -> +4F8C ... +4FEC -> +5244 -> +4F50: its 4th argument (r8) is set, which skips the Create (+4FF4
  `bne +5244`) and returns the fallback target [X10+0x90].
- **Why** (ngibackbuffer, ARM code): the lock is called from +3000/+351C/+3844/+551C with r3 =
  [bb+0xA4]; bb's constructor (+1464) sets [bb+0xA4] = (flags & 2) >> 1; its only caller, the factory
  +5A0, passes flags = `mov sb, #3`, a constant. So bit 1 is always set: **the runtime never takes
  its DSB path, on the bench or the phone** (the same DLL, installed by the game's SIS). Round 8's
  "the runtime already asks for double buffering" read the Create call, not whether it is reached.
- **What it does instead**: [X10+0x90] is the screen's own memory -- +4A44: BITGDI 53 on the DSA's
  screen device [X10+0x48] (CFbsScreenDevice::HardwareBitmap), SCDV 10 GetInfo into [X10+0x6C], its
  address [X10+0x70]; with none, the HAL screen address + 0x20 (the bench's 0xC9200020). The convert
  (+CB70, ours as cvt_fast) writes the frame straight into it; the unlock (+5280 -> +52F8) then calls
  BITGDI 58 (CFbsScreenDevice::Update(const TRegion&)) with the DSA's region [X10+0x58], and, with a
  pacing interval [X10+0x60], arms an RTimer (euser 930 on X10+0x64) and SetActive (1090) -- the
  fixed period each present stretches to (round 16). A plain DSA: the frame buffer is shared with the
  window server, whoever writes last is what the panel shows.
- So the step-2 premise ("a DSB open holds the panel") does not apply to this runtime, and the bench's
  present path is the phone's (the same lock, convert and Update; EKA2L1 draws an Update's buffer over
  its composed screen, the phone's panel shows the shared frame buffer). The emulator switch built for it
  (EKA2L1_DSBHOLD, below) stays as a general one, off by default; it changes nothing here.
- Round 1's GL window (our own window over the runtime's, A4_WINDOW) not showing on the phone is then
  most likely the runtime's own writes: A4_WINDOW skipped the game's present but not the framework's
  post (+606/+6AC/+6B4, round 37), whose convert writes the whole frame buffer -- over the pixels the
  EGL layer had just blitted -- and Updates it.
- Route B's probe, simplified (no DSB to close): (v0) normal; (v1) the runtime's presents skipped (the
  game's and the framework's), the frame through an EGL window surface on the runtime's own window
  (X10+0x28), the runtime's DSA left running; (v2) as v1, with the runtime's DSA stopped at the
  window's start by its own +597C (X10 vtable [7]: waits out a DSB post if one is open, Closes a
  DSB if created, Cancels the DSA) and restarted at its end by its own +567C (vtable [6]: Restart).
  Both entries checked by address (X10[0] = base + 0x23C3C, base from it) and by a hash of their code.

**EKA2L1_DSBHOLD** (emulator, this round; general, not used by this runtime): while a guest
CDirectScreenBitmap is open -- from the emulator's DSB stub's Create to its Close or destruction, watched
at those guest addresses by a host hook at dyncom block entry (common::g_pc_hook, new; the stub's
offsets checked against its code and vtable at load, and a stub loaded twice watched at both) --
every window-server redraw ends with the last direct-screen post drawn over it. A model for a
phone whose display driver shows only the DSB's buffers while its session is open; unconfirmed on
any device.

### Round 55, route B: the probe build (rb55 = C54 + A4_TAKE2 + A4_RBPROBE + A4_ABCYCLE15)

- Built from the stock exe (a4/x_02_Game) with the current tree and A4_SCRATCH_SIZE=0x63000: rb55 827,172
  bytes; its baseline rb55base (C54 + A4_TAKE2) 823,732. The first build at the default size (0x22000)
  died in the menus (rb55a: KERN-EXEC 3 reading 0x447060 -- the half-step snapshot's block, past a
  scratch that ended at 0x423000); C54's blocks run to 0x460000, and t54 had been built at 0x63000.
  injector.py now refuses those flags (A4_MESH, HALFSPEC, SLOWREC, FRAMEREC, KINEDRAW, ENGINE) below
  0x5F000.
- What it does (a4_gl.c, A4_RBPROBE; MBX above for the why): the variant (PF_MODE bits 18-19) steps 0, 1, 2
  every RB_WINS = 4 telemetry windows (TEL_EVERY 23 renders a window: ~6-8 s a variant on the phone).
  v0: the runtime presents, a white 48x6 bar top left drawn into the frame. v1: at a present with 3D that
  the game would make, the finished 565 frame (3D and HUD) is uploaded (glTexSubImage2D, 320x240 into a
  512x256 565 texture of a context of our own on an opaque 565 window config), drawn into an EGL window
  surface on the runtime's own RWindow (X10 + 0x28), a red bar over it, and swapped; the game's present
  and the framework's post (a4_fw_hold) are skipped; presents with no 3D are skipped too (the screen keeps
  our frame), up to 30 in a row -- then the screen is given back (a menu). v2: as v1, a blue bar, and the
  runtime's DSA stopped by its own +597C when we take the screen (if [X10+0xCC] says it is started),
  restarted by its own +567C when we give it back (variant change, menu, failed swap). Each use of a
  runtime object checked first (bb as cvt_install; X10's vtable = text + 0x23C3C; the observer vtable
  0x28 after it; vtable [6]/[7] = +567C/+597C; 25 words from +597C hash 98EED16D, 64 from +567C
  CF0BA489, both from the game's own ngibackbuffer.dll); a failed check refuses the probe (state = step).
- Record (RBP, 0x412A00, 'RBP1', in every capture after FRC2; tools/r51sum.py prints it): state and
  why, per variant: 3D presents, frames through EGL, swaps failed, frames the runtime presented, empty
  presents held, framework posts skipped, upload / draw+swap / make-current ticks and maxima, screen
  taken / given, owned frames with the runtime's DSA flag off, changes of that flag.
- What the owner's eye answers (the bench cannot): in red and blue stretches, is the game on the panel and
  moving, or frozen / black / flickering? White stretches are the runtime as before.
- Bench, function (no model; rbrun.sh = telerun4 with a window screenshot every SNAP s): the second rb55a
  never left variant 0 in the race -- the frame start set PF_MODE = A4_ABMODE every frame (the chain of
  A4_ABCYCLEn branches had no 15); fixed (the variant bits kept). Then rb55a: v1 230 frames through EGL,
  v2 184, no swap failed, every check passed (an opaque 565 window config, X10 0x00D0D708, ngibackbuffer
  text 0x71000000), the DSA stopped and restarted by the runtime's own code. But the screen was taken and
  given back 229 times in v1: with its presents skipped, nothing paces the game's loop, which presents ~30
  times between two renders, and 30 empty presents was the menu test. Now by time (RB_IDLE_MS 400 with no
  3D). rb55b: taken 3 / given 2 in v1, 2 / 2 in v2; empty presents held 87,090 (v1) where the runtime
  re-presented 8,248 (v0): the loop spins faster when a present costs nothing, at the same 23 renders a
  window.
- The window snapshots: the EGL frames (a red or blue bar) whole, HUD included, upright. The runtime's
  frames are upside down in the bench's window -- as in every window screenshot of this ROM since October
  (blit_ck2_45.png): EKA2L1 draws a direct-screen post rotated 180 for this screen mode, the window server's
  composite not. Not the phone's (the owner plays upright), so the bench's two paths disagree with each
  other where the phone's agree; the probe's GL is the standard top-left ortho over a texture whose first
  row is the frame's top.
- One more path closed after reading the code back: a 3D frame drawn to another target (the garage's car)
  while the probe owned the screen went to the runtime but left the framework's posts skipped; now any 3D
  frame that is not the screen's gives the screen back first.
- **The phone build: rb55, sha256 9e654f3840ed06866d2b9ed51ac8a58cdb6bc4eba6cc665c2b798ea5069edb93**
  (827,180 bytes; C54 + A4_TAKE2 + A4_RBPROBE + A4_ABCYCLE15, A4_SCRATCH_SIZE 0x63000, from 1cfe68a + the
  garage give-back). Battery on this exact image: rb55c (race 60 s, no model): v0 276 3D presents, v1 230
  through EGL, v2 184, no swap failed, taken 3/2, stops 2 = restarts 2, faults 1 (the known ngilightserver
  one); rb55mn (race 30 s, then pause menu, quit to the main menu, garage): every 3D frame of the race's v1
  and v2 stretches through EGL (92 + 92), the pause and the menus back to the runtime (given 1 + 1, restart
  1), the garage's 3D frames to the runtime, the probe not owning at the end; screenshots right (direct-
  screen ones rotated 180 as this bench always shows them). The model run rb55m (W-4, 90 s) was on the
  image before the garage give-back: EGL frames 368 (v1) and 276 (v2), upload 4.6 ms, draw + swap 8.6-9.2
  ms, make-currents 0.15 ms a frame against the runtime's present 7.7 ms -- the model's charges, not the
  phone's; the probe measures the phone's.
- Rule 2 -- not a repeat of: phone round 1 (a4_backup565 / bf605bc4: a backed-up window of our own over
  the runtime's, the runtime's DSA live and its framework post not skipped -- the content never won, the
  ~1.2 s fight) nor its untested opaque-565 rebuild; nor rounds 2-54 (every one presented through the
  runtime's DSA). No round put an EGL surface on the runtime's own window, stopped all of the runtime's
  writes, or stopped its DSA.
- Rule 4 (a) guesses: (1) that the N95's EGL accepts the runtime's RWindow (in the app's session) for a
  window surface -- the probe's question; refused cleanly if not (state 13), runtime presents on. (2) that
  WSEGL's post shows with the runtime's DSA live (v1) or stopped (v2) -- the question. (3) that the runtime's
  stop/restart behave as when the window server calls them -- they are its own code, entered as its own
  observer thunks do (this in r0), hashed; the bench ran both 2-3 times a race. (4) the swap's real cost --
  measured, not assumed. (5) ordinals 6/22 (eglCreateWindowSurface/eglSwapBuffers): round 1 used them on
  the phone. None can be settled on the bench: its EGL composites through the window server, not WSEGL.
  (b) Read back after writing: the whole a4_gl.c probe block, its three call sites (bring-up note, frame
  hook, a4_fw_hold), the a4_tel.c stepping, slot and record write, r51sum.py's reader against the struct
  layout; the X10 RunL (+55EC: a deferred Update only, at most one stale flush after taking the screen).
  (c) States the bench did not enter: WSEGL's own DSA (the bench's EGL has none) and a window-server
  abort/restart of the runtime's DSA caused by it -- counted (owned frames with its DSA off, flag changes);
  a failed swap (give back, the runtime presents that frame); an incoming call or the screen saver while we
  own the screen; a refusal at any check step (each leaves the runtime presenting, with the step recorded).
- What the owner does: one race (any track, a few laps), then quit to the menu; telemetry C:\a4tel0..7.dat
  back. What to look at: the top-left corner bar -- white = the runtime presenting (as before), red = our
  EGL present, blue = EGL with the runtime's DSA stopped. Each colour lasts ~6-8 s in turn. In the red and
  the blue stretches: is the game moving normally, or frozen / black / flickering / torn? Any difference
  in smoothness between the colours?
- Sent to the owner unbaked (rb55, sha256 9e654f38...edb93) for baking. The owner usually finishes a race
  and quits the game straight to the N-Gage hub: that is fine. The probe's per-variant totals and the
  aggregates are cumulative in every capture, and the game's own menus before quitting already exercise the
  give-back. What matters is a race long enough for each colour to come round more than once (a lap or
  more).

## Phone round 55 (rb55 9e654f38, route B probe): EGL frames shown; controls dead and the race slow in red/blue

Owner (N95, one race, quit to the hub): "During the red/blue stretches, the controls stopped responding. I
didn't notice any difference graphically speaking. However the race ran the slowest I've ever seen. Notice
how I said the race and not the game overall."
- **Answered: an EGL window surface on the runtime's own window reaches the N95's panel**, in v1 (runtime's
  DSA live) and v2 (stopped): the red and blue bars are drawn only by the probe's GL, and the picture under
  them looked as the runtime's. Round 1's "never shown" was the runtime's own writes, as read above.
- **Not answered: the cost.** The eight files sent are not rb55's: every header is an older build's (modes
  3118/1070/558/46/35886 -- rounds 19-53 -- formats AG20/AG29, one file version 3, sizes 154,288-171,048);
  none has the 'RBP1' record or rb55's size (171,560). rb55 did write: the variant only steps after a
  capture, and a capture needs every ring file Replaced first (a failed Replace stops the telemetry for
  good, and the owner would never have seen red). So C:\a4tel0..7.dat on the phone should be rb55's now,
  unless another build has run since. Asked for them again (date and size to check).
- Read of the slowness, pending those files. Not the skipped posts: the framework's post (+6B2's blx r2) is
  ngibackbuffer +3178 on bb itself (bench regs: r2 = 0x71003178, r0 = bb 0x00D0E900, 2,754 calls in ~10 s):
  its own vtable [0x58] call, runtime export 40 (the display owner), the core post [0x38] (lock, convert,
  unlock), the buffer index; its unlock arms RTimer::HighRes (euser 930, the def names checked in
  kernelhwsrv's euseru.def) and SetActive, and blocks nowhere. On the bench, input kept working through the
  red and blue stretches (the held accelerator: 166 km/h in a blue one) and the W-4 model held 85.3 ms a
  render in all three variants. So the likelier cause is the phone's EGL path itself: the upload (153,600
  bytes of 565 into a 512x256 texture, which MBX's driver twiddles on the CPU), the swap (WSEGL's DSA, the
  pvr2d blit, the update), or the two eglMakeCurrent a frame between the pbuffer's context and ours. A frame
  over ~427 ms (5 steps, the clamp at +15B1C) makes the race itself slow down -- the owner's "the race,
  not the game". The files' per-variant upload, draw + swap and make-current ticks say which.
- **The files (second send): rb55's** -- 171,560 bytes, 'RBP1' in each, captures 64-71 (v1 64-67, v2 68-71;
  the race ran caps 0-71, six cycles of the three variants). tools/r51sum.py and the frame record:
  - Probe: state up, every check passed on the phone (X10 0x0070ABB8, ngibackbuffer text 0x78CBC000, one
    opaque 565 window config); v1 552 and v2 552 frames through EGL, no swap failed, no make-current failed;
    taken 6 / given 6 (v1), 6 / 5 (v2; the race ended in v2), the DSA stopped 6 and restarted 5 times.
  - **Phone costs a frame: the upload 37.9 ms (max 84), draw + swap 11.9 ms (max 36 in v1, 16 in v2),
    the two make-currents 0.15 ms.** The runtime's present in v0: 2.5 ms; readback + composite 8.8.
  - Renders: v0 84.8 ms (529 renders: at the game's 85.33 ms step), v1 109.6, v2 103.4 -- the upload's
    38 ms less the convert it replaced. Logic: 11.5-12.4 steps a second in every v1/v2 window (the 11.72
    the step is): the race did not slow by its own clock. **[Wrong -- phone round 56: rb55 was built without
    env A4_HALFSTEP=1, so each 85 ms step was a half-step and the race ran at half speed in every variant.]** Frame record (417 renders, 2.3 s blocks): the
    car's speed 1,537-2,145 units/s and its heading changing in v0 and in the EGL blocks alike -- the game
    took the owner's keys in red and blue. What the owner saw as dead controls and the slowest race is the
    picture: ~9.5 frames a second against 11.8, and possibly the EGL frames reaching the panel late (WSEGL
    queues its posts; nothing in the telemetry sees after the swap returns).
  - v2 made no empty presents at all (v1 2,392 held, v0 3,577 runtime presents): with the runtime's DSA
    stopped the race loop's presents stop short of our hook. Not followed: see below.
- **Verdict on route B: it does not pay on the N95.** The probe uploaded the finished frame (its 38 ms is
  the probe's, not route B's), but the swap is route B's: 11.9 ms a frame for one textured quad -- WSEGL's
  post (its DSA, the pvr2d blit, the update) and the tile render. What route B would remove is the readback
  and composite (8.8) and the runtime's convert and Update (2.5): 11.3 ms. A swap of a real 3D frame costs
  that 11.9 plus whatever of the 3D's tile render the readback now absorbs. So at best even, before the cost
  of moving the HUD to the GPU (the painter's sprites, text, speedometer, minimap -- the game's 2D blends
  with the 3D, so the HUD has to move with it). Route B shelved; A4_RBPROBE stays as a bench and probe
  build only.
- Where the frame time is (v0, this race, at the 85 ms gate): 3D 32.9, composite 8.8 (read 6.8), 2D 3.3,
  present 2.5. The visible rate is the logic step's (11.72) because one render plus the step does not fit
  twice in 85 ms; frames between steps (the half-step machinery, on in 3118) need the render under ~40 ms.
  The 3D phase is the work.
- **Correction (owner): the controls did NOT work in red/blue.** "I was clicking them and the car didn't
  respond until it went to white. Not even the pause menu would pop in until it was on white. It was as if
  the controls were being queued." The frame record's turning was the road and the traffic, not the keys:
  my "the game took the owner's keys" above is wrong.
- **Why, from the binaries.** The race loop +808 never returns: it runs `b +838` until r4[0x1C] (quit) is
  set. Its "presents" (+1CE, the call our a4_gl_frame hook sits in) go to [r4+0x20]->vtable[5]: the object
  is runtime.dll's (bench: object 0x00D34488, vtable 0x70103448 = runtime text +3448), and vtable[5] is
  runtime +16BC: with arg1 = N it arms a timer of N x 1000 us on its own active object, then loops
  CActiveScheduler::Current() / RunIfReady (the stubs +248C, +246C, +237C) until that timer's object has
  run. It is the game's **yield**: the only place the race lets its active objects run -- the window
  server's events (keys, the pause menu), its timers. DoPresent was never a present; the screen write is
  the framework's post (+606 -> +6B4 -> bb vtable [0x34] = ngibackbuffer +3178). rb55 returned "presented"
  from a4_gl_frame for every DoPresent while it owned the screen, so no yield ran: keys queued until white.
  The bench missed it: its accelerator was held down before every stretch began (rule 4c's "a key held
  down", exactly). Every earlier build that skips a DoPresent (A4_SKIPREPRES's +950/+97E, A4_DROPHOLD's
  holds) skips yields too; they leave enough of them that nobody noticed.
- So the accounting above is wrong in one line: "pres" (2.5 ms in v0) is the yield's wait, not the runtime's
  convert and Update -- those run inside the framework's post, which no timer of ours separates. What route
  B would remove is readback + composite (8.8) plus that unmeasured post; what it adds is the swap (11.9,
  measured with the default swap interval -- if it waits for the panel's refresh, eglSwapInterval(0) would
  change it) plus the HUD on the GPU. **Route B is not settled; un-shelved pending a second probe.**

## Round 56 (owner: "think through this closely ... reduce uncertainty to a minimum, then plan accordingly and execute")

Facts settled before building, each with its source:
- **DoPresent is the runtime's yield** (binaries + kernelhwsrv's euseru.def): runtime +16BC = RTimer::After(arg1 x
  1000 us) (euser 928) and CActive::SetActive (1090) on its own object, then a loop of RThread::RequestCount
  (1788) and CActiveScheduler::Current (427) / RunIfReady (422) until that object has run; Cancel (1088) if
  not. The race loop (+808, `b +838` until r4[0x1C]) never returns to the scheduler otherwise. Its sites in a
  race: +950 (-1 -> [this+0x80] ms) and +97E (10 ms) -- skipped since round 19 (A4_SKIPREPRES) -- and +9A6
  (10 ms, after the render): the one yield a race iteration keeps.
- **The framework's post is the one screen write**: +606 -> +6AC/+6B4: bb vtable [0x34] = ngibackbuffer +3178:
  bb vtable [22] (+4204, a getter), runtime export 40 (the display owner), [0x38] = +34A4 (lock +4EA0, the
  convert, unlock +5280: CFbsScreenDevice::Update and RTimer::HighRes + SetActive for pacing, no wait), the
  buffer index. Skipping it skips nothing else. +6B6 only pops; its caller reads no register. +606 runs every
  loop iteration, so the runtime converts and Updates an unchanged frame several times a render (phone v0:
  3,577 converts in a session whose race had 529 v0 renders) -- a cost of its own, not yet measured.
- **eglSwapInterval is libGLES_CM 133** (RM-320 ROM): libGLES_CM's EGL exports are forwarders in one block
  (0x804BFC4C-0x804BFCD4) holding exactly 1-25 and 133, 173, 174, 175; each `b` to a veneer into libimgegl,
  whose 32 exports are EGL 1.0's 25 one to one (2-25 checked) then 29-32; 173 -> 29, 174 -> 30, 175 -> 31,
  133 -> 32 -- EGL 1.1's four in alphabetical order (BindTexImage, ReleaseTexImage, SurfaceAttrib,
  SwapInterval), as the emulator's table names them. imgegl 31 takes four arguments (SurfaceAttrib), imgegl 32
  two: it reads the current context's draw surface, clamps the interval to the config's
  EGL_MAX_SWAP_INTERVAL (0x303C) and EGL_MIN_SWAP_INTERVAL (0x303B), and calls the window-system layer's
  vtable [0x20](surface, interval). eglGetConfigAttrib = 9 (-> imgegl 9).
- The bench had no eglSwapInterval (dispatch 0x1184 registered as a comment) and refused the two swap-interval
  config attributes: added (EKA2L1: accepted, a no-op; range 0..1).

The probe, second form (rb56; A4_RBPROBE rewritten, record 'RBP2'): DoPresent's call is never changed. Four
variants, each RB_WINS = 4 telemetry windows: v0 the runtime (white bar; every post timed, in a4_fwpost_tramp
-> a4_fw_done); v1 EGL at swap interval 1 (red); v2 EGL at swap interval 0 (blue; the config's range recorded);
v3 the runtime with a post skipped when no new frame came since the last post made, while a race runs (yellow).
Owned (v1, v2): every framework post skipped, the screen given back after 400 ms with no 3D, at a 3D frame to
another target, at a failed swap, at a variant change. The DSA stop/restart of rb55 is gone (it answered: the
EGL frame shows with the runtime's DSA live). A4_RBCHECK (bench): each skipped post's frame hashed against the
one on the screen (the last upload, or the last post made). New bench test (keyrun.sh / keytest.py): in a race,
in each variant, the pause key pressed and the time to the pause menu measured -- run on rb55 first, where it
must fail in red and blue (the test of the test).

### Round 56 bench battery on rb56 (b2101970), before the finding below
- Test of the test (keyrun.sh/keytest.py, pause key in a race, time to the pause menu): rb55 fails as it must (red: no
  menu within 6 s; blue ~6 s); rb56 white 0.36-0.78 s, red 0.78-1.20 s, blue 1.20 s.
- A4_RBCHECK (rb56chk): every post skipped in v1 (12,349) and v2 (12,587) held the frame last uploaded, every one in v3
  (13,342) the frame last posted (0 different). v3 made 372 posts for 368 renders against v0's 6,429 for 460. The
  config's swap interval range read 0..1; set without refusal. Bench costs, no model: v0 post 2.08 ms; v1 draw+swap
  11.3 ms, v2 11.0 (the bench's interval is a no-op).
- Menus (racemenu.sh, the pause menu walked, main menu, garage): clean.

### Pausing in an EGL stretch opened the N-Gage application (bench) -- the pause key's long press
Seen first in the key test (kt56b, kt56c): F1 in a red stretch started ngiplay0x20003b78.exe instead of the pause menu.
New test (keyseqrun.sh / keyseq.py): wait for a colour (two polls), release 5, tap F1 once, 20 screenshots, resume,
three times. rb56, red: 9 of 12 runs opened the N-Gage application (ks56, ks56r/s/t, ksqc, ksg1, ksia, kswa, ksca),
always at the first tap; white: 0 of 5 runs (ks56w, ks56u, ksf1-3). The chain, each link from a run:
- The application is started by playserverccd.dll in the PlayServer process (ks56r: RProcess::Create, lr 0x71F00687).
- log-ipc (ksia): the last request before it is a blind Send 0x400003 from the game to PlayServer, just after key
  events. 0x400003 is playclientgss export 8 (+2A2: Send(0x400008 - 5, n)). ngigamesystemservices.dll (one export; the
  N-Gage notification banner: tick vtable[6] +5C0 -- elapsed time, 200 ms clamp, 0.7 s slide -- and draw vtable[7] +648,
  both called by the game every loop iteration at +66A/+676 before the framework's post) reports events with it:
  vtable[12] +8CC(n) -> playclientgss 8(n).
- The game calls vtable[12](0) at +AB2, in the race loop, when the byte [0x1F0029C + 0x180] (bench 0x1F0041C) is set
  (+A96). WATCH (kswa): it is set by +9A60.
- +9A60 is in the update of game state 12 -- racing (+9A10; ksf3: 5,743 of 6,974 state updates): a count at
  [r6 + 0xE94] (0x1F0131C) dropped by one per call (+9A4C), the flag set when it runs out. State 25 (+9A7E) has the
  same count. WATCH on the count (ksca): armed to 24 by +1326, disarmed (-1) by +13A8; in the launch it ran out.
- +1326 is the key-down handler: game key bit 0x1000 (the left softkey -- the pause key) arms 24; +13A8 in the key-up
  handler disarms it. **Holding the pause key for 25 race-loop iterations is the game's long press, and it opens the
  N-Gage application** -- a stock feature. In ksca the first tap's count reached 5 before its release; the third ran out.
- Why only in owned stretches: the count is iterations, not time. A race iteration with no render makes the framework's
  post and nothing else (A4_SKIPREPRES took the +950/+97E yields in round 19); skip the post and the iteration costs
  almost nothing. rb56b's race run: v0 9,386 posts for 460 renders (20 iterations a render), v1 117,066 posts skipped
  for 460 (254 a render), v2 114,015, v3 123,175 for 368. Inside a bench tap (xdotool: down and up a few ms apart)
  the count fell 19 times in ksca's first tap and ran out (25) in its third; rb56b's record holds 39-112 drops
  (counted + held back) over the taps of a run -- the bench's taps sit on the edge of 25.
- The phone: round 55's v0 windows made 3,577 posts in ~45 s, ~80 iterations a second, so the long press there is
  ~0.3 s. A post costs ~9 ms of the ~12.5 ms iteration (comp 8.8), so without it 25 iterations are ~0.1 s: a tap.
  **rb56 as built would likely have opened the N-Gage application at the owner's pause in red, blue and yellow.**
  rb55 could not show it: it queued every key until white.
- A false lead on the way: launches stopped for three runs (ks56v, ksqa, ksqb) and a stored state (a pending N-Gage
  message) looked likely; ksqc launched again. Chance.
- A limit of the probe, not fixed: a notification banner in an owned stretch is drawn (+676) into the back buffer that
  is not posted, so it is not seen.

Fix (A4_LONGPRESS, rb56b): +9A4C and +9A84 (each `subs r0,r0,#1 ; str r0,[r7,#0x14]`, a `bhs` after) become a BLX to
a4_lp_tramp (a4_tramp.s): a4_lp_count (a4_gl.c) lets the drop count at most once per A4_LP_MS = 12 ms (the v0
iteration on the N95); a drop that does not count stores nothing and leaves C set, so the bhs goes on as for a count
not run out. The last drop's time is in struct glr (A4_PERF is full); the probe record counts drops counted, held back
and run out (lpn, lpheld, lpfire; r51sum prints them). The long press keeps its stock length whatever an iteration
costs. Not bench-only: any route B build needs it.

Bench, rb56b (3d626bf1, 827,600 bytes; rb56 rebuilt without the flag is byte-identical to b2101970):
- Red taps (ksb1-6): 18 taps, 18 pause menus, 0 launches (record: counted 1-3, held back 38-109, run out 0).
- A 1 s hold (keyseq.py HOLDF1=1.0): red (ksbh1) and white (ksbh2) both opened the N-Gage application -- the long
  press is kept. White taps (ksbw): pause menus, no launch.
- Key test (kt56d): white 0.38 s, red 0.81-0.82, blue 0.79-0.80, yellow 0.79-0.82 to the pause menu; no launch.
- Race run (rb56bR, 150 s): checks 0 different in every variant; v0 post 2.10 ms; v1 draw+swap 10.26 ms, v2 9.68.
- Menus (rb56bmn): the pause menu walked, its confirm, main menu, garage: clean, no launch.
- N95 model (W-4, m_rb56b, 150 s): every variant at 85 ms a render (the step's gate); checks 0 different.
  **[Misread -- phone round 56: 85 ms a render with one step each is the half-speed symptom of a build without
  A4_HALFSTEP=1; round 54's model runs of C54 made 46-51 ms a render and ~23 steps a second.]** v0 4,466
  posts for 460 renders (1.00 ms each); owned v1 167,496 posts skipped for 460, v2 133,241 for 414, v3 152,744 for
  368 (~360-415 iterations a render). Upload 4.61 ms, draw+swap 9.00 (v1) / 8.86 (v2). **The model is far from the
  phone here**: round 55 measured upload 38 ms and a post ~9 ms on the N95, the model 4.6 and 1.0 -- glTexSubImage
  and the runtime's convert are not in it. Its route B numbers are not to be read as the phone's; this round's
  record is what calibrates them.

Rule 2 -- the phone round this asks for (rb56b) is not a repeat of phone round 55 (rb55: EGL frames with DoPresent's
yield skipped, keys queued, no swap interval 0, no v3) nor of any bench row above: it is the first with the yield kept,
swap interval 0 (v2), the runtime's re-posts skipped (v3), the posts timed, and the long press held to its stock length.
The bench rows it rests on and does not repeat: kt56/kt56b/kt56c (time to the pause menu per variant -- never what a tap
opened), ks56* and ksq/ksg/ksi/ksw/ksc (rb56: the launch found and traced), ksb1-6/ksbh1-2/ksbw/kt56d/rb56bR/rb56bmn
(rb56b).

Rule 4, before it goes out:
- (a) Guesses. (1) A4_LP_MS = 12 is the N95's v0 iteration, from round 55's post count over its v0 windows' length, not
  from a stock build; the round's record (lpn/lpheld/lpfire) will show how many drops the owner's taps make -- the bench
  cannot model an N95 iteration. (2) State 25's count (+9A7E) is the same as racing's: same code, same object; never
  entered on the bench. (3) Nothing else the owner would see counts race iterations: a race iteration in an owned
  stretch runs ~12x as often on the bench; the screenshots of red, blue and yellow stretches showed nothing wrong, which
  is not proof. (4) A human tap is under ~0.25 s.
- (b) Read back: the built image disassembled at +9A46, +9A7E and a4_lp_tramp; a4_lp_count; the injector's two byte
  checks; r51sum's record words 26-28 against struct rbp's field order; rb56 rebuilt without the flag, byte-identical.
  Remembered only: nothing in the diff.
- (c) Not entered on the bench: a human-length press (80-300 ms) at the N95's iteration rate; a press across a variant
  change; state 25; a long press during yellow (v3); the drop clock at its bss zero (the first drop counts).

Phone round 56: rb56b (3d626bf1, 827,600 bytes, unbaked) sent to the owner on approval. Asked: steering and a tap of
pause in every colour (a long hold opening the N-Gage application is the game's own), any visible difference between
white / red / blue / yellow; C:\a4tel0-7.dat (171,560 bytes each) back.

## Phone round 56 (rb56b 3d626bf1): the whole game at half speed -- rb55, rb56 and rb56b built without A4_HALFSTEP

Owner (N95, eight files): "There was no difference between colors. They all played awfully: sluggish, heavy, as if the
game was dragging along a heavy weight. Even the race countdown was taking longer to complete (about 2 seconds to pass
from one countdown to another)."
- The files: captures 69-76, all rb56b (171,560 bytes, format AG31, 'RBP2'). (My first read took older uploads of the
  same names -- the copy globbed every a4telN upload; redone with the eight named.)
- **The cause is my build, not the probe.** The countdown at 2 s a number is the game's clock at half speed. Since
  round 31 the step is halved in the image by env A4_HALFSTEP=1 (+52B98, +52B9C, +15F28: 85.33 -> 42.67 ms), and
  A4_HALFLOGIC runs the game's whole step on every other step "so the game keeps its speed" (a4_gl.c, +1271). The
  record's build lines carry it through round 53 (C53.flags, A4_HALFSTEP=1, scratch 0x62000); rb55's, rb56's and
  rb56b's commands did not: each 85 ms step a half-step, a whole step every 171 ms. The numbers said so and I read
  them as a gate: phone round 53 (correct) 4,240 steps over 2,722 renders, ~24 a second (half-steps in real time),
  63-69 ms a render; rounds 55 and 56 1,979 and 2,102 steps over 1,687 and 1,798 renders, ~11.7 a second, 85-116
  ms. On the bench the same: round 54's model runs (m_t54, m_g54b, m_ab54) 46-51 ms a render, 23-28 steps a window
  of ~1.1 s; rb55m and m_rb56b exactly 85 ms, one step a render. So round 55's "the race did not slow by its own
  clock" and "the visible rate is the step's" are wrong (marked there), and neither round's "how it plays" says
  anything about route B: every variant ran the same half-speed game.
- Guard (tools/injector.py): A4_HALFLOGIC without env A4_HALFSTEP refuses to build (and A4_HALFSTEP without
  A4_HALFLOGIC: double speed). Checked: rb56b's own command now fails; with A4_HALFSTEP=1 it builds rb56c
  (3d84ab81, 827,600 bytes; 8 bytes from rb56b, the three step words).
- **What the round still measured** (costs a call, not of the game's speed):
  - eglSwapInterval works on the N95 (the config's range 0..3; set 5 times, refused 0) and **changes nothing**:
    draw + swap 12.07 ms at interval 1, 12.04 at 0. The swap does not wait for the panel. Round 55's open question
    ("if it waits for the panel's refresh, eglSwapInterval(0) would change it") is answered: it does not.
  - The upload: 37.45 / 37.61 ms a frame (round 55: 37.9). The make-currents 0.12-0.14.
  - **The runtime's own post: 2.84 ms a call** (v0, 3,654 posts, max 33). In a correct-speed race the loop makes
    ~1.3 posts a render (round 53: 3,455 converts for 2,722 renders), so skipping the re-posts (v3) saves under
    1 ms a render; v3's 42,392 skipped (108 a render) are the half-speed build's spinning loop.
  - Pause key: drops counted 21, held back 89, run out 0 -- no tap became a long press. But the iteration rate
    A4_LP_MS was sized on (round 55's v0, ~80 a second) was a half-speed build's; at round 53's ~20 posts a second
    the stock long press is over a second and the 12 ms floor does not bite. It matters only where posts are
    skipped (route B).
- **Route B, settled on two rounds' per-call costs**: presenting through EGL costs the upload (37.5 ms) and a swap
  (12.0, whatever the interval) against the runtime's post (2.84) plus the readback and composite route B would
  remove (8.8). Without the upload (the 3D drawn into the window surface, the HUD moved to the GPU) the swap alone
  is about what it removes -- the round-55 estimate stands. Route B shelved; A4_RBPROBE and A4_LONGPRESS stay as
  bench and probe options.
- Rule 4 lesson: a build's recipe is more than its flags file. An environment switch (A4_HALFSTEP) outside
  C54.flags was dropped between rounds 54 and 55 and two phone rounds ran a different game. The injector now
  refuses the mismatch; and a number exactly equal to a constant (85.3 ms = the step) is a symptom to explain,
  not a gate to accept.
- Confirmed by measurement (W-4 model, m_rb56c, 150 s): rb56c -- rb56b's flags with A4_HALFSTEP=1 -- renders at
  47.3 ms (v0), 53.0 (v1), 53.7 (v2), 46.3 (v3), 2,960 renders and 3,500 steps (window 123: 31 steps in 1,333
  ms, 23.3 a second: half-steps in real time), against rb56b's 85 ms and one step a render. Round 54's correct
  builds made 46-51. At the right speed v0 posts ~2 a render (1,467 for 736) and v3 saves only those.
- Next: route B is closed. The main line's candidate is round 53's baseline (lift on, memo out) with round 54's
  exact cut A4_TAKE2, built with A4_HALFSTEP=1 -- put to the owner.

## Round 57 (owner: "build it, test it and send it ... explain why it's closed ... consider other alternatives")

Build recipe slips found on the way (both now refused by the injector): rb55/rb56/rb56b lacked env A4_HALFSTEP=1
(above), and were built on the pristine game image (9b9e6d10, 764,856 bytes) instead of the handoff's baseline
(4baef63a, 765,940: general-v1, the fill and textured patches) that every candidate through round 53 used. The
injector prints its input's hash and refuses the pristine image in a GL build unless A4_PRISTINE_OK=1.

Candidate r57: baseline + C54.flags (round 53's without the memo and its A/B) + A4_TAKE2 (round 54's exact insert,
-30% instructions), A4_HALFSTEP=1, A4_SCRATCH_SIZE 0x62000 (round 53's). sha256
2f07552e22eefcbdef827ea68ea87b05b8c042c28e6d8c39da2576ab4b3bce2e, 824,836 bytes, no 0xDEADBEEF. Its recorder twin
y57 (+ A4_CARREC A4_YREC A4_AUTOPILOT), 825,852 bytes. Battery r57bat.sh (round 53's, renamed) on 2f07552e:
- Twin y57 recorded (88.3 MB): all cars x-z 99% 25 max 96, height 99% 8 max 42, heading 99% 10 max 43; camera
  x-z 99% 36 max 55, yaw 99% 26 max 35; frames 1,405 chase, reversals 0 (r53: 24/99, 9/61, 13/87; 37/64, 25/45).
- Tele, W-4 model, 120 s: framestat 2,937; 48.6 ms a render over 2,369 renders (r53's slots 49.3-51.6), 3D 21.4,
  comp 7.2 (read 5.7), 2D 2.7; render call 39.0 ms, 7.2 before the first triangle; window 100: 23 steps in 998
  ms (half-steps in real time). Drawn speed against its neighbours 10-90% 0.96-1.04, 0 off by 25%; chase
  bearing 99% 5, camera yaw and car heading one-frame reversals 0; presents 3,030, held 0, drop frames 0.
- Crash-steer (cap 85): one camera-yaw flick of 6/2048 in 417 frames (r53: one heading-less-yaw reversal) --
  the run-to-run level. Finish (twin, cap 85): reached, 195 finish-view frames, reversals 0. Exact binary, key 5
  held, 300 s: racing to the end, 0 reversals of note.
- Menus (racemenu: the pause menu walked, its confirm, main menu, garage): 18 shots, faults 1 (the boot fault).
  Marks and marks on the Veyron run. Veyron autopilot: three 1,023/2,048 yaw jumps, each exactly at the game's
  own cut between mode 2 (chase) and mode 6 (the wrecked-car view looking back; the car stuck at x ~26,400) --
  the game's cuts, not frames of ours. +3A15C b510 0004 (no bench unlock), no 0xDEADBEEF.
- Rule 2: not a repeat -- no phone round has run TAKE2, nor round 53's code without the memo and its A/B; and it
  is the first correct-speed build since round 53 (rounds 55-56 ran at half speed).
- Rule 4: (a) nothing new is a guess in code: TAKE2 was checked exact in round 54 (1,758,056 faces, 0 differing;
  its mutant flagged 3,054); its phone gain (~1.6 ms on the model) is the estimate the round measures. The
  recipe is checked by the guards and by the model's rate. (b) Read back: the build log (input 4baef63a, the
  step halved), the C53 -> C54 flag diff (only the memo and its A/B gone), shipchk. (c) Not entered: the phone
  itself; a telemetry ring with no A/B (r51sum labels its one slot with round 50's names -- cosmetic).
- Sent to the owner (approved in advance: "build it, test it and send it"): r57 = 2f07552e, unbaked.

### Why route B is closed (per-call costs, phone rounds 55 and 56)
- What it removes from a frame: the readback (6.7-6.9 ms), the composite's loop (~2), the runtime's post (2.84 a
  call, ~1.3 a render at the right speed): ~12-13 ms.
- What it costs: eglSwapBuffers 12.0 ms a frame at interval 1 and at 0 alike (not the panel's refresh: WSEGL's post
  and the tile render), plus getting the game's 2D onto the GPU -- an upload runs at ~4 KB/ms (37.5 ms for a frame),
  and drawing the HUD on the GPU means taking every 2D blitter of the game (text, minimap, speedometer, sprites).
- So at best even before the HUD. The one unmeasured part: whether the swap's 12 ms is the ARM working or waiting.
  If waiting, a second thread could hide it -- which matters only inside a pipeline where the GPU draws everything.

### What is left that has not been tried, ranked by what it could change
Where the phone's frame is (round 53, the right speed, 65 ms a render): before the first triangle 13.7 (the game's
front end), the 3D pass ~28 (the game's per-object work, our hook and insert, GL at ~31 us a call + 1.5 us a
triangle ~8), the composite 8.8 (read 6.7), the 2D 3.2, the rest (logic, posts, yield) ~10. 30 fps is 33 ms: half.
No trim of our own code gets there; only removing whole blocks of the ARM's work does.
1. **Let the GPU transform the meshes (the VGP).** The N95's OMAP2420 has the full MBX with the VGP vertex
   processor (TI's brief; 3DMark06 2.36 Mtris/s) and nothing uses it: the game transforms every vertex on the ARM
   (+345E4), the static objects are unpacked every call (+CE8C), and every face is culled, inserted and drawn per
   face. The mesh list already gives the GPU view-space vertices and a projection; the next step gives it object
   space and the object's matrix (cam[0x3c]). The cars first: 2,064 of ~3,400 mesh faces a frame (map 2's census),
   rigid, in persistent arrays (CBasicMesh) -- baked once, drawn with the car's matrix. Blocker known: the paint
   and windows are span mode 1, which samples the frame being drawn (the reflection) -- it needs its own answer.
2. **Bake the static world into GPU buffers at load** (track, buildings, props in world space, a camera matrix a
   frame, the GPU clipping): the game's world front end and the per-face path both go. The largest possible cut
   and the largest reverse engineering: where the track's geometry lives, the game's visibility and LOD rules, the
   road's generator (+27402, its own transform +346B0).
3. **A CPU-idle census on the phone**: a lowest-priority thread counting while the game runs says how much of each
   frame the ARM waits (on the GPU in the readback or the swap, on timers in the yields). Waits can be overlapped by
   a second thread; work cannot. It would also settle route B's caveat. A ride-along probe, cheap.
4. **Frames between renders by reprojection**: the list's triangles are in view space under a GPU projection, so
   redrawing them with the next camera is cheap -- but the frame is synthesized (cars moved rigidly, gaps at the
   edges, the painter's-ordered rest not reprojectable): the owner's "no cheap tricks" decides.
5. **eglCopyBuffers instead of glReadPixels**: one timed call; low odds (the N95 offers no pixmap configs).
6. Small cuts, ~1 ms each: the re-posts skipped (v3, ~0.8 ms at the right speed), lean_tail, the blended classes'
   second drawing.
Before building 1 or 2: their ceilings on the N95 model -- the race with the cars' meshes not transformed or drawn,
with the static world not drawn, and with the present path skipped -- an upper bound for each, bench only.

## Phone round 57 (r57 2f07552e): back to round 53's level; a heading snap-back in a hard turn

Owner (N95, eight files): "Yep, back to how it used to be on r53. There is some small snap-back that we need to iron
out. Other than that, it went back to r53 levels."
- Files: captures 115-122, all r57 (171,048 bytes, mode 3118, no A/B). Copied by their exact upload names this time
  (round 56's first read globbed older uploads).
- Whole race: 2,808 renders in 122 windows, 65.8 ms a render (round 53: 65.4 / 65.7); 3D 30.0, composite 8.9 (read
  6.9), 2D 3.3, present 2.3; render call 56.3 ms (55.6 / 56.4), before the first triangle 12.3 (13.7 / 14.5); steps
  4,448 over 2,865 spans (half-steps in real time, as round 53's 4,240 / 2,722). TAKE2 shows before the first triangle
  (-1.4 to -2.2 ms) and is within the noise over the whole frame. Converter 3,308 fast converts (~1.2 a render).
- Frame record (the last 417 frames, 382 chase): bearing reversals 19 (round 53: 15, 12), camera-yaw one-frame
  reversals 10 (11, 5), drawn speed against its neighbours off by >25%: 2% (0%, 1%) -- the latter uneven present
  intervals (81, 44, 47, 80, 48 ms) with the car advancing in proportion, not motion backwards.
- **The snap-back, found**: at 68.37-68.61 s, in a hard left turn (heading -45 to -50 /2048 a frame), the drawn heading
  goes 760, 710, 663, **704**, 668, **580**, 566, 588: back 41 (~7 degrees) for one frame, then on by 36 and 88 (~15
  degrees); the chase camera with it (yaw 668, 711, 671, 582). The frames turn the car by the last step's turn
  (A4_KD_TURN, whole pulses: "the next step repeats the last turn in 87% of steps") and the snap fix fades a step's
  disagreement over A4_KD_TAU -- here the frames jumped instead: either the game's heading changed by more than a turn
  pulse (a contact, a bounce) and the fix took it as a cut, or the fade does not cover this case. Next: reproduce on
  the bench (a hard turn into a wall, steerhold / autopilot), name the rule, fix; no phone round for it until then.

### Round 57 bench: the ceilings (what the cars and the roadside scenery cost, by not running them)
Bench-only injector switches (never a phone build): A4_CEIL_CARS makes the CBasicMesh wrapper +34AEA return at once
(the cars: transform, cull, insert, and their faces' draws); A4_CEIL_OBJS makes the static objects' routine +CE8C
(one caller, +D2F6) return at once. Neither caller reads the result. Each build differs from r57 by 4 bytes a switch.
Screenshots: cars off -- no car, the player's shadow on the road at 190 km/h; objects off -- the roadside buildings,
trees and props gone, the road, tunnel walls, skyline backdrop and cars left (so +CE8C is the roadside scenery).
W-4 model, 120 s each (rbrun.sh; its bare `wait` hung on an Xvfb it had started in this fresh container -- now waits
for its snapshots only):
| build | render call | before 1st triangle | 3D | composite | ms a render |
| r57 (r57t) | 39.0 | 7.2 | 21.4 | 7.2 | 48.6 |
| cars off | 30.3 (-8.7) | 4.2 | 16.1 | 6.9 | 44.5 |
| scenery off | 28.8 (-10.2) | 4.0 | 14.4 | 7.1 | 42.9 |
| both off | 20.6 (-18.4, 47%) | 1.5 | 9.8 | 5.9 | 42.7 |
- The present path needs no run: on the phone it is measured -- composite 8.9 + ~1.2 posts a render x 2.84 = ~12.4 ms.
- Read: the cars and the roadside scenery are half the render call (on the phone, x1.44 the model: ~26 of 56 ms). A
  GPU path for them would keep their draw calls (~31 us a call + 1.5 us a triangle on the phone) and a cull, so it
  takes most but not all of that. With both gone the frame sits on the half-step's cap (42.67 ms a frame, 23.4 fps):
  past it needs the step itself shorter (the engine at 3x, A4_THIRDSTEP, task 38) as well.

### Round 57 bench: the CPU-idle census (A4_IDLECEN, A4_ABCYCLE16), built and run
- What it is (a4_gl.c, a4_tel.c, r51sum.py 'AG32' / 'IDL1'): a thread at EPriorityNull (absolute 3 in a foreground
  process, kernelhwsrv sthread.cpp ThreadPriorityTable) that counts while nothing else is ready; its counts over a span
  against its idle rate are that span's idle CPU, per window and inside the yield, the readback and the post. No import
  block added: the game's own RLibrary::Load / Lookup slots (euser 1308 at code+0x50FA8, 1838 at +0x50FB8, raw ordinals
  0x51C / 0x72E in the image) load euser and look up RThread::Create (1158, the RAllocator variant: NULL = the
  creating thread's heap, kernelhwsrv us_ksvr.cpp), SetPriority (1786), Resume (1795), User::After (645). The thread
  counts in windows with AB_IDLE (bit 20, alternated by A4_ABCYCLE16) and sleeps 50 ms at a time otherwise. Build
  i57 = r57 + both: 826,064 bytes, no 0xDEADBEEF.
- Bench (W-4 model, 120 s): the four ordinals resolve on the RM-320 ROM and the thread runs -- calibration 5,137,879
  counts in 106 ticks (100 ms of User::After), 48,471 a tick. Its cost: slots off / on 48.5 / 48.9 ms a render (the
  windows' own spread is 41-54). In the race it counted **0** -- in every window, in 5,178 yields (0.72 ticks each), in
  the readback and in the post, even yields of 5 ticks and more.
- Why, from the binary: the runtime's yield (+16BC) arms its timer and then **spins** -- RThread::RequestCount,
  CActiveScheduler::Current, RunIfReady, round and round until the timer's object has run (round 56's reading) -- so
  the game's thread never blocks there and nothing below it runs, on the phone as here. On the bench the readback and
  the post are host work, so no guest thread waits in them either. What the phone can still show: blocking inside the
  MBX driver's readback (a wait for the tile render) and in the post's window-server round trip -- the census rides
  along in the next phone build to say how much of the frame those are. A bench and probe option only.

### Round 57 bench: the snap-back, reproduced and named (A4_KD_MINTURN)
- Reproduced with jsteer.sh (the autopilot off, steering held 1.5 s and let go 0.5 s, W-4 model at cap 85, 120 s,
  build ys57 = r57 + A4_CARREC + A4_YREC): tools/snapscan.py (a drawn frame-to-frame heading change of 15 or more
  against the frames' own direction, then back) finds 4 zig-zags in each of two runs (s_ys57, s_ys57b). The
  player's, step 181: the game 268, 304, 290, 290 (a +36 kick against a -28 a step turn -- contact forces +0F0/+0F8
  16/46 at that step -- then -14, 0); drawn 254, 279, **327**, 323, 285: past the game's 304 by 23, back 38. The
  phone's 663, 704, 668, 580 is this shape (hard turn, a jump the other way, then on). The other three are the AI's
  own heading wiggle (+-20 a step) made larger.
- Cause: the frames turn the car by the last step's whole change (A4_KD_TURN); a change that does not repeat takes
  them past where the next step puts the car, and the corrector brings them back against the motion.
- The game turns the car by its steering +7FC (-4096..4096; ~28 a step at full lock at speed: -2750 -> -18,
  -2171 -> -14), and the steering predicts the next turn better than the last turn does (correlation with the
  next step's change: +7FC 0.69, the last turn 0.36). tools/predrules2.py scores rules on a recording (draw-only:
  the game's steps are the same whatever the rule), per step |predicted - next turn|, overshoots (|predicted| >= 15
  and the next turn 0 or the other way) and lags (the next turn >= 15 and the prediction short by 15):
  | rule | player overshoots (3 runs) | player lags | others' overshoots | others' mean error |
  | the last turn (r57) | 17, 14, 18 | 2, 9, 1 | 43, 87, 5 | 2.46, 2.68, 2.23 |
  | reversal stopped, start capped 20 (ysr57) | 16, 14, 17 | 13, 23, 15 | 27, 41, 19 | 2.30, 2.50, 2.20 |
  | steering eased -> 0, kick against turn and steering -> 0 | 3, 2, 3 | 4, 11, 4 | 23, 26, 2 | 1.98, 2.16, 1.83 |
  Most of the player's overshoots are the last turn before the steering lets go (-24, -18, then 0; +7FC -3654,
  -2750, 0). The kick at step 181 is covered by the second clause (steering left, the turn +36).
- The narrow rule on the bench run (s_ysr57): zig-zags 0, but the player's heading off its path 99% 32 (r57 21, 21)
  and lags as above -- so not that one.
- The steering rule's own run (yse57, steering eased + kick): zig-zags still 4, all the player's own turn reversals:
  the game +14, +14, +14, then -19 a step while its steering read 1024, 1147, 820, 268, -397, **-1118**, -2891 --
  the heading follows the steering late, and once the steering has grown past 1,024 the other way the next turn
  reverses (18 of 18 such steps in the four recordings; under 1,024 just after the flip it goes on, 18 of 18). Third
  clause: no turn after a step that turned against a steering grown past 1,024 the other way.
- The rule as built (a4_gl.c A4_KD_MINTURN, three clauses: steering eased; a kick against the step before and the
  steering; a turn against a steering grown past 1,024), scored per step on the four recordings: the player's
  overshoots 17, 14, 18, 15 -> 2, 2, 2, 2, mean error 1.61, 1.90, 1.62, 1.69 -> 1.40, 1.79, 1.43, 1.60, 99% 27-28
  -> 24-26; the other cars' overshoots 43, 87, 5, 14 -> 23, 25, 2, 3. (predrules2.py's "lag" counts a 0 where the
  next turn went the other way, an improvement on the old prediction, so it is not read.)
- Bench run of it (ysf57, jsteer 120 s): zig-zags 2 (r57: 4, 4) -- a crash spin (the game +90, +62, +28, +21, +10)
  and an AI car's contact (-32 then 0); the player's heading off its path 99% 22 (r57 21, 21), the camera's yaw 99%
  23 (23); the player's per-step overshoots 14 -> 1.
- Found on the way: a4_gl.s was never run through the preprocessor (only a4_tramp.s was), so A4_IDLECEN's two
  0xDEADBEEF placeholders were in every build since the census was added, unpatched and unreferenced (the first r58
  build showed them at +BA664). The injector now preprocesses both; r58 rebuilt, no 0xDEADBEEF.

### Round 58 candidate (r58 = r57 + A4_KD_MINTURN)
- Built: the handoff baseline (4baef63a), A4_HALFSTEP=1, A4_SCRATCH_SIZE=0x62000, C54 + A4_TAKE2 + A4_KD_MINTURN.
  a4_r58_cand.exe 3e82ac5ad7c5491579f46780fa6274f8494597367724d3b2bfc8aeb215d83d87, 825,016 bytes, +3A15C b510 0004
  (no bench unlock), no 0xDEADBEEF. Recorder twin y58 (+ A4_CARREC A4_YREC), a4_rec_emit2 at +0BA75C.
- Battery (r58bat.sh, r57's on the exact binary 3e82ac5a):
  - Tele, W-4 model, 120 s: framestat 2,905, faults 1; 49.3 ms a render over 2,346 renders (r57 48.6; r53's slots
    49.3-51.6), 3D 21.1, comp 7.3 (read 5.6), 2D 2.9, present 3.0; drawn speed 10-90% 0.96-1.04, 0 off by 25%;
    camera yaw and car heading one-frame reversals 0 in 417; presents 2,937, held 0, drop frames 0.
  - Crash-steer (cap 85): reversals 0 (yaw and heading), bearing 99% 3.
  - Finish (twin, cap 85): reached (178 mode-1 frames after 11 of 0x18), reversals 0. Exact binary, key 5 held,
    300 s: 344 chase frames, reversals 0; the 0x17 views as before.
  - Menus: 18 shots, faults 1 (the boot fault). Marks, and marks on the Veyron.
  - Veyron autopilot: chase 314 frames, car heading reversals 1 (11/2048), camera yaw one-frame reversals 5 (r57: 3,
    all the game's 1,023 cuts; its Veyron stuck early, a different route). Read from the frames: between steps the
    camera turns with the car exactly (-9/-9, -15/-15, -13/-13: the rigid chase camera, r47); at each step it takes
    the game's offset again, and in this long turn the game's own camera fell behind the car more every step
    (heading less yaw -5, -14, -22, -32), so the step frame turns the camera back 7. Drawn position does not read
    the turn; the rule changes only how far both turn between steps. Not the rule's: the rigid camera's, in any
    build since r47, seen now because this route has the turn. Follow-up: carry the offset's own change between
    steps.
  - The first twin (y58) was built without A4_AUTOPILOT (r57's twin had it): its player stood still, the other
    cars' heading 99% 9 max 37 (r57 10, 43). Rebuilt with it (826,096 bytes); re-recording queued.
  - +3A15C b510 0004, no 0xDEADBEEF.
- Rule 2: not a repeat -- no phone round has run A4_KD_MINTURN; r58 differs from round 57's r57 (2f07552e) by the
  rule alone (and a4_gl.s now preprocessed, which leaves the census placeholders out; r57 never had them).
- Rule 4: (a) guesses: that +7FC is every car's steering (the AI's overshoots fell 43, 87 -> 23, 25 on the bench, so
  it behaves as one); the 1,024 threshold (36 steps); that the phone's snap is this kick (the phone's frame record
  has no game step headings -- the round says). (b) Read back: the whole code diff (the two struct words, the
  three clauses, the injector's preprocessing line). (c) Not entered: missed steps (the rule then compares with an
  older step's turn and steering), the first step of a race (traw/sraw as the block was left), a scripted camera.
- Twin re-recorded with the autopilot (y58b, W-4 model, 120 s): all cars x-z 99% 34 max 144, heading 99% 28 max 171;
  camera yaw 99% 34 max 90; snapscan 33 zig-zags (r57's y57: heading 99% 10 max 43, yaw 26/35). Read: this route
  ends in crashes -- the game's own player headings 1234, 1154, 962, 942, 849 (-192 in one step), an AI car's 1377,
  1433, 1561, 1433, 1305 (+-128 a step) -- where any frame between steps is off the line between them. The rule is
  not it: scored on this recording (predrules2.py), the player's prediction is the same with and without it (mean
  error 3.60, 99% 64, overshoots 34 either way: the rule never fired on its steps), the other cars' better with it
  (overshoots 103 -> 57, mean 3.48 -> 3.13). (y57's route had no such crash, so the two twins do not compare.)
- Sent to the owner (the agreed order: "fix the snap-back on the bench, then send a build to confirm" -- "yes"):
  r58 = 3e82ac5a, unbaked. What the round answers: whether the snap-back in hard turns is gone, at round 57's speed.

### Round 58 bench: what the roadside scenery costs, and what the GPU can and cannot take
- Mapped (a4stock2.dis): the scenery routine +CF6E (one caller, +D7D0) walks the track's static object records (48
  bytes: x, y, z, radius, +0x10 a timer, +0x14 type, +0x1C vertex count, +0x20 int16 vertices, +0x24 face count,
  +0x28 10-byte faces, +0x2C an animation counter) between two track sections ([track+0x6A80+0x30/0x34][section]).
  Each object: sphere cull +358FC (dropped wholly past ZW 29,900 or behind -64, then four planes), push the camera
  matrix (+32542: a 12-word stack at cam+0x3C), translate by the object's place (+321BC, 14-bit rotation, then
  + translation); types 0x44 (a knocked prop: moved, rotated by +32302/+32322 a step, gone after 40) and three
  table types (+ADFE0; their glow sprites by +CABE) aside, then +CE8C draws it (unpack, +349F8) and +32558 pops.
  So a static object is the camera matrix times a translation: 92 distinct meshes, placed only by position.
- What it costs (s58a / s58o: r58 with and without +CE8C, W-4 model, GPUMODEL_CALLS, 120 s each): 49.2 -> 43.0 ms
  a render; GL calls a frame 690 -> 316: draw elements (the list) 112.5 -> 4.7, texture-matrix loads 92.7 -> 2.3,
  binds 198 -> 83, depth funcs 19.6 -> 0.6, draw arrays (the table) 112 -> 98. The list is almost all scenery.
  Front end, a cycle profile gated on +CF6E..+D7D4 (sp58c): ~1.17M modelled cycles a frame (~3.5 ms), of it
  engi_349F8 (transform, culls) 38.7%, a4_mesh_insert 37.1%, +CE8C's unpack 14.7%, the walk 4.4%, the sphere cull 0.7%.
- So "the scenery on the GPU" as first thought -- object-space meshes, a matrix an object -- needs a draw call (and
  a matrix) for each of ~89 objects a frame: about what it would save. Not that.
- The list's draws, from dumps (A4_LISTDUMP: every 64th list as mesh_draw gets it; tools/listcmp.py replays the run
  grouping under other rules and rasterises each as the list draws -- texz GEQUAL, the rest ALWAYS, 24-bit depth --
  comparing the face shown at every pixel; ld58, 27 frames, 462,746 list pixels):
  | rule | draws a frame | pixels changed |
  | the build's (box overlap only) | 93.3 | -- |
  | A4_MESHZ (round 46, dropped with z3 in round 51) | 93.2 | 0 |
  | depth-tested faces pass every depth-tested run | 90.7 | 0 |
  | every face depth-tested | 20.4 | 11,185 (2.4%) |
  Only ~3% of the list's faces are depth-tested (the game draws most of them affine: mesh_fallback). Of the
  all-tested rule's changed pixels, 34 are ties and 8,728 a clearly nearer face -- and seen (false colour by texture,
  lcview): the roadside's blue props stand into the walls behind them, and the game's order paints them whole; a
  depth test cuts them. The table order is part of the picture.
- What can merge draws without touching the order: one draw call draws its triangles in order. Consecutive faces in
  table order change texture 175 times a frame but depth function only ~21: with the textures on shared pages (an
  atlas) the list's runs could carry the table's order across textures. The obstacle is repeat: face texture spans
  50% 16x29 texels, 90% 64x95, 99% 160x218 -- some faces tile their texture. Texture sizes added to the dump (ld58b).
- ld58b (the dump with each key's texture size; tools/atlasim.py, 35 frames): the list's textures are 16-128 texels
  a side (64x64 and 128x128 the most), a frame's set 110K texels (median, max 255K -- a quarter of a 1024x1024
  page). A face fits its texture's tile (after a shift by whole tiles) in 91.9% of faces, 2x2 tiles in 96.1%, 3x3 in
  97.1%. The list's draws a frame, grouped by the build's rule with the fitting faces on one atlas page:
  | textures | draws a frame |
  | as now | 82.8 |
  | atlas, a texture once | 48.4 |
  | atlas, each texture stored 2x2 | 33.5 |
  | atlas, 3x3 | 28.1 |
  | every face on the page (the floor: the depth-function changes) | 12.3 |
  Same picture by construction (one draw call keeps its triangles in order; nearest sampling, so no bleeding at an
  entry's edge). Worth on the phone at ~31 us a draw: ~49 fewer list draws ~1.5 ms a frame, plus most of the ~115
  binds and ~90 texture-matrix loads the scenery adds; the table path's ~100 draws a frame not yet simulated.
  Costs: the textures uploaded into pages (2x2: four times the texels, at the ~4 KB/ms the phone uploads), page
  management, and the faces that do not fit kept on their own textures. Not started: a decision for the owner.

## Phone round 58 (r58 3e82ac5a): the snap-back gone; tearing worse the faster the car goes

Owner (N95, eight files): "It improved decently. I think we can work with it." Asked how else to raise the frame
rate and cut the tearing in fast races ("the faster I go, the more noticeable the screen tearing is"); the aim stays
a stable 30 fps.
- Files: captures 253-260, r58 (171,048 bytes, mode 3118, no A/B), copied by their exact upload names (ph58/).
- Speed as round 57: render call 55.7 ms (r57 56.3), before the first triangle 11.2 (12.3); steps 4,822 over 3,149
  spans (n=1 1,629, n=2 1,379: half-steps in real time). Whole race 69.2 ms a render over 3,199 renders -- window 259
  is a 7.5 s load (326 ms a render); light windows 40.7-43.6, heavy 59.7-71.3.
- The rule's effect (frame record, last 373 frames): car heading one-frame reversals 1 (r57 bearing reversals 19);
  camera-yaw one-frame reversals 15 of 4-13 /2048 (r57 10). The camera's yaw steps unevenly in a turn (8.1-8.8 s:
  +8, +9, +19, +10, +5, +12, +2, +9) while the game's camera falls behind the car (heading less yaw 0 -> 31): the rigid
  camera taking the game's offset again at each step (the Veyron's, round 58 bench) -- the follow-up stands.
- Pacing, from the frame record (208 chase frames): render start to present 67.1 ms mean, sd 13.0; presents 42-90
  ms apart (10-90%). Each frame is drawn for its render start and shown at its present, so the picture is off its
  display moment by speed x (delay - mean delay): at the car's ~4 units/ms, 47 units at the median, 77 at 90%. A
  frame's delay less the previous frame's: sd 9.7, median ~2 ms -- predicted from the last frame's, the same error
  would be 8 (median) and 48 (90%). The faster the car, the larger this uneven step, in proportion.
- Tearing (round 29's study, the runtime's binary and the ROM's scdv.dll): the runtime asks for a double-buffered
  direct screen bitmap and waits for each transfer before the next; a tear is the display driver's transfer meeting
  the panel's scan, below the game and us. Its seam is the difference between two consecutive frames: the motion
  between them, which grows with speed. Levers: more frames a second (a smaller jump each) and even pacing (no
  frame jumping twice as far as its neighbours).

## Round 59: frame pacing (A4_PACE) and the chase camera's offset carried (A4_CAMYAWRATE)

Owner: "yes, go ahead with r59 pacing and camera fix".
- A4_PACE (a4_gl.c pace_ph): each render's speculation fraction is moved by the last frame's render-start-to-present
  delay less the running mean of those delays (an 1/8 average, ms x 16), within +-40 ms (A4_PACE_MAX), in the logic
  clock's units (250 a ms): the frame drawn for the moment it will be on screen, a steady time behind it. The
  present is stamped where the frame record stamps it (the first real present after the render; held and skipped
  re-posts are not). Not in slow motion (A4_SLOWMOQ computes its own fraction from the clock) nor across a load
  (a delay of 500 ms or more is not a sample). Frames within a step stay monotonic (committed only grows).
- A4_CAMYAWRATE (the chase camera, ENGDRAW branch): the game camera's yaw less the car's heading at a step moves on
  by its own last change between steps, when that change is steady (within half itself plus 2 of the step
  before's, the same way, at most 64 /2048, three steps of this camera with none missed) -- the other cameras' rule
  (A4_CAMALL), and round 57's lesson: a change turning back is not carried.
- Candidate r59 = r58 + both (C59.flags): a4_r59_cand.exe a835a129efbb1f9a47f2552b2db664bf3345e8a70ac52a9bb2348cabb8a851fa,
  825,576 bytes (r58 825,016), +3A15C b510 0004, no 0xDEADBEEF. Twins y59 (+ CARREC YREC AUTOPILOT), ys59 (+ CARREC
  YREC: jsteer). Running: the steering twin, then the battery (r59run.sh).
- Battery on a835a129 (r59run.sh): tele (W-4, 120 s) 48.8 ms a render (r58 49.3), camera yaw and heading reversals
  0; crash-steer reversals 0; menus 18 shots, faults 1; marks; Veyron camera-yaw reversals 4 (r58 5). Both finish
  runs stopped short: the twin's car stood at (-4607, -8216) and the key-5 car pushed at a wall (x -5147..-5387, z
  32344) for their last 417 frames. Not the build: with A4_KINEDRAW the car calls run as stock (HP_HOOK is empty;
  the frames' fraction reaches only the drawing), and unsteered runs have stuck before (rounds 45, 57).
- **A4_PACE does the opposite of its aim** (tools/pace.py, the frame record, displayed picture speed against its 9
  neighbours): tele r58 10-90% 0.96-1.04 (1-99% 0.92-1.11), r59 0.94-1.08 (0.86-1.16, 3% off by >15%); the Veyron
  r58 17% off by >15%, r59 33%. The error was in round 58's reading, not the code: what makes the picture's motion
  uneven is the CHANGE in the delay from frame to frame, not the delay's distance from its mean. Unpaced, the
  displayed motion's error per frame is the delay's first difference; paced by the last delay, it is the second
  difference -- larger whenever the delay does not trend (white steps: x1.4; a spike: twice; a level shift: once
  each way instead of once). For any predictor from the past delays alone it is no better (an AR(1) delay with
  rho 0.72, the phone's: paced sd ~12.7 ms against unpaced 9.7). Dropped; the code stays under its switch, off.
- A4_CAMYAWRATE replayed off and on over the same frames (tools/camrate.py): y59 (autopilot) 6 frames of 1,365
  carry anything, s_ys59 (jsteer) 111 of 1,430, and the yaw off the game camera's path and its reversals are the
  same either way (99% 25 / 23, reversals 4 / 14): on these routes the game's camera offset moves 1-2 /2048 a step,
  which rounds to nothing. The case it is for (the offset falling 8-9 a step behind in a fast turn) is the Veyron's:
  a Veyron recording next.
- The carry, replayed (camrate.py) over seven recordings: carrying any steady change was a wash (s_ys59: 53 frames
  moved, mean off the game camera's path 6.0 vs 5.9; 15 better, 33 worse); only while the camera falls behind
  (|offset| growing two steps running, 4..64 a step), 14-20 frames a run, 3.5-5.8 -> 2.4-3.4, 63 better, 24 worse.
  Built so (f079ff84) and battery-run: tele 48.0 ms a render; crash-steer 0 reversals; finish reached (179 mode-1
  frames); menus, marks. But the Veyron: camera-yaw one-frame reversals 22 (the frame record's count; 28 by camoff's),
  heading less yaw 50% 14, 90% 43, 99% 79 -- the game's camera hunting behind the car (offset -50, -40, -46, -44).
  Not the carry's: it moved 25 of the 417 frames, and redrawn without it the reversals are 28 still. The cause is the
  rigid camera's rule itself: the offset taken afresh at each step, every change a jump at the step's first frame.
- So, A4_CAMBLEND instead (replaces the carry): the offset moves from the step before's to this step's over the step
  (yaw = the drawn heading + o(k-1) + (o(k) - o(k-1)) q; a jump over 128, a missed step or a new camera: o(k) at
  once). Replayed (tools/camoff.py): reversals 28 -> 11 (s_ysf57), 20 -> 16 (s_ys57b), 13 -> 12 (s_ys59c), the same
  on the autopilot runs (9, 7, 10, 3: a rigid camera there); the yaw off the game camera's path the same within 1-2
  at 90-99%. Candidate r59 = r58 + A4_CAMBLEND (A4_PACE off): 92a6a5fe27d2688754621eeb64bc163a807daa35d7d5ea6e0a936aeb6a006239,
  825,152 bytes, no 0xDEADBEEF. Running: the steering twin (s_ys59d), then the battery.
- Steering twin s_ys59d (the blend built): the frames drawn equal the replay's blend exactly (camoff: drawn = blended,
  reversals 14 both); the rigid rule on the same frames would have made 19, its worst yaw off the game camera's path 79
  against 58. Twin y59b (autopilot): reversals 4 each way (a rigid camera there).
- Battery on 92a6a5fe (r59run3): tele (W-4, 120 s) 49.4 ms a render (r58 49.3), camera yaw and car heading
  reversals 0, drawn speed by present 10-90% 0.96-1.04; one frame with its triangles halved -- a camera change to
  mode 0x2A (camera deviation 540, 964 triangles against 2,177), the view's own count, not held, not a fault.
  Crash-steer reversals 0. Finish (twin) reached, 181 mode-1 frames; exact binary key 5, 300 s: 417 chase frames,
  bearing reversals 2 (small), the race not ended in the window. Menus 18 shots, faults 1 (the boot fault); marks;
  Veyron autopilot camera-yaw reversals **0** in 417 frames (r58 5; the carry build 22) with the game's camera
  behind the car (heading less yaw 90% 40), heading reversals 0; marks on the Veyron. +3A15C b510 0004, no 0xDEADBEEF.
- Rule 2: not a repeat -- no phone round has blended the camera's offset; round 47's rigid camera took it afresh at
  each step (every round since). A4_PACE never went out.
- Rule 4: (a) guesses -- that a camera offset a step late (42 ms) is not seen (it changes a few /2048 a step); that the
  phone's stepping yaw (round 58, 8.1-8.8 s) is this offset's jump (the frame record's heading less yaw jumps at the
  step frames, as the bench's); the 128 cut. (b) Read back: the blend block as committed (the three lines and their
  guards), the flags file (C59 = C54 + TAKE2 + KD_MINTURN + CAMBLEND; PACE, CAMYAWRATE off). (c) Not entered: a
  camera mode change in the middle of a blend (cwv resets to 1: no blend until two steps of the new camera), slow
  motion (qt is the slow step's fraction: the blend follows it), a missed step (no blend), the scripted cameras (their
  own branch, untouched).
- Sent to the owner as r59 (the owner's "go ahead with r59 pacing and camera fix"; the pacing is not in it): 92a6a5fe,
  unbaked. What the round answers: whether the camera turns smoothly in fast turns, at r58's speed.

## Phone round 59 (r59 92a6a5fe): the camera fine

Owner (N95, eight files): "The camera looks fine. I'm not even sure there was an issue with that."
- Files: captures 133-140, r59, copied by their exact upload names (ph59/).
- Speed: 3,220 renders, 62.9 ms a render (r58 69.2 with a 7.5 s load window in it, r57 65.8); render call 53.5 ms
  (55.7, 56.3), before the first triangle 10.8 (11.2, 12.3); steps 4,876 over 3,271 spans. Heaviest window 88.9 ms a
  render (3D 55.3); the slowest frames ~120 ms at 2,200+ triangles.
- Frame record (417 frames, 300 chase): camera-yaw one-frame reversals 13 (r58 15, r57 10), the first ones around
  contacts (car heading turns of 28-38 in a frame); car heading reversals 2, both at contacts. Drop frames 3 (two
  around the start, one a camera cut: deviation 660). Delay render start -> present 62 ms mean, its change frame to
  frame sd 12.4 ms (r58 9.7, r57 5.2): this race's render times varied more; picture speed per displayed frame off
  its neighbours by >15% in 17% of frames (r58 11%, r57 4%) -- the race's content, no pacing change in r59.
- Next (proposed in round 59's message, not objected to): the atlas simulated on the table path.
- Rule 2, missed: the atlas proposed in round 58 (and simulated on the list, atlasim.py) is rounds 24-25's A4_ATLAS --
  built for the table path, run on the phone twice: calls a render -26%, but every glTexSubImage2D into the 1024x1024
  page cost ~100 ms on the N95's driver (mid-frame or after the composite alike), and the steady windows gained 1-3
  ms. Its own verdict: "a texture that grows during a race cannot be had on this driver; one built whole at a load
  could, but needs a 2 MB staging buffer". Not proposed again without an answer to the upload cost.
- Where the frame is now (ph59): light windows 42.0-42.3 ms a render -- the half-step's cap (42.67 ms, 23.4 fps):
  3D 11.5-15.7, composite 9.3-10.3, 2D 3.2, present 2.3, rest 10-16 (the part before the first triangle, the logic
  ~0.6 a span, and the wait for the step); heavy 62-89 (3D 32-55). Average 62.9 (~16 fps).

## Round 60 bench: the world's ceilings (owner: "if it is not reachable with this approach, then let's find a different approach")
- A4_CEIL_ROAD (+2732E, the one-face drawer under +20938, insert +27402) off, W-4 model, 120 s (c59road): 46.4 ms a
  render, render call 35.6 ms (r57t 39.0), before the first triangle 6.8 (7.2), 3D 17.1 (r59t2 21.6). The road's
  surface itself is still drawn in its snapshots: +2732E is a class of single faces (328 a frame), not the road --
  what it is, to be read.
- The whole world off (cars + scenery + +2732E, c59all, W-4 model, 120 s): render call 18.2 ms (39.0), before the first
  triangle 1.1 (7.2), 3D 5.0 (21.6); 42.7 ms a render -- the half-step's cap (42.67), so the frame's own work is under
  it and hidden. Snapshot: the road surface, the sky backdrop, speed lines, the nitro glow and the HUD remain (the road
  is drawn by yet another path). The world's three blocks are 20.8 ms of the 39.0 ms render call (53%; ~30 ms on the
  phone at x1.44). A GPU-native world must cost well under that, and past ~23.4 fps the frames between steps are
  needed as well.

## Round 60 bench: the scenery prototype (owner: "yes, start the scenery prototype") -- 1, the drawing order
- What a GPU-native draw can keep of the game's picture, replayed on dumps before writing it: A4_LISTDUMP now also
  records each list face's mesh call (the lifted +349F8's entry bump, MESHMEMO_BUMP, kept under A4_LISTDUMP too; the
  ids in A4_MEMO_TABLE's area, 0x460000, free without the memo), a4_list_emit2. ld60 (30 frames, 501,958 list pixels,
  ~48 meshes a frame -- scenery objects and car parts). tools/objorder.py, against the list as drawn now:
  | how the GPU would draw the meshes | pixels changed |
  | every face depth-tested (no order) | 2.49% |
  | meshes far to near (by deepest point), each in its own depth slice, depth-tested inside | 3.31% |
  | the same by nearest point / by the mean | 6.09% / 3.76% |
  | meshes far to near, faces painted in their own order | 6.19-8.84% |
  | depth-tested, small meshes (<=8..32 faces) nudged nearer by 1/64..1/16 | 2.48-2.54% |
  So: plain depth testing, the fewest changes and the freest order (texture-sorted, any batching).
- Of its 12,483 changed pixels (tools/objdiff.py): 7,854 inside one mesh, 4,629 between two (4,333 of those a mesh of
  16 faces or fewer against a larger one). The same-mesh pairs (objpair.py): 778 of 858 at ~90 degrees to each other
  (crossed or interlocking parts the game paints one over the other; the depth test cuts them where they meet), 80
  parallel (overlays 13 units apart at the median -- the recessed-door case). To be judged in real screenshots.
- The build, next: A4_GPUOBJ -- the lifted +CE8C (A4_ENG_0CE8C) with an entry edit that hands an object to
  a4_obj_take when all its faces are the list's class (no alpha map, no colour key) and its view translation fits;
  the object's matrix (the camera stack's top) kept, its mesh expanded once into per-corner arrays grouped by texture,
  drawn before the list with the list's projection and every face depth-tested -- the list's painted faces too (a car
  behind a building must not paint over it). Pixel check against r59 in lockstep runs; timing on the W-4 model.

### Round 60 bench: the scenery prototype, 2 -- built and running (A4_GPUOBJ)
- Code (a4_gl.c, a4_eng.h, eng/edits.py; lifted.c regenerated with the committed flags -DA4_ENG3 -DA4_FASTXF -DA4_MESH,
  the only change the new entry line): the lifted +CE8C (A4_ENG_0CE8C) asks a4_obj_take at +CE90; an object taken has
  its matrix ([cam+0x3C], 12 words) kept and its game path skipped; its mesh (keyed by vertex and face pointers) is
  expanded once into 12-byte per-corner records grouped by texture index; gobj_flush, before the list at the walk's
  first triangle, loads the list's projection, each object's matrix as the modelview (GL_FIXED: rotation << 2,
  translation << 16, |t| < 32000 checked), binds per group, draws arrays, depth GEQUAL writing; the list's runs then
  all depth-tested. With A4_GPUOBJ off, r59 is byte-identical (checked: cmp).
- First runs: every object refused -- the take read ctx[0] as the mesh routine's mode; the mode is the constant 0 the
  caller pushes (+CF50), ctx[0] another argument. Fixed. Then (cap 85, 60 s): 25,515 objects taken, 45,160 refused
  because the arena filled (172 meshes, 7,509 corners = 88 KB); no texture refusals, no far ones. A snapshot looks
  right (buildings, trees, palms, road, car). Arena now 0x46E000-0x4C0000 (A4_SCRATCH_SIZE 0xC0000, bench). Timing run
  on the W-4 model: running. (r59t2's render call for comparison: 39.8 ms, 7.6 before the first triangle.)
- A frame-exact pixel check needs two builds in step; half-step builds part after ~136-626 steps even in lockstep
  (round 45's oracle note), so the check here is visual plus the replay's 2.5%.
- Timing run 1 (W-4 model, 120 s, 0xC0000 scratch): render call 38.2 ms (r59t2 39.8), before the first triangle 7.1
  (7.6); 48.2 ms a render. But the mesh table (256) filled: 31,315 objects taken, 85,982 refused for a full store,
  193 for a texture outside the class, none far; 653 frames drew objects, 73 GPU draws a frame for 48 objects. So 27%
  of the objects saved 1.6 ms. The one access violation is the boot one r59t2 has too. Snapshots right (bench grabs
  are upside down, as every take-over grab).
- Store changed: 1024 meshes, a hashed lookup (2048 slots) in place of the linear scan, and a full store refuses for
  the rest of the frame and is emptied at the next one (a frame's items point into it). Next run: a 1.5 MB arena
  (bench) to read the race's working set (meshes and corners) before choosing the phone's size.
- Timing run 2 (1.5 MB arena, bench): 98,661 of 104,009 objects taken (95%); the race's working set 818 meshes,
  41,802 corners (490 KB). Render call 35.3 ms (r59t2 39.8), first triangle 4.5 (7.6), 45.4 ms a render (49.4). The
  GPU model's charges hardly moved (r59t2 / this, a frame: 625 / 749 calls, 203 / 198 draws, draw time 4.9 / 5.0 ms):
  the list's draws went, ~100 object draws came (1.6 groups an object, 65 objects a frame). Against round 58's ceiling
  for +CE8C (-6.2 ms a render) this kept 4.0; the rest is the per-object draws and matrices (~2 ms of driver calls).
- So batched: the walk's matrix for a static object is the camera's (the stack base [cam+0x38]; +32542 copies the top
  up a slot) with t + (R p) >> 14, p = [obj+0..8] (+D104 ldm r4 -> +321BC, 32-bit muls). An object is taken only if
  its rotation is the camera's and its translation exactly that (the game's own arithmetic), and keeps d = p - o, o
  the camera's place (p - c = R^T t / 2^14); the flush loads one modelview (R, t moved to o), sorts the frame's
  (texture, object, group)s by texture and copies each texture's corners, offset by d (word arithmetic, no carry
  between halves), into a 4,095-corner staging buffer, one draw a texture. Anything else is left to the game.
- Timing run 3 (batched, bench): render call 32.7 ms (r59t2 39.8, -7.1), first triangle 4.5, 44.2 ms a render
  (49.4, -5.2); 3D 17.3 ms. GPU model a frame: 403 calls (625), 127 draws (203), draw time 3.5 ms (4.9), other 2.1
  (3.3). Objects: 154,420 of 159,772 taken (96.6%), 19.5 object draws a frame; refused 28 for another rotation (knocked
  props), 4 for range, 0 for a translation the game did not compute so, 919 for textures outside the class. 910
  meshes, 45.5k corners. Snapshots right (palms, poles, buildings, signs).
- For a phone: the store keyed by data pointers could outlive a track; each mesh keeps its data's first three words
  and a mismatch empties the store at the next frame, as do 64 frames without objects (a menu, a load). 2,048 meshes,
  ~1 MB of corners (A4_SCRATCH_SIZE 0x1A0000: 1.6 MB of .bss; the owner's N95 8GB has 128 MB). Candidate r60 = C59 +
  A4_ENG_0CE8C + A4_GPUOBJ, c6f7ad6c..., 832,612 bytes; twin y60 (+ CARREC YREC AUTOPILOT). Battery running.
- Battery on c6f7ad6c (r60bat): twin y60b (autopilot, W-4) all cars x-z 99% 24 max 98, heading 99% 13 max 71; camera
  reversals 3 each way (r59: 4). Tele (W-4, 120 s) **44.3 ms a render** (r59 49.4); its windows 42.1-43.9 ms: at the
  half-step cap (42.67); camera yaw and car heading reversals 0; drawn speed by present 10-90% 0.97-1.04; one drop
  frame. Crash-steer reversals 0. Finish run (twin, cap 85): the car stuck (no speed, heading wobbling +-5 for the
  last 417 frames) -- rerun r60fb reached the finish (163-186 finish-mode frames a file, as r59), so the autopilot's
  own crash (+CE8C only allocates per-frame buffers, unpacks into them and calls the mesh routine: nothing the logic
  reads); its counters: 310,130 of 316,438 objects taken (98.0%), 906 meshes, 45.4k corners, 19.6 object draws a
  frame, refused 2,599 texture, 53 rotation, 12 range, 0 moved; resets, stale, gaps 0. Exact binary key 5, 300 s:
  397 chase frames, bearing reversals 1. Menus 18 shots; marks; Veyron autopilot camera-yaw reversals 3 (r59 0, r58
  5), heading reversals 0; marks on the Veyron. +3A15C b510 0004, no 0xDEADBEEF. (The battery's counter files were
  empty: I wrote the hook as 70BF164, a digit short of 700BF164.)
- Rule 2: not a repeat -- no phone round has drawn the game's objects through the GPU's transform: rounds 24-25 were
  texture uploads (the atlas), every round since 27 the game's own projected triangles (the list), round 58's
  ceilings bench only.
- Rule 4: (a) guesses -- the MBX driver's cost for these draws (the model: 3 us a call, 20 a draw, 150 ns a vertex;
  only the phone's render call can say); that a depth test in place of the game's order for the scenery reads as the
  game (the replay: 2.5% of the list's pixels, props that stand into walls cut); that 1.6 MB more .bss loads (N95 8GB);
  that a second race never finds another mesh under a held address with the same first words and no 64-frame gap.
  (b) Read back: the take (the camera's rotation, the translation by +321BC's 32-bit arithmetic, the origin and T),
  the flush (one modelview, the insertion sort, the offset copy: low half x + dx, high half y + dy, no carry across),
  the store's limits and the stale checks; C60 = C59 + A4_ENG_0CE8C + A4_GPUOBJ. (c) Not entered: a second race in
  one session, the cities the battery does not drive, the store or the staging filling (resets 0 on every run), a
  knocked prop (refused: the game draws it).
- Sent (owner: "yes, send r60"): r60 unbaked, c6f7ad6c0d96825787fee5be4dc91fd3a34bb8b9a2ea90f6ad7d400949f50b7a,
  832,612 bytes (the battery's binary, cmp'd by hash). Asked of the phone round: the frame rate against r59 on the
  same track, props next to walls (the depth test), a second race without restarting, the start (1.6 MB more .bss).
  Owner's question before sending: a third frame per update drawn from prediction is the same technique as the
  in-between frame since round 46 (A4_KINEDRAW); the way past the 23.4 fps cap without it is the engine at 3x (task
  38, bench, not phone-ready). Next after this round: back to the 3x engine, not more predicted frames.

## Phone round 60 (r60 c6f7ad6c): faster; scenery showing through walls, bikes and tyres black

Owner (N95, sixteen files, four photos, Paris night races and a tunnel): "Framerate feels a bit better. ... Some parts
of the map now show through some places or walls that they shouldnt (similar to the issue of the tunnel showing
through the wall). ... cones, pneumatics or bycicles, they're still there. But the bycicles and the pneumatics seem to
have lost their textures. They now look pitch-black. I played a second race consecutively. No issues so far in that
regard. The game starts normally." Then a save with everything unlocked (Paris was never reachable on the bench).
- Files: two sets of eight, copied by their exact upload names (ph60/a, ph60/b).
- Speed (r51sum, steptime): race a 58.7 ms a render (r59's phone round 62.9), render call 49.0 (53.5), before the
  first triangle 8.0 (10.8); race b 61.0 / 60.6 (two segments), render call 51.8, first triangle 7.8. The front end's
  ~3 ms went as the bench said; the rest less than the bench's -7.1 (the tracks differ: r59's round is not Paris).
- The cause, read in the code (not yet seen on the bench): the table's classes the game never depth-tests (shadows,
  cut-outs, flat slabs, the colour-key span) are tested in a list frame only where the list's tile grid holds a
  nearer face (mesh_tri_covered, round 28), and sprites likewise (mesh_bb_depth). The GPU's objects were in no grid, so
  a prop's own shadow -- painted by the game before the prop -- now lands on it after (bikes, tyres black), and
  cut-outs and slabs behind an object paint over it. Worse: mesh_draw set MESH->active from the list's faces alone,
  and the list is now almost empty (it was mostly scenery), so whole frames left the list regime (every class tested)
  for the old one; and mesh_has would read a stale face hash in such a frame.
- Fix (r61 source): each taken object keeps a screen box (its bounding sphere, extent x 1.75, the 8 corners projected
  with the game's reciprocal table, mesh_scr_i) and its nearest and farthest buckets (ZW >> 3, as mesh_take's); the
  grid takes the objects at their nearest bucket (gobj_grid, also in a frame whose list is empty), MESH->active is
  set when objects were drawn, mesh_bb_depth counts an object nearer than the sprite as a cover (its far bucket, at
  most the sprite's - 1), mesh_has only with list faces this frame, and the flush asks for depth writes itself.
- Bench now: the owner's save installed (the bench's own kept in save_bench/); Instant Play goes to Paris. Recording
  r60, r61, r59 there.
- Instrument (bench, A4_GOBJ_AB=n): the objects taken by turns of 2^n frames in one run, the frames that drew them
  marked by a yellow 16 x 8 bar (top middle, the frame's last 3D quad, a4_gl_flush); A4_GOBJ_NOGRID turns the grid
  fix off (r60's behaviour). tools: abpairs.py (scratchpad) pairs a marked frame with its unmarked neighbour from a
  tunrun recording (REC=8). First tries drew no mark: at the frame's start (the road covered it), then top left
  (under the HUD's bracket); magenta is the composite's sentinel, so yellow.
- Paris (Instant Play with the owner's save), r60 behaviour (NOGRID), 498 on/off pairs, 30 looked at: with the
  objects on, near things are cut or gone -- a building at the right edge, a traffic light, parasols in the GNN
  camera, a lamp post -- and with the grid fix (r61, 485 pairs) the tunnel's right wall is missing and the night
  outside shows (the owner's first photo). The cause: the GPU's projection is the list's, its near plane at
  w = ZK - ZK/64 = 126; an object partly nearer loses that part. The list never takes a face nearer than ZK.
  No bikes in the pairs looked at (the shadow cause for them stands on the code alone).
- Fix (r62 source): the whole object to the game when its sphere reaches w < ZK + 4 (refused_near); the sphere's
  radius now the farthest corner's distance (an integer square root at the mesh's build) in place of extent x 1.75,
  which also tightens the boxes.
- r62 A/B in Paris (A4_GOBJ_AB=3, two runs; the first stuck behind a car at the start, the second a whole lap,
  580 pairs, 30 looked at): the building at the right edge, the traffic light, the lamp posts, the tunnel stretch and
  Montmartre the same with the objects on and off.
- The bikes: black silhouettes by the Moulin Rouge wall in r60's A/B run in the frames WITHOUT the GPU's objects as
  well (f0647, f0649, f0651 off; f0646, f0648, f0650 on), and in r59's own Paris run (rec_pa_r59, the "GOT
  INSURANCE?" frames): not round 60's. In the GNN camera's view from above they read as bike-shaped shapes on the
  ground with no bike. An older fault the owner noticed now; which class draws them: A4_KINDVIS on r59, running.
- A4_KINDVIS on r59 (Paris): the bikes stay black -- not the flat, additive, shadow or alpha-map class (green, cyan,
  red, yellow); a textured class with black texels. Round 28's precedent: the tunnel signs' texel pointer swapped by
  the game's own animator (+D32E), dark in one frame and lit in the next. The stock game on the same route: running.
- r62 timing (LA, W-4 model, the bench's own save back for the comparison): render call 33.9 ms (r60 32.7, r59
  39.8), first triangle 4.9, 44.7 ms a render (44.3, 49.4); 110,914 of 117,817 objects taken (94.1%), 4,714 left to
  the game for the near plane, 18.6 object draws a frame; one store reset (a 64-frame gap without objects).
- The stock game (the handoff baseline, unmodified, the game's own software renderer; owner's save, Paris, rec_pa_stock):
  the parked bikes are the same black silhouettes (stfr f0037-f0038). So the black bikes (and, presumably, the tyres)
  are the game's own night look, not this port's; the owner noticed them now.
- Candidate r62 = C60 flags (r60's), source with the grid/regime fix and the near-plane refusal:
  e378860a627a1a6e2d9a90ea76309051c2a91a35c3202077ba31ca2a7e3e26b5, 834,472 bytes, no 0xDEADBEEF, +3A15C b510 0004;
  twin y62 (+ CARREC YREC AUTOPILOT, rec pc 700BA75C unchanged). Battery r62bat (r60's, the counters at 700BF5E8,
  plus Paris on the exact binary with the owner's save): running.
- Battery on e378860a (r62bat): twin y62b camera reversals 4 each way (r60 3, r59 4). Tele (W-4, 120 s, LA): 44.9 ms
  a render (r60 44.3, r59 49.4), windows 40.9-50.9; camera yaw reversals 1, car heading 2, all at one frame (29142,
  a 12/2048 jolt: a contact's signature); drawn speed by present 10-90% 0.96-1.04; drop frames 0. Crash-steer
  reversals 0. Finish (twin, cap 85): the game stopped stepping -- 1 step in the last 416 frames, frames still
  presented, mode 2 throughout (r60's stuck run still stepped: a car against a wall); being looked at with
  screenshots. Exact binary key 5, 300 s: the finish reached (165 finish-mode frames), bearing reversals 1. Menus 18
  shots; marks; Veyron autopilot camera reversals 1, heading 1; marks on the Veyron; archive restored. Counters (at
  700BF5E8 this time): LA 159,157 of 169,103 taken (94.1%), 6,423 near; finish run 93.0%, 15,410 near; no resets.
  Paris, exact binary, owner's save (pa_r62c): 77,019 of 98,348 (78.3%; 15,644 textures outside the class, 1,668
  meshes refused at the build), 3,810 near, 4 resets by the 64-frame gap; its recording: tunnel walls, near
  buildings, lamp posts all drawn. +3A15C b510 0004, no 0xDEADBEEF.
- Twin finish rerun with screenshots (r62fc, cap 85, 330 s): stepping (170 steps in the last 416 frames), modes 2 and
  24; lap 3/3 at 0 km/h on top of a traffic car -- the autopilot's recorded route meeting other traffic, as r60f.
  The first twin run's state (frames presented, 1 step in 416, mode 2) is not identified: seen once in five twin
  finish runs (r59f, r60f, r60fb, r62f, r62fc), never in the exact binary's (r60f2, r62f2 reached the finish), and
  the step gating is r59's; a pause by the autopilot's keys is a guess.
- Rule 2: not a repeat -- phone round 60 drew the objects with the near plane cutting them and outside the list's grid
  and regime; r62 is those two fixed, nothing else.
- Rule 4: (a) guesses -- that the owner's "showing through" was the near plane and the grid (the bench A/B shows both
  and their absence after; the photos are of Paris and a tunnel, the bench's of the same); that the tyres are the
  game's own look as the bikes are (the bikes are, by the stock game; tyres not looked at); that the 64-frame gap
  reset (4 in a Paris race) costs a frame's rebuild only. (b) Read back: the take's near test (M[11] + rad + 2 >
  -(ZK + 4), the near plane at ZK - ZK/64), the radius (an isqrt of the largest x^2+y^2+z^2, rounded up), the box
  (8 corners of the sphere's box through mesh_scr_i, clamped), gobj_grid (nearest bucket per tile), GOBJ_ONLY in
  mesh_draw's two empty-list returns, active = n > lost || nobj, mesh_has only with list faces, gobj_bb_deep (at most
  sb - 1), the flush's depth mask; the bench switches (A4_GOBJ_AB, A4_GOBJ_NOGRID) absent from the candidate's flags.
  (c) Not entered: the unexplained twin state above; a knocked prop near the camera (refused for its rotation before
  the near test); cities other than LA and Paris.

## Round 63 (owner: "I don't want to fake frames if that means we're gonna keep having that snap-back and online
## lagginess. Whatever you have to do to fix this, do so"): the engine at 3x on today's renderer, prediction retired

The direction of round 50, taken up again: every frame drawn from a real simulated state (plan steps 1-4 there).
- C63 = C60 less the half-step's prediction (A4_HALFLOGIC, HALFMOTION, HALFSPEC, HALFPHYS, FRACPHYS, KINEPHYS,
  KINEDRAW, ENGDRAW, CAMALL, SCRIPTHOLD, GAPRATE, SPECCAMSAFE, SPECTRAFFIC, SLOWMOQ, KD_MINTURN, CAMBLEND), A4_ABMODE
  1070 (3118 less AB_HSPEC), plus A4_ENG3 and A4_ENG_ALL; built with A4_THIRDSTEP=1 (the step 7111 units, 28.44 ms).
  Kept: the renderer (TAKE2, PX6, ZSKIP, FASTXF, the lifted front end, GPUOBJ), the telemetry, A4_DROPHOLD (a real
  game glitch: empty frames) and A4_SKIPREPRES. The scenery code used iabs32, defined under A4_HALFSPEC: its own
  go_iabs now (r62 rebuilt byte-identical, e378860a).
- go63 (W-4 model, 120 s, LA): the race runs whole; **39.9 ms a render (25 fps), every frame a real state**;
  render call 31.5 ms, first triangle 4.0; spans: 1,760 renders after 1 substep, 1,210 after 2, 22 after 3 (a
  frame ~37 ms against a 28.44 ms step: the advance a frame alternates); camera yaw and car heading reversals 0;
  objects 93.7% taken. Snapshots: the race, a takedown, the crash camera, GNN's -- all drawn right.
- Oracle again (round 50's, re-run because the lifted code was regenerated since): o1 = C63 less ENG3 and ENG_ALL
  (the game's own engine at its step) and o3 = C63 + THIRDSTEP, both + CARREC YREC AUTOPILOT(_UNSTICK)
  BENCH_SEED LOCKSTEP=1 (o3 LOCKSTEP_P 7111): running. (ENG_ALL without ENG3 does not link: lifted.c is generated
  with ENG3 -- so the reference is the game's own Thumb, the true one.)
- Oracle o1 / o3 (jtrun, 90 s each, the same seeds and autopilot): the countdown the same (revs from record 101,
  moving at 149); the speed curve within 1-2 of +B4 and the gears at the same steps to 7.7 s (B4 >= 60/80/100/120/140
  at steps 12/23/38/46/56 stock, 11/23/37/45/55 at 3x; gear 2 at 32, 3 at 85 both); distance +1.3% at 10 s; the paths
  part after (the bench driver holds '5' and the walls steer it; both runs end stuck on something -- stock at ~38 s,
  3x at ~51 s). Car contacts 1 and 8. As round 50's r15: the per-step rates hold; whole-race figures are the paths'.
- Input: on substeps 1-2 the player's key word is the whole step's (+14754 replay), and +25E8 (the key queue) runs
  on substep 0 only -- so the keys are read every 85 ms, as stock and as every half-step build. Reading them every
  substep needs the race rules' press edges (r2 = 0 queries of +2092, the boost among them) kept over the whole step:
  not in r63; next if the owner still feels lag.
- Candidate r63 = a4_r63_cand.exe (C63, THIRDSTEP, A4_SCRATCH_SIZE 0x1A0000):
  0807b0b25bc4665e8279bb48fa83b7bf29dd707e76ed8f6ecf2ef263255be922, 916,336 bytes (go63's exe, cmp'd), no 0xDEADBEEF,
  +3A15C b510 0004; twin y63 (+ CARREC YREC AUTOPILOT; rec pc 700BA62C). Battery r63bat (counters at 700BCA54): running.
- Battery r63 (exact binary 0807b0b2): every run whole (faults=1 each, the bench's known shutdown fault). Twin y63
  (jtrun, W-4 model): the race runs. Tele (calibrated N95 bench, 120 s): **39.3 ms a render**, 3D 16.6, presents a
  median 36 ms apart; camera yaw and car heading reversals 0; held back 0, drop frames 3. Race to the finish (cap 85,
  autopilot and key 5 held): whole, chase modes 1 and 2. Menu, mark, Veyron autopilot (reversals 0), Paris on the
  owner's save: whole. Scenery taken 93.0% / 93.8% (LA), 71.6% (Paris). **Crash-steer (cap 85): camera yaw one-frame
  reversals 14, car heading 0** -- the camera stepping, not the car: below.
- The 3x camera's stepping (A4_CAMPROBE, a4_cp2: 12 words a placement, a ring at 0x5A2000 emitted through
  a4_camp_emit, EKA2L1_PCMEM 700C4D04; tools campro2.py). The chase camera's yaw is the car's heading plus terms the
  game changes on its whole step only: race+0xE4 (cam word 39), the sway (cam+0xBC, word 47) that +4B40 recomputes
  from cam+0xA4 on every call, and +12DEC's stack argument. At the whole step the yaw less heading jumps (-143 to
  -96 at cnt 1483: race+0xE4 -79 plus sway -17); on substeps 1-2, +5968 made again on the moved car keeps the terms,
  so the yaw follows the car for two substeps and jumps on the third -- a step at 11.72 Hz, reversing for a frame in
  a hard turn. Blending the four words a step late does not help: +4B40 recomputes the sway from cam+0xA4, and the
  yaw is formed from the recomputed and argument terms (a recompute with the words set gave 898, not the blend).
- The fix (eng_cam_pose, a4_eng.h; hooks +14EF0 after +12DEC, +14CB2 before the whole step's camera): the chase
  family (1, 2, 3, 0x2A) is placed from the car on every substep, the whole step's included -- the game's pose at
  each whole step taken as an offset from the car (x, z less the car's, height, yaw less heading, pitch), the step
  before's turned to this step's heading; each substep's pose is the car's place and heading now plus the offset
  moved a third of the way from the step before's to this one's (a step late, as round 59's offset blend, which the
  owner called fine). The game's own placement still runs on substeps 1-2 (its track segment), and its own pose is
  put back before its next whole step (eng_cam_unblend) unless something else wrote the words since. A new mode, a
  respawn, or a jump in the offset (> 1024 in x or z, 512 in y, 128 in yaw) takes the step's offset at once.
- a4_cp3 (the same probe on the fix), crash-steer at cap 85, against a4_cp2 (the same route; the run is chaotic,
  so the counts are per run): placements' yaw one-substep reversals 25 -> 3, the camera's yaw less heading change
  99% 516 -> 32; frames (frec): camera yaw one-frame reversals 12 -> 1, and that one the car's own (heading -12, +40,
  -11 at the same frame: a wall); heading less yaw reversals 26 of 112 -> 6 of 100 moving frames.
- Candidate r64 = a4_r64_cand.exe (C63 + the camera fix, THIRDSTEP, scratch 0x1A0000):
  2fd6c8ebfc09fcc43a885f8609450c176c2d05371c8a1a71f1479164c80b6515, 919,032 bytes; twin y64; hooks as r63 (counters
  700BCA54, rec 700BA62C). Battery r64bat (r63bat on r64): running.
- Input (owner: "online lagginess"): the keys reached the car at the game's 85 ms whole step only. A4_LIVEKEYS
  (eng_keys_peek / eng_keys_merge, a4_eng.h; edits +14754, +14948; a4_eng_keys records the handler's object and
  mask): a substep builds the player's key word as the whole step does (+1477C..+14946, the 7/8 view keys skipped)
  from a copy of the key object (+25E8 is the handler: a 16-event ring at +0xD, one transition per key a call,
  [0] the last call's keys, [1] this call's; +2092 held / pressed; +20F6 the double tap from +0xA..+0xC; +25B6
  posts releases of the opposite steering through +257A) with the handler run on the copy -- the state the next
  whole step's handler will find, the game's object untouched -- and takes the steering bits only (1, 2, 0x200,
  0x400: actions 2-5, held). Kept the whole step's: 4 (the start: a press of action 1 or raw key 15; then the
  auto-throttle's 4 while the car moves and neither 8 nor 0x40 is set), 8 (action 0 held while moving: the
  brake), 0x40 (action 0 at rest or reversing), 0x20 -- the presses and the brake, which the race rules and the
  whole-step decisions (+1BE86 the boost, +1B5DA the drift) need at the step (the bench autopilot's
  "a stopped car with '5' held stays dead" is the start being a press). Not used in a race's end (+128's +28
  > 12, the word forced to 8). A key down before substep 1 steers 57 ms sooner, before substep 2 28 ms.
  Bench probe A4_KEYPROBE (4 words a substep, ring 0x5A2000 through a4_camp_emit; tools/keypro.py). The
  source without the flag compiles 12 bytes smaller than r64's (the new label edit), so r64 is a242513's.
- Battery r64 (exact binary 2fd6c8eb; the container restarted twice, the battery resumed from the Veyron run with
  the car archive put back first): every run whole, faults=1 each (the known one). Tele (calibrated bench,
  120 s): 40.9 ms a render (r63 39.3), 3D 17.7; camera yaw and heading reversals 0; held back 0, drop frames 0.
  **Crash-steer (cap 85): camera yaw one-frame reversals 0 (r63: 14)**, heading less yaw reversals 5 of 131
  moving frames (r63: 40 of 171). Race to the finish (both): whole; r64f2 met mode 0x18 (a scripted view, 46
  frames, bearing reversals 23: the scripted cameras still move at 11.72 Hz, not changed here). Menu, mark,
  Veyron autopilot (camera reversals 0), mark on the Veyron, Paris on the owner's save: whole. Scenery taken
  93.7% / 93.4% (LA), 72.7% (Paris); r64f2's 10 resets are 64-frame gaps without objects (the finish).
  Not a repeat of: r63's battery (the camera's pose), a4_cp2 / a4_cp3 (the probe, not the candidate).
- A4_LIVEKEYS probe (a4_kp = C63 + LIVEKEYS + KEYPROBE, crash-steer at cap 85, 70 s; the dump's PCMEM blocks
  carry a 12-byte header, keypro.py reads them): 1,536 substep records; of 64 steering changes, **26 reached the
  car on substep 1 (57 ms before the whole step would have read them), 18 on substep 2 (28 ms), 20 with the
  whole step** (the thirds a uniform press time gives). The bits not taken that differed: 4 (74) and 0x20 (61),
  the start press the script's re-press of '5' makes -- kept the whole step's, as meant. Car heading reversals 0.
- Candidate r65 = a4_r65_cand.exe (C65 = C63 + A4_LIVEKEYS, THIRDSTEP, scratch 0x1A0000):
  d364f1dba886a90df8cfa884a664e437417143956d69ec780d259f33e952c8cb, 919,448 bytes; twin y65 (rec 700BA62C),
  counters 700BCA54. Battery r65bat (r64bat on r65): running.
- Battery r65 (exact binary d364f1db): every run whole, faults=1 each (the known one). Tele (calibrated bench,
  120 s): 40.6 ms a render, 3D 17.5; presents a median 38 ms apart; camera and heading reversals 0; drop frames 4.
  Crash-steer: camera yaw reversals 0, heading less yaw 6 of 203 moving frames. Race to the finish (twin, the
  autopilot): whole, 2.07 u/ms in its last 14 s (r64f 2.29). Menu, mark, Veyron autopilot (camera reversals 0),
  mark on the Veyron, Paris on the owner's save: whole. Scenery 93.7% / 93.9% (LA), 71.0% (Paris).
  Two runs with '5' held by the script were slow at their end: r65t2's car crawled 0.12 u/ms on one heading for
  its last 16 s (r63t2 2.27, r64t2 1.82), r65f2's 0.06 u/ms with mode 0x18 (a scripted view) for 306 of 417
  frames. Checked, not settled by proof: LIVEKEYS takes only the steering bits (the probe: the other bits that
  differ are the kept press bits 4 and 0x20); the twin and the crash-steer runs drive (r65c2 0.46 u/ms against
  r64c2's 0.23); and s65 (the exact binary, '5' held, cap 85, 150 s, a shot every 10 s) accelerates to 188 km/h,
  is turned round by a wall ("REVERSE / ARE YOU LOST?"), gets GNN's report, a takedown, and ends on the grass at
  50 -- the bench driver, which steers by walls and has stuck before (round 63's oracle: both ended stuck).
- Rule 4 for r65. (a) Guesses: that the chase pose words (cam+0x10..+0x20) are outputs the game makes afresh
  each whole step -- made moot by eng_cam_unblend putting the game's own back unless something else wrote them;
  the cut thresholds (offset 1024 / 512 / 128, heading 256), not counted on the bench; that bits 1, 2, 0x200,
  0x400 are the held steering (the disassembly: +2092 with r2 = 1, +1DE78's turn-held 81C; the probe's changes
  follow the script's presses); the key object's 0x1D bytes (every read in +25E8, +257A, +2092, +20F6 is within
  +0..+0x1C); that the two slow runs are the bench driver (s65 above; more A/B runs would settle it). (b) Read
  back: eng_cam_pose, eng_cam_unblend, eng_keys_peek / eng_keys_merge, a4_eng_keys's record, and the edits as
  lifted.c carries them (+14754, +14948, +14CB2, +14EF0). (c) Not entered: chase modes 3 and 0x2A (1 and 2 only);
  a turn over 256 /2048 in a step; the phone's keypad and its key repeat; two steering keys at once (the
  release +25B6 posts, on the copy); a race's end with LIVEKEYS (excluded, not entered).

## Round 65 (owner: "Don't send. Fix the open issues first"): the slow runs, the scripted camera

- The two slow runs, settled as the bench driver. (1) The deterministic pair o3n / o3k (C63 + THIRDSTEP + the
  lockstep oracle's recorders, BENCH_SEED, AUTOPILOT_UNSTICK, LOCKSTEP 1 / P 7111; o3k + LIVEKEYS; jtrun 90 s,
  NOKEY5, cap 85): o3n against o3k part at step 119 after the start, o3n against itself (o3n2) at step 117 --
  the oracle is not deterministic past ~10 s (EKA2L1_DETTICK=50 to pin the tick stalls the boot: framestat 69),
  and LIVEKEYS parts no sooner than a build parts from itself. (2) q64 / q65 (C63 / C65 + CARREC only, '5' held by
  the script, cap 85, 120 s, alternating, three each; tools/crawl.py over the whole race's step records):
      q64_1 speed median 212, crawling 0%       q65_1 206, 0%
      q64_2 141, 27% (374 steps at once)        q65_2 118, 39% (215)
      q64_3  80, 34% (336)                      q65_3  48, 49% (390)
  r64 crawls as r65 does: the bench driver, which steers by the walls, sticks on something in two runs of three
  either way; three a side cannot tell the builds apart and the key word cannot either (no steering pressed:
  the merge gives the whole step's word). Not a repeat of r64bat / r65bat (one run each, the last 16 s only).
- The scripted camera (A4_CAMSCRIPT3, eng_cam_script in a4_eng.h): the crash and takedown camera (0x18, +554C)
  plays a keyframe script (cam+0x248; +5205A, read: a clock at +0x8C, an end at +0x90, a done byte at +0x88,
  eleven channels at +0x5C (Q12, x a scale +4 plus an offset +0x30 through +520F0), each with a keyframe list at
  +0x94; a call moves each channel by (keyframe - value) / (keyframe's step - clock), or takes the keyframe and
  unlinks it once its step is reached, then clock + 1) and turns it into the pose with the car (+3D2A: cam+0x10..
  +0x24 and cam+0xC from channel 10). The next whole step's channels are fixed by the lists already, so a substep
  places the camera with each channel j thirds of the way to its next value, on the car as it is now; the player
  held off (the done byte set for the call), the channels put back, so the script stays the whole step's. Not
  before the mode's setup ran (cam+0xC0 0x18; +554C would run +54E8, which allocates). Probe a4_cs (C66 +
  CAMPROBE, a race to the finish, '5' held, cap 85): 96 substep placements in one crash; from the script's 4th
  step on the camera moves 8-13 units a substep in one direction (cnt 1625-1665) -- and then 39-47 at the whole
  step, the wrecked car's own move: a wreck moves on the whole step only (+1DFE6; its path +2292A integrates the
  car's velocity from a base, the reason round 50 kept it at the game's step), so in a crash the car and the
  world still step at 11.72 Hz while the camera's motion around the car is smooth. At the crash's start (clk 2-3)
  the car's own heading swings between substeps (1996, 1941, 2020, 1892): the 3x contact on the moving car.
  C66 = C65 + A4_CAMSCRIPT3 (a4_r66t.exe, 920,156 bytes; not a candidate yet: no battery).
- The wreck at 3x (A4_WRECK3, owner: "go with the crash motion"). A wreck (+034 != -1) ran on the whole step
  only (+1DFE6, +1EA68, the measure x 1): in a crash the car, and so the camera and the world, stepped at 11.72 Hz.
  +2292A read: per step v = the measured velocity (+010) +/- +1C's vector x the path entry's first word >> 12, -
  +18's x its third (the entry halved when the controller's +1D9CC is clear); near the ground (y - +0D0 < 30,
  frame > 2) v x (n - 1) / n, n the frames left; position = +0A8 (the last measured one) + v, the height the
  entry's +38 + the ground, less a bounce 25 x sin(frame << 13) when the controller's +B28 bit 0x1000 is set;
  the falling case (+B28 bit 0x800): position += velocity, y - 2. Frame +034 and step +048 + 1 a step (2 with
  the double rate), the end at the path's length or +048 past 0x24 / 0x48. At 3x (ENG_WRECK3; edits +1DFE6,
  +1DFEE, +1EA68, +1EABC, +2292A's): every substep runs it, measured x 3 like any car; the rpm's -0x100, the
  push (eng_thirdv per word) and the damping ((3n - 1) / 3n) a third; the position + a third of v (eng_thirdv);
  the height closes on the path's point by the step's end (y0 + (target - y0) / (3 - j), 1 on the last); the
  falling case a third of vy and a ninth of the 2 (thirding y + vy - 2 whole would take the 2 off the measured
  velocity every substep: 3x gravity); the frame, the step counter and the end on the step's last substep
  (GE; ENG_SUB_END loses its wreck clause under A4_WRECK3). tools/lift.py: a rate edit may carry a condition.
- Probe a4_cw (C67 = C66 + WRECK3, + CAMPROBE; a race to the finish, '5' held, cap 85, 300 s): 144 crash-camera
  substep placements; the wreck moves every substep (x 27049, 26983, ..., 66 a substep), the camera with it
  (66-68 a substep), the car's heading turning 778, 778, 729, 687, 687, 652, 604, 550 -- against a4_cs's (C66,
  the wreck at the step) 1996, 1996, 1996, 1941, 2020, 1892: its crash start swung because part of a wreck ran on
  the substeps (the orientation's lag, the contact) and part not; whole, it turns one way. faults=1 (the known).
- A/B w0 (C66 + CARREC) / w1 (C67 + CARREC), the race to the finish twice each (tools/wreck.py over the step
  records): w1_1 one wreck, entry 226 a step, 35 steps, 6,079 units, the steps 194, 170, 174 ... 188 (flat),
  then 175 down to 92 (the damping) and the respawn's 305, the height a smooth curve (-288..-229..-364); w0_2
  one wreck (the game's step), entry 289, 36 steps, 7,929 units, 96 then 270 flat, then 264 down to 87 and the
  respawn's 619; q64_2 / q65_3 (earlier, the game's step) the same shape. w1_2 and w0_1 no wreck. The 3x wreck's
  path has the game's shape and scale; no wreck flung (round 50's r7). Its flat stretch drifts up ~1 a step (the
  push's thirds); not settled whether stock's would at that crash.
- Candidate r67 = a4_r67_cand.exe (C67 = C63 + LIVEKEYS + CAMSCRIPT3 + WRECK3, THIRDSTEP, scratch 0x1A0000):
  f35c3426bede7809ad94425acbbdfae576fb68cb93a14c39e9b245ed5b87727a, 921,044 bytes (= a4_r67t, cmp'd); twin y67.
  Battery r67bat: every run whole, faults=1 each (the known one). Tele (calibrated bench, 120 s): 38.6 ms a render,
  3D 16.4, comp 7.3 (read 5.9); camera and heading reversals 0; held back 0, drop frames 2 (the car stuck in its
  last 16 s, the bench driver). Crash-steer: camera yaw reversals 0, heading less yaw 3 of 117 moving frames. Race
  to the finish (twin; exact binary, '5' held): whole. Menu, mark, Veyron autopilot (reversals 0), mark on the
  Veyron, Paris on the owner's save: whole. Scenery 93.1% / 96.1% (LA), 71.3% (Paris). No crash camera in this
  battery's recorded windows; a4_cw's race (above) is the crash's run. +3A15C b510 0004, no 0xDEADBEEF.
- Rule 4 for r67 (over r65's). (a) Guesses: that the crash script's next value is fixed by its lists (true while
  nothing adds a keyframe before the head between steps -- +51FEC links new nodes before the sentinel, at the
  tail); that the slow motion's between-step camera calls (+1300C) leave the fractions harmless (not entered:
  no takedown in slow motion on the bench); the wreck's thirds (a 1/step drift up in w1_1's flat stretch, not
  settled against stock's at the same crash); the falling wreck's ninth of the 2 (not entered: no falling wreck
  seen). (b) Read back: eng_cam_script, eng_sdiv, the edits in lifted.c (+1DFE6, +1DFEE, +1EA68, +1EABC,
  +2292A's eleven, the GE gates), lift.py's conditional thirds as generated, ENG_SUB_END under A4_WRECK3.
  (c) Not entered: a takedown in slow motion; a falling wreck (+B28 0x800); an opponent's wreck seen through the
  takedown camera; a wreck that starts on substep 1 or 2 (counted on the step's end, not seen in the probe).

## Round 67 (owner: "start on that remaining job"): the frame under one 3x update (28.4 ms) -- the cars first

- The target: a frame no longer than the engine's 28.44 ms step, so each frame draws one real update (35 fps,
  even motion) -- the owner's choice over drawing between states.
- Today's cost of the cars (ceil67.sh, W-4 model + EKA2L1_GPUMODEL_CALLS=1, 120 s each): b67 (r67) 41.5 ms a
  render, render call 32.8, before the first triangle 4.2, 3D 18.1, comp 7.5 (read 5.9); cc67 (r67 +
  A4_CEIL_CARS, the CBasicMesh wrapper +34AEA returning at once) 32.6 ms a render, render call 24.3, before the
  first triangle 1.8, 3D 12.5. The cars are 8.9 ms of the frame. Not a repeat of round 57's ceilings (r57's
  half-step build, no GPU scenery).
- The car census (A4_CARCENSUS, bench; a4_car_census at the lifted mesh routine's +34A10 when called back to
  +34B16; tools/carcen.py; 60 s, cap 85): 7 calls a frame (max 11), ~1,900-2,100 faces; 18-19 distinct meshes:
  the player's (393 vertices, 702 faces, mode 2: 542 faces class 0x104000 -- the paint's reflection -- and 160
  0x4000; 3 descriptors, 255 x 255), opponents' (171-248 vertices, 272-376 faces, mode 1, class 0x4000 with a few
  never-drawn faces holding 0x100000; one 127 x 127 texture), small ones (40-65 vertices, 34-78 faces). Every face a
  TFace (+B08EC); no coloured-key or alpha texture; texcoords x16 (0..4080, some v -32). In the chase view the
  player's car sits at z -379..-170 (in 57 of 1,157 calls a vertex nearer than 126: a camera's change); an
  opponent beside or behind the camera now and then.
- The mesh routine read (+349F8): per face the outcodes' OR (0x20: skip) and AND (skip); the 3D back-face test
  +3118E when [sp+0x44] is 0 and the mode is not 0; the class: the mode's words from the table at the literal
  +34DD4 ([mode*8] near, [mode*8 + 4] otherwise), kept whole when the face's own flags hold 0x4100000, else
  without those bits; modes 0 and 6 add the colour-key and alpha bits; the insert +349B0.
- A4_GPUCAR (a4_gl.c, with A4_GPUOBJ; edit +34A18 in the lifted mesh routine, after the game's transform and its
  whole-mesh outcode check): a car mesh (the caller +34B16, a mode 1-7 with the back-face test) is baked once into
  the scenery arena as per-corner records grouped by (descriptor, class) -- class 0x104000 bound through tri_tex
  with the mode-1 bake, 0x4000 plain, anything else refused -- and the routine's per-face work skipped; at the
  frame's first triangle (after gobj_flush) each car is drawn with the list's projection and its own matrix
  [cam+0x3C] as the modelview, a draw a group, depth-tested and writing, back faces culled; its sphere's screen
  box and buckets go into the list's grid (gobj_grid, gobj_bb_deep). Refused: a mesh nearer than the list's near
  plane, a vertex or texcoord past int16, a translation past 32,000, a full store. Its own state at 0x466000
  (items, 24 meshes), the arena shared with the scenery (emptied together).
- gc1 = r67 + A4_GPUCAR: snapshots (cap 85, 60 s, ten): the player's Mini with its paint and reflection, the
  opponents, the HUD -- as r67's. g68 (the W-4 model + GPUMODEL_CALLS, 120 s): **34.7 ms a render (41.5)**,
  render call 26.3 (32.8), before the first triangle 2.2, 3D 13.5, comp 7.5 (read 6.0); in the last windows 32.5-
  32.9 ms (3D 12.0-12.8, comp 7.9-8.2, 2D 2.1-2.5, the yield 2.7-3.0, the rest 6.8-7.3); spans of one substep
  2,622, of two 758. Cars taken 13,900, 7.3 draws a frame; refused near 170, outside a mesh frame 734.
- Where the rest goes (gc1). A cycle profile (p68c: W-4 model, EKA2L1_PCPROF_CYCLES, 1.5G instructions after
  6G): our code's functions -- a4_cvt565_run 13.3% (the runtime's present convert, 565 to the screen's 32-bit, a
  pass over the frame a post), a4_comp_row 9.4% (the composite, BGRA to 565), a4_engv_29736 6.4% (the painter),
  gobj_flush 6.4% (the scenery's staging copy), engi_349F8 5.8%, lean_tail 4.1%, batch_draw 3.1%, bb_row2 1.9%,
  a4_obj_take 1.9%, tex_upload 1.8%, tex_index 1.5%; the game's own 28.8% (+33EA8 5.3%: the alpha span; +49480
  2.3%). The mode-1 re-bakes are rare (m1_rebakes 28 in the race, 0 texels in the windows): not a cost now. A
  gated profile (ON a4_gl_hud, OFF cvt_fast) opened once and never closed -- not used.
- Ceilings (bench only, W-4 + CALLS, 120 s): no present convert (A4_CEIL_CVT, cv68) 32.1 ms a render; no composite
  rows (A4_CEIL_COMP, cp68) 32.5 -- against gc1's 34.7: each ~2.5 ms, neither removable as it stands.
- The scenery's staging copy in ARMv6 (a4_px.s a4_gobj_copy: three corners a turn, SADD16 on the halves; A4_PX6):
  checked against the C loop (A4_GOBJCOPYCHECK, cap 85, 60 s): 5,978 runs, 0 words differing. Timed (gc2, g68b):
  34.8 ms a render against gc1's 34.7 -- no change past the runs' noise (~1 ms).
- The model's GPU charges a frame (g68b's heavy window): 542 GL calls, 158 draws, 9,965 vertices, 134 binds, 15
  reads -- draw 4,649 us (draws 3.1 ms, vertices 1.5), tile render 3,829, readback 1,536, post 2,300, other 2,696
  (calls 1.6, binds 1.1): ~15 ms of a ~33 ms frame, every charge serial in the model. The next levers are the draw
  calls, binds and calls (~5.8 ms) and the present path (convert 2.6 + composite 2.2 on the CPU, post 2.3, read
  and tile 5.3) -- not CPU loops before the read.

## Round 68 (owner: "No, let's go for a phone round instead"): candidate r68

- r68 = a4_r68_cand.exe (C68 = C67 + A4_GPUCAR; the ARMv6 staging copy under A4_PX6; THIRDSTEP, scratch
  0x1A0000): f5231dc61dc3ad1f2396e1803d658fe8b9efc8f76e4796d0b8e466aadf7eca71, 927,116 bytes (= gc2, cmp'd); no
  bench-only flag in C68. Twin y68 (+ CARREC YREC AUTOPILOT; rec 700BA62C). Counters a4_gobj_emit +0BD860 (the
  scenery's 0x74, the cars' 0x40 at +0x2000; gobjsum.py reads both). Over r62 (never sent) and r60 (the phone's
  last): the scenery's fixes (the grid, the near plane), the 3x engine with real frames only (r63), the chase
  camera from the car (r64), live steering (r65), the crash camera on its script and the wreck at 3x (r67), the
  cars on the GPU (r68).
- Battery r68bat (exact binary; restarted once: the container's restart left /tmp/.X99-lock, Xvfb would not start,
  the twin's first run had no display -- cleared, rerun whole): every run whole, faults=1 each (the known one).
  Tele (calibrated bench, 120 s): **34.6 ms a render (r67 38.6)**, 3D 13.6, comp 7.5 (read 5.9), 2D 2.3; camera and
  heading reversals 0; drawn speed off its neighbours by >25% in 19% of frames (r64 33%: more frames of one update);
  held back 0, drop frames 2. Crash-steer: camera yaw reversals 3, each at a car heading reversal of its own (the
  wall's push, 5 in this run; 0 in r67's -- the route's own variety: r68 changes no physics). Race to the finish
  (twin; exact binary, '5' held), menu, mark, Veyron autopilot (reversals 0), mark on the Veyron, Paris on the
  owner's save: whole. Cars on the GPU: LA 13,842 taken (near 243 refused), the finish 41,437 (57 meshes built over
  11 store resets), Paris 18,103 (9.8 draws a frame); refused for class, texture, range or a full store: 0;
  stale 0. Scenery 94.1% / 93.4% (LA), 71.5% (Paris). +3A15C b510 0004, no 0xDEADBEEF.
- Rule 4 for r68 (GPU cars). (a) Guesses: that the game's own back-face test and GL's culling agree (the same
  corner order as the scenery's TFaces; bench snapshots show whole cars, not proven face by face); that the mode-1
  bake at the frame's first triangle reflects what the table's late draw did (in GL mode the 565 buffer holds the
  last frame's picture at both times; re-bakes are rare); the nitro flame and other table sprites over the car
  (depth-tested in the car's grid tiles; round 28 found the game's order there unlike any depth test -- seen in
  snapshots only); faces never drawn yet given their class by the routine's rule; a car crossing the near plane
  switching between the GPU and the game for a frame (two rasterisers: a change of look at that frame).
  (b) Read back: a4_car_take, gcar_mesh, gcar_flush, gobj_grid's and gobj_bb_deep's car loops, the +34A18 edit in
  lifted.c, a4_gobj_copy and its call. (c) Not entered: a mesh of more than 6 texture groups or a held store of 24
  meshes (refused / reset), a car mesh whose texture changes under the same address (the fingerprint), the garage
  and menus (refused outside a mesh frame), the phone's own GPU on per-car modelviews.

## Phone round 68 (r68 f5231dc6): "impossible to play" -- the speed keeps resetting, the car slides as on ice and
## will not turn, a traffic car goes back and forth; "I think physics are broken"

- Telemetry (8 captures, capidx 46-53, the last ~22 s of the run; copied to the scratchpad's phone68/): 52.2 ms a
  render (bench r68t2 34.6); 3D 24.7-28.7 (bench 13.6), comp 9.0-10.0 (read 6.9), 2D 2.8; render call +12992 43.6
  ms; steps 2,415 in 1,272 spans (n = 1: 298, 2: 869, 3: 52, 4: 42, 5: 11; the bench's n = 4-5 is 2 in 3,410),
  1.2 ms a step; textures remade 1,518 (bench 0-115), 78 held, 92 evicted (bench 0): the phone's texture store
  thrashes, a draw cost of its own.
- The frame record (417 frames, the player's car as drawn): the car crawls, ~0.1 u/ms for the whole 22 s (r68t2
  ~2 u/ms in a race): first reversing (moving along ~+17 deg while its heading is 1136/2048, ~200 deg), then
  forward along ~225 deg while its heading swings 1136 -> 1281 -> 1160 -> 1226 -- the motion does not follow the
  heading (the "ice"). Camera yaw and heading one-frame reversals 0: the camera follows the car; the car itself
  is what is wrong.
- The 3x engine (r63+) ran on a phone for the first time in r68; r60 (the last phone build) ran none of the
  lifted engine functions (A4_ENG_ALL was new in r63). So every engine change since r60 is untested on hardware.
- Bench s68h (r68 + CARREC, '5' held, a heavier GPU model 6,45,300,16,5000,800,5,30,2300 for the phone's frame
  lengths): 37.0 ms a render call (not the phone's 52), spans n = 1: 1,073, 2: 1,492, 3: 47, 5: 2; the car drives
  (~1.5 u/ms, heading and motion together). Frame length and two-step spans alone do not reproduce it.
- Next (bench): what the phone has that the bench does not. The lifted functions' frames are C arrays
  (stk[], uninitialised); on the phone the GL driver's own code and euser's run on that stack between the
  game's steps, on the bench the GL is the emulator's (off the guest stack) -- a slot a substep reads that this
  call never wrote is the previous call's value here and junk there. A4_STKPOISON (bench only: lift.py emits a
  call at each lifted function's entry, a4_stkpoison in a4_eng.c fills its frame with xorshift junk; r68 rebuilt
  without it byte-identical, f5231dc6). Run p68: running.
  Not a repeat of: round 63's oracle o1/o3 (no junk on the stack), the r68 battery (the same).
- p68 (r68 + CARREC + A4_STKPOISON, '5' held, 120 s): the car drives (~2.5 u/ms). Junk in the lifted frames is
  not it.
- The phone's frames, read closely: from the car's first forward frame its step is exactly (-2, -2) units a
  substep (-4/-4 a two-substep frame, -6/-6 three) for 17 s -- no acceleration, no deceleration, the direction
  fixed at 225 deg while the heading went 1281 -> 1160 -> 1226: a frozen velocity. The screenshot: Paris at
  night, the Veyron, lap 1/3, WANTED, 9 km/h with the nitro's flame.
- The bench has had it since r65 and it was read as the bench driver: diagscan.py over every bench capture in
  the scratchpad -- r67t2 143 frames of a fixed whole-unit step (6/4, 5/5, 4/6 along a guard rail, 64 km/h on
  the speedometer, WANTED), r65t2 the same at its end ("crawled 0.12 u/ms on one heading for its last 16 s",
  round 65). r63t2 and r64t2 clean.
- Caught on the bench with the car recorder: st1 (q68 = r68 + CARREC, '5' held, 120 s): steps ~732-1160 (36 s)
  the velocity frozen at (6, 15, 12), then (6, 15, -3), against a target (+680) of (70-77, 0, 45-50); not a
  wreck (+34 = -1), no contact after step 734 (+E0 counts up), height on the ground (+8 = +D0). It froze as a
  contact with another car ended (+DC set, speed 102 -> 15 over steps 700-730). The velocity is the measure's
  (+22832: 3 x the substep's displacement, every component a multiple of 3), the displacement a third of the
  velocity: once the control's change is nil, (6, 15, 12) is a fixed point.
- And the grip. +9FC (the traction's source: T = grip x 500 / 144, +526E0) in every recording: stock (o1)
  1,048,576 (its ceiling) in 2,026 of 2,031 race steps, y60b (the stock engine) 1,048,576 in all 736; every 3x
  recording (o3, y63b, y68b, st1) 4,096 in every race step. The 3x engine has driven every car on 0.4% of its
  grip since r63. The grip rises 0xD55 a step on the road (+1E602) and is set to 4,096 by the contact routine
  (+1EAAC) when car+B28 bit 7 is set (the car-to-car collision +1FDEC sets it, +1DE78 clears it at entry) -- but
  bit 7 is never set at a record, so the writer of 4,096 is not yet known. Next: EKA2L1_WATCH on the player's
  +9FC (0x023FA41C on q68), each writing PC once. Run gw1: running.
- gw1 (EKA2L1_WATCH=023FA41C, WATCHONCE): writers +1AB8E (the game: 0x100000 at the car's set-up), the lifted
  +1DE78 (+C9240 / +C9250: the rise and its ceiling) and the lifted +1EA64 at +0CD020: 0x1000. bw1 (the same on
  car+B28): bit 7 is set by +1FDEC (the car-to-car hit) and cleared by +1DE78 -- and vp68 (A4_VELPROBE: per
  substep, the player's control) shows car-to-car hits on 42 of ~1,600 substeps, real ones (partners 70-180
  units away), while the grip at the velocity change is 5,233/5,234 on every substep (4,096 + a third of 0xD55).
- The cause, in the compiled code: +0CD010.. in a4_q68.exe is `add r0, sl, #0x9c0; ...; mov r0, #0x1000;
  str r0, [sl, #0x9fc]` -- no load of car+B28, no test. In the IR (clang -O2 -emit-llvm of a4_eng.c with C68's
  flags) the test is `tail call void @llvm.assume((B28 & 0x80) != 0)`: clang proved the bit-clear path undefined
  and removed it. The lifted functions keep the game's stack in a C array (stk[]) never initialised, and read
  slots this call never wrote (stale stack to the game's Thumb; an undefined value to C); the grip store's pointer
  could alias the array, so only the other path read a provably undefined slot. The IR also passes undefined stack
  words as `noundef` arguments at three calls (+1CDDA, +239B6 -- the box test --, +22C60 from their callers):
  immediate undefined behaviour too. A4_STKPOISON (p68) wrote the arrays through an opaque call, which is why its
  grip was stock's: 1,048,576 in 738 of 755 race steps, 0 / 4,096 / rising after real events.
- So every car in every build since r63 (the first to run the lifted engine) drove on 0.4% of its grip: the car
  reaches its velocity target at most T/48 a substep with T = 18,170 instead of 3.6 M -- it slides ("icy"), does
  not follow its heading ("impossible to turn"), and every knock costs speed it regains slowly ("the speed keeps
  resetting"); a car pinned against another or a rail can sit at a fixed velocity indefinitely (st1, the phone).
  The bench's straight '5'-held drives barely need grip, which is why they looked right.
- The fix (tools/injector.py): a4_eng.c (which includes eng/lifted.c) compiled with -ftrivial-auto-var-init=zero:
  every local zeroed, nothing undefined left for the optimiser (A4_NOZEROINIT=1 builds the old way). The zeroing
  goes through __aeabi_memclr4 (a4_eng.s, new: the image links no C library). IR with it: no llvm.assume, no
  undefined argument. Compiled +1EA64: `ldr r0, [r8, #0xb28]; tst r0, #0x80` before the grip store. z69 (C68 +
  the fix): 928,660 bytes (r68 + 1,544). Run zq1 (z69 + CARREC, '5' held, 120 s): running.
- zq1: whole (faults=1, the known one). Grip in the race's steps: 1,048,576 in 991 of 1,258; 4,096 (17), 0 (8) and
  the rise after each (5,234, 6,372, 8,647 ...) -- stock's pattern. +E4 never above 0 (stock's; r68's runs 1-6).
  |velocity - target| (x + z, the race's moving steps): median 2, 90% 37, over 6 in 20.0% (stock o1 0 / 12 /
  11.9%, a lockstep autopilot drive; r68 s68h 4 / 72 / 42.0%, st1 26 / 116 / 64.2%; p68 2 / 16 / 15.0%). Speed
  (+B4) median 206 (r68 113-145, p68 217). Not a repeat of: p68 (the arrays written through an opaque call, not
  zeroed; its build is not a phone build). Next: o3z (the round 63 oracle on C68 + the fix, jtrun 90 s, against
  o1) and tc1 (EKA2L1_WATCH on the traffic's two clocks, race+0x378/+0x37C = 0x02039F48:0x02039F50): queued.
- o3z (round 63's oracle, C68 + the fix + CARREC YREC AUTOPILOT(_UNSTICK) BENCH_SEED LOCKSTEP, jtrun 90 s,
  against o1, the game's own engine): the countdown the same (revs from record 101, moving at 149); speed to
  20/40/60/80/100/120/140 at steps 0/0/10/22/37/45/54 (stock 0/1/12/23/38/46/56); gears 2, 3, 4 at steps 32, 85,
  292 (stock 32, 85, 291); top speed 264 both; distance -0.1% at 10 s, -1.0% at 20 s and at 30 s; B4 at 14.3,
  18.4, 22.5, 26.6, 30.7 s: 222/227, 128/131, 198/199, 176/180, 231/230 (3x/stock). Round 63's o3 on the same
  route: gear 4 at step 424, distance -11.2% at 20 s, B4 124/227 at 14.3 s -- it parted from stock after ~8 s,
  which round 63 read as the paths' chaos; it was the grip. Both runs end stuck (stock from ~38 s, 3x ~47 s:
  the bench driver, as round 63). 3x: one wreck (step 493) and 17 car contacts, stock 1 -- the 3x drives on
  after stock stopped. Not a repeat of: o1/o3 (round 63, before the fix). Its car contacts all come after record
  560 (35 s), where stock had stopped; before, neither run has any.
- tc1 (z69 + CARREC, EKA2L1_WATCH on race+0x378..+0x37F, 40 s): both traffic clocks are written by the lifted
  +141B0 (the race rules, the whole step) and advance by exactly 256 (the time scale race+0x84C) a whole step, 369
  of 369 writes -- the substeps' traffic (eng_tmul: the clock plus a third / two thirds of 256) is on an even
  step, not the owner's car going back and forth. Every racer and pursuer had the player's 0.4% grip (the cause
  is in +1EA64, run for every car), so a car that could not hold its line is the likelier one: covered by the fix,
  not proven -- the phone round will say.
- Candidate r69 = a4_r69_cand.exe (C69 = C68's flags; the fix is the build's: a4_eng.c zero-initialised):
  e42821754d62007043c9a93722438405e4dcb59f9b0e0b4d9fabed0c1c6db48f, 928,660 bytes (= z69, cmp'd); +3A15C b510
  0004, no 0xDEADBEEF. Twin y69 (+ CARREC YREC AUTOPILOT; rec 700BA62C). Battery r69bat (r68bat on r69, plus the
  twin's grip and diagscan.py over its runs): running.
- The phone's frame against the bench's on the same track (Paris, the owner's save; pa_r68c is r68 on the bench):
  draw calls a render 76 / 82, triangles 639 / 651 -- the same work -- but the time inside our GL hook in the 3D
  pass 18.8 / 3.7 ms and the batched draws 4.13 / 0.74 ms: the phone's driver costs about five times what
  EKA2L1_GPUMODEL charges for these calls. Textures remade 26 / 3.3 a window. The model was last calibrated
  before the scenery (r60) and the cars (r68) moved to per-object draws (a modelview and a client-side vertex
  array each): the next draw-time work starts from this, not from the model's 34.6 ms.
- Battery r69 (exact binary e4282175): every run whole, faults=1 each (the known boot-time one, before the first
  frame). Twin y69b (jtrun, autopilot, 120 s): grip 1,048,576 in 596 race steps, 0 (in the air) 15, 4,096 3, the
  rise after; +E4 never above 0; distance 306,483 over 1,292 steps, speed median 242 (y68b on the same route:
  143,245 and 113). Tele (calibrated bench, 120 s): 36.6 ms a render (r68 34.6: the car now drives faster through
  more of the city), 3D 15.0; camera yaw and heading reversals 0; held back 0, drop frames 0. Crash-steer (cap
  85): camera yaw and car heading one-frame reversals 1 each, at the same frame (heading -12, +52, -13: a wall's
  push; r68c2 3 and 3); modes 2, 0x18, 0x2A. Race to the finish, exact binary, '5' held (r69f2): reaches the
  race's end camera (0x17) and stays -- r68f2 never left the chase view in 300 s. The twin's race to the finish
  (r69f) never reached a race: from post 1,501 every post the same 1,660,408 instructions (a static screen; the
  menu walk after the battery's earlier races changed the save) -- rerun with the bench save put back: running.
  Menu, mark, Veyron autopilot (camera reversals 0), mark on the Veyron, Paris on the owner's save: whole; Paris
  moves 2-3 u/ms along its heading. Scenery 94.2% / 93.9% (LA), 78.5% (Paris); cars on the GPU 15,998 / 35,516 /
  15,884 taken, stale 0. diagscan.py over t2, c2, f2, v, Paris: the longest run of equal x/z steps 3 frames (r67t2
  143, the phone 317). +3A15C b510 0004, no 0xDEADBEEF.
- Rule 4 for r69 (the grip). (a) Guesses: that zero stands in for the stale stack the game's Thumb would read in
  every slot the lifted code reads unwritten -- checked for the player's car by the oracle (o3z: speed, gears and
  distance within 1% of stock for 35 s) and the twin's grip, not word by word for the opponents, the wreck, the
  camera or the lifted render functions (painter, 3D front end, mesh), which the battery runs only as wholes; that
  the class is closed -- the IR of a4_eng.c with the flag has no llvm.assume and no undefined value, but the other
  C sources (a4_gl.c, a4_prim.c, a4_tel.c: hand-written, not lifted) were not audited; that the owner's "traffic
  car going back and forth" was an opponent or pursuer on 0.4% grip (tc1 rules out the traffic's clocks, nothing
  proves the rest). (b) Read back: the injector's flag and its opt-out, __aeabi_memclr/4/8 in a4_eng.s (the
  8-byte loop, the byte tail, n = 0, an unaligned start), the compiled +1EA64 (`ldr r0, [r8, #0xb28]; tst r0,
  #0x80` before the grip store), the IR counts with and without. (c) Not entered: the phone with the fixed engine;
  a police pursuit with grip (the pursuers grip too now); the 3x edits of rounds 63-67 that were tuned while every
  car drove on 0.4% grip (the wall's push eng_kick, the crash camera, the wreck at 3x) -- run in the battery, not
  re-derived.
