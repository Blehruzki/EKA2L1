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

Verified on the bench (stock vs. composite, same race): sky, player car, the
3D scene, minimap, speedometer, boost bar, position/lap/WANTED all render
correctly, 0 faults. One artifact remains: a thin green/red/white bar at the very
top edge (the HUD banner strip), present only when the primitive-capture hook is
installed. It is written by the game itself into its own surface (0x911088,
confirmed by reading that memory), not by any composite code -- the composite
never touches those rows (`CLIP_TOP`), and the GPU readback there is the magenta
sentinel.

**Root cause (run down, bench rounds E-series).** The bar is a *runtime
code-integrity response in the game*, triggered by the one byte-level change the
GPU path cannot avoid: the capture detour overwrites `draw_primitive`'s first two
Thumb instructions at `+0x300B2` with a BLX. Isolation, each a bench run:

- Not the blit, the fill-hook, or GL -- a capture build with no blit, no
  fill-hook and no GL (`--capture` without `--gl`) still shows it.
- Not a dyncom recompilation quirk -- it is identical under dynarmic.
- Not the hook's work or its per-prim cost -- a *passthrough* trampoline that
  does nothing but re-execute the two displaced moves and return (functionally
  identical to stock, zero added behaviour) still shows it. The only thing left
  is the changed code bytes at `+0x300B2`.
- The clean v5 build modifies other code (the fill/loop veneers at `+0x2ABAA`,
  `+0x3357A`, ...) and has no bar, so the game checks a specific region that
  includes `draw_primitive` but not those points.

So the game detects the modification at `draw_primitive` and degrades the HUD
banner in response. Making it go away means either not modifying that region (but
that is exactly where primitives must be captured) or stopping the game reacting
to the modification -- i.e. defeating an integrity check. That is anti-tamper
work, which stays with the project owner, not this performance track. The
composite rendering itself is correct; the bar is the game's response to the hook,
and whether it also appears on the device depends on the owner's tamper handling
there.

## Status

Stages 0, 1, 1b, 2, 3a, 3b complete; stage 4 core done -- the full textured 3D
scene renders on the GPU into the game's surface (41 textures, ~741 prims/frame,
0 faults). The software-rasterizer skip is implemented and renders; its CPU win
is a device measurement (see above), with the `-DA4_NO_BLIT` build as the
instrument. The 2D HUD now composites correctly over the GPU 3D (see above). The
one remaining artifact, a bar in the top HUD banner, is run down: it is the
game's own code-integrity response to the capture hook's byte change at
`draw_primitive`, so it is anti-tamper work for the project owner, not this
performance track (see above). Remaining on this track: device pixmap.
Foundation proven end to end (inject -> link
libGLES_CM past the guard -> ES1.1 context -> GPU render -> presented frame), and
the game's 3D pipeline is mapped down to the per-triangle hook `+0x300AE`, with
one real textured triangle now drawn by the GPU. Stage 4: drive ALL the frame's
textured primitives through GL into the game's actual render surface (not a
corner) -- a texture cache keyed on `desc+0x30`, batched draws, the correct
viewport from the render context, and the software 2D HUD composited over the
GPU 3D -- then measure the win against the software renderer.
