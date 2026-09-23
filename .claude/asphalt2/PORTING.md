# Asphalt 2: Urban GT 2 — N-Gage to S60v3 port

Running the N-Gage build of the game on an S60v3 phone, as a loader and an API
shim rather than an emulator: our own 9.x application loads the old EKA1 image
into a code chunk, answers its 462 imports with 9.x equivalents, and bridges
the two object graphs. The other file here, `README.md`, is about a different
job — patching the official S60v3 build — and shares nothing with this.

Everything lives in `toolchain/port/`. `gate6.cpp` is the loader and shim,
`gate6.s` its imports, `gen_shim.py` generates the import table into
`gate4_shim.cpp`, `build_gate6.py` builds `gate6.sis`.

## The two machines, which are not the same machine

**The phone** is a Nokia N95, RM-160, firmware v35.2.001, S60 3rd Edition
**FP1** (Symbian 9.2), 128 MB RAM, hacked — so signing and capabilities are
not a concern.

**The emulator** is EKA2L1 running a Nokia 5320 (RM-409) ROM, which is FP2
(Symbian 9.3). It is not the phone's firmware: EKA2L1 has trouble with any
S60v3 ROM other than the 5320, which is why that one is in use.

Both of the measurements taken from the 5320 ROM that were ever in doubt have
since been checked against the phone's own `BitGdi.dll` and `Ws32.dll`, pulled
off `Z:\sys\bin` with X-plore, and both hold. Ordinals and published vtables
are stable across feature packs; do not spend another round doubting this.

