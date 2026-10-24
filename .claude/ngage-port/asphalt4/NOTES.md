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

## Next

1. The race finish, profiled, for the user's slowdown there.
2. A patching route for the exe: room for new ARM code, and whether the
   platform checks `ASPHALT4_NGI.EXEv` against a changed exe.
3. First rewrites, measured by instructions per frame on the same scenes:
   the alpha fill (`+2ABAA`/`+52F44`), the sprite painter's opaque 8-bit
   path, the two span fillers.
4. An on-screen frame counter in a phone build, to calibrate the cap.
