# Asphalt 4 (N-Gage 2.0): why it is slow, and making it faster

A side quest beside the gate6 port. The subject is Gameloft's Asphalt 4: Elite
Racing for the N-Gage 2.0 platform, in a community build with the trial
removed and some assets taken from the Symbian HD version. It installs and
plays on an N-Gage 2.0 phone (user-tested). The goal: find what makes it the
slowest Asphalt on S60v3, patch a smoother general build, then a PowerVR MBX
build for the N95 class. **General version first**: measuring decides where the
time goes, and an MBX renderer only pays off if the software renderer is the
bottleneck.

The game files are commercial data and are never committed. They live in the
scratchpad (`a4/`), with the N-Gage app installer the user supplied.

## User's report (before any change)

About 10-15 fps on the N95 by eye, and much worse when a race ends and the
finish animation plays. The Symbian HD version does not use OpenGL ES either
(user-confirmed).

## The package

`Racing.4_v20-no-startup-dialog.n-gage` is MIME multipart (`--KBoundary`):
retailer XML, a metadata SIS (shop icons, `c:\private\20007B38\import\`), and
the game SIS. `tools/ngsplit.py` splits it, `tools/sisx_extract.py` lists and
extracts a SIS.

The game SIS: `asphalt4_ngi.exe` (764,856 bytes, uncompressed EABI E32, UID3
0x2001092B, stack 0x5000, heap max 16 MB), `ngifilemanagerfactory_2001092b.dll`,
eleven `.bar` archives (`main`, `tracksa`-`g`, `carsa`, `carsb`, `sounds`), MP3
music, a 4.8 MB intro video, and `private\2001092B\ASPHALT4_NGI.EXEv`. That last
file starts `deadbabe` and looks like an integrity record of the exe. Whether
the platform checks it against a patched exe is still open: the community
build's exe already differs from retail, and it runs.

## The binary (static reading, not yet measured)

- **No OpenGL ES import.** All 3D is Gameloft's own software renderer
  (`Lib3D::CMesh`, `TFakeZFace`, `CSoftLine3D` in the RTTI strings).
- **Thumb code.** Of 1,431 call sites into import stubs, 1,410 are Thumb BLs
  (`tools/callcount.py`). Thumb-1 on ARM11 has no conditional execution, eight
  low registers and no 64-bit multiply: the leading suspect for the inner loops.
- **Software division is heavy.** `__aeabi_idivmod` 279 call sites,
  `__aeabi_uidivmod` 35. These cores have no divide instruction.
- **Floating point is not the problem.** `dfpaeabi` imports single-precision
  only (`fadd`, `fmul`, `fdiv`, `f2iz`, `i2f`, `ui2f`, `f2d`, `d2f`), about 40
  call sites in all. The renderer is integer.
- Graphics go through the NGI runtime (`runtime.dll` ordinals 9-12 hand out
  interfaces; calls are virtual, not imports), so how a frame reaches the panel
  (rotation, colour conversion, scaling) is not visible from the import list.
- A built-in FPS counter exists: the format string `FPS:%3s.%1s %d` and
  `CFPSCounter`. Switching it on is the measuring instrument for the phone.

## The bench

The N-Gage app 1.40 for S60v3 (`N-Gage_v2.0_for_s60v3.zip`): its outer SIS
drops `d:\private\2000a2ae\n-gage.sisx`; that inner package installs directly
with `eka2l1_qt --install`. The zip's second SIS ("Patch & UnPatch") only
replaces `playcommon.mbm` and is not needed.

On the RM-409 bench, the game first died in the PlayServer: **USER 8**. That was
an emulator installer bug (`EMU_PATCHES.md`, "A package that spells one new
directory two ways"), found with the new `EKA2L1_PANICSTACK` probe. Fixed and
reinstalled, the game boots to its sound prompt (in French, the bench phone's
language) and, untouched, starts a **demo race on its own**: a benchmark that
needs no input.

Install order on a fresh bench: `n-gage.sisx`, then the `.n-gage` file's
metadata SIS, then its game SIS. Run: `eka2l1_qt --run 'E:\sys\bin\asphalt4_ngi.exe'`.

The N-Gage app also installs its own `libc`, `libm`, `libz`, `libstdcpp`,
`libpthread` and friends to C: and E:, shadowing the ROM's Open C. A
drive snapshot from before it was taken (scratchpad
`drives-before-ngage.tgz`) in case the gate6 bench needs the old state back.

## The bench made to run at a phone's speed (round A1)

The game paces itself by the NGI runtime's clock, `runtime.dll` ordinal 11,
which reads `TTime::UniversalTime()` (euser 855), and drives its loop from
`CPeriodic`/`RTimer`. Both are host wall-clock time in the emulator, so
`EKA2L1_CYCLETICK` (tick SVCs only) cannot model the phone here. Instead the
emulator got a **CPU speed cap**, `EKA2L1_CPUCAP=<instructions per µs>`
(`EMU_PATCHES.md`): guest code never runs faster than the cap, and timers and
the clock keep phone-like proportions. Checked: capped at 50, the guest
measured 50.0. Needs `cpu: dyncom` in `config.yml` (the gate6 bench uses
dynarmic: put it back afterwards).

Frames are counted at the direct-screen post (`EKA2L1_FRAMESTAT`). The game's
own `CFPSCounter` (code +0x3B84..+0x3C72, `FPS:%3s.%1s %d` at +0x3C80) is
dead code: nothing calls it.

**Cost per frame, uncapped dyncom** (instructions are host-independent):

| scene | instructions per frame |
|---|---|
| "Activer le son ?" menu, static | 11,210,6xx, constant to the 6th digit |
| demo race | 7-13 M, mostly 9-11 M |

**Calibration.** The demo race at about 9.5 M instructions a frame gives the
N95's 10-15 fps (user, by eye) at roughly **115 instructions per µs**: about
2.9 cycles an instruction at 332 MHz. That figure absorbs everything a phone
pays and the bench does not count (cache misses, the window server, audio) and
rests on an estimate by eye. It is a guess until the phone measures a frame
rate. Uncapped, this container's interpreter runs 130-160, so at 115 the cap
is barely above it here and will bite on a faster PC.

## Where the time goes (round A1, `EKA2L1_PCPROF`)

Exact profiles: the menu over instructions 2-3 G, the race over 9-12 G.

| image | menu | race |
|---|---|---|
| asphalt4_ngi.exe | 93.1% | 92.0% |
| ngibackbuffer.dll (one block, +CCB8) | 6.9% | 7.1% |
| drtaeabi.dll (division helpers among it) | 0.0% | 0.5% |

**Software division is not the problem**: 0.5% at most. Suspect 2 dropped.

**The menu** (a still screen, 11.2 M instructions a frame):
- 65%: `+29736`, the generic sprite painter. Per pixel it re-tests the
  sprite's format flags (4- or 8-bit palette, alpha mask), converts the
  palette entry from RGB888 to RGB565 (`+298F4..+2991E`), reads a second alpha
  nibble, and switches on an effect mode (`[r7+0x34]`). The background is a
  full-screen sprite drawn through it every frame.
- 18% + 10%: a translucent rectangle fill, `+2ABAA`, calling a 565 blend
  function `+52F44` (about 20 Thumb instructions) **once per pixel**: the
  white wash over the menu background, every frame.
- 7%: the back buffer's own copy to the panel.

**The race**:
- 35%: `+333F4`, the textured span filler. Per pixel: affine texture step,
  texel fetch, a pseudo-random dither step (`muls` by a constant, add), a
  saturating 565 add for the light; with eight low registers it spills to the
  stack and reloads inside the pixel (`str r2,[sp,#8]` / `ldr r2,[sp,#8]`).
- 16%: `+33964`, its transparent twin (`+33B16`: texel, skip if 0, store;
  about 13 Thumb instructions a pixel).
- 6.5% `+2E1C8`, then a long tail of 1-3% functions.

So the cost is pixel loops written in Thumb, on every pixel, every frame,
whether or not anything changed. The fixes that follow are rewrites of those
loops in ARM with everything in registers, and fast paths that skip the
general case: a cached 565 palette, an opaque 8-bit sprite path, a two-pixel
blend. The finish-animation slowdown is likely more of the same (full-screen
overlays and effect sprites on top of the 3D), still to be measured.

## A first rewrite, and the exe's protection (round A2)

`patch/fill.c` replaces the translucent fill `+2ABAA`: the same arithmetic
(`C*a + P*(16-a)` is `(C-P)*a + 16P` modulo 2^32), `C*a` formed once a
rectangle, 13 ARM instructions a pixel against about 35 Thumb ones and a call.
`tools/a4patch.py` appends compiled ARM code (`-fropi`, no relocations
needed) to the code section, shifts the later sections, writes an entry
veneer (Thumb `bx pc` into ARM, then `b`) and fixes the header CRC.
`tools/equiv.py` runs original and patched images under unicorn from the
original entry point: **1,000 random cases byte-identical**, including clipped
and off-screen rectangles and out-of-range alphas. The test was tested: the
original changes exactly the 5,000 pixels of a 100x50 fill, and a deliberately
broken replacement fails on the second case.

**The patched exe does not run, and neither does any changed exe.** The
patched build died while loading: a division by zero at `+2D4AA` (divisor
`[r4+8]` = 0, through `__aeabi_idiv0` / `__rt_div0`, USER-EXEC 3), right after
the game opened `ASPHALT4_NGI.EXEv`. A control with **one byte changed in a
dead string** (`FPS:` to `FPT:` in the unused FPS counter's format, no code
touched, CRC fixed) died too, differently (E32USER-CBase 65). The original
runs for minutes every time. The code that names the game's own executable
(`ASPHALT4_NGI.EXE`, literal at `+4C208`, used at `+4BE26`/`+4BE54`) is
deliberately obfuscated: control-flow flattened through a dispatcher at
`+50F8C`, with junk bytes after each call. So the exe is protected against
modification: a changed file trips a check that sabotages the run later.

Getting a modified exe past that check (patching the check, regenerating its
record, or hiding changes from it at run time) is defeating a protection
mechanism, and that is out of scope for this work. The profile, the analysis,
the verified fill rewrite and the tools stay here as findings; nothing in
this directory touches the check. The bench was left with the original exe.

## Why it runs worse than Asphalt 3 (round A4) -- the answer

Both games profiled the same way, unmodified, on the capped dyncom bench.
Asphalt 3 is "Asphalt 3: Street Rules" (UID 0x2000AFE3); it boots to a
language menu, then a sound prompt, then runs a demo race on its own, exactly
like Asphalt 4. So the comparison is demo-race to demo-race.

**Cost per frame, measured in guest instructions (host-independent):**

| scene | Asphalt 3 | Asphalt 4 | A4 / A3 |
|---|---|---|---|
| demo race | **7.14 M/frame** (84 frames) | **9.50 M/frame** (196 frames) | **1.33x** |
| static menu | 7.66 M/frame | 11.21 M/frame | 1.46x |

On the same phone CPU, 33% more work a frame in the race is 33% fewer frames a
second. **That is the regression the user sees: Asphalt 4 is not doing
anything new for the money -- it is the same kind of frame, costing a third
more.**

**Where the difference is, and is not:**
- **Same renderer.** Both import no OpenGL ES and carry the same `Lib3D`
  software rasteriser (`CMesh`, `TFace`, `TFakeZFace`, `CSoftLine3D`; A3 also
  has `CShadow_Low/High`). Neither uses the MBX.
- **Same output size.** The platform's back-buffer-to-screen copy
  (`ngibackbuffer.dll +CCB8`, the single biggest block in both) costs about
  700K instructions a frame in each -- identical -- so both render the same
  240x320 frame. A4 is not drawing a bigger or extra frame.
- **Same delivery, same engine type.** ~90% of each race frame is the game's
  own code; the rest is the shared back buffer. Division is negligible in both.
- **The extra 2.4 M/frame is entirely A4's own pixel shading.** Both spend the
  bulk in a few span-filler functions (A3 `+7BA4A`/`+7C9B0`/`+5D962`; A4
  `+333F4`/`+33964`/`+2E1C8`). A4's readable inner loop (`+33594`) shades each
  pixel with a texture fetch, a dither/noise step (xor chains), and a
  saturating 565 light add, spilling to and from the stack every pixel because
  Thumb's eight registers do not hold the working set. A4 simply does more
  arithmetic per pixel than A3 for a frame that looks, to the eye and to the
  frame-copy cost, the same size and the same kind.

So the honest answer to "how is the newer game slower": it renders the same
sized frame with a heavier per-pixel shader, in Thumb, and gains about a third
in cost for no change a player would call an improvement. The lever for a
faster build is that span-filler -- fewer operations per pixel, or ARM with the
working set in registers (the `patch/fill.c` approach, applied to the shader).
No game was modified to establish any of this; it is all measurement.

## Where it stands

The question the user asked -- why this Asphalt is the slow one in absolute
terms -- is answered where the time goes (above): the game's own per-pixel
loops in Thumb. **What is not yet done is the comparison the user actually
asked for: why it runs worse than Asphalt 3.** That needs Asphalt 3 profiled
the same way (resolution, per-frame pixel work, effect count) and has not been
started -- it needs the Asphalt 3 package.

On the integrity check (round A3), earlier claims here were overstated and are
corrected: it is **not** established that the check is "the platform, not the
game." The byte of a changed exe is read back through a ROM libc routine called
from a platform DLL (`ngifilemanagerfactory`), but both observed failures ran
through *game* code -- a divide-by-zero at `asphalt4_ngi.exe+2D4AA` for the
patched build, a different fault via `+4B8E0` for a one-byte string change. So
the check may be game-side, platform-side, or both; it has not been cleanly
localised. What is established: the `.EXEv` record is not a plain CRC/MD5/SHA
table of any obvious region of the file (`tools/exev.py`).

The rewrite in `patch/fill.c`, its equivalence test, the profile and the bench
instruments stay as findings. The bench was left with the original exe and the
`dynarmic` setting.