The game's own files must sit together in one directory, the way the card has
them — `E:\System\Apps\6rbc\` with `6rbc.app`, `6rbc.cwa`, `6RBC.dat`,
`cis.dat`, `cwivenc.dat`, `cwp.dat`, `nc.dat`, `nokia_en.rle` and the
`framework`, `plugins`, `streams`, `videos` subdirectories. The game finds
them beside wherever `CApaApplication::DllName()` says it was loaded from, and
a missing one is not something it survives. Nothing under `System\Libs` is
needed: those are EKA1 binaries the phone cannot load and the shim answers
every import into them itself.

## How a round works

Build with `python3 build_gate6.py <outdir>`; install the SIS; run twice. The
first run is the one being measured. The second reads what the first left in
`C:\g6box.dat`, panics with `G6BOX <number>`, writes `C:\g6box.txt` and stops
without running the game. So every measurement costs two launches, and a third
run reports nothing.

`g6box.txt`:

```
steps   how many imports and markers went past
last    the last one
flags   16 a slot of ours was entered · 32 the frame loop ran · 64 it left · 128 it exited
path    0 = E:\System\Apps\6rbc\ · 1 = E:\ · 2 = C:\
stack   the deepest the stack has been, in bytes
slot    the last vtable slot of ours the framework entered (0x504 = our timer's RunL)
tail    the last sixteen, oldest first
from    where each was called from, as an offset into the loaded image
```

Numbers under 464 in `tail` are import indices — `gen_shim.build()` names
them. 900 and up are breadcrumbs: markers planted in the game's own code,
listed in `kCrumb` in `gate6.cpp`. Current numbering:

```
900 c9d84  901 c9dc0  902 ca470  903 c9e2c  904 c9e38  905 c9e44  906 c9e6c
907 c9fb0  908 ca014  909 ca168  910 ca1d0  911 ca238  912 ca29c  913 ca2c4
914 ca2e0  915 ca2fc  916 ca390  917 ca038  918 ca060  919 ca088  920 ca0b0
921 ca0d8  922 ca100  923 ca128  924 ca150  925 ca1b4  926 c9d20  927 ca170
928 ca178  929 ca180  930 ca1a4  931 ca1ac  932 ca1b0
```

Renumbering happens whenever `kCrumb` changes, so regenerate this list rather
than trusting it. `TRACE_IMPORTS` in `gate6.cpp` turns on an RDebug line per
import, which the emulator's log shows in order and a phone would spend real
time on; leave it at 0 for anything going to hardware.

## What the shim does

The framework owns a 9.x object, the game owns the old one, and only the slots
the game overrides are bridged. Beyond that:

- **`CCoeEnv`** — the game reads the environment's fields at 7.0s offsets and
  also calls cone on it. It gets a *copy* with the old layout, refreshed on
  each `CCoeEnv::Static()`, with the app UI word pointing at the game's own
  object because the game reads its own members off it. Calls go back to the
  real environment.
- **`CDirectScreenAccess`** — `Gc()`, `ScreenDevice()` and `DrawingRegion()`
  are inline in ws32.h, and 9.x moved all three one word later (0x18/0x1c/0x20
  becoming 0x1c/0x20/0x24) because EKA2's `CActive` grew. Shifting the pointer
  is wrong: `iStatus` at 4 and `iActive` at 8 did *not* move, and the game
  reads `iActive` every frame. It gets a shadow in the old layout, refreshed
  from `RunL`; `StartL` and `Cancel` are turned back.
- **`CFbsBitGc`** — the game blits by vtable slot. Old slot 46 is
  `BitBlt(const TPoint&, const CFbsBitmap*)`; on 9.x it is 57. It gets a
  graphics context of ours whose vtable has the old shape, every slot a thunk
  into the real one. `kGcSlot` holds the pairing, read out of both ROMs.
- **The frame loop** is kicked from inside the game's own `FocusChanged` and
  `Draw`, gated on `CCoeControl::IsFocused()` and on an eikcore export the
  N-Gage ROM shows to be `CEikAppUi::IsForeground()`. The first is asked of
  the wrapper; the second is answered yes.
- **The decryptor.** One function, 0xd5094 for 0x1c0 bytes, ships
  XOR-encrypted with 0x56DB7802, and two more regions at 0x10af44 and 0x10b388
  follow at run time. On EKA1 a code segment cannot be written to, so the game
  attaches to itself as a debugger and writes its own plaintext in through
  `RDebug::WriteMemory`, resolving that and four others by ordinal from
  euser. 9.x has no RDebug; our chunk is plain writable memory, so
  `WriteMemory` is a copy, `RThread::Id` need only be consistent with itself,
  and the rest go through the ordinal tables in `gate4_shim.cpp`.
- **Run-time lookups** go through per-library old-to-new ordinal tables, since
  the game keeps euser.dll and efsrv.dll open at once. `RLibrary::Load` is
  watched to know which is which. A lookup is never answered with null,
  because the game calls what it is given without looking.
- **Caches.** Anything written as data and then run as code — the image, the
  stubs, every thunk, the decryptor's output — is followed by
  `User::IMB_Range`. The emulator has no instruction cache and never notices;
  a phone faults at the first instruction.

## Ruled out, with the evidence

Do not re-open these without new evidence. Each cost at least one round.

- **Feature pack difference (FP1 vs FP2).** The phone's own `BitGdi.dll` and
  `Ws32.dll` were measured: `CDirectScreenAccess` offsets identical,
  `CFbsBitGc` vtable identical (73 entries, `BitBlt` at 57).
- **The blit.** A build with `BitBlt` pointed at a no-op rebooted at the same
  import, from the same caller, with the same tail.
- **Stack overflow.** The record carries the high-water mark: 1412 bytes on
  the phone, 2712 in the emulator, against the 64 KB the image asks for.
- **The frame loop failing on a later frame.** `frames` is 1 everywhere. All
  of this is one-time engine setup inside the first `RunL`.
- **The five-millisecond flush timer was not the reboot.** `box_start` did arm
  a `CPeriodic` at 5 ms whose every tick flushed the record to the memory card
  — two hundred flushes a second, free on the host file the emulator writes to
  — and removing it changed nothing. It was worth removing and it was not the
  cause. The marker had moved between two runs, which looked asynchronous, but
  those two runs differed in configuration as well, so that reasoning was
  unsound.

Still open rather than ruled out: whether the file I/O has any part in it. The
record now lives on C: rather than the memory card, which is the one component
with a removable driver, and flushes on every record again — thinning it to
one in sixty-four had made the report read up to sixty-three events stale,
which is the worst possible property for this.

## Where it stands

**Emulator:** ~15,800 steps. Gets through startup, the decryptor and the
engine setup, then faults reading 0x30002 — a string pointer — at game+0xd5abc
(the game's `stricmp`), called from a case in the jump table at 0xd94f0. The
last hundreds of imports are `__udivsi3` from game+0xef1c4.

**Phone:** reboots within a couple of instructions of entering the state
machine, every time, in nine rounds across four configurations. With the
record flushing accurately the last marker is 909 (game+0xca168, case 9, two
instructions in) with the screen taken over, and 926 (the dispatcher) without.
No panic, no leave, nothing in those instructions — stack loads, stack stores
and arithmetic — that could account for it.

Both stop inside the first `RunL`, in the game's own obfuscated state machine
at 0xc9cd4 — seventeen cases dispatched through a jump table at 0xc9d38, each
ending by loading a state number and re-entering the dispatcher, so the state
sequence reads statically out of the literals (bias 0xa9f52710). It is not a
one-off check: the emulator runs it thousands of times.

## Open

- 30 imports still unanswered, mostly the deliberately stubbed N-Gage
  libraries and a CServer/CSession implementation the game carries.
- `OfferKeyEventL` (old control slot 1, 9.x slot 3) is unbridged, so there is
  no input.
- The framework base-class constructors are still `LOCAL_NOOP` approximations.
- `mke32.py` has no `.bss` support, which is why the context is reached
  through a pointer baked into each thunk.
