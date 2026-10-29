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
