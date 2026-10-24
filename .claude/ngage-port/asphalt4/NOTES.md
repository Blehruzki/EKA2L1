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

## Next

1. Frame rate on the bench, from the demo race: count frames (the FPS counter,
   or the NGI back-buffer flips).
2. Profile the demo race: `EKA2L1_PCTRACE` over the game's code range, block
   entries weighted by block size, to name the functions that take the time.
3. The same through a race finish, for the user's slowdown there.
4. How the frame reaches the panel: the NGI back buffer and page scaler.
