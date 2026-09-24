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











## Open

- 30 imports still unanswered, mostly the deliberately stubbed N-Gage
  libraries and a CServer/CSession implementation the game carries.
- `OfferKeyEventL` (old control slot 1, 9.x slot 3) is unbridged, so there is
  no input.
- The framework base-class constructors are still `LOCAL_NOOP` approximations.
- `mke32.py` has no `.bss` support, which is why the context is reached
  through a pointer baked into each thunk.
