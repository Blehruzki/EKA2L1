# Asphalt 2: Urban GT 2 — N-Gage to S60v3 port

Running the N-Gage build of the game on an S60v3 phone, as a loader and an API
shim rather than an emulator: our own 9.x application loads the old EKA1 image
into a code chunk, answers its 462 imports with 9.x equivalents, and bridges
the two object graphs. The other file here, `README.md`, is about a different
job — patching the official S60v3 build — and shares nothing with this.

## The hardware is a Nokia N95

**The phone every hardware round is run on is an N95: Symbian 9.2, S60 3rd
edition Feature Pack 1.** The emulator here runs an RM-409 (Nokia 5320), which
is 9.3 / FP2, because that is the ROM EKA2L1 has. One feature pack apart, and
for the shape of the import sequence it has not mattered -- the phone and the
emulator track each other event for event over fifty-five consecutive events.

Where it can matter is **ordinals**. euser and efsrv export ordinals are not
guaranteed identical across 9.1 to 9.4, and every ordinal in `gate6.s` that was
taken from a `kernelhwsrv` def file rather than from the device is a guess that
happens to hold on FP2. `SetKeyBlockMode` was keyed to the wrong ordinal once
already. When an import behaves strangely on hardware and not in the emulator,
this is the first thing to suspect.

Everything lives in `toolchain/port/`. `gate6.cpp` is the loader and shim,
`gate6.s` its imports, `gen_shim.py` generates the import table into
`gate4_shim.cpp`, `build_gate6.py` builds `gate6.sis`.

## Read this first: settled, retracted, and the rules

*This file is a working log, appended to as things happen, and a log tells you
what was believed at the time rather than what is true. This section is the
part that is maintained. When something below is overturned, it is recorded
here and the section it overturns is marked.*

### Settled

- The game's image has no data and no bss; every global lives behind
  `Dll::Tls()`, and that root is built correctly by our loader.
- The reboots -- a month of them -- were **two `RFile`s closed with
  `RHandleBase::Close`**, which closes the file server session out from under
  everything after it. Fixed with `RFile::Close` (efsrv 300). Nothing about
  write volume, write rate or extending files was ever involved.
- `RFile::Open` of `cwivenc.dat` returns `KErrNone`. The phone does not die
  there.
- **Furthest reached: 136 traced events** (builds 44 and 45, twice). It dies
  inside a `User::Free` -- of `0x7b7cd8` in build 44, `0x7b89a8` in build 45:
  the same point, a different heap layout.
- The heap is sound, the pointers are sound, **and so is the fatal free**: at
  event 128 `User::CountAllocCells` walks the heap clean at 1209 cells, every
  free matches a live cell, and the fatal one matched a live 27-byte cell with
  its verdict written to disk before the run ended. The fault is inside
  `User::Free`, on a cell that everything we can measure says is fine --
  including its header, which build 47 read: 27 bytes requested, header
  `0x28`, exactly the emulator's shape. Heap chain, pointer, size and header
  all correct, and so is the neighbour build 48 read. **Build 49 then settled
  it: with all three deallocation ordinals answered by a no-op, the run stops
  in exactly the same place.** Nothing was freed -- every freed pointer in the
  run is unique where build 48 reused them -- and the traced event count, the
  last import, the stack high-water, the import count, the free count and the
  verdict count are all identical. `User::Free` is innocent, the cell was never
  damaged, and rounds 44 to 48 are retired. The fault is in the game's own code
  after the 99th `delete` returns, which nothing has looked at because the free
  was standing in front of it.
- **The log goes quiet after that delete whether or not the run survives it.**
  Nothing in the next hundred instructions calls a traced import, and the one
  allocation among them is logged only if it fails. Probes in the emulator show
  the run getting past the delete every time. "It dies in the free" and "it
  stops at the 99th free" were both the end of the recording, not the end of
  the run.
- **The fatal delete is at image offset 0xcc8c0**, named by the `lr` its
  wrapper now records, and round 50's probes show the phone getting past it
  every time.
- **The actual fault is `0x139588`: `ldr r2, [r1, #0x240]`, with `r1` taken
  from `[r6 + 4]`, a field nobody ever wrote.** On the phone it holds
  `0xa6dfb180`, in the emulator `0xeaf88340` -- different nonsense in each,
  which is what an unwritten field looks like. `r1 + 0x240` is the faulting
  address in both.
- **The field is poisoned by one call: `bl 0xe9988` at `0xcc890`,** which is
  handed `&this->[4]` as its fourth argument. Probes on either side show a good
  heap pointer going in and garbage coming out; it returns 1. Everything before
  that call leaves the field intact, the temporary the "fatal" delete frees is
  created and destroyed inside the same sequence, and the object's other seven
  words are unremarkable.
- **`0xe9988` cannot be instrumented from the inside.** One probe at its third
  instruction takes the run from 1240 records to 553, before any marker outside
  it is reached. Flags are preserved by `probe_plant`, so this is the game, not
  the thunk. Every instrument from here watches state through wrappers we
  already own rather than patching a game byte.
- **The poisoning store is `str sl, [ip]` at `0x1082c0`,** through an
  out-parameter held at `[sp, #0xb8]` -- which is what `&this->[4]` became when
  `0xe9988` passed it down. One word, which matches the measurement that only
  word 1 of the object changes. *Named by a run that perturbed; awaiting a
  clean confirmation.*
- **The value is a pointer calculation, not a poison.** `mul` and `mla` over
  runtime values -- `sl = sb*r1 + r8`, then `sl*(r4+1)` scaled -- is
  `base + index * size`, not the image's multiply-by-constant obfuscation. So
  `this->[4]` is meant to hold a computed pointer and one of the inputs is
  wrong. That is also why the value is deterministic per machine and different
  between machines. Confirmed on its own terms in E21: a probe filtered to fire
  only when the store targets the watched field fired exactly once.
- **The computation is fully unwound and every step checks against a register.**
  `array[5]` is a decoy -- it is multiplied by a zero `sb` -- and the stored
  value is `755139455 * r8`, which reproduces the observed `0x1c975dc0` from the
  observed `r8 = 0x42d08240`. **`r8` arrives already wrong**, so the bug is one
  level further up the same function.
- **The value the store clobbers is the correct one.** `[field + 0x240]` --
  the exact word `0x139588` dies reading -- is a good heap pointer at every one
  of the 83 stations before the store. The field held a valid object and the
  call overwrote it.
- **NOPping that one store takes the wall down.** 292 imports against 248, no
  access violation at all, on through two `RLibrary::Load`s never reached
  before, and then an orderly `User::Leave`. A workaround, not an explanation,
  and named as one -- but the first movement here since build 44.
- **The poison lands between a successful allocation and the `delete` called
  from image offset `0x104828`,** inside one of `0xe9988`'s callees. Watched
  from the wrappers, `this->[4]` is intact at every station before that and
  wrong at every station after.
- **`0x139588` is the instruction EKA2L1 cannot run the *original* N-Gage
  binary past either.** That was recorded here long ago and filed as an
  emulator deficiency. It is not one. The original game under the emulator, our
  port under the emulator and our port on an N95 all stop at the same
  instruction for the same reason, so whatever fills `[r6 + 4]` on a real
  N-Gage is happening on none of the three. That is the port's real problem.
- **Build 48's `0x28` header was reuse, not damage.** With the leak on, the
  same free reads `0x20` -- exactly `align8(27 + 4)`. RHeap had handed over a
  whole recycled cell rather than split off a remainder too small to be one.
- **The run is deterministic.** Three runs of build 48 produced byte-identical
  7624-byte logs and identical boxes. A single run is now worth something,
  where under rule 1 it was not.
- **The ring's request column cannot be used to call a header wrong.** RHeap
  hands over a whole free cell rather than split off a remainder too small to
  be a cell, and any deallocation not going through ordinals 315, 408 or 410
  leaves a stale live entry for an address that is later reused. Six of 32
  frees have a header larger than `align8(request + 4)` for those reasons.
- **The emulator called this one right.** It predicted build 49 would not get
  past the wall, and the phone agreed. That is one data point, not a licence to
  trust it -- for the protection paths it is still wrong -- but for questions
  about the shape of the import sequence it has now been right twice.
- **Log volume is not what moves the emulator.** Three extra records per free
  with no dereference leave it ending exactly where it did (1066 -> 1258
  records, the same `0x30002`). When a reading changes, the write is not the
  suspect; what the instrument *reads* is.
- The setup state on the phone matches the emulator exactly: 20612 bytes of
  spare arena, a 3768-byte context, all four optional wraps installed. Nothing
  is being silently skipped there.
- The phone and the emulator run the same import sequence, tracking event for
  event over 55 consecutive events at an offset of 27.
- The game's own protection paths -- `6RBC.off`, `cwdynlog.dll`, GD1DRV, and a
  `TickCount` stopwatch with `Math::Random` mixed into its result -- behave as
  designed. The empty container the emulator dies walking is the *correct*
  state two seconds into a run.
- EKA2L1 cannot run the original N-Gage binary either (KERN-EXEC 3 at
  0x139588). Our port gets *further* under the emulator than the original does.

### Two panics, not one

**Every run on the phone raises one KERN-EXEC 0 and one KERN-EXEC 3**, and has
done for many rounds. It was reported up to about round 40, then stopped being
mentioned, and I stopped asking -- so every theory in this file since has been
built around a single failure while the phone was showing two.

KERN-EXEC 3 is an access violation. **KERN-EXEC 0 is a bad handle** -- a
different fault, with a different cause. Both are kernel-side, so neither is
one of our own `G6xxx` panics.

Two panics most likely means two *processes*: the app, and something starting
it again afterwards. The emulator shows exactly that shape -- `User::Leave`,
`User::Exit`, then our loader panicking `G6MEM` while failing to allocate the
image on a relaunch. **Which run the log we read belongs to is not
established.** The log is written from position 0 every time, so a relaunch
overwrites the first run's record with its own, and every log in this file may
be the second run rather than the first. Nothing more should be built on the
logs until that is settled.

### Retracted, and why

| Believed | Why it was wrong |
|---|---|
| The app runs once per launch | It runs over and over. Twelve launches in one 45-second emulator session, each identical, each leaving and being restarted. `file_replace` truncates, so each was erasing the last one's log. |
| The port has one failure per run | It has two. One KERN-EXEC 0 and one KERN-EXEC 3, every run, for dozens of rounds -- and the logs may be recording whichever process ran second. |
| The file server was overflowing a 31-byte cell | The allocation ring was searched in slot order and a stale entry won. There was no overflow. |
| The reboot was a write ceiling, then a write rate, then extending writes | Three theories fitted to the shadow of the closed-handle bug. A build writing the log heavily does not reboot once the handle is fixed. |
| Builds 38, 39 and 40 regressed | Each stopped early on a single run. Build 40 later reached 128. Two working instruments were reverted over it. |
| A run is a coin flip | Build 42 stopped at the same point five times out of five. The variance was real earlier and is not the explanation now. |
| 512 bytes meant a disk sector | `LOG_BLOCK` is 8. Sixty-four records is eight of our own blocks. |
| The read came from caller 0xe4774 | It came from 0xed694. The import trace records the call from inside a shared helper. |
| The emulator is a reference | For the protection paths it never was; it cannot run the original game at all. |
| EKA2L1's KERN-EXEC 3 at 0x139588 on the original binary is an emulator bug | It is the same fault our port hits, on real hardware. `[r6+4]` is unwritten in all three cases. What looked like the emulator's limitation was the game's own missing state. |
| The port dies in `User::Free` / at the 99th free | It dies 60-odd instructions later, at `0x139588`. The free was the last thing *logged*, and nothing between it and the fault calls a traced import. |
| Builds 38-43 failed at 64 records | They did not fail there at all. 64 records is eight log blocks; build 44, with the same code, reached 136 events. Four builds were judged on a hidden tail. |
| The allocation ring bounds the heap | It does not. `gate6_result` records every non-zero result, and `RFile::Open` is wrapped too, so a failed open puts `KErrNotFound` in it. A `heapTop` built from it was the whole address space, and the read it guarded walked off the end. |

### Standing rules, each bought with a wasted round

1. **A hardware round is three runs**, compared on the longest. One run is not
   a build's behaviour.
2. **One variable per build.** Build 38 changed four things and cost three
   rounds to unpick.
3. **When every measurement of a suspect comes back clean, remove the suspect
   rather than measure it harder.** Rounds 44 to 48 each measured one more
   property of the fatal cell and each came back clean; round 49 turned the
   free off and answered all five at once. The switch had been in the source
   the whole time.
4. **Suspect the instrument first.** Five of the failures in this file were the
   tool, not the game -- and three of those were read as the game dying when
   the recorder had gone deaf.
5. **A measurement that agrees with the theory gets checked as hard as one that
   does not.**
6. **Never ship from a directory another script built into.** `ref.sh` once
   left a different configuration in `out/` and it went to the phone.
7. **Ordinals come from the device, not from a def file.** The phone is an N95
   (9.2); the emulator ROM is a 5320 (9.3).
8. **The three confirmations go in the reply, every time -- generated, not
   remembered.** They were asked for explicitly, given for rounds 47 to 50,
   and then dropped at round 51 without my noticing: the same shape as every
   other lapse here, a discipline held while it is new and let go once it is
   routine. `toolchain/port/rules.py` prints all three from `ROUNDS.md`, so
   the numbers in them are whatever the file says rather than what I remember;
   `emurun.sh` prints it after every run and `checkrec.py` before every commit.
   Running it also catches a stale "Where we are" -- the first time it ran it
   reported round 50 as the best, two rounds after that stopped being true.
9. **Every run goes in `ROUNDS.md`, on either machine, before the next one is
   started.** This said "hardware round" for fifteen emulator runs, which is
   exactly how long they went unrecorded -- written up in prose here and
   nowhere a repeat could be caught. `toolchain/port/emurun.sh` now appends the
   row itself, with the record count and the fault, and leaves `TODO` in the
   last column; `checkrec.py` refuses while any `TODO` remains. The old
   scratchpad runners forward to it so there is no way to run one unlogged.
10. **A hardware round's row says what it settled, or says that it settled
   nothing.** One row: the single change, how many runs, the result, and what
   it bought. Five of builds 38 to 43 have an empty last column, and saying so
   out loud is the only thing that stopped the sixth.
11. **This section is updated in the same commit as the section that changes
   it.** It went stale one round after it was written, which is how the log got
   into the state that made it necessary. `toolchain/port/checkrec.py` fails
   when a new section is appended without it; run it before committing.

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

Build with `python3 build_gate6.py <outdir>`; install the SIS; **run once**.
The run appends its whole history to `C:\g6box.log`, eight bytes an event, a
block of sixty-four at a time. Read it with `readlog.py`, which names imports
and markers, counts them, and -- the reason it exists -- diffs two logs and
prints where they part company. `emulator-reference.log` beside this file is
the emulator running the same build, for exactly that.

