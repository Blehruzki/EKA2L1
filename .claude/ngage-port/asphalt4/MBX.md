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
