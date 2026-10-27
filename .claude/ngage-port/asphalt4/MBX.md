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