The older two-launch route still works: the second launch reads `C:\g6box.dat`,
panics with `G6BOX <number>` and writes `C:\g6box.txt`. That summary is now a
convenience, written once per block rather than once per event, and the log is
the thing worth reading.

`g6box.txt`:

```
steps   how many imports and markers went past
last    the last one
flags   16 a slot of ours was entered · 32 the frame loop ran · 64 it left · 128 it exited
path    0 = E:\System\Apps\6rbc\ · 1 = E:\ · 2 = C:\
stack   the deepest the stack has been, in bytes
slot    the last vtable slot of ours the framework entered (0x504 = our timer's RunL)
hits    how many times each marker fired, 900 upward
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
- **The write count is a ceiling, and it was ending every run.** Across every
  configuration -- 5 ms timer plus a flush every sixteen imports, a flush per
  event on the card, a flush per sixty-four, a flush per event on C: -- the
  phone went down after about two thousand `RFile::Write` calls from this
  process, whatever the flushing did and whether the target was the card or
  internal flash. 655 imports plus ~1400 timer ticks; 1923 records; ~2005
  records. That is why the log buffers sixty-four events per write.
- **Writing the record to the memory card was holding the run back.** A
  `CPeriodic` at 5 ms flushed it two hundred times a second, free on the host
  file the emulator writes to and not on a card. Removing the timer alone
  appeared to change nothing, but that run had also been thinned to one flush
  in sixty-four, and an unflushed write dies with the file server, so its
  report was reading up to sixty-three events stale. With the record on C: and
  every write flushed, the game goes from dying two instructions into case 9
  to cycling the state machine, and gets there in two seconds rather than
  eight. Keep the record on C: and keep flushing every write.

Still open: the reboot itself, which survives all of the above.

## Where it stands

**Emulator:** ~15,680 records. Gets through startup, the decryptor and the
engine setup, then faults reading 0x30002 — a string pointer — at game+0xd5abc
(the game's `stricmp`), called from a case in the jump table at 0xd94f0. The
last hundreds of imports are `__udivsi3` from game+0xef1c4.

**Phone:** `CAknAppUi::SetKeyBlockMode` was the fault in the frame-loop kick, twice over.
Diverting it was wrong — it is a 9.x method on the framework's app UI, and the
game's own object is not one — so it is stubbed to a no-op in `gen_shim.py`
(avkon 1529 -- see below) and out of `kDiverts`. With that the phone went from 216 records
to **2863, every one of them identical to the emulator's**, and it is into the
state machine at 0xc9cd4 and cycling. That run ended in a reboot, but at the
196th pass through a loop it had already survived 195 times: the write ceiling
again, not the game. `phone-2026-09-23c.log` beside this file is that run.

Then the same build at the cheap cadence stopped at 192 records — a block
boundary, so somewhere in 192..255 — with KERN-EXEC 3, which is where the runs
before the SetKeyBlockMode fix stopped too. The only difference between it and
the 2863-record run is how often the record is written, so either the fault in
that window moves with the timing (the exact cadence spends milliseconds in the
file server between events, and the window covers the rest of telephony,
`RequestComplete` and the whole frame-loop kick) or 2863 was the lucky one.
`phone-2026-09-23d.log` is that run, and a zoom window over 176..336 is what
goes over it next: dying inside the window names the record, sailing past it
says the timing is what matters.

The zoom window found it. The phone stops at record 221, and 217..220 —
`RequestComplete`, `SetKeyBlockMode`, `KeySounds`, `PushContextL` — match the
emulator exactly. 221 is `CActive::Cancel` from a caller that is not an offset
into the image at all (0x74258070), where the emulator has
`CCoeControl::IsFocused`. The game's own four `bl` sites for that stub are all
at 0x398b8..0x39e04, so nothing in the game called it: something branched into
the stub table and arrived with a stale return address.
`phone-2026-09-23e.log` is that run.

It is a race rather than a code path: the 2863-record run is the same build on
the same phone and went 220 to `IsFocused` like the emulator. The log could not
show what the race was against, because it only ever recorded the game calling
out and never the framework calling in — so `gate6_slot` writes to it now (52
such entries in the emulator run, three inside the frame-loop kick), as do the
load address and, for a `Cancel`, the object it was made on. A `Cancel` on
something that is neither the timer nor the direct screen access object cannot
have come from the game, so it is recorded and refused rather than made, and
the run says what comes next instead of ending there.

The next run stopped in the same place, so it reproduces: `CActive::Cancel`
again, from 0x742a8070 this time, against a load base of 0x4600000 — about
0x788a8070, which is RAM-loaded code and not the image.
`phone-2026-09-23f.log` is that run. With the slot records in, both machines
run an identical cascade of twelve framework calls into our vtables after
`PushContextL` (app 5, appui 19, appui 18, app 12, doc 21, app 6, app 7,
app 5, app 5, doc 20, appui 17, appui 14) and then part: the emulator carries
on to appui slot 3, the phone branches into the stub table. Every slot that
was logged is inside the copied count, so it is the call after the last one.

**It is the heap.** Padding the vtable copies against exactly that — a slot
past the end — broke the emulator instead, deterministically, at the first app
slot of that cascade, three runs out of three; bisecting showed the padded
allocation alone did it and the instrumentation was innocent. Nor is it the
padding as such: `HEAP_NUDGE` makes one allocation at load time and never
touches it, and 384 bytes reproduces the failure exactly while 96 and 2048
leave the run alone. So the port has a fault that depends on where the heap
puts things, and that is what a phone running the same build twice and getting
through only once looks like. `VT_MARGIN` and `HEAP_NUDGE` in `gate6.cpp` are
the bench for it; both are zero in a real build, and the baseline is ~15,800
records and the usual 0x30002.

Worth saying plainly: the emulator can reproduce this class of failure on
demand now, so narrowing it costs nothing at the phone.

**And it was `CAknAppUi::SetKeyBlockMode` after all**, still being called for
real. `BY_ORDINAL` in `gen_shim.py` is keyed on the ordinal in the *N-Gage's*
library; the entry stubbing this one named 2927, which is the 9.x ordinal it
resolves to, so it never matched and the import stayed a direct call. Avkon
therefore wrote CAknAppUiBase's members at its own offsets into the game's app
UI, which is not one of its objects, and what the write landed on depended on
where the heap put things: it was setting our app UI's vtable pointer to 1 — a
TBool at avkon's offset, the vptr at ours — which the framework then dispatched
`ProcessCommandParametersL` through, faulting on 0x4c past 1. Keyed on 1529 it
is a no-op, and every layout that failed now runs the distance, the vtable
padding included.

How it was found, since the method is the transferable part: the fault PC came
out of the emulator's own register dump, `romimg` named the ROM function from
the module base in the emulator's log (`CEikonEnv::ConstructAppFromCommandLineL`
+0x238), `capstone` showed the two instructions (`ldr r0,[r0]` then
`ldr r2,[r0,#0x4c]`) that say a vtable pointer had become 1, and `vptr_check`
— which runs on every import and every call in, and is still there — named the
call it happened during.

**That fix was real and it was not the phone's fault.** The next run stopped in
exactly the same place with exactly the same caller, and the canary never
fired: nothing overwrites the app UI's vtable pointer on the phone. Two
different builds stopping identically also retires the word race — it is
deterministic, and the 2863-record run that got past it is the one that needs
explaining, not this one. `phone-2026-09-23g.log` is that run.

**`gate6_cancel` took the context in the wrong register.** Every one of the
nineteen functions a `ctx_thunk` points at takes the context third, in r2,
because that is where the thunk puts it. This one declared it second. So it
read r1 — and `CActive::Cancel()` takes no arguments, so r1 is whatever the
caller last left there — and dereferenced it immediately. Reaching this
function at all was fatal, and the record shows exactly that: the trace record
for import 279 is written by the thunk on the way in, and the `Cancel on`
record the function itself writes never appears. The emulator never showed it
because nothing in its fifteen thousand records calls `Cancel`, which is also
the honest answer to why a diff against the emulator could not have found this
one.

Still unexplained, and worth keeping in view: the caller. The log records
0x742a8070 against a load base of 0x4600000, so about 0x788a8070, which is
neither the image nor the chunk. The game's own four `bl` sites for that stub
would all record a small offset, so this arrived by an indirect branch with a
stale return address. With the register fixed the handler runs, records what
`Cancel` was called on, and refuses an object that is neither ours — so the
next run says who, and carries on.

**With the register fixed the phone went from 258 records to 6988**, and the
record shows the handler doing its job: `Cancel on 0x603388`, no stray, and the
run carrying on for another six and a half thousand events.
`phone-2026-09-23h.log` is that run. It ended in a reboot, and this one is not
the write ceiling: 58 slot entries and about 325 writes all told, against the
two thousand that ends a run.

**It is not hung, and that reading was wrong.** The emulator runs the same
five-state cycle 636 times and has longer import-free stretches than the phone
does -- 3510, 3043 and 2944 records against the phone's 2944, 2017 and 1266,
two of them exactly equal. The spinning is the game's obfuscated state machine
working, not waiting, and the phone simply stopped part-way through an
ordinary stretch of it. The locals it reads there are never written and its
`r9` is never set, which says the same thing: that arithmetic is obfuscation,
and reading it as a wait loop was reading too much into it.

**The reboot is the instrument.** Breadcrumbs were 6227 of the phone's 6988
records, 89% of them, and they are planted in the hottest code in the run:
every pass costs a call out of the game, a record, and -- inside the zoom
window -- a file write and a flush. All of it happens inside a single `RunL`,
so none of that time is given back to the active scheduler, and roughly ten
seconds of a thread not yielding is what the phone's watchdog resets. That
also matches what the reboots always looked like: seven to ten seconds, no
panic, no leave.

So `PLANT_CRUMBS` is off by default and the zoom window with it. The emulator
reaches the same 0x30002 with 5184 records instead of 15813 and about eighty
writes instead of three hundred, and the game's own loop runs with nothing of
ours in it. The breadcrumbs earned their keep finding the way into the state
machine and can go back on for a question that needs them.

**And it was.** With the breadcrumbs out the reboot stopped: the next run is a
plain KERN-EXEC 3 at 2112 records, `phone-2026-09-23i.log`. The phone's last
sixty records appear in the emulator's run verbatim, so the two are in
lockstep right up to the fault, and it is now inside one of the decrypted
regions -- code that reads as rubbish in the file, so `DUMP_DECRYPTED` writes
all three regions to `C:\g6code.bin` on the bench and they disassemble like
anything else.

What the game is doing there: building a five-character DLL name a character
at a time (`AtC`, `__modsi3`, `Append`), calling a function pointer it
resolved earlier with it, and then scrubbing the name off the stack --

```
0010b1f4  bl   #0x1191e8        @ TDesC16::Ptr()
0010b1f8  ldr  r3, [sp, #0x38]  @ the descriptor's length word
0010b1fc  bic  r3, r3, #0xf0000000
0010b200  lsl  r3, r3, #1       @ length in bytes
0010b210  strb r2, [r0], #1     @ fill it with 0,1,2,... and die here
```

anti-tamper, erasing the name it just used. The phone faults between
`TDesC16::Ptr` at 0x10b1f8 and `RLibrary::Lookup` at 0x10b244. All four
imports on that path map correctly (`Ptr` is euser 1807), and the descriptor
is `sp+0x38`, on the stack, so the write should be in bounds.

A breadcrumb cannot be planted there at load time -- the decryptor writes
straight over it -- so `kLateCrumb` is planted from `gate6_write_memory`
instead, once the region carrying it has arrived, with markers from 940.
Marker 941 sits on the scrub and fires once per byte: the emulator runs the
site twice and writes ten bytes each time. If the phone writes ten and stops,
the pointer is wrong; if it writes many more, the length is.

The late crumbs never fired: the run stopped before reaching them, at 1984
records (`phone-2026-09-23j.log`), earlier than the run before it. Worth
knowing why they are not comparable -- **two builds are not two runs**. The
same phone on two builds parts company at record 1574, in the middle of the
division storm, long before any crumb site. Whatever this game derives from
depends on our build, so only phone-against-emulator on the *same* build says
anything.

On that footing the phone is again in lockstep to the end. It faults in an
earlier loop of the same function, at 0x10b030, on the third of nine
characters:

```
0010aff4  bl  #0x118d78        @ TBuf16<9> at sp+0x58
0010aff8  ldr sl, [pc, #0x33c] @ a pointer the decryptor wrote, at 0x10b33c
0010b024  mov r0, sl ; mov r1, r5
0010b02c  bl  #0x118fc8        @ TDesC16::AtC(i) -- returns a reference
0010b030  ldrb r4, [r0]        @ and the game reads through it
```

The emulator reads all nine, twice. Both source descriptors are plain inline
`EBufC`s of length nine, at image+0x17f0e8 and +0x17f100, well inside a
0x1849bc text section, so the text is there to be read -- which puts the
suspicion on the pointer. It is a literal *inside a decrypted region*, so it
is not in the file to be checked: the decryptor writes it, and whether it
comes out relocated for where the image actually landed is the question.
`NOTE_LITERAL` and `NOTE_TARGET` log both from `gate6_write_memory`. The
emulator says 0x0487f0e8 against a base of 0x4700000 -- correct -- and a
length word of 9. A correct answer on the phone is 0x0477f0e8 against
0x4600000.

**The pointers are right.** The phone says 0x0477f0e8 against a base of
0x4600000, and a length word of 9 -- exactly what it should be, for both
descriptors (`phone-2026-09-23k.log`). So neither the pointer nor the data is
wrong, and reading the third character of nine ought to work.

That run also ended in KERN-EXEC 0 rather than 3, and at 1952 records rather
than 1984 or 2112 -- a third build, a third place, in the same name-building
code. Something the game does depends on our build. The obvious suspect was
the tracer, since it makes every IAT entry point at a thunk of ours and this
code is arithmetic over fetched values: `TRACE_EVERY_IMPORT = 0` leaves the
table pointing straight at what answered it. The emulator faults in exactly
the same place with it off, so the tracer is innocent and that idea is dead.

**What the emulator's own fault is.** It has been stable across every build,
which makes it the better thing to chase, and it is now read: at game+0xd5abc,
the game's `stricmp`, with `ldrb r3,[r5]` and r5 = 0x30002. The caller is case
6 of a second obfuscated state machine at 0xd9430 --

```
000d94e4  mov r0, r8        @ "6RBC.off"
000d94e8  ldr r1, [r5]      @ a name out of a table
000d94ec  bl  #0xd5aa4      @ the game's stricmp
```

-- so the game is searching for a file called `6RBC.off` and one of the
entries it walks has 0x30002 where a name pointer should be. `6RBC.off` is in
neither the game's directory nor inside `6rbc.cwa`, so the search is one that
cannot succeed; what matters is that the walk does not stop when it runs out
of entries. Whatever ends that table is not ending it here.

**Correction, from watching the walk rather than reading it.** It does not run
off the end of the table: `PLANT_WALK` puts a breadcrumb on each of the three
cases that drive the search, carrying r5 with it (`crumb_plant_r5` adds one
`mov r3, r5` to the stub, and the handler logs the register and the four words
it addresses). The search runs **once**. The very first node is already wrong.

```
case 0  start     r5 = 008b8000   @ the container
        its words 008b8000 007039f0 00000000 000f000e
case 6  compare   r5 = 007039f0   @ the first node
        its words 00030002 0000007e 00700be8 40070002
```

So `head = container[+4] = 0x7039f0`, and that node's first word -- where the
search expects a `char *` -- is 0x00030002. Nothing in those four words looks
like a name record. The container is page-aligned and holds its own address at
[+0], which is what a pool or a queue head looks like rather than a heap cell,
so the question is no longer where the walk stopped but where the container
came from and who was supposed to fill it.

Where those addresses live, since it matters: gate6's own `$HEAP` is at
0x700000 with a maximum of 0x4000000, so the container at 0x8b8000 and the
nodes at 0x7039f0 and 0x700be8 are all our own heap. Page-aligned is not
evidence of anything here.

**The name the decrypted code builds is `cwdynlog.dll`**, and the emulator
says so itself: `Try loading cwdynlog.dll to Gate6 failed`. It is built a
character at a time precisely so it is not in the image as text -- searching
for it there finds nothing -- and the file ships nowhere: not in the installed
copy, not inside `6rbc.cwa`, not in the original release. A logging DLL that is
not shipped, so the load is expected to fail, and after it the game looks up an
ordinal and calls whatever comes back, which `gate6_library_lookup` answers
with a no-op rather than null. `6RBC.off` is the same kind of thing: absent
from the original release too, so that search is meant to fail.

Also checked and not the problem: the installed game directory does not match
the original release. The release ships four DLLs -- `bin/ARENAFRAMEWORK.DLL`,
`bin/main.dll`, `Libs/GAMECOMMS.DLL`, `Libs/GAMEUTILS.DLL` -- and the installed
copy has twenty-six from somewhere else and no `bin/` at all. Restoring `bin/`
changes nothing, which figures: `main.dll` is 1,616,972 bytes with a code
section of 0x1850fc, the same image as `6rbc.app`.

**What the container turns out to be.** `result_thunk` wraps a call and
records what came back -- which a trace on the way in cannot do -- and with it
on the allocators the whole history of 0x8b8000 reads out: allocated at 21728
bytes, freed, 448, freed, 1056, freed, then 20 four times over, each freed, and
finally 4 bytes at record 4552 which is still live at the fault. The
allocations bracket `memcpy` calls from 0x1036xx and one of the strings beside
`6RBC.off` is `basic_string`, so that address is a container's buffer being
grown and recycled. The search reads [+0], [+4] and [+0xc] from it, which fits
the 20-byte tenant and not the 4-byte one. So the search is handed a pointer to
a buffer that was freed long before, and following it further means reversing
the game's whole container layer.

Two things worth keeping from building that tool. `r0`-`r3` do not survive a
call, so an argument cannot be held in one across it -- the copy `stmdb` pushed
on the way in is the one to read. And wrapping an import twice makes the
trace's caller column point at our own outer thunk, so `from` is meaningless
for anything `WATCH_ALLOCATIONS` covers.

**Back to the phone's own frontier**, which is 3500 records short of all this.
It dies reading the third of nine characters through `TDesC16::AtC`, and the
emulator reads all nine. `NOTE_TEXT` now logs those characters from our own
read at startup, before the game touches them: the emulator gives 00770066
00690076 002f0072 006f0066 00000070, "fwvir/fop" before descrambling. If the
phone logs all five words the text is readable and the fault is inside `AtC`;
if it stops partway, the memory is.

**It reads all nine**, byte for byte what the emulator reads: 00770066
00690076 002f0072 006f0066 00000070 and 006f004e 00490066 00330067 00320036
00000032, "fwvir/fop" and "NofIg3622" before descrambling
(`phone-2026-09-24a.log`). The memory is readable and the text is right, so
neither is the fault.

**And a methodological correction that cost two readings.** That run stopped at
1962 records, which is exactly a block boundary -- 810 records of notes and
then eighteen full blocks of 64 -- so the last record is the last *flush*, not
the fault, and the sixty-odd events after it were never written. The same is
true of the run before it. Twice now the last record has been read as the
place it died, and twice that was over-reading a cadence. `LOG_ZOOM` exists
for exactly this and was switched off; it is on again over 1850..2100, which
costs about 250 writes and gives the death point exactly.

**The sequence is fixed; where it stops is not.** Counting imports rather than
records, which is the only measure comparable across builds, the phone has run
2051, 1923, 1887, 1887 and now 1853 (`phone-2026-09-24b.log`). Every one of
those is a *prefix* of the next longest -- this run's 1853 imports differ from
the previous run's 1887 at no point at all -- so the game does exactly the same
thing every time and only the stopping point moves. That is worth holding on
to: it rules out a data-dependent divergence and points at something about how
far it gets rather than what it does.

The endpoint also moves with instrumentation in a way that fits. Taking the
breadcrumbs out took the phone from 700 imports to 2051 at a stroke, which is a
speed effect and not a fix; 1850 for the zoom window was set too late and only
three events fell inside it, and that run rebooted rather than panicking.

What the exact records did give: the run ends right after `TDes8::SetLength`
from 0x13f62c, and the next thing the code does is

```
0013f630  ldr r0, [sp, #0x18]      @ the TPtr8's length word
0013f634  bic r0, r0, #0xf0000000  @ its length
0013f638  bl  #0x119768            @ HBufC16::New(that many)
```

An allocation whose size comes straight out of a descriptor. The emulator makes
that call nine times for 27, 31, 28, 27, 26, 31, 26, 31 and 28 characters; a
bad length here would ask the phone for something enormous, which is a better
explanation for a reboot than for a panic. `arg_thunk` records a call's first
argument *before* it is made -- `result_thunk` could not, and a call that never
returns leaves nothing otherwise -- and it is on `HBufC16::New`.

**The lengths are fine** -- 27, 31, 28, 27, the emulator's own first four
(`phone-2026-09-24c.log`) -- so that idea is dead, and the run it came from
stopped at 1785 imports, earlier again.

**It is time, and the instrument was eating it.** Six builds running an
identical sequence, each a prefix of the last, stopping at 2051, 1923, 1887,
1887, 1853 and 1785 imports -- monotonically shorter as more measurement went
in. The reboots are the watchdog: the game never returns to the active
scheduler through any of this, and about ten seconds of that is what the phone
resets. Every record is time the game does not get, and for several rounds the
answer to "why did it stop earlier" was me.

Where the cost is is not the writes -- at 64 events to a block the whole run is
about thirty of them. It is the trace itself, on every import: six registers
saved, three literals loaded, a call out, a record appended, and back, against
a real `TDesC16::AtC` of about ten instructions. `TRACE_SKIPS_HOT` leaves the
eleven hottest imports unwrapped -- `__udivsi3` alone is 57% of all calls, and
the eleven together 88% -- which takes the emulator's run from 5122 records to
773 for the same fault at the same place. What is left still names every file
opened, every library loaded and every frame drawn.

**And it worked.** Measured the only way that compares -- phone against
emulator on the *same* build -- the phone went from 1887 of 4915 imports to
455 of 705: **38% to 65%** (`phone-2026-09-24d.log`). Its last records are the
emulator's 470-475, an alternating pair of allocations, so it stopped mid-loop
again rather than anywhere meaningful.

Checked and cleared on the way: `mke32.py` defaults `heap_max` to 1 MB, which
would have been a fine story -- the S60v3 framework costs far more heap than
the N-Gage's did, and the emulator hands out 64 MB whatever the header says.
But `build_gate6.py` already overrides it to 0x4000000, so both machines have
64 MB and that is not it.

So the instrument comes down again, from pruning to a whitelist:
`TRACE_MILESTONES` wraps only files, libraries, the screen, the frame-loop
kick, and the two the record needs to close itself properly. 197 records in
the emulator against 705 and 4915, the same fault in the same place, and the
block drops to eight events so at most seven are lost when it stops. Twenty-
five times lighter than the trace that was in place two rounds ago.

**The 38% to 65% was not real, and neither was the conclusion drawn from it.**
Those two numbers came from different trace sets, so their denominators were
different -- the exact mistake flagged one paragraph earlier, made immediately.
On a yardstick that does hold, the milestone subsequence extracted from every
log and counted against the emulator's 169, the phone reads:

```
2%  4%  33%  2%  6%  6%  6%  38%   60% 58% 58% 58%  56% 53% 53%   59%
```

Flat at 58-60% since the first build with the breadcrumbs out, through a
twenty-five-fold reduction in instrument. So the trace was never what limited
the run, the reboots aside, and "it is time" was wrong. The stopping point is
fixed.

**Where it is fixed.** The phone's last milestone is the emulator's 123,
`RLibrary::Lookup` from 0x10b0e4, and the two instructions after it are

```
0010b0e4  mov r6, r0      @ what the lookup answered
0010b0f0  bx  r6          @ and straight into it
```

`gate6_library_lookup` hands that address over. It now refuses an `RLibrary`
whose handle is zero rather than asking the kernel about it -- a lookup on an
object that is not open is KERN-EXEC 0, invalid handle, which is the panic the
phone reports and which the emulator allows. The guard never fires here, since
every library the emulator looks up is open, so it is a fix that cannot be
tested on this side. What can be seen either way is the handle and the answer,
and both are now on record for all 66 lookups.

**The handle was fine, and that is what gave it away.** 0x40750035, open, so
the guard never fires and that idea was wrong too (`phone-2026-09-24f.log`).
But the lookups are now on record site by site, and every one of them answers
the same on both machines -- including 0x10b128, where both correctly hand back
our own no-op. The phone also got past the scrub to 0x10b244, further than any
earlier reading had it.

**It is a kernel device driver.** Resolving the two sites the phone stops
between, through euser's exports in the ROM:

```
0x10b0e4  ->  euser 624  User::LoadLogicalDevice(const TDesC16&)
0x10b244  ->  euser 490  RBusLogicalChannel::DoControl(TInt, TAny*)
```

The game loads an LDD -- an N-Gage device driver -- opens a bus logical channel
to it and calls control on it. S60v3 has no such driver, so the load fails, the
channel never opens, and a control call on a channel that is not open is
KERN-EXEC 0: invalid handle, the exact panic, deterministic, and untouched by
anything done to the instrument. That is why the reach has sat at 58-60% since
the breadcrumbs came out.

None of it can be honoured, so `gate6_library_lookup` no longer passes any of
it on: the twenty euser ordinals for logical and physical devices, bus channels
and `RDevice` all answer with the no-op, which returns zero -- KErrNone for the
load and the free, and nothing for the channel. The emulator refuses 624, 490
and 623 twice each, reaches the same 170 milestones and the same fault, so the
game carries on without its driver.

**It worked.** The phone refused 624, 490 and 623, exactly as the emulator
does, and moved for the first time in seven builds: 118 milestones against 102,
**60% to 69%** by count, and positionally it now reaches the emulator's
milestone 141 of 170 -- 83% of the way to the emulator's own frontier
(`phone-2026-09-24g.log`). The plateau was that driver.

It rebooted rather than panicking, and this time the instrument is not a
plausible culprit: 230 records and about thirty writes, against the 6988 and
the thousands that caused the earlier ones.

**Worth being plain about what is and is not ahead.** The only drawing in the
emulator's whole run is at milestone 16 -- `SetClippingRegion`, `SetAutoUpdate`
and one `CFbsScreenDevice::Update` -- which is the black band with pixels
already seen on the phone. Nothing draws again before the emulator faults at
170. So the splash is past the emulator's frontier as well, and the archive
search at game+0xd5abc is now the wall for both machines rather than just this
one.

**Two runs of that build, deliberately identical** (`phone-2026-09-24h.log`):
222 records against 230, 110 milestones against 118, and one a *prefix* of the
other. Same path, different moment, so the reboot is asynchronous -- it is not
in the code the game is running.

Two things are asynchronous here. One is the window server: direct screen
access is a promise to stop drawing when told, and the game holds it while
computing for thousands of operations inside a single `RunL`, never back in
the active scheduler and so never able to hear an abort. `HOLD_THE_SCREEN`
tests that by not taking the screen -- but it cannot be a one-line switch, as
the graphics context only exists once `StartL` has run and the run dies
writing through a null at three milestones. Left on until the stand-in exists.

The other is memory, which varies with whatever else the phone is doing and
would equally explain two runs eight milestones apart. That one is cheap to
settle: `User::Alloc`, `AllocL`, `AllocZL` and `HBufC16::New` return zero on
failure rather than panicking, so `result_thunk` now records **only** the
zeroes -- nothing at all in the ordinary case, the whole answer if it happens.
None in the emulator's 135 allocations.

**Not memory.** 110 milestones again and not one allocation returned zero
(`phone-2026-09-24i.log`). The two runs do differ at record 97, but only in the
caller column, and only because wrapping an import twice makes the trace record
our own outer thunk -- the same trap noted above, and the runs are otherwise
identical.

So the window server is what is left, and it can be tested after all without a
stand-in context: start the access, let `dsa_refresh` take the graphics context
out of it, and then give the screen straight back with `Cancel`. The game is
told it still has it, since it will not move otherwise, and nothing is drawn
between there and the end of the emulator's run so nothing is lost by the lie.
`RELEASE_THE_SCREEN` does that; the emulator reaches the same 170 milestones
and the same fault with it on, which makes it a clean test rather than a
change of behaviour. If the phone stops rebooting, the reboot was a client
holding the screen and not listening; if it does not, the window server is
innocent and something else is interrupting.

**The window server is innocent too.** 115 milestones with the screen given
back, inside the 110-118 band of the runs that held it, and still positionally
the emulator's 141 (`phone-2026-09-24j.log`). Nor is anything leaking: loads
against closes are 15/13 on the phone and 19/17 in the emulator, file sessions
7/4 against 8/5 -- the same two and three outstanding on both. And the blind
spot the milestone trace leaves between 141 and 158 is only 203 calls, almost
all of them the name descrambler again, so nothing exotic is hiding in it.

Which leaves the plainest explanation: the game takes longer than the phone
allows in one `RunL`, and always has. Every earlier reboot was ours -- two
thousand writes, then six thousand breadcrumbs -- and removing those bought
real distance each time, which fits. What is left is the game's own speed on a
332 MHz phone.

`LOG_THE_CLOCK` reads `User::TickCount` every sixteenth milestone, which is ten
readings and 38 ticks across the emulator's whole run. It settles the question
either way: stopping at the same moment each time and a different milestone is
a clock, and stopping at the same milestone after a different time is not. If
it is the clock, the answer is to stop computing inside a `RunL` at all -- to
give the game its own thread, so the active scheduler stays free to service the
framework while it works.


















## Open

- 30 imports still unanswered, mostly the deliberately stubbed N-Gage
  libraries and a CServer/CSession implementation the game carries.
- `OfferKeyEventL` (old control slot 1, 9.x slot 3) is unbridged, so there is
  no input.
- The framework base-class constructors are still `LOCAL_NOOP` approximations.
- `mke32.py` has no `.bss` support, which is why the context is reached
  through a pointer baked into each thunk.

## Stopping to take stock

**The clock says it is not time either.** 63 ticks across the phone's whole run
against the emulator's 38 -- about a second, not the ten a watchdog would want
(`phone-2026-09-24k.log`). That was the last hypothesis standing, and it is
wrong like the others.

What is actually established, as against guessed:

- **Ruled out by measurement**: the write ceiling (30 writes now), instrument
  weight (58-60% held across a 25-fold reduction), memory (no allocation
  returns zero), resource leaks (loads/closes and file sessions match the
  emulator exactly), the window server (releasing the screen changes nothing),
  elapsed time (one second), and a data-dependent divergence (every phone run
  is a prefix of the emulator's sequence).
- **Fixed, and each one real**: the GCC98/EABI vtable and ABI work, the
  decryptor, the cache flushes, `SetKeyBlockMode` on the right ordinal,
  `gate6_cancel`'s register, and the driver refusal -- which moved the phone
  off a plateau it had sat on for seven builds.
- **Still unexplained**: the reboot, at 107-118 milestones, five runs running.

**A reboot is not a user-side fault.** Symbian's own documentation is plain
about it: a user thread that touches bad memory gets KERN-EXEC 3, and the OS
reboots when the faulting thread is a *kernel* one. So whatever is happening is
kernel-side, which a user process can reach in very few ways -- essentially
through a device driver, or by taking a system server down with it.

**And the game drives a kernel device.** The LDD it loads is named `GD1DRV`,
and `gd1drv.ldd` (uid2 0x100000af, a kernel LDD) sits in `system/libs` of both
N-Gage ROMs beside `gd1eng.dll`, and in no S60v3 ROM. The game opens a bus
logical channel to it and issues `DoControl`. Refusing those calls stopped the
KERN-EXEC 0, correctly -- but it leaves the game running on whatever it makes of
a driver that answered zero to everything, which is not the same as working.

## What this needs, to be worked on without the phone

**The N-Gage is already here.** EKA2L1 has NEM-4 and RH-29 installed with their
ROMs, and the original unmodified game runs on them: `--device NEM-4 --run
0x101fd42d` reaches the same files in the same order and loads GD1DRV.LDD
before it stops. That is the reference this work has never had -- the game
behaving *correctly*, to compare the port against, instead of inferring correct
behaviour from a 5320 running the port.

**EKA2L1 is this repository.** Every divergence so far has been the emulator
being lenient where hardware is strict: invalid handles tolerated, DSA rules
unenforced, an absent LDD shrugged off. Those are all things that can be made
strict here, and a stricter emulator reproduces the phone's failures locally
instead of one per hardware round.

**GD1DRV can be emulated.** EKA2L1 has an `ldd::factory` framework and no
GD1DRV in it -- `suitable_ldd_instantiate_func` finds nothing, which is why
even the native run cannot use the driver. Writing that factory would let the
native game get past it and show what the driver is actually for, which is the
one thing needed to decide what the shim should answer instead of zero.

## What GD1DRV is

EKA2L1 answers this itself: `ldd/src/collection.cpp` maps the name `gd1drv` to
`mmcif_factory`, the **MMC interface**. Its channel implements two EKA1
controls, `select_card = 4` and `card_info = 6`, and the second fills a
twenty-byte `{ TUint32 cid[4]; TUint32 type; }` with the memory card's CID and
a type of 0, ROM.

The game's own code, in the decrypted region, is exactly that:

```
0010b248  add r5, sp, #0x4c   @ the channel
0010b250  mov r1, #4          @ select_card
0010b25c  bx  r6
0010b26c  bl  #0x118f38       @ zero twenty bytes at sp+0x24
0010b270  mov r3, #4
0010b274  str r3, [r4, #0x10] @ type = 4, unknown
0010b27c  mov r1, #6          @ card_info
0010b288  bx  r6
0010b294  ldrb r3, [r1, r3]   @ then eight bytes out of the CID, 14 down to 7
```

So the game asks the card who it is and reads eight bytes of the answer. This
is the copy protection: an N-Gage game shipped on a card, checking the card.
Answering nothing leaves the type at 4 -- the game is told its card is not a
game card. `gate6_mmc_control` now answers `card_info` the way EKA2L1's own
channel would, a zero CID and ROM, so the game is told what the emulator would
tell it rather than what an absent card would. The emulator's run is unchanged
at 170 milestones, so this is not the wall, but it removes a wrong answer.

**The native reference is real but limited.** The original game does run on
NEM-4 (`--run 0x101fd42d`) and does load GD1DRV.LDD -- but EKA2L1's N-Gage
support takes it down at 0x9EBD3A00 well before our port gets on the S60v3
side, so it cannot serve as a full trace to diff against. It is still worth
having: it showed that the native game reads `E:\game.id` from the card root,
which our port never opens.

## Where it actually stops

Reading the tail properly rather than the milestone count: the phone gets
**past** the whole driver interaction -- select, card info, free, close -- and
then opens a file at 0x10a314 and dies on the `RFile::Read` at 0x10a814.

```
0010a7f8  mov r0, sp          @ a TPtr8 on the stack
0010a804  bl  #0x1195b8       @ TPtr8::TPtr8(buffer, length)
0010a810  bl  #0x11a418       @ RFile::Read(that)
```

Which is worth pausing on, because `RFile::Read` is the file server writing
into *our* address space across an IPC boundary. A descriptor that points
somewhere it should not is no longer a fault in this process; it is a server
writing where it was told to. That is one of the few things a user process can
do that ends kernel-side, which is what a reboot means.

`WATCH_THE_READS` records the descriptor before each read -- its type and
length word, its maximum, and its buffer. The emulator's are all unremarkable:
type 2, lengths matching maxima, buffers on the stack at 0x40xxxx or in the
heap at 0x8bxxxx. Anything on the phone pointing into the game's chunk at
0x46xxxxx, or anywhere that is not stack or heap, is the answer.

## A buffer overflow that was mine, not the game's

*What this section said before was wrong, and the way it was wrong is worth
keeping.* Recording each read buffer against the cell it was allocated in, in
the emulator, gave this:

```
max 0x8      buf 0040f880   (stack)
max 0x80     buf 0040f888   (stack)
max 0x2823   buf 008c3e38   cell 008c3e38 size 10275
max 0x5      buf 0040f7c8   (stack)
max 0x54e0   buf 008b2a78   cell 008b2030 size 73216
max 0x2807c  buf 008bd448   cell 008bd448 size 31        <-- 163964 asked for
max 0x2807c  buf 008b9b70   cell 008b9b70 size 163964
max 0x2807c  buf 008b9b70   cell 008b9b70 size 163964
```

and the sixth line was written up here as the file server being handed a
thirty-one byte cell and asked for a hundred and sixty kilobytes -- a
server-side overflow, which is the one kind of thing a user process can do
that ends kernel-side, which is what a reboot means. It fit so well that it
went in as a finding.

It is an artefact of the instrument. `allocPtr`/`allocLen` is a ring of the
last thirty-two allocations and it is never told about frees, so an address
that has been handed out twice appears in it twice. The search ran the ring in
slot order and stopped at the first entry containing the buffer, which is
whichever of the two happens to sit at the lower index -- here the stale
thirty-one byte one. Searching newest-first instead, the same run reports:

```
max 0x54e0   buf 008b2a78   cell 008b2a78 size 21728
max 0x2807c  buf 008bd448   cell 008bd448 size 163964
max 0x2807c  buf 008b9b70   cell 008b9b70 size 163964
```

Every read sits in a cell exactly its own size. There is no overflow, and there
never was one.

### Probes, and what they cost to have built

What settled it is a new instrument. A *probe* is a breadcrumb that also
reports two of the game's own registers, at one exact instruction:
`probe_plant` takes the address as given and refuses the site if what stands
there cannot be moved, where `crumb_plant` hunts forward for an instruction it
can move and so answers a few instructions late. Two registers, a marker, and
the site are enough to ask "what was in here, here".

Three of them took the overflow apart in two runs.

The first five went around 0xe4774, which this section had named as the guilty
caller. They reported the table at r6 = 0x8b1c28, index 0, the slot at +0x240
written with 0x8b2a78, and the same slot read back -- correct, and with a
length of 0x54e0 rather than 0x2807c. That path was never the one. The import
trace records the call to `RFile::Read` from *inside* the helper at 0x10a814,
which is the same address whichever caller asked, and the attribution to
0xe4774 was a guess dressed as a reading.

So the next probe went on the helper's own first instruction, reporting `lr`:

```
lr 047e3774  buf 0040f7c8   ->  caller 0x0e3770
lr 047e4778  buf 008b2a78   ->  caller 0x0e4774
lr 047ed698  buf 008bd448   ->  caller 0x0ed694
```

(the image loads at 0x4700000). The read this section was about comes from
**0xed694** -- the caller it had cleared as correct -- and a third probe, on
that caller's own allocation, closes it:

```
0x000ed678  r0 = 008bd448   r4 = 0002807c
```

The game asked for 163,964 bytes and got a cell of 163,964 bytes, and read
163,964 bytes into it. There is nothing wrong with the read.

### What is left of it

- The reboot has no explanation again. This was the leading one for four days.
- 0xd5a08 is not `User::Alloc` but a one-instruction veneer into an import
  stub at 0x119608 (`ldr ip,[pc,#4]; ldr ip,[ip]; bx ip`), one of a table of
  them at 0x1195f0 and up reading an IAT at 0x10184xxx. Allocation is in
  `kHot` and untraced, which is why no import record appears between the
  `RFile::Open` and the read.
- The read helper at 0x10a7dc is fully read:
  `read(void *buf, TInt len, RFile *f)` builds a `TPtr8(buf, len)` on its own
  stack, calls `RFile::Read`, and returns the length read or -1. The length is
  the caller's own and is never derived from the buffer.
- `CLAMP_THE_READS` clamped reads that were not too long. That it starved the
  game (43 milestones against 170) was the instrument breaking the run, not
  evidence about the buffer.

**Twice now the instrument has been the bug** -- the breadcrumbs that rebooted
the phone, and now this. The pattern is the same both times: the tool was
written in the same hour as the theory it went on to confirm, and nothing was
pointed at the tool until the theory ran out of places to go. A measurement
that agrees with the theory has to be checked as hard as one that does not,
and the cheapest check is a second instrument that does not share the first
one's assumptions. Here that was three probes and two emulator runs, and it
could have been run on day one.

## What the next hardware round is for

Four reboots in a row and the one thing they do not say is *where*. The three
newest phone logs end at 224, 226 and 265 records, and with eight events to a
block the last record on disk is up to seven events before the one that killed
the phone. That ambiguity is not academic: it is the same gap that let 0xe4774
be named as the caller doing the damage when it was 0xed694.

So this build is for location, and carries three changes, all of them cheap:

- **`LOG_ZOOM = 65`.** From the sixty-fifth traced event on, every record is
  written and flushed as it happens. About eighty extra writes on a run of the
  length the phone manages; the build that took the phone down by write volume
  alone did around two thousand. *If this one reboots noticeably earlier than
  the last three, the instrument is implicated again and the window closes.*
- **`RFile::Open` names its file.** `arg_thunk` now keeps the third argument
  as well as the first two, so the name descriptor can be read: the last
  twenty-four characters, which is the filename and enough of the path to
  place it. The emulator's six opens read
  `nokia_en.rle`, `6RBC.dat`, `cis.dat`, `cwivenc.dat` ×3. A record that says
  "a read" becomes one that says where in its own loading sequence the game
  had got to.
- **Probes on the read helper.** Two records per read, naming the caller.

What the phone and the emulator do is otherwise the same shape. Comparing the
import histograms of the phone's last run against the emulator's, the phone is
a strict prefix -- fewer of everything, nothing it does that the emulator does
not, including the one pass through direct screen access. It is not taking a
different path. It stops.

And it stops fast: the tick records put the whole run at **64 ticks, one
second**, from the first milestone to the last. Whatever kills it is not a
watchdog and not a slow leak.

### Checked while waiting: the game's globals are not missing

The image declares `dataSize = 0` and `bssSize = 0`: the game has no writable
static data at all, so every global it has lives behind `Dll::Tls()`. That is
the GCC98r2 pattern for a polymorphic DLL, and it is the sort of thing a loader
that never runs a DLL attach would silently lose -- which would explain the
container full of stale pointers that the emulator dies on.

It is not lost. Reading the image out:

```
000b8f24  operator new(8)               @ the TLS root
000b8e48  [r5+4] = 0x10182f38           @ its table
          [r5+0] = operator new(0x28)   @ ten pointers, zeroed
          Dll::SetTls(r5)
000b8f44  set(a, b): Dll::Tls()->[0][b] = a
000b8fa4  b 0xc8b2c -> UserSvr::DllTls(0x10000000)   @ the handle is the
                                                       image's own code base
```

and three probes say it all happened:

```
0x000b8f54  r0 = 0089aac8   -> 0089aaf0 04882f38 00000000 00000000
```

`0x89aaf0` is the ten-pointer array and `0x4882f38` is `0x10182f38` correctly
rebased into our chunk. The root is built, the handle survives our relocation,
and 9.x answers it.

*What nearly went in here as a finding* is that `UserSvr::DllSetTls` never
appears in the trace while `DllTls` appears six times -- read as "the creator
never runs". It never appears because `TRACE_MILESTONES` traces a whitelist
and 304 is not on it. Three probes and one emulator run, no hardware, and the
theory was dead before it was written down. That is the intended cost of one
now.

## The instrument goes nearly silent

> *Superseded in its premise: write volume was never what took the phone down.*

The zoomed build (`g6box-28`) did what it was for and cost what it was warned
it might. It located the death precisely for the first time -- and it died
sooner than the three before it.

```
                 records  traced events  ticks  est. writes
g6box-30             226           115      -           86
g6box-24             224           107      -           86
g6box-18             265           106     64           91
g6box-28 (zoom)      295            86     27          129
```

Records went up only because the build writes more per event; the yardstick is
traced events, and it fell. So did the clock.

**Where it dies.** With every record flushed as it happened, the last one on
disk is the last thing that happened, and it is the `RLibrary::Lookup` from
0x13f5e4 -- the second pass through that site, having succeeded on the first
fourteen records earlier. Our own `gate6_library_lookup` logged nothing at all
after it, so the phone went down inside the handler, before it had read the
library's handle. The site is:

```
0013f5d8  ldr ip, [r4]      @ a function pointer out of a table
0013f5e0  bx ip             @ -> RLibrary::Lookup        <- last record
0013f5e4  mov r6, r0        @ whatever it answered
0013f5f0  bx r6             @ ... is called, unconditionally
```

**What that is worth against what it cost.** Two of the four things this port
has spent hardware rounds on turned out to be the instrument. The run that
recorded most also died soonest. The write budget has never been measured
against reach, only guessed at -- so rather than guess again at the right
weight, `SILENT` takes it to nearly nothing:

- `LOG_BLOCK` 1024 and no zoom: the log never fills, so it never writes.
- No probes, no read watching, no allocation watching, no clock.
- The box, which was rewritten on every one of the fifty-eight framework calls
  into our vtable slots -- half the whole budget, and not one of those writes
  survives a reboot -- now goes down once every thirty-two traced events. Four
  writes on a run of the length the phone manages, carrying the count, the last
  import and the ring of the last thirty-two events.

Three or four writes against a hundred and twenty-nine. **Nothing the shim
does changes; only what it says about it.** The emulator reaches the same
0x30002 with the same trace, and writes no log at all.

The question is the one thirty rounds have not asked: does the phone still go
down when almost nothing is being written? Either answer is worth the round.
If it still reboots, the instrument is finally exonerated and every future
build can afford to talk. If it does not, the log has been the bug all along,
and the next instrument is a memory-only ring read out at the end.

## The reboots were ours

The silent build did not reboot the phone. It panicked KERN-EXEC 3 -- an
ordinary unhandled exception in the game's own thread -- and got further than
any run before it.

```
                          traced events   how it ended    writes
g6box-18 .. g6box-30        106 .. 118    reboot            86-91
g6box-28 (every record)            86     reboot              129
g6box-32 (silent)               128+      KERN-EXEC 3         3-5
```

Thirty rounds. **The instrument was the reboot**, all of it, and the write
volume was the variable the whole time -- which is why the reboot moved around
with each build and never matched anything the game was doing. It is the third
time the tool has been the bug and by far the most expensive: two of the three
theories this file records at length were autopsies on a corpse we made.

What the box says, with the game's own panic instead of a dead phone:

```
  128 traced events at the last write, so 128..159 in all
  last import 283  RLibrary::Close
  reached a slot of ours, THE FRAME LOOP RAN
  stack high-water 1932 bytes
```

`THE FRAME LOOP RAN` has never been set on hardware before. And the phone's
last sixteen events are the emulator's, instruction for instruction, offset by
twenty-seven:

```
  phone 117..127   283@13f610 332@13f63c 326@13f68c 326@10abf8 325@10b08c
                   326@10b0e4 326@10b128 326@10b244 326@10b2d4 326@10b308 283@10b37c
  emu   144..154   283@13f610 332@13f63c 326@13f68c 326@10abf8 325@10b08c
                   326@10b0e4 326@10b128 326@10b244 326@10b2d4 326@10b308 283@10b37c
```

The phone died between events 128 and 159, which maps onto the emulator's
155-186; the emulator's own 0x30002 falls between 160 and 191. **Those windows
overlap**, so the phone and the emulator may now be failing at the same place
-- which would make the rest of this local.

### The instrument that should have been there all along

A log that appends pays a write per block and loses whatever has not been
flushed. A box is a fixed record rewritten in place: one `file_write_at`
carries the whole of it however big it is. So the ring went from sixteen
events to sixty-four -- same single write -- and it goes down every sixteen
traced events, which is about ten writes a run against the eighty-six the
rebooting builds were doing. The last box therefore always holds every event
since the one before it, and forty-eight more for context.

`gate6_fault` now writes the box before it panics, so if the exception handler
ever does run the record is exact rather than up to fifteen events short.
It did not run this time: KERN-EXEC 3 is what the kernel raises when nothing
handled the exception, and our own panic category would have shown instead.
`User::SetExceptionHandler` is not taking on 9.x, which is its own small
problem and worth one look later.

**The rule this earns:** the instrument's cost is a measurement, not a guess.
Every future build states its write budget, and no build goes to hardware
spending more than the last one that survived.

## The emulator's own fault, read out

With the phone and the emulator failing in the same window, the 0x30002 fault
is worth the probes. Six of them, one run:

```
0x000cc914  r5 = 008b80e0   str r5, [r5]     <- User::Alloc(4), then self
0x000cc92c  r5 = 008b80e0   ldr r2, [r5]
0x000ccb68  r5 = 008b80e0   ldr r0, [r5]     -> the container, = the cell
0x000d9428  r0 = 008b80e0   r1 = 0483fce0    <- find(container, "6RBC.off")
0x000d9484  r6 = 008b80e0                    <- head = [container + 4]
0x000d94e4  r5 = 00703abc                    <- and stricmp on its [0]
```

and the machine at 0xd9428, once its jump table is unpicked, is nothing
exotic:

```
find(container, name):
    for (n = container->[4]; n; n = n->[0xc])
        if (!stricmp(name, n->[0])) return n;
    return 0;
```

So the game asks for **four bytes**, writes the cell's own address into it, and
later reads `[cell + 4]` as the head of a list. `User::AllocLen` says a
four-byte request gets a **thirty-six** byte cell here, so that read is inside
the cell -- it is not out of bounds, it is uninitialised. It holds 0x703abc,
whose `[0]` is 0x00030002, and stricmp walks into it.

The container's words are the giveaway:

```
0x8b80e0:  008b80e0  00703abc  00000000  000f000e
           00000000  00000000  008b2dec  008b2df8
```

Word 0 is what 0xcc914 wrote. The rest is not noise -- two heap pointers, a
pair of counters -- it is a **live-looking object the game allocated earlier,
freed, and is still reading**. Our heap handed that address back out for the
four-byte cell and word 0 went over the top of it.

### Three fixes tried, none of them a fix

| | traced events | stricmp reached | ends |
|---|---|---|---|
| as it is | 179 | yes | fault 0x30002 |
| zero the cell's slack | 43 | **no** | clean `User::Leave` |
| pad every allocation by 16 and zero | 160 | no | fault elsewhere |
| free nothing at all | 160 | no | fault elsewhere |

Zeroing works exactly as intended -- the list reads empty and the walk stops
before a single node -- and the game then gives up at a *third* of the
distance. It was reading that memory on purpose. Padding moves every cell in
the heap and fails earlier somewhere else, which is the trap `VT_MARGIN` set
two months ago. Leaking gets no further either.

**So the walk is a symptom.** The container is meant to hold a list of names
and it holds a freed object; the question is what was supposed to fill it, not
how to survive its being empty. That is the same shape as `6RBC.off` and
`cwdynlog.dll`: things the game looks for that are not there.

`User::AllocLen` is imported now and `gate6_alloc` stays, switched off, with
all three experiments behind their own constants -- they cost nothing off and
each one is a question that will be asked again.

*Two process notes.* `REPORT_LAST_BOX` -- the startup panic that reported the
previous run's box -- truncates the log and kills the run before the game
starts, which cost four confused iterations here before it was spotted. It was
the only way to read a record off a phone that had just rebooted; the box
survives on its own now, so it is off. And a build with no writable globals
cannot hold a `static Context *` for a one-off measurement: it fails at the
link, which is the design working.

## What the container is waiting for, and why the emulator is not a reference

> *Refined: our port gets further under the emulator than the original N-Gage binary does.*

Following the empty container back a layer at a time:

```
0xcc90c   User::Alloc(4) -> the slot;  [slot] = slot          "empty"
0xcc928   0xe9808(table = owner + 0x28, 1, 0)
0xcc940   r0 = 0xe98c4(table, 1, *slot, 0, key = 0)
          ... twenty instructions of shift-and-add ...
0xcc984   [slot] = that                                        install
0xccb68   find(*slot, "6RBC.off")
```

The twenty instructions between are a multiply by 3467093631 followed by a
multiply by 4016970111, and those two multiply to **1** mod 2^32. The whole
chain is the identity: `[slot] = 0xe98c4(...)`, obfuscated.

And 0xe98c4 is not a container lookup at all. It walks a table of 24-byte
entries for one whose `[+4]` matches the key, and for each match computes

```
overrun = entry[+0xc] - 1.5 * entry[+0x10]      clamped at zero
```

`0xd59e0`, which fills `entry[+8]`, resolves to **`User::TickCount`**;
`0xd59e4`, one veneer along, is `Math::Random`. The entry the probes caught
reads

```
[+4] = 0 (the key, matched)   [+8] = 0x8e -> 0x90   (ticks, rising)
[+0xc] = 0x30 -> 0x2e         [+0x10] = [+0x14] = 0xf00
```

0xf00 is 3840 ticks: **sixty seconds**. So this is a stopwatch, not a heap,
and the result it hands back is

```
return sl( fallback | overrun | (count << (Math::Random() % 16)) )
```

-- the fallback with junk OR'd into it if anything has overrun, which is
anti-tamper machinery of the same family as the `6RBC.off` and `cwdynlog.dll`
searches and the GD1DRV card check. Two seconds into a run nothing has
overrun, so it returns the fallback unchanged, and `[slot] == slot` is the
**correct** result. The empty container is not a symptom of anything. The game
then walks it anyway.

### The emulator is not ground truth for these paths

Which raises the obvious question: what does the real game do here? EKA2L1 has
both N-Gage ROMs installed and the original is sitting on `e.ngage`, so it can
be asked directly --

```
eka2l1_qt --device RH-29 --run 0x101fd42d
```

-- and **the unmodified game, on its own platform, dies the same way**:
KERN-EXEC 3, after opening `cwp.dat`, `nc.dat` and `cwivenc.dat` in the same
order our port opens them. The fault is at 0x139588:

```
0010a9f0  mov r0, #0x24        @ thirty-six bytes
0010a9f4  bl  operator new
0010aa00  blne #0x139568       @ construct(it, r4)
00139588  ldr r2, [r1, #0x240] @ <- faults; r1 is not an object
```

`[r1 + 0x240]` -- the same +0x240 table this session opened with at 0xe4724.

So EKA2L1 cannot run this game on the N-Gage either, and **the emulator has
never been a reference for the protection paths**, only for the shape of the
import sequence. Two conclusions follow. The 0x30002 fault may be an artefact
of whatever EKA2L1 is not giving the protection rather than a defect in the
port. And the phone, which is the only real platform in this loop, is the only
thing that can say which.

## A twelve-bit offset, and what build 34 is for

`arg_thunk` kept the third argument of a wrapped call by emitting

```
str r2, [r12, #offsetof(Context, argR2)]
```

and that instruction encodes the offset in **twelve bits**. When `SILENT` set
`LOG_BLOCK` to 1024 the log buffer in front of the field grew to eight
kilobytes, `argR2` moved to offset 9404, and the assembler-by-hand truncated it
to 1212 -- so the write went 8192 bytes short, landing inside the log buffer,
where in a silent build nothing ever reads it. No damage: builds 32 and 33 are
unaffected, and the only casualty was a field being read back as zero. But it
is the fourth self-inflicted instrument bug in this file and the first one that
could have corrupted state rather than only lying.

The thunk carries the field's own address as a literal now, which cannot be
truncated, and the silent build's buffer is a quarter of what it was. None of
the other thunks index the context; they all use literals already.

**Build 34** is still silent, and carries:

- The box every **eight** traced events rather than sixteen: about twenty
  writes on a run of the length the phone manages, against the eighty-six the
  rebooting builds were doing. It pins where it stopped to within eight events.
- **The last file opened**, in the box. It rides the write the box was making
  anyway, and it turns "it stopped at a read" into "it stopped on this file".
  The emulator's says `...s\6rbc\cwivenc.d`.
- **What `User::SetExceptionHandler` returned.** KERN-EXEC 3 is the kernel's
  panic for an exception nothing handled, so our handler is not running and our
  own category never gets its chance. The emulator answers KErrNone; if the
  phone answers anything else, that is why, and a working handler would give
  the faulting address outright.
- A log again, by accident and worth keeping: a 256-record buffer fills once in
  a run of this length, so one extra write buys the first 256 records while the
  box holds the tail.

## It is the log file, not the number of writes

> *Superseded: the log was never the cause. See "The reboots were the handle".*

Build 34 rebooted the phone at eighty-eight traced events. It was supposed to
be the cheap build.

```
build  writes  what it wrote     traced events  outcome
18/24/30  86-91  log + box           106-118     reboot
28       129     log, every record        86     reboot
32         4     box only               128+     KERN-EXEC 3
33        10     box only               128+     KERN-EXEC 3
34        12     box + one log flush   88-95     reboot
```

The count does not order these: eighty-six writes got further than twelve. But
**every build that wrote the log rebooted, and the two that wrote only the box
did not.** Six runs, no exceptions.

The difference between the two files is not how often they are written but
*how*. The box is one fixed record rewritten at position zero -- it extends the
file exactly once, when it is created, and never again. The log appends: every
block goes to a new offset and the file grows. On a phone that means the file
server updating the FAT on the internal drive, over and over, from inside a
startup sequence that never yields. The box does none of that.

So the rule is not a budget any more, it is a shape: **nothing this build
writes may extend a file.** Build 35 writes only the box, ten times, and the
history that the log was there to provide comes from the ring instead -- which
is free, because one write carries it whatever its depth. It is a hundred and
twenty-eight events deep now, which covers the whole of a run the phone gets
through.

Build 34 did buy two things. `User::SetExceptionHandler` returns **KErrNone on
the phone**, so the handler is installed and the exception still is not reaching
it -- the reason KERN-EXEC 3 shows instead of our own category is something
else, and worth one look later. And the last file opened is
`...s\6rbc\cwivenc.d`, the same as the emulator's.

## KERN-EXEC 0: an RFile closed as a plain handle

A three-run test settled this in one go. Delete the box and run: reboot. Leave
the box in place and run: **KERN-EXEC 0**, four to six times over, every time.

The only code that behaves differently when `g6box.dat` exists is the startup
read of the previous run's box, and in it:

```c
file_open(ctx->boxFile, ctx->boxFs, &name, 1);
...
rhandle_close(ctx->boxFile);        // RHandleBase::Close
```

On 9.x an `RFile` is an `RSubSessionBase`, whose first member is the
`RSessionBase` it belongs to. So the first word of an `RFile` is **the file
server session's handle**, and closing an RFile as a plain handle closes the
session out from under everything that follows -- which is invalid-handle,
KERN-EXEC 0, on the next file operation. The emulator allows it and says
nothing, exactly as `CLAUDE.md` says it would.

There were two of these. The other, `rhandle_close(probe)`, runs on *every*
startup, on the sibling-`.cwa` probe, and is the same mistake. It now uses
`RFile::Close` (efsrv ordinal 300, imported for this).

The startup read is gone entirely rather than fixed. It existed to get a record
off a phone that had just rebooted, which the box now does on its own; it wrote
a second file that grows, which the previous section says nothing may do; and
it carried this bug. `g6box.txt` goes with it.

**What this does not explain is the reboot.** When the box is absent the read
never runs, and that is the run that rebooted. Two separate faults were being
read as one, and every second run has been polluted by this since the box was
introduced.

*Still open, noted rather than changed:* the game closes its own files through
import 99, which `gen_shim` names `RFsBase::Close` and which we answer with
`RHandleBase::Close`. It is called on `object + 4` at 0x10a7c8 -- the same
address the read helper treats as the `RFile` -- so it is very likely the same
mistake in the game's own path. The other caller, 0x34ac4, closes `r4 + 0xa0`
and then touches `r4 + 0xa8`, and which of those is the RFile is not clear from
the code. The current mapping reaches a hundred and twenty-eight events, so it
stays until there is a reason beyond suspicion.

## The reboots were the handle, and the log was never the problem

Build 36 went out with the wrong binary. `ref.sh` built the reference with
`LOG_ANYWAY` on, restored the *source* afterwards and left that build sitting in
`out/`, which is where the `.sis` was copied from. So the phone got a build
writing the log at a block of eight -- the shape that had rebooted it every
time for a month.

It did not reboot. Three runs, no reboot, and further than any silent build:

```
run 1 (deleted first)   KERN-EXEC 3   131 traced events
run 2 (deleted first)   KERN-EXEC 3   131 traced events
run 3 (files left)      KERN-EXEC 3, and sometimes 0 alongside it
```

So the previous section is wrong. It is not that the log extends a file and the
box does not. **It was the two RFiles closed as plain handles**, which closed
the file server session; everything written afterwards went through a dead one,
and the more a build wrote the worse that got. The correlation with the log was
real and the cause was not. Fix the close and the log is free.

That is the fifth instrument bug and the first that was hiding a real one --
every reboot for a month was this, and each of the theories built on top of it
(a write ceiling, a write rate, extending writes) was fitted to its shadow.

*The process failure is its own lesson.* A script that builds one configuration
and ships another is a trap that goes off silently, and this one went off in the
user's hand. `ref.sh` builds and leaves the shipped configuration now; there is
only one binary.

### Where it stops

The run ends one event short of the emulator's, and the last records are ours,
from immediately before the call:

```
-- about to call import 109       RFile::Open
--   asked for  007c506c          the RFile
--   asked for  007c5068          the RFs, four bytes below it
--     text     ...ystem\apps\6rbc\cwivenc.d
                                  <- and nothing further
```

The name is read correctly by our own code, so the descriptor is sound. The
open of `cwivenc.dat` either never returns or the fault is on the instruction
after it. The emulator, at the same event, opens it and gets KErrNone.

The user also reports **a black bar with pixels in it, in every run**, and one
to two seconds before the panic. Something is being drawn.

Build 37 keeps the log, and puts a result thunk on `RFile::Open` -- wrapped
*inside* the trace thunk, so the trace still records the game's own return
address rather than ours, which is the alignment against the emulator for the
one import being asked about. Whatever it returns, or the absence of any record
at all, answers this.

## RFile::Open succeeds; the fault is in the free after it

Build 37 answered its question in one record:

```
import 109  RFile::Open  from 10a314
--   by import  6d
-- returned     0                 <- KErrNone
```

The open succeeds. The game's next two instructions are

```
0010a314  mov r0, r4        @ keep the result
0010a318  mov r0, r5
0010a31c  bl  #0x118e68     @ __builtin_delete -- the name buffer
```

and that is where the phone stops. `__builtin_delete` maps to
`scppnwdl::_ZdlPv`, which is correct, and the buffer came from `HBufC16::New`
on the same heap, so the free itself is right. **A free only faults on a heap
that is already wrong**, which means the damage was done earlier and this is
merely where it surfaces -- and the emulator, whose heap is a flat region that
forgives almost anything, sails through.

So build 38 matches every free against the allocations still outstanding. The
ring is 256 deep and tracks `__builtin_new`, the three `User::Alloc` variants
and `HBufC16::New`; every free marks its cell spent, from the first one, so a
double free cannot read as an ordinary match. Records are rationed -- plain
matches only over the stretch the run dies in -- but a **double free** or a
**stray** (a pointer never handed out) is reported wherever it happens.

The emulator's baseline: sixty-nine frees, every one matched to a live cell,
no doubles and no strays. If the phone shows either, that is the corruption,
and it will name the pointer.

*Also worth recording*: four panics in that single run, two KERN-EXEC 0 and two
KERN-EXEC 3. One thread cannot panic four times, so more than the game's thread
is going down -- which fits a heap the file server is also writing into.

## Build 38 regressed, and was reverted rather than explained

> *Superseded: build 38 was not a regression; that was one run. See "Three runs of one build".*

Build 38 -- the free-matching one -- took the phone from 132 traced events to
**nine**, dying in the framework's own startup with the box never written past
arming. The emulator ran it to the usual 170 and the usual fault, so there is
nothing local to bisect against.

It changed four things at once: a 256-deep allocation ring in place of a
32-deep one, `arg_thunk` on three free imports, `__builtin_new` added to the
result-wrapped allocators, and the free bookkeeping itself. Any of them could
be it, and finding out costs a hardware round per guess.

So it is reverted to build 37 whole. **A change that breaks something and
cannot be bisected locally is not worth keeping while it is unexplained**, and
four changes in one build is how a round gets wasted -- the same lesson as
build 34, which broke the write budget, and build 36, which shipped the wrong
binary. One variable.

### A search in time instead

The question build 38 was asking was *which* free. The better question is
*when* the heap went bad, because that brackets the write that did it without
needing to identify the victim.

`User::CountAllocCells` walks the whole heap cell by cell. On an intact heap it
returns a count; on a broken one it walks into the damage. Build 39 calls it
every four traced events and keeps the last event at which it came back in the
box, with the cell count. However the run ends, the box then says when the heap
was last whole -- and the emulator, for comparison, walks clean the whole way:

```
heap last walked clean at event 160, 803 cells
```

That is one import and one call every four events against build 37, which is
the smallest delta that can answer anything.

## Build 39's instrument worked and could not say so

> *Partly superseded: the walk never ran at all -- the run never reached event sixteen.*

Build 37, reinstalled unchanged, reached 132 traced events again with a single
KERN-EXEC 3. So the phone had not changed and builds 38 and 39 really did
regress -- and build 39 differs from 37 by one import and one call, which is as
clean a one-variable result as this project has had.

But "regressed" is the wrong word for what build 39 probably did.
`User::CountAllocCells` walks the heap and faults on a broken one; the run died
at traced event ten, and the box only wrote every sixteen, so **the walk's
verdict never reached the disk**. An instrument that detects the thing it was
built for and then dies before it can report reads exactly like a regression.

Build 40 fixes the reporting rather than the walk. The box goes down *before*
each walk carrying "begun at event N", and again after it carrying "came back
at event N". A walk that faults leaves the two disagreeing; one that returns
leaves them equal. The emulator now reads

```
heap walk: begun at event 160, last came back at event 160, 803 cells
```

with no false positive -- the first attempt flushed only before the walk, and
the emulator's own unrelated fault then left the two fields apart and the flag
lit for the wrong reason.

The interval is sixteen rather than four, so eight walks on a run of this
length is sixteen box writes against the forty-odd the log already does. That
also disambiguates the two readings of build 39: if the run reaches 132 again,
walking every four events was itself the perturbation; if it stops early with
the two fields disagreeing, the heap is broken by then and we have the bracket.

*The ordinal question stays open.* `user_countalloccells` was taken from a
`kernelhwsrv` def file, not from the 5320's own `euser.dll`, and euser ordinals
are not guaranteed identical across 9.1 to 9.4. It works against the RM-409 ROM
in the emulator, which is the same firmware family, so it is probably right --
but "probably" is how `SetKeyBlockMode` got keyed to the wrong ordinal months
ago, and the device's export table is sitting in `z/rm-409/sys/bin/euser.dll`
if this needs settling.

## An assumption that was never tested: that a run is repeatable

> *Partly superseded: the variance is real but does not explain builds 38-43, which fail deterministically.*

Build 40's box says the heap walk **never ran** -- it fires every sixteen
traced events and the run reached eleven. So neither the call nor the import
can be what stopped it, and the same is true of build 39.

Lined up against build 37's run, build 40 is **identical for sixty records**
and then takes a different turn:

```
 59  -- entered appui slot 4      | -- entered appui slot 4
 60  -- entered control slot 3    | -- entered appui slot 8        <<<
 61  -- entered appui slot 7      | -- entered control slot 26
 62  -- entered appui slot 4      | import 305  UserSvr::DllTls
 63  -- entered control slot 29   | import 279  CActive::Cancel
 64  -- entered control slot 41   | end
```

The framework calls a different slot, the game cancels an active object, and
the run ends. Slot order is the one thing in this record already known to vary
-- `readlog --no-slots` exists because two feature packs do not agree on it --
so this may be no difference at all.

Which exposes the assumption underneath five builds of reasoning: **that one
run of a build is that build's behaviour.** Builds 38, 39 and 40 each stopped
at nine to eleven events and each was read as a regression caused by whatever
it had changed. Build 37 reached 131 and 132 twice. Three against two is not
enough to tell a real regression from a coin landing the same way three times,
and every conclusion drawn from those three builds rests on it.

So the next round is not a new build. It is **build 40 again, three times**,
with nothing changed. If it reaches 132 even once, the last three builds were
never regressions and the heap walk is still an open instrument. If it stops at
eleven every time, the difference is real and worth bisecting properly.

It costs no install and it tests something that should have been tested before
the first "regression" was declared.

## Three runs of one build: 128, early, early

```
build 40, run 1   128 traced events, heap walked clean, 1209 cells
build 40, run 2   stopped before the first box write
build 40, run 3   stopped before the first box write
```

Same binary, three runs, one of them as far as build 37 ever got. So:

**Builds 38, 39 and 40 were never regressions.** Each stopped early once, each
was read as broken by whatever it had changed, and two working instruments were
reverted on the strength of a coin landing the same way three times. The
reasoning in the three sections above is wrong wherever it treats a single run
as a build's behaviour.

**The heap is intact.** At traced event 128 -- four events before the free that
faults -- `CountAllocCells` walks the whole heap and counts 1209 cells. The
chain is sound. So the free is not faulting because the heap is structurally
broken, and the theory the last two builds were built on is dead.

That is worth more than it cost. It narrows the free to the **pointer**: one
that was never handed out, or one already freed. Which is precisely what build
38's free matching was built to find, and build 38 works. Both instruments ship
together now.

### The rule this earns

**A hardware round is three runs, not one.** Every count in this file taken
from a single run is an upper bound on nothing: the same build reaches 128 or
stops at eleven depending on something none of these instruments see. Compare
the longest of three, and never call a build a regression on one run again.

Some of what is written above will have to be re-read with that in mind -- the
write-budget table in particular, whose rows are single runs.

## Sixty-four records, five times: the log was dying, not the game

> *Superseded: sixty-four records is eight of our own log blocks, not a sector.*

Build 41, three runs, all three stopping at nine to eleven traced events. And
every short run in this record -- builds 38, 39, 40 and all three of 41 --
stopped at **exactly sixty-four records**.

Sixty-four records is 512 bytes. One sector. A number that round is not a game
crashing; it is a file that stopped growing.

`log_block` never looked at what `file_write_at` returned, and it advances
`logPos` whether or not the write landed. So a file server that starts refusing
leaves the log at 512 bytes, every later write goes to a higher offset and also
fails, and the run carries on with no record at all. The box stops at the same
moment for the same reason -- which is exactly why those runs read as "ended at
event nine" and were written up three times as regressions.

**So the short runs were never short.** They are runs where the instrument went
deaf early, and the game very likely carried on to the same place it always
does. The KERN-EXEC 0 the user reports on those runs is the natural end of that
story: writes on a session that is no longer good.

That also retires the "build 41 is 0 for 3" reading from the section above, and
the dose-response it seemed to show across builds 37, 40 and 41. There is no
gradient; there is a coin, and what it decides is whether the *log* survives,
not how far the game gets.

Build 42 stops discarding the error. The first failed write panics with its own
category -- `G6WR` for the log, `G6BW` for the box -- carrying the file
server's error code as the reason. The run is already over as far as the record
goes; this way the phone puts the reason on its own screen, and a number that
the user can read off is worth more than a file that is not there.

## No write error, so the bisect is real

Five runs of build 42: sixty-four records and nine traced events, every one.
And **no `G6WR` or `G6BW` panic** -- so `file_write_at` is not failing, the
512-byte theory is wrong, and the run genuinely ends there.

That also kills the coin. Five identical runs is not a coin; builds 38 to 42
stop here reliably, and build 37 does not, twice. The one long run of build 40
is the outlier. So there is a real difference between 37 and everything after
it, and it has to be bisected rather than reasoned about -- three sections of
this file are what reasoning about it produced.

```
build 37   28600 bytes of code, 145 relocations, 37 imports   reaches 132
build 42   25560 bytes of code, 158 relocations, 38 imports   stops at 9
```

The code got *smaller* while gaining features, which is odd enough to check:
the diff is 113 insertions against 9 deletions, all of them intended, and
`gate4_shim.cpp` is untouched since long before either. Nothing was lost in the
merge that built 41. Left as a compiler artefact, noted in case it turns out
not to be.

**Build 43 is build 37 plus the free matching, and nothing else.** The heap
walk is gone and so is `user_countalloccells` -- the one thing builds 39
through 42 all carried and 37 did not, and the one whose ordinal was taken from
a def file rather than from the N95. Build 38 failed without it, but that was a
single run, and single runs are what this whole detour was made of.

If 43 runs the distance, the import was the difference and the free matching is
finally in hand. If it stops at nine, the free matching is the difference and
the bisect continues into it. Either way the next answer is one variable wide.

## The sector was a block boundary of our own

Build 43 -- build 37 plus the free matching, with the heap walk and its import
removed -- stops at sixty-four records too, both runs. So the import was not
it, and the free matching is where the bisect goes next.

But first, two things that were wrong.

**Sixty-four records is eight blocks of eight.** `LOG_BLOCK` is 8; the log
flushes on block boundaries; so a file of exactly 512 bytes means the run died
somewhere in records 64 to 71 with the last flush at 64. It is a boundary of
our own making, and reading it as a disk sector produced a whole section above
about extending writes and the file server. The `G6WR` panic that never fired
had already said as much.

**The 3040-byte code shrink is not damage.** `gate6_arg` went from 0x808 to
0x49c while *gaining* code, and so did every other function that touches the
context -- `gate6_library_lookup`, `gate6_result`, `gate6_write_memory`,
`gate6_cancel`. All of them shrinking together is the compiler re-optimising
around a different `Context` layout, not code going missing. The remaining
hand-assembled offsets were checked: `arg_thunk` was the only one that indexed
the context with a twelve-bit immediate, and it uses a literal now.

### What build 44 carries

Both build 43 runs are identical through record 62 and then differ by one slot
from build 37 -- `appui slot 8` where 37 has `control slot 29` -- and die
within the next eight records, which the block hides.

- **Every record written on its own for the first 768 bytes.** Ninety-six
  records at exact resolution, then back to blocks. The true last record goes
  on disk instead of the last multiple of eight.
- **The setup's own state, in a box write taken before the game runs.** Spare
  arena remaining, `sizeof(Context)`, and a bitmask of which optional wraps
  installed. Every failing run still produces a box, so this is the one record
  guaranteed to come back -- and a thunk silently skipped for want of arena
  would look exactly like the game dying. The emulator reads 20612 bytes spare,
  a 3768-byte context, and all four wraps installed.

## The furthest run yet, and it dies inside a free

Build 44: **136 traced events**, four past the previous best and well past the
64 that four builds in a row had been read as. So builds 38 to 43 were not
failing where they appeared to -- the log's block boundary was hiding the tail,
exactly as the section above worked out.

The free matching ran, and it clears the pointer:

```
31 frees, 31 matched a live cell, 0 double frees, 0 strays
```

The setup state matches the emulator exactly -- 20612 bytes of spare arena,
a 3768-byte context, all four optional wraps installed -- so nothing is being
silently skipped on the phone that is present here.

And the last three records are:

```
731  -- about to call import 0x13b      User::Free
732  --   asked for  007b7cd8
     (nothing further)
```

**It dies inside a free of 0x7b7cd8**, and whether the ring knew that pointer
is the one thing not on disk, because the exact-logging window was placed over
the first 96 records -- where the failure was wrongly believed to be.

So build 45 writes the pointer and flushes it *before* anything is done with
it. Thirty-one frees in a run can each afford a write of their own, and this is
the one place the record has to survive the thing it is recording.

Two facts now stand together and constrain what is left: at event 128 the heap
walks clean with 1209 cells, and every free up to the fatal one matches a live
cell. The heap is sound and the pointers are sound -- so either the fatal
pointer is the first bad one, or `User::Free` is faulting for a reason that is
not the cell it was given.

## Build 48: the neighbouring cell, and a bound that was not a bound

Every property of the fatal free that belongs to the cell itself now measures
correct -- the allocated chain walks clean, the ring recognises the pointer,
the requested size is 27 bytes and the header in front of it reads `0x28`. What
that leaves is the part of a free that is about somebody else's memory.
`RHeap::Free` coalesces: it reads the header of the cell *after* the one being
freed to decide whether the two can merge. A damaged neighbour would give
exactly the run we have -- a clean walk, a good pointer, a good header, and a
fault inside `User::Free`.

So build 48 reads the neighbour. The cell's own header gives its length, so
the next header sits at `payload - 4 + length`, and the record carries that
address, the word at it, and what the allocation ring makes of it.

### The first attempt walked off the end of the heap

The read was first guarded by a `heapTop` -- the highest address any recorded
allocation had reached, `result + arg` maintained in `gate6_result`. It did not
work. The emulator's fault moved from `0x30002` (its normal ending, 1066
records) to `0x9B0000` at 768 records: an unmapped high address, which is what
an instrument reading past the end of the thing it is measuring looks like.

The reason is worth keeping, because the ring is used for more than this.
**`gate6_result` records every non-zero result into the allocation ring, and
the ring is wrapped around `RFile::Open` as well as the five allocators.** A
failed open returns `KErrNotFound`, so `allocPtr` gets `0xFFFFFFFF` and
`heapTop` gets `0xFFFFFFFF + arg`. One failed open -- and the game's file-exists
switch performs several -- and the bound is the whole address space. The bound
was computed from a ring that does not only contain allocations.

### What is safe, and how it was checked

The neighbour is dereferenced only when the ring vouches for it the same way it
vouched for the cell being freed: some entry's payload has to start one word
past the candidate header. Nothing else is touched. A neighbour the ring does
not know is reported as an address and left alone, which is itself an answer --
it means a free cell, or one older than 256 allocations.

Two emulator runs separate the instrument from the effect:

| | records | fault |
|---|---|---|
| read disabled | 1066 | `0x30002` |
| three extra records per free, **no dereference** | 1258 | `0x30002` |
| ring-vouched dereference | 1236 | `0x30002` |

The middle row is the one that matters. Extra log volume does not move the
emulator; only the dereference did. That is the check rule 4 asks for -- the
`0x9B0000` reading was not assumed to be the off-heap read just because the
theory said so.

The records it produces here look like this:

```
free                     from 8b89e0
  matched a live cell of from 24
  cell header word       from 28
  cell header word       from 0
    next cell            from 8b8a04      <- the neighbour's header address
    next cell            from 28          <- the word in it
    next cell            from ffffffff    <- SPENT: the ring freed it already
```

The write count is unchanged: the new records go inside the block the verdict
already flushed, and `log_block` is called exactly as often as in build 47.

## Build 49: stop measuring the cell and turn the frees off

Rounds 44 to 48 each closed one description of the damage and found none.
Chain, pointer, size, header, neighbour: every measurable property of the fatal
free is correct, and the neighbour corroborates the header from a record that
does not pass through our arithmetic. There is nothing left to measure about
that cell, and a sixth round describing it harder is the mistake this file
already has five entries for.

So build 49 intervenes instead. The three deallocation ordinals -- 315
(`User::Free`), 408 (`operator delete`) and 410 (`operator delete[]`) -- are
answered by a function that does nothing. `LEAK_EVERYTHING` has been in the
source since the use-after-free theory and has never been shipped.

It splits the question either way:

- the run **advances** -- the fault is in the free, and the port has moved past
  a wall it has been at since build 44;
- the run **dies in the same place with nothing freed** -- then the fault was
  never in `User::Free`. The free is merely the last thing we write before it,
  and everything between that record and the next traced import has been
  wearing the blame.

### The switch had to move before the instrument

`LEAK_EVERYTHING` was implemented late in the setup, after the free watcher and
the trace loop had already wrapped those IAT entries -- so turning it on
*overwrote* both wrappers and turned off every record of a free at the same
time. That is two variables in one build, and the round would not have been
comparable with 48. It is installed before the watcher now: the no-op is what
gets wrapped, the same records come out naming the same pointers, and the only
difference on the wire is that nothing is freed. A `LEAK` flag in the box says
so out loud.

### What the emulator says first

| | imports reached | ending |
|---|---|---|
| build 48 | 289 | `0x30002`, at a free of `8b8a58` |
| build 49 | 275 | `0xEAF88580`, at a free of `b32248` |

The leak is demonstrably in effect: in build 48 the emulator freed `8b89e0`
ten times over and `8b8a58` eight times, because the heap kept handing the
same addresses back. In build 49 **every freed pointer in the run is unique**,
which is only true if nothing is being returned to the heap.

And it does not get past the wall. The emulator still ends at a free -- of a
27-byte cell with a `0x28` header, the same signature as the phone's -- with no
free having actually happened. That is a prediction for the hardware round
rather than an answer, because the emulator is not a reference and dies of a
different fault. But it is the way to bet.

## Round 49: the free was never the wall

Three runs, 7592-byte logs to the byte, and the box carries `LEAK: nothing is
freed`. Every one of the 99 freed pointers in the run is unique -- build 48
freed `7b7c10` three times over, because the heap kept handing it back -- so
nothing went to the heap, and the run stops in exactly the same place:

| | build 48 | build 49 |
|---|---|---|
| traced events at the last box write | 128 | 128 |
| last import in the box | `RLibrary::Close` | `RLibrary::Close` |
| stack high-water | 1932 | 1932 |
| imports in the log | 248 | 248 |
| frees / verdicts | 99 / 32 | 99 / 32 |
| stops at | the 99th free | the 99th free |

`User::Free` never ran, and the wall did not move. It is the last record before
the fault because it is the last thing we write, not because it is what faults.
Everything between that record and the next traced import -- the game's own
code, after `delete` returns -- is what five rounds of heap forensics were
standing in front of.

One number falls out of it. The fatal cell's header reads `0x20` here,
`align8(27 + 4)`, where build 48 read `0x28` on the same free. That was reuse,
not damage: RHeap hands over a whole recycled cell rather than split off a
remainder too small to be a cell. The build-48 entry warned the request column
could not tell those apart; the leak removes reuse and the header snaps to the
arithmetic.

### What it costs to have taken five rounds over this

Rounds 44 to 48 were each a sound measurement, and each one came back clean.
The rule they were missing is not about heaps: **when every measurement of a
suspect comes back clean, the cheapest next move is to remove the suspect, not
to measure it more precisely.** `LEAK_EVERYTHING` had been sitting in the
source, unshipped, the whole time.

## Build 50: the log going quiet is not the game dying

Round 49's `lr` record names the fatal `delete`: image offset **0xcc8c0**,
returning to 0xcc8c4. The code there:

```
000cc8b8  cmp    r7, #0
000cc8bc  movne  r0, r5
000cc8c0  blne   #0xd5a0c        <- the delete, 99th of the run
000cc8c4  mov    r7, r8
000cc8c8..e0     (a constant-multiply chain on r4)
000cc8e4  cmp    r4, #0
000cc8e8  beq    #0xcca88
000cc8ec  ldr    r0, [r6, #4]
000cc8f0  mov    r1, r7
000cc8f4  mov    r2, #1
000cc8f8  bl     #0x10a9e0
...
000cc908  mov    r0, #4
000cc90c  bl     #0xd5a08        <- User::Alloc(4)
000cc914  str    r5, [r5]
```

**Not one instruction in that stretch calls a traced import.** The allocation at
0xcc90c is wrapped, but `gate6_result` logs allocations only when they *fail*.
So the log goes quiet after the delete whether the run survives the next
hundred instructions or not, and "it dies in the free" -- and then "it stops at
the 99th free" -- were both reading the end of the recording as the end of the
run.

The emulator proves it outright. Six probes planted along that stretch, one
run:

```
marker 990 at 0x000cc8c4   r6 = 8b2710     straight after the delete
marker 991 at 0x000cc8ec   r6 = 8b2710     the first load after it
marker 992 at 0x0010a9e4   r0 = eaf88340   entered the call it makes
```

The run gets past the delete every time. What it does not get past is what
`[r6 + 4]` hands it: **0xeaf88340**, which is an ARM branch word, not an
object. 0x10a9e0 allocates 0x24 bytes and calls 0x139568 with that value, and
0x139568 dereferences it -- the emulator's fault address, `0xEAF88580`, is that
garbage plus 0x240.

Two things follow. The near one: `[r6 + 4]` is uninitialised or stale, which is
the same shape as the container at 0xcc914 that this file already describes --
a field holding what was in the memory before, read as a pointer. The far one:
**0x139568 is where EKA2L1 cannot run the original N-Gage binary either**
(KERN-EXEC 3 at 0x139588). Past this point the emulator may be measuring
itself.

So build 50 ships the probes. It is build 49 plus six markers, and it answers
the question the log cannot: how far past the delete does the *phone* get. In
the emulator they fire three times in a run and cost ten records each, so the
write budget is unchanged in kind.

The constant-multiply chains on r4 either side of the delete are obfuscation,
not arithmetic: the first multiplies by 3467093631 and the second by
4017970111, which are inverses mod 2^32. r4 comes out of the pair unchanged.

## Round 50: the fault, at last, and it is an old acquaintance

Three runs, identical. Markers 990, 991 and 992 fire; 993 does not.

```
marker 990 at 0x000cc8c4   r6 = 7d68e8   r4 = cea7a67f
marker 991 at 0x000cc8ec   r6 = 7d68e8   r4 = 1
marker 992 at 0x0010a9e4   r0 = a6dfb180
```

`r4` is `0xcea7a67f` before the second obfuscation multiply and `1` after it,
which is the pair of inverse constants doing nothing, as expected. `r6` is
stable. What is not stable, and not a pointer, is what `[r6 + 4]` hands over.

`0x10a9e0` takes it, allocates 0x24 bytes and calls `0x139568` with it:

```
00139568  push  {r4-r8, sb, sl, lr}
0013956c  sub   sp, sp, #0x1c
00139570  mov   r6, r0
00139574  ldr   r3, [pc, #0x118]
00139578  str   r3, [r6]              <- a vtable: this is a constructor
0013957c  mov   r4, #0
00139580  str   r4, [r6, #8]
00139584  str   r4, [r6, #0xc]
00139588  ldr   r2, [r1, #0x240]      <- and here it dies
```

`r1 = 0xa6dfb180`, so it reads `0xa6dfb3c0`. In the emulator `r1` is
`0xeaf88340` and the fault address is `0xEAF88580` -- the number the emulator
has been printing for weeks, which is that same instruction, arrived at the
same way.

The object at `r6`:

```
[r6+00] 913458     [r6+10] 0
[r6+04] a6dfb180   <- the only nonsense in it
[r6+08] 7d76d0     [r6+18] 0
[r6+0c] 4800000    [r6+1c] 0
```

Three plausible heap pointers, one large mapped address, four zeros, and one
field holding different garbage on the phone than in the emulator. That is what
a field nobody wrote looks like.

### What this costs the emulator as an alibi

`0x139588` is where **EKA2L1 cannot run the original N-Gage binary** -- KERN-EXEC
3, written into this file long ago and filed under "the emulator is not a
reference". It was not the emulator. Our port reaches the same instruction with
the same kind of value in the same register, on a real N95.

Three runs of three different things fail identically:

| | |
|---|---|
| the original binary, under EKA2L1 | KERN-EXEC 3 at `0x139588` |
| our port, under EKA2L1 | fault at `0xEAF88580` = `r1 + 0x240` |
| our port, on an N95 | fault at `r1 + 0x240`, `r1 = 0xa6dfb180` |

The original game runs on a real N-Gage. So something that fills `[r6 + 4]` on
that device fills it on none of these three, and that is the port's actual
problem. It has been sitting in this file, mislabelled, since before the month
of reboots.

**Next: who is `r6`, and what is supposed to write its fifth word.** That is a
static question about the image, so it costs no hardware round.

## Where `this->[4]` goes bad: one call, and no hardware round spent

Round 50 put the fault on a pointer read out of `this->[4]`. Three probes
across the function that owns it, in the emulator, narrow the damage to a
single instruction:

```
000cc864  add r0, r5, #0x28        marker: this->[4] = 8d8ee8   a heap pointer
000cc86c  bl  #0xe97cc
000cc870  bl  #0xd5fbc
000cc874  add r3, r5, #4           marker: this->[4] = 8d8ee8   still fine
000cc884  ldr r0, [r4, #4]
000cc888  mov r1, r5
000cc88c  mov r2, #1
000cc890  bl  #0xe9988             <- r3 is &this->[4], its fourth argument
000cc894  mov r4, r0               marker: this->[4] = eaf88340  garbage
```

**`0xe9988` is handed `&this->[4]` as an out-parameter, runs for three hundred
records, returns 1, and leaves nonsense in it.** The field is a good heap
pointer on either side of `0xe97cc` and `0xd5fbc` and only changes across that
one call.

Two details fall out of the same run. `0xd5fbc` returns `0xb32248` -- which is
the pointer the "fatal" delete at 0xcc8c0 frees, so that temporary is created
and destroyed inside this sequence and was never anything to do with the fault.
And `r6`'s other seven words are unremarkable on both machines: three heap
pointers, one large mapped address, four zeros. Only the fifth word is wrong.

`0xe9988` sits a hundred and ninety-six bytes after `0xe98c4`, the `TickCount`
stopwatch with `Math::Random` mixed into its result that this file records as
one of the game's protection paths. That is a neighbourhood, not evidence, and
the next step is inside `0xe9988` -- which is more emulator bisection, not a
round on the phone.

**No build shipped for this.** The emulator and the phone have now agreed for
three rounds running, the question is where inside one function a value goes
bad, and that is answerable for free.

## Instrumenting a function that will not be instrumented

The next question was where inside `0xe9988` the field goes bad, and the
obvious way to ask it -- probes along the function -- does not work.

**One probe at its third instruction takes the run from 1240 records to 553**,
and the run dies before even the marker at `0xcc864` outside it is reached. Ten
probes did the same. The site is `str r0, [sp, #0x30]`, an ordinary
instruction; `probe_plant` saves and restores CPSR around its call, so this is
not a clobbered flag. A single patched word in that function is enough to end
the run, which for a game carrying a decryptor, a `TickCount` stopwatch and
three other protection paths is a finding rather than an obstacle -- and a
standing constraint on every instrument from here.

### The watch: a station that patches nothing

So the field is watched instead of the function. The first probe latches the
object's address, and from then on **every wrapper we already own re-reads
`this->[4]` and writes one record**: `gate6_trace` on each traced import,
`gate6_result` on each allocation, `gate6_arg` on each free. No byte of the
game is touched to get one.

It does not perturb the run: 1292 records against 1240, the same
`0xEAF88580`.

What it shows:

```
1236  >> watched field   8d8ee8
1237  import 283  RLibrary::Close(void)   from 135850
1238  >> watched field   8d8ee8
1239  about to call import 198  (delete b32220, called from 0x13589c)
...
1249  >> watched field   8d8ee8      <- after an allocation returned
1250  >> watched field   eaf88340    <- before the next delete
1251  about to call import 13b  (delete c4fe50, called from 0x104828)
```

The field survives everything up to and including a successful allocation, and
is poisoned before the `delete` at image offset `0x104828`. That call sits in
one of `0xe9988`'s callees, in the game's obfuscated dispatch style -- constant
multiply chains, `b #0x1037ac`, and operand words inlined in the instruction
stream.

### Two negatives worth keeping

**The poisoned value is not a transform of the good one.** Solving
`0x8d8ee8 * C == 0xeaf88340 (mod 2^32)` gives `C = 0x17052F88`, nothing
resembling the obfuscation constants. The two machines do not even agree on
the shape: the value's 2-adic valuation is 6 in the emulator and 7 on the
phone, so it is not one function of one differently-based pointer.

**It is not `Math::Random` reaching the field directly, either.** The value is
deterministic per machine -- the same `0xEAF88580` from the emulator every run,
the same `0xa6dfb180` on three phone runs.

The phone's *pre*-call value is still unknown: round 50's probe set did not
include `0xcc864`. The current build does, so the next hardware round yields it
without being spent on it.

## The store that poisons the field, and what it is computing

Four emulator runs, E17 to E20, from "somewhere inside `0xe9988`" to one
instruction.

**E17 -- it is a deliberate store, not an overrun.** The watch was widened from
one word to four. Across the flip, word 1 changes and words 0, 2 and 3 do not:

```
before: 83dc3e00  8d8ee8    8b29d8  4900000
after : 83dc3e00  eaf88340  8b29d8  4900000
```

A copy that ran long, or an overrun from the cell in front, takes neighbours
with it. This takes exactly one word, which also clears our own shim of having
scribbled on it.

**E18 -- the other function in the window does tolerate a probe.** `0xe9988`
dies on a single patched word; `0x103774`, which is running when the field goes
bad, does not. That was the fork in the road, and it went the good way.

**E19 -- and it is not either of that function's two array stores.** The only
non-frame stores in it are the same instruction twice,
`str sl, [r5, ip, lsl #2]`. Both were probed; all fifty firings fill two
five-element arrays at `0x40f640` and `0x8d9144`, and not one of them addresses
`this+4`. The flip is bracketed between them, and the stretch between is
dispatcher jumps -- one call out, no stores.

**E20 -- so instrument the obfuscation's own choke point.** The function
dispatches through a jump table at `0x1037ac`
(`cmp r3, #0x16; ldrls pc, [pc, r3, lsl #2]`), reached by `b` from everywhere,
with the block's key loaded inline into `r0` just before the jump. A probe
there is a station at every block boundary without knowing the path -- and made
*quiet*, reporting only when the watched word has changed, it is silent until
the one boundary that matters. It fired **once**, with `r0 = 0xe649867c`.

That key appears twice in the image. One of the two blocks ending in it is
this:

```
00108294  mul  sb, sl, sb
00108298  mov  sl, r1
0010829c  mla  sl, sb, sl, r8        sl = sb*r1 + r8
001082a0  mla  r3, sl, r4, sl        r3 = sl*(r4 + 1)
001082a4  add  r3, r3, r3, lsl #2
001082a8  add  r3, sl, r3, lsl #10
001082ac  add  r3, sl, r3, lsl #3
001082b0  rsb  r3, sl, r3, lsl #4
001082b4  add  r3, r3, r3, lsl #3
001082b8  rsb  sl, sl, r3, lsl #7
001082bc  ldr  ip, [sp, #0xb8]       <- the out-pointer, off the frame
001082c0  str  sl, [ip]              <- the store
```

`ldr ip, [sp, #0xb8]; str sl, [ip]` is a store **through an out-parameter held
in a stack slot** -- which is what `&this->[4]` became after `0xe9988` passed it
down. One word, matching E17 exactly.

### What the value is, and why that matters

The arithmetic is not the obfuscation. Obfuscation in this image is
multiply-by-constant, built from shift-adds with an inverse chain later;
`mul sb, sl, sb` and `mla sl, sb, sl, r8` multiply two **runtime** values.
`mla r3, sl, r4, sl` is `sl * (r4 + 1)`, and the shift-add tail scales it
again.

That is the shape of `base + index * size`, not of a hash. So `this->[4]` is
meant to receive a **computed pointer into an array**, and it is coming out
wild because one of `sb`, `r1`, `r8` or `r4` is wrong. The field is not being
deliberately poisoned by anti-tamper; it is a pointer calculation with a bad
input.

Which also finally explains the shape of the value: deterministic per machine,
different between machines, never appearing anywhere earlier in the log, and
not a constant multiple of anything -- exactly what `a*b + c` over
machine-specific addresses produces.

### The caveat on E20, stated plainly

**That run perturbed.** The emulator's fault moved from `0xEAF88580` to
`0x1C976000`, and the field's garbage changed with it -- consistently, since
`0x1c975dc0 + 0x240` is the new fault, so the mechanism held. But a probe that
moves the outcome is a probe whose evidence has to be confirmed, and rule 5
says a measurement that agrees with the theory gets checked as hard as one that
does not. **The next run re-confirms `0x1082c0` without the dispatcher probe**,
by planting a single quiet probe there instead.

## The value, computed: every step now checked against a register

E20's site needed confirming, and E21 to E23 did that and then followed the
arithmetic up.

**E21 -- the store confirms itself.** A single probe on `0x1082c0`, filtered to
fire only when the register it reports *is* the watched field's address, with
no probe on the dispatcher. It fired once:

```
marker 992 at 0x001082c0   ip = 8b2764   sl = 1c975dc0
```

`this` is `0x8b2760` that run, so `ip` is `this+4` exactly, and the fault
address is `0x1c975dc0 + 0x240`. That is the instruction naming its own target,
which is a stronger statement than "the word changed near here".

**E22 -- `array[5]` is a decoy.** The block runs exactly once in the whole run,
so there was nothing to filter after all. At `0x108298`, `r1` holds `array[5]`
= `0x782efefd` and `sb` is **zero**, so `mla sl, sb, sl, r8` reduces to
`sl = r8`. The load two instructions earlier -- `mov r7, #5;
ldr r1, [r1, r7, lsl #2]` -- is multiplied away. Obfuscation, not content.

**E23 -- and the rest is arithmetic that checks out.** `r8 = 0x42d08240`, which
is exactly the candidate the tail inversion predicted for `r4 = 0`, and

```
755139455 * 0x42d08240 = 0x1c975dc0     (observed)
```

So the chain of custody is complete and every step is confirmed against an
observed register:

```
r6 = fb0896a0 , sb -> r8 = 42d08240 -> * 755139455 -> 1c975dc0
   -> str sl, [ip]  at 0x1082c0,  ip = this+4
   -> ldr r0, [r6, #4]  at 0xcc8ec
   -> ldr r2, [r1, #0x240]  at 0x139588  -> fault at 1c976000
```

**`r8` arrives already wrong.** De-obfuscating `r6` and `r8` with this block's
own constant (`483517375`, the identity chain with `r4 = 0`) gives `0x3AAC9160`
and `0x346B0DC0` -- not pointers either, so they came through a different
chain, or with a different `r4`. Where `r6` and the first `sb` come from is the
next question, and it is one level further up the same function.

### A caveat that has to be repeated

The fault has read `0x1C976000` since E20 rather than `0xEAF88580`. That is not
the dispatcher probe -- E21 removed it and the value stayed. It is the
`lastWatch` field added to `Context`, which moved every field after it and with
them the heap layout. **The mechanism is unchanged** (the fault is still the
stored value plus `0x240`, and the store is still `0x1082c0`), and the value
being layout-dependent is itself consistent with a pointer computed from
addresses. But the number is not comparable with anything before E20, and
nothing should be inferred from the change in it.

## The pre-call value was the answer all along

**E24 asked the one question that mattered.** `0x139588` dies on
`ldr r2, [r1, #0x240]`. So: read `[field + 0x240]` at every station, guarded so
the poisoned value is never dereferenced. Before the store, at all 83 stations:

```
>> watched field          8d8f38
>> would read at +0x240   8d9c60
```

A good heap pointer, stable throughout. **The field already held a valid
object, with exactly the word `0x139588` wants, and `0xe9988` overwrote it.**

**E25 acted on that.** The block containing the store runs exactly once in a
run (E22), so one word of the game's code -- `str sl, [ip]` at `0x1082c0` --
was replaced with `mov r0, r0`. The field keeps what it had.

```
                 imports   ends
before            248      fault at the stored value + 0x240
store NOPped      292      no access violation at all
```

The run goes on past everything it has ever reached: `RLibrary::Load` at
`0x13964c` and again at `0x13f588` -- sites never seen before -- `Lookup`,
`HBufC16::New`, a second pass of the `0xe98c4` stopwatch, more deletes. Then
`User::Leave` from `0x2b20`, and the app exits. The `G6MEM` panic after it
carries `1616972`, which is the byte size of `6rbc.app`: that is our *loader*
failing to allocate the image on a relaunch, not the game.

So the failure mode has changed completely, from a wild pointer dereference to
**the game's own error path**, taken in an orderly way. That is the first
movement at this wall since build 44.

### What this is and is not

It **is** a workaround. It does not explain why the game computes a wild
pointer at `0x1082c0`, only that the value it clobbers was the correct one. The
honest reading is that `0xe9988` is meant to *find* something and hand it back,
finds nothing, and writes a computed miss where the caller expected the thing
it already had.

It **is** narrow: one word, in a block measured to execute once. It is not a
blanket patch of the game's code, and the region tolerates it where `0xe9988`
tolerates nothing.

And it **is** the first candidate fix this project has had rather than another
measurement. The next question is what `User::Leave` is complaining about --
but that is a question at a place the port has never stood before.

## Every library the game loads, and the one that does not

E26 wrapped `RLibrary::Load` on both sides: `arg_thunk` widened to keep `r1`
(the file name, the one register it used to throw away) and a `result_thunk`
for the return code.

| called from | name | result |
|---|---|---|
| `d5058`, `d589c` x3, `d516c`, `d54b8`, `13f588` x5, `10b08c` x2 | `euser.dll` | `KErrNone` |
| `13964c` x5 | `efsrv.dll` | `KErrNone` |
| `e815c` | `c:\system\cwdynlog.dll` | **`KErrNotFound`** |

So the game resolves euser and efsrv **by name at runtime** rather than through
its import table -- which is worth knowing on its own, because those lookups go
to the *phone's* ordinals, not to ours, and this file's standing worry about
ordinals across feature packs applies to every one of them.

The two Loads it reaches only after the wall came down, `0x13964c` and
`0x13f588`, both succeed. The only failure in the run is `cwdynlog.dll`, which
this file already records as a protection path and which is *supposed* to be
missing. **So a `Lookup` on an unopened handle is not where the KERN-EXEC 0
comes from**, and that theory is closed before it cost a round.

Widening `arg_thunk` moved every literal offset in it by four bytes. That exact
arithmetic went wrong once before -- the store landed in the log buffer instead
of the field, and cost a round -- so each offset is now spelled out beside the
instruction that uses it, and `ARG_WORDS` went from 16 to 20.

The emulator run now ends with **no fault at all**.

## The app is not run once. It is run over and over.

The phone raises two panics per run. Chasing that turned up something larger.

`file_replace` truncates, so every launch of the app was erasing the previous
launch's log and writing its own from position 0. Nothing recorded which launch
a log belonged to. So build 52 stamps it: the box carries a **launch counter**,
read out of the previous box before `file_replace` destroys it, and the log
goes to `C:\g6box<N>.log` -- one digit, patched at startup, so no launch can
overwrite another.

One 45-second emulator session:

```
g6box1.log .. g6box9.log     16776 bytes each, byte-identical
box: LAUNCH 12
     179 traced events, last import User::Leave
     flags: THE FRAME LOOP RAN, User::Leave, User::Exit
```

**Twelve launches.** Each one identical, each reaching 179 traced events --
further than any number in this file -- leaving cleanly and exiting, and being
started again. (The digit saturates at 9, so launches 10 and up share
`g6box9.log`; the counter in the box is the true one.)

So every log this project has ever read was whichever launch happened to run
last, and every count in this file was measured on an unknown member of a
series. That did not invalidate the findings -- the launches are identical --
but it was not known to be true, and it is exactly the kind of thing that has
cost this project rounds before.

Reading the previous box was done here once and removed, because it closed an
`RFile` with `RHandleBase::Close` and took the file server session down with
it -- the bug behind a month of reboots. This one closes with `file_close`,
efsrv 300, and touches the handle no other way.

### What it changes about the phone

The phone's "one KERN-EXEC 0 and one KERN-EXEC 3 per run" is now two
observations about a *series* of launches, not two failures in one. The first
launch's own record has never been seen. Build 52 is the first build that can
show it.
