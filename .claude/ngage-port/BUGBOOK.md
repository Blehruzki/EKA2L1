# The bug book

Every bug that cost rounds on this port, with its symptom, its cause, the fix,
and how it was found -- so that the next port starts from here rather than
from round one. Read this **before** proposing a test for a symptom: most of
what a new title will do on the phone, one of these two titles already did.

The entries are grouped by where the bug lived. Each names the hardware round
(`R`) or emulator run (`E`) in `ROUNDS.md` that settled it, and the symbol in
`toolchain/port/gate6.cpp` that carries the fix. `SYMBIAN.md` has the platform
facts the fixes rest on; `CLAUDE.md` has the lessons about method.

The three questions to ask of any new symptom, in order:

1. **Is it ours?** The instrument, a diagnostic left switched on, a guess from
   an earlier round still in the build. Half the rounds of this project were
   spent on bugs the port itself had introduced (sections 1 and 7).
2. **Does the bench show it?** EKA2L1 is forgiving where a device is not
   (section 10 lists the known gaps). A bug the bench cannot show is diagnosed
   from the phone's log, which means the instrument has to be ready before
   the round, not after.
3. **What does the platform's own source say?** The S60 sources under
   `/home/user/symbiansource` answered the two hardest bugs here (sections 8
   and 9) in one round each, after many rounds of guessing.

---

## 1. Bugs the port did to itself

**The reboots were our own handle.** `R1-36`. Symptom: the phone rebooted
after a fixed number of log writes; thirty rounds of theories about write
ceilings and rates. Cause: two `RFile`s were closed with `RHandleBase::Close`,
which closes the *file server session* the whole process shares. Fix:
`RFile::Close`. Lesson: when every theory fits, the instrument is the bug.

**"64 records" was the log's own blocks.** `R38-44`. Six rounds judged
regressions on a record count that was eight of the port's own log blocks.
Fix: `readlog.py`, which decodes the log; never compare raw sizes.

**A diagnostic shipped for thirty builds.** `R85`. `LEAK_EVERYTHING`, a
build-49 probe that answered `User::Free` and both `operator delete`s with a
no-op, stayed on; the phone ran out of memory after several races and died
in zlib's own allocation (`User::Leave(-4)`). Fix: deallocators restored,
with a quarantine for cells under 4 KB. Then `R92`: the quarantine
(`GAME_FREE_BACK_FROM`) and `GAME_LEAK_ALL` were *also* left on and killed
the second title after 34,000 frames. Lesson: every knob has a shipped value
of 0 and `checkrec.py`/`rules.py` list them before a build goes out.

**The flush did not cover the records that mattered.** `R97-99`. The
"endgame" flush fired only on traced imports, so three rounds concluded
"it dies inside the cancel" from a log that simply had not been flushed.
Fix: `log_event` flushes while `timerOff` counts down. Lesson: test the test
-- make the instrument show a known event before trusting its silence.

**A wrong box word read as a finding.** `R100-103`. `BOX_EXC` shared a slot
with the MDA `NewL` address, so "exception handler NOT installed" was an
audio pointer. Fix: `NOTE_EXC_INSTALL`; `readbox.py` names the collision.

**The bench ran the previous binary.** `E341`, `E345`. A build error, or an
edit script that refused a stale anchor and wrote nothing, left the old
`.exe` in place and the run measured nothing. Tell: an identical record or
frame count. Fix: the `BUILD` record (`NOTE_BUILD`, from `GAME_BUILD`) at the
head of every log, and the code size printed by the build as a checksum.

**Bench knobs are not features.** `FORCE_CANCEL_AT`, `FORCE_DEACTIVATE_AT`,
`FORCE_BACKGROUND_AT`, `YIELD_TO_IDLE`, `RELEASE_THE_SCREEN` (see 9): every
one is 0 in a shipped build, and a regression run on the exact shipping
binary precedes every release (`E348`, `E353`, `E354`).

### 1.x The gc thunk indexed the object, not its vtable (E370)

**Symptom.** Ashen's first call through the window gc stand-in jumped to 0:
`KERN-EXEC 3` at pc 0, r0 = r12 = the real gc.

**Cause.** `gc_thunk` was `ldr r0,[cell]; ldr ip,[cell]; ldr pc,[ip,#slot*4]`
-- the real object in both registers, the slot read off the object. It had
never executed: the direct-screen stand-in's one exercised entry, BitBlt, is
the port's own blit, so a hundred Asphalt rounds never ran a thunk.

**Fix.** `ldr ip,[r0]` for the vptr. Both Asphalts regressed clean (E375,
E377).

### 1.y A hex build number read as decimal (round 109)

**Symptom.** A C5-00 report was written up as "build 019" and the user was
on 025. readlog printed every value in hex and `BUILD 19` is 0x19.

**Fix.** readlog prints the build as `25 (0x19)`. The lesson is the one the
instrument chapter already carries: an instrument's output format is part of
its contract, and a number without its base is a guess.

### 1.z A C: install read nothing on Asphalt Urban GT (round 110)

**Symptom.** A C5-00 with no memory card, the game installed to phone
memory: the game's two pack opens return -18 `KErrNotReady`, then it
faults on a null object. Every N95 test of a C: install had worked.

**Cause.** The wrapper that rewrites the game's `E:` paths to the install
drive was installed behind a guard wanting `RFile::Open`, `Create` and
`Replace` all imported. This title imports no `Replace`, so the wrapper never
went in; its opens went to E:, which on every test phone also held the
hand-copied card dump. The guard was itself the fix for an out-of-bounds
thunk write found by a static sweep -- a fix that quietly switched a feature
off for the next title.

**Fix.** Each wrapper on its own guard (build 027). Reproduced on the bench
by hiding E: and laying the SIS's files on C: (E378, E379). The chosen-drive
note (754) and the C: retry (755) from build 026 are what made it readable.

### 1.w A stand-in for one title reached the others through the shared view (E390)

**Symptom.** Asphalt Urban GT dead at 338 records, `KERN-EXEC 3` in ws32
with a null `this`, the day after Ashen's fonts went in.

**Cause.** The screen-device stand-in Ashen needs for its font requests was
written into the environment view's old `iScreen` for every title, and the
Asphalts hand that word on to direct screen access, which reads a real
`CWsScreenDevice` off it. Nothing on Ashen's path showed it.

**Fix.** A title knob (`GAME_SCREEN_FONTS`), off for the Asphalts. The
regression run is what caught it, two minutes after the change: the shared
file ships to every title, so every title runs before anything ships.

### 1.v The N-Gage card's `Game.Id` (rounds 111, 112)

**Symptom.** Ashen build 002 and 003 on the N95: a data abort reading 0x54
inside the forwarded foreground event, before the first frame. The log shows
`PackFile.Dat` opened (0), then `RFsBase::Close` and `UnTrap` at once; the
bench goes on to size and read the pack.

**Cause.** Not the pack. Right after opening it, the engine's init
(image 0x720f8) reads `E:\Game.Id` -- `RFs::ReadFileSection(name, 0, des,
0x64)` at 0x72214 -- and 0x72218 abandons the whole init when that fails,
recording nothing. The resume routine later dereferences the engine object the
init never made. `Game.Id` is the six-byte file ("N-Gage") at the root of an
N-Gage card; the path is the literal "E:\" plus the name, whatever drive the
game was installed to, and a SIS installs nothing at a drive's root. The
bench's E: is the card dump and has the file, so 26 Ashen runs never saw it.
`ReadFileSection` is not a milestone, so the read was invisible in the log;
the disassembly between the open and the close found it, and E394 (the file
hidden) reproduced the N95's abort, same address.

**Fix.** `ReadFileSection` hooked (`GAME_ANSWER_GAME_ID`, build 004): the
real read first, and a not-found on a name ending `\Game.Id` answered with
the six bytes, logged as note 761. E396 (hidden) passes, record for record
against E393. A hook of its own on the way: the handler has six arguments
and `arg5_thunk` pushes the saved lr between the context and the game's fifth
argument, so the first build handed a return address to efsrv as the length
(E395, `FSCLIENT 27` EBadLength). `arg6_thunk` lifts the fifth argument above
the context.

### 1.u Quitting the game was a crash (round 113)

**Symptom.** Ashen on the N95: `G6FLT 22500`, "Application closed", when
the player confirms Quit. The log ends with a leave of -1003 inside the
game's tick, re-thrown by the game at 0xb7138, and the terminate.

**Cause.** The game quits through `CEikAppUi::Exit()`, the last thing its
tick does once the quit flag is set. On 9.x `Exit()` is a leave -- bafl's
`KLeaveWithoutAlert`, -1003 -- for the active scheduler's catch to turn
into a quiet end. A C++ throw cannot unwind GCC98r2 frames, so the game's
own trap takes it, the game re-throws, and with no trap above it the
thread terminates. Found by replaying the N95's key sequence on the bench
(E398) and dumping the leave hook's stack raw (E400): euser, bafl,
eikcore, then the return from `Exit()` at 0xb66d0.

**Fix.** `Exit()` hooked (build 005): the record flushed, the thread
exited with reason 0. The main thread's exit ends the process. The
Asphalts never import `Exit()`; they leave through `User::Exit` themselves.

### 1.t Doubles the other way round (round 113)

**Symptom.** Ashen's music is "bass pulses" on the N95. On the bench, with
the wav-capture driver (E403): a 16 kHz stream whose samples sit at -28,
-27... forty at a time, amplitude under 40, 75% of the energy below 100 Hz.

**Cause.** GCC98r2 keeps a double's high word first (the FPA order) and
EABI keeps it last. The game's pitch table is built in doubles -- its
constants 2, 1536, 8363, 0.5, 1048576 and 1 read as those numbers only
high word first -- through `__divdf3`, `__muldf3`, `__adddf3`,
`__floatsidf`, `Math::Pow` and `Math::Int`, which the shim forwarded to
dfpaeabi and 9.x euser as they were. Every value crossed with its words
swapped: denormals in, the table garbage, each voice stepping forty
times too slowly through its sample. Single floats are one word and never
suffered. The Asphalts import `__adddf3`, `__muldf3`, `__floatsidf`,
`__extendsfdf2` and `__truncdfsf2` and have shipped as they are; whatever
they compute with them is wrong in the same way and has not shown.

**Fix.** `GAME_FPA_DOUBLES` (build 006): a thunk on each register helper
swaps r0:r1 and r2:r3 in and r0:r1 out (`dswap_thunk`), and a handler on
each `Math::` function copies its operands swapped and swaps the result
back. 6 helpers and 3 functions hooked in Ashen; music on the bench (E408).
Off for the Asphalts until a round of their own.

### 1.s The picture modes, for a title that draws through the window gc (round 114)

**Symptom.** Holding C does nothing in Ashen; it cycles the picture mode
in the Asphalts.

**Cause.** Two paths. The Asphalts render into the port's own buffer and
the port fits that to the panel; the hold's clock ticks in the port's
frame loop. Ashen blits its 176x208 frame through the environment's
`CWindowGc` into a wrapper control sized 176x208, so neither the fit nor
the clock ever saw it.

**Fix.** `GAME_SCREEN_MODES` (build 007): the wrapper takes the whole
screen when the game asks for its 176x208, the game's `Rect()` still
answers 176x208 (its frame bitmaps are sized from it: E372), the blit
lands at the fitted rectangle through `DrawBitmap(TRect, bitmap)`, and a
100 ms CPeriodic ticks the hold. No repaint of the port's own: drawn from
the timer it either stopped the game's loop (E414, E415) or killed the
emulator's window server (E420), and a hook on Deactivate never fires
because Ashen activates its gc once and keeps it (E419). The new fit shows
at the game's next blit, within half a second on a page with a blinking
cursor. Along the way: old window-gc slots 52 and 53 are Activate and
Deactivate and map to 68 and 69, not 67 and 68 -- a Deactivate forwarded
to Activate with a stray window is a null window in the emulator's
server (E417, E418 under gdb). Count a slot map by code, not by eye.

### 1.r A stray Cancel that was the game's frame timer (round 115)

**Symptom.** Ashen on the N95 survives being put in the background and dies
on return: `E32USER-CBase 42`, EReqAlreadyActive.

**Cause.** The port's hook on `CActive::Cancel` forwards a Cancel only for
its own wrapped timer and its DSA shadow; anything else was treated as a
stray branch into the stub and dropped, which was right for the Asphalts
(their four call sites are all on those two). Ashen makes its frame timer
with `CPeriodic::NewL` -- a real 9.x object -- and cancels it on losing the
foreground. Dropped, the timer ran on under the background (405 ticks),
and the gain's `Start` on a still-active object is panic 42.

**Fix.** `GAME_CANCEL_ROM_OBJECTS` (build 008): a stray Cancel on an object
whose vptr is in the ROM (a 9.x object) goes to the real Cancel; one on an
old-layout object, vptr in the image, is still dropped. The bench cannot
background an app by sending its window back (E424, E425: no focus event
from the emulator's window server), so the bench knob calls the port's
foreground forwarder with 0 and then 1, which is the path the phone takes.
E426 reproduced the panic, E427 cleared it.
### 1.q The menu was a row of dots: two faults, one symptom (round 116)

**Symptom.** Ashen on the N95 (builds 004 to 008) and on the bench: every
string the game draws in the system font -- the main menu, the prompts, the
version line -- is a row of one-pixel dots. The bitmap text (the page
titles, the softkeys) is fine.

**Cause 1: VA_LIST.** All of that text is built by the game's own variadic
wrappers (0xb3b78, 0xb3c88, 0xb3d90 and three more), which collect their
arguments with VA_START and call `TDes16::FormatList(fmt, VA_LIST)`. Under
GCC98r2, VA_LIST is `TInt8* [1]` (e32def.h), and an array argument is
passed as its address: r2 is the address of a stack word holding the va
pointer. The 9.x euser is an EABI build whose VA_LIST is the compiler's
`va_list`, one word passed by value: r2 *is* the va pointer. So 9.x took
the game's array as the argument area and its one element, a stack
address, as the `%s` string -- and every item came out as the bytes of
that address and whatever lay above it, two to eight glyphless characters
(E435: each item's text is exactly the wrapper's pushed r2 and r3). Fix:
the FormatList hook passes the array's element (`GAME_VA_LIST`). `Format`
itself needs nothing; both ABIs leave the trailing arguments in r2, r3 and
on the stack.

**Cause 2: the font slot.** The screen stand-in answered the game's font
request through the primary vtable's slot 26, counted from GDI.H as
`GetNearestFontToDesignHeightInPixels`. The count left out the EABI rule
that an override of a non-primary base's virtual gets a slot of its own in
the primary table: CWsScreenDevice's eight MGraphicsDeviceMap overrides
follow CBitmapDevice's, and 26 is `GetNearestFontToDesignHeightInTwips`.
Twelve twips is the smallest font there is -- bold honoured, height not,
the same CFbsFont for 12, 13, 17 and 19 (E437). The pixel getter is slot
18. Fix: slot 18, and ReleaseFont through the MGraphicsDeviceMap table at
+4, slot 9, with that subobject as `this`; the font's own HeightInPixels
is logged after the call, which is what settles a slot (E438: 12, 12, 12,
17, 19).

**What was not the fault.** Two readings were tried first and both were
wrong, and the record keeps them: a `{TText16*, TInt}` string object passed
by reference to `%s` (E431, E432: a hook answered such pairs -- it was
cause 1 seen from the other end, and it is gone), and the raw 9.x CFbsFont
handed to the game's old CFont slots (E433, E434: a stand-in remapped
them -- but the game's own code shows old slot 6 taking a descriptor, so
the old table is the 9.x Do* order shifted by two and the raw font's vptr
already serves it; the stand-in is gone). Both would have been caught a
round earlier by logging what the game got back -- the formatted length,
the font's height -- instead of what it was given.
### 1.p S60 3.0 keeps the control's window one word elsewhere (rounds 118-120)

**Symptom.** Asphalt 2 and Asphalt Urban GT on a Nokia N91 (S60 3.0) show
a white window and close, every launch, on both titles. The logs are
identical in shape to the N95's until the first draw, then Urban GT takes
a data abort at address 0 inside ws32 (pc just below `StartL`, a null in
r1) and Asphalt 2 ends through the exception path.

**Cause.** The port builds the game's old-layout CCoeControl by hand and
fills its `iWin` (old 0x20) from the 9.x wrapper control's word at 0x28,
an offset measured on 3.1 and 3.2 ROMs. On 3.0 that word is zero. The game
handed the zero to `CDirectScreenAccess::NewL` as its window; NewL stores
what it is given, and `StartL`'s first use of the window is the fault. The
dump before StartL (770) showed it directly: the bench's object has the
window reference at word 10 and the screen device at 11, the N91's has 0,
0 and the device at 12 -- so 3.0's CDirectScreenAccess also carries one
more word before the window than 3.2's, which would have put the port's
measured gc, device and region offsets (0x1c, 0x20, 0x24) one word off as
well. The CCoeEnv layout, the other suspect, matched the bench word for
word (769).

**Fix (builds 030 and 196).** A window is an object whose first word is
the session's buffer (`MWsClientClass::iBuffer`, the base of every window
handle, set from the session when the window is constructed), and the
session's buffer sits in the real environment at 9.x 0x30. So the port
validates instead of trusting: the measured word at 0x28 if it passes that
test (3.1 and 3.2: unchanged behaviour), else the control's one word that
passes it (3.0), else `DrawableWindow()`'s answer if it passes (Ashen
imports that one), else the measured word as it was. The DSA offsets are
found at run time: the window the port handed NewL is looked for in the
real object before the first StartL, and the gc, device and region are the
three words before it (W32STD.H's order).

**The first cut of the fix was wrong, and the bench caught it (E448,
E449).** It preferred the game's own import of `CCoeControl::Window()`,
and on the RM-409 that import answered 0x140 for a window: old cone
ordinal 231 does not land on `Window()` in a 9.x ROM, exactly the
`epoc9.def` warning in the root CLAUDE.md, and nothing had noticed because
the port's own stub answers the game's calls to it. Both Asphalts died at
0x140 on the bench inside a minute. Hence the rule the final cut follows:
no candidate is taken unvalidated, whatever its source.

**What the three rounds cost, and why.** Round 118 could only say where
(the first draw); round 119's witnesses said which call (StartL, never
returning) but not what it was handed; round 120's dumps said that. Each
round asked for one more thing the previous log could not contain. A
private offset measured on two ROMs is a measurement of those two ROMs;
the fix asks the ROM at hand instead.
### 1.o A base-class veneer run with the wrong object (round 123)

**Symptom.** Ashen build 010 on an N91 (S60 3.0): a white window, then the
application grid, on every launch, after round 120's window fix had put
the Asphalts right on the same phone.

**Cause.** The port's wrapper control forwards the framework's Draw (9.x
slot 41) and FocusChanged (26) to the game's old control's slots 24 and
19. Ashen overrides neither: those slots hold the compiler's base-class
veneers -- `ldr ip, [pc, #4]; ldr ip, [ip]; bx ip` through the game's
import table -- straight back to cone's `CCoeControl::Draw` and
`FocusChanged`. So cone's base Draw ran with the game's old-layout object
as `this`. On 3.1 and 3.2 it read that object's words at the 9.2 offsets
and happened to find something it could live with; on 3.0 it reads iWin
one word earlier, found zero, and faulted at address 8. The trigger is
the `KAknFullOrPartialForegroundGained` event 3.0 sends at launch, which
provokes a framework redraw 3.1 does not; the same forwarding ran once per
launch on 3.1 and 3.2 too, harmless only by the layout it landed on.

**Fix (build 011).** The wrapper keeps its own base Draw and FocusChanged
before hooking them, recognises a veneer in an old slot by its three
instructions, and calls the base function on the wrapper instead. A real
override still goes to the game's object, so the Asphalts, which override
both slots, are untouched (E461 to E464).

**The rule it adds.** A slot the game does not override must never be
forwarded: a veneer is cone's own code expecting cone's own object. The
same applies to any old-vtable forwarding the port does, and the record of
which slots are veneers (772) is written at construction so the next
title's layer can be read off the log before anything is forwarded.

## 2. Loading the N-Gage image

**The entry point is an offset, entered in ARM mode with `lr = 0`.**
`README.md`, gate 1. Returning from it is not an exit; `User::Exit` is.

**Ordinals are not stable across Symbian versions.** `README.md`, gate 2.
Of 1,680 euser ordinals present in both the 7.0 and 9.x def files, 8 agree.
The old side is named from the 7.0s def files (EKA2L1's `epoc6.def` is a
95.6% lead, not proof); the new side is confirmed at the call site. A wrong
ordinal is a wrong *function*, which on a phone is a reboot, not an error.
`shimtable.py`, `gen_shim.py`.

**The game's copy protection patches itself.** `E145-151`, `R51`. A state
machine in the image writes an address into its own literal pool and later
stores through it; the store at `0x1082c0` was the wall every run hit.
Fix: one image patch (NOP the store), applied by the loader after reading
the image. Lesson: a death at the same instruction on every path is the
code, not the data, and the disassembly is the document.

**Symbian installs an E32 image nowhere but `\sys\bin`.** `R80-83`. The
standalone installer failed on the game's `.app`: the check is on the
*content* (the E32 header), not the name. Fix: the packager scrambles the
header bytes (`6rbc.bin`), the loader XORs them back after reading its copy,
and the game's own second open gets the scrambled bytes and never notices.
`build_release.py`.

**The loader's own handle on the image.** `R58-59`. The game's open of its
own image failed (`KErrInUse`) because the loader still held it. Fix: close
after reading. Also: `gate6.cpp` has no writable data section of its own
(`R52`), so every variable lives in the context block or on the stack.

**The generated shim is per game.** `E324`. `gate_imports.h` and the shim
are produced from one image; a hook index from another game is not an
error, it is a wrong function. The loader refuses an image whose UID3 is not
the one the tables came from (`G6HDR 7`).

## 3. ABI and object layout (7.0s game, 9.x framework)

**Base-class constructors must not be forwarded.** `README.md`, step 2.
The game allocates its application object at the 7.0s size and calls the
base constructor; the 9.x constructor writes 9.x field offsets into it.
Fix: `gen_shim.py` intercepts the constructors and destructors of every
class the game derives from, and the port builds a *wrapper* object of the
9.x layout beside the game's old-layout object, forwarding only the slots
the game overrides. `instrument()`, `OBJ_*`.

**Take the vtable from an object, not from an ordinal.** `README.md`,
"Take the vtable from the object". Exported vtable symbols drift between
feature packs; a vtable copied from a live 9.x object is the one that ROM
uses. Slot counts were measured by walking each table in the ROM
(`CONTROL_SLOTS 44`, etc.): one short is fatal (`SetExtent` calls slot 27).

**`CActive` grew by a word on EKA2.** `gate6.cpp`, `dsa_refresh`. The game
reads `CDirectScreenAccess`'s fields inline (`Gc()`, `ScreenDevice()`,
`DrawingRegion()`, `iActive`) at 7.0s offsets. Fix: a shadow object in the
old layout, refreshed from the real one every frame, with `StartL` and
`Cancel` turned back to the real object. The same trick (an old-layout
shadow or wrapper fed from the new object) is the general answer whenever
the game reaches *into* a framework object rather than calling it.

**Methods on `CCoeEnv` need the real environment in `r0`.** `E331`. The
game calls cone methods on the environment pointer it holds, which is the
port's view of it. Fix: `gen_shim` names every import that is a method on
`CCoeEnv` and wraps each with a five-instruction thunk that swaps in the
real `CCoeEnv` when `r0` is ours. `NOTE_COEENV_SWAP`.

**An object built from a base constructor and a copied vtable has only the
bases' mixins.** `R104`, `E346-348`. The wrapper app UI was `CAknAppUiBase`'s
constructor plus `CAknAppUi`'s primary vtable; `CAknAppUi`'s own constructor
is not exported, so the vtable pointers of the two mixins it adds
(`MEikStatusPaneObserver`, `MCoeViewDeactivationObserver`) stayed zero.
The first framework call through one -- cone's `DoDeactivation` on losing
the foreground -- read a vtable at 0: `KERN-EXEC 3`, fault address 0.
Fix: `mixins_install` decodes the offset from avkon's exported non-virtual
thunk (`subs r0, #k`) and installs no-op vtables at `k` and `k-4`. Lesson:
every class level's constructor writes its own secondary vtable pointers;
a copied primary vtable replaces one of them.

**The 9.x `CEikDialog` constructor overruns a 7.0s-sized cell.** `E277`.
Fix: `GAME_ALLOC_PAD` pads every allocation. This replaced the quarantine
of section 1, which had been masking it.

**The C++ runtime's per-thread state.** `README.md`, "The C++ runtime's
per-thread state". `CTrapCleanup` and the active scheduler are per thread;
a worker the game creates has neither until the port makes them
(`WORKER PROBE` in the box shows whether a worker got its allocator).

## 4. Threads

**A thread's stack is 64 KB at most on EKA2.** `R60-61`. The game asked for
more and `RThread::Create` failed. Fix: clamp. (EKA2L1 accepted any size
until it was taught the ceiling.)

**Which thread am I on.** `R68-69`. `on_main_thread` measured the distance
between stacks, which works on the bench (stacks megabytes apart) and fails
on a device (kilobytes apart). Fix: ask the kernel for the thread id.
Symptom was a worker's first euser call dying, and the box's worker probe
"answering by not running".

**A duplicate thread name.** `R90`. The second title creates two threads
with one name; `RThread::Create` returns `KErrAlreadyExists` on a device
(the bench did not mind). Fix: a name of the port's own per `Create`.

**A handle the game had already closed.** `R91`, `E327`. The game passed
the second `RThread::Create` the handle of the first, which it had closed;
EKA2 kills the process (`KERN-EXEC 0`) where EKA2L1 returns `KErrBadHandle`.
Fix: keep the port's own handle. `EKA2L1_STRICTHANDLE=2` makes the bench
behave like the device for this.

**The worker's semaphore wait.** `R63-65`. The SoundServer handshake hung
the main thread in `RSemaphore::Wait`. Fix: wait in 100 ms slices so the
log keeps flowing and the hang has a place. The heap-sharing theory for
the same symptom (`R66`) was wrong: measured identical record for record.

**Logging from the wrong thread.** `R62`. `box_flush`'s `RFile::Flush` on a
handle another thread owns is `KERN-EXEC 0` on a device (`EOwnerThread`).
Fix: guard every file call with the owning thread's id.

## 5. Timers and active objects

**The frame timer.** `README.md`, "The timer wrapper". The game's `CTimer`
subclass has the 7.0s layout; the port diverts `ConstructL`, `After`,
`CActiveScheduler::Add` and `SetActive` to a wrapper with a real `RTimer`.

**`DoCancel` must be the real `CTimer::DoCancel`.** `R99-100`, `E332-336`.
Completing the request from `DoCancel` instead of `iTimer.Cancel()` leaves
one signal more than the scheduler will consume: `E32USER-CBase 46` (stray
signal) some frames later. The same rule holds for a DSA cancel: EKA2L1 had
to be patched to stop completing a client-initiated cancel twice
(`src/emu/services/src/window/classes/dsa.cpp`).

**Priority is not why an object does not run.** `R102-103`. `DoRunL` scans
from the head after every wake and runs the first ready object; an object
at position 26 cannot be starved by one at 34. The queue dump
(`sched_dump`, `NOTE_AO_*`) is what settled it; a theory about the queue
never will.

## 6. Display

**The frame buffer: 32 bits a pixel, 1,280 bytes a line, 240 visible.**
`R70-76`. Eight format guesses, then a ruler painted into the frame
(`SCREEN_RULER`) and a keypad-steppable line length gave the number. The
pitch is a padded stride for addressing rows and nothing else; the pad is
off-panel. HAL answers for the *mode* are not believed, only the pitch and
the first-pixel offset (`gate6_screen_info`). A phone's line is a fixed
number of pixels at whatever depth the mode says (C5-00: 2,048).

**The wrap: source origin 16, and the row carry.** `R95-98`. The game's own
buffer starts sixteen pixels in; columns that run past the panel's edge come
back through the left, one row down. Fix: `srcOrigin`, and `screen_columns`
with a per-column row carry (`rowD`), both in `screen_fit.h` with host
tests (`fittest.cpp`, `columntest.cpp`). The two titles differ in exactly
one runtime constant here.

**The status-pane band.** `R76-78`, `R87-89`. A band across the top was the
status pane; `ENoScreenFurniture` did not remove it; the inset is asked of
Avkon (56 on the N95, 48 on a 5320) and the picture laid out below it; the
full-screen mode posts a region of the port's own covering the whole panel
(`Update(const TRegion&)`), because `CFbsScreenDevice::Update(void)` kills
the game on the phone (`R88`). `SetFullScreenApp` is called before the
inset is asked for; `PANE_MAKE_INVISIBLE` crashed (`E244`) and is off.

**Picture modes.** `R87`. Five modes cycled by holding C, saved in
`C:\gate6.cfg`; the scaler is table-driven (Bresenham, no divide: there is
no `__aeabi_uidiv` in the image).

**The residual frame after minimising.** `R105`, section 9 below.

## 7. Memory

**Allocation after the heap is full.** `R85-86`, `R92`: section 1. A third
party's claim of "a memory problem" was checked against their log and was
not one (`R86`); the log is the record, not the report.

**`G6MEM` on a relaunch.** `E83`. A run left behind holds its
memory; enough of them and the next launch cannot allocate the image. On the
bench: `pkill -x eka2l1_qt` after every run (`emurun.sh`).

## 8. Backgrounding (the crash)

**Both titles died on losing the foreground.** `R93-104`, twelve rounds.
The chain, read from cone, avkon and viewcli: `EEventFocusLost` ->
`HandleForegroundEventL` -> the control's `FocusChanged` (the game pauses
itself: audio down, timer cancelled) -> `KAknFullOrPartialForegroundLost` ->
the view server deactivates the app view -> viewcli's receiver ->
`CCoeViewManager::DoDeactivation` -> every `MCoeViewDeactivationObserver`
-> `CAknAppUi::HandleViewDeactivation` on the *wrapper*. The cause was the
missing mixin vtable (section 3). What the twelve rounds taught, in order:

- the frame loop does not stop, the game stops it (`R96`): a process that is
  alive but no longer driven looks exactly like a dead one in a log that
  only records the frame loop;
- the fault catcher: EKA2 re-enters the process entry point with
  `r4 = KModuleEntryReasonException` and the fault frame on the user stack;
  `_start` dispatches it to the installed handler, which logs registers,
  walks the scheduler queue and panics with the import index (`gate6_fault`,
  `G6FLT`). Without it a `KERN-EXEC 3` has no address;
- the bench can be made to walk the phone's chain: `DeactivateActiveViewL`
  on the wrapper (`E346`) reproduced the death on EKA2L1, which no focus
  event could;
- the idle death (`R93-94`) was separate: the screensaver after the
  inactivity timeout. Fix: `User::ResetInactivityTime` once a frame
  (`EKA2L1_INACTIVITY=1` counts the calls on the bench).

## 9. Backgrounding (the residual frame)

**The last frame stayed on the panel after every switch.** `R94-105`.
Cause: the port cancelled its direct screen access on the first frame
(`RELEASE_THE_SCREEN`, a round-60 guess about a reboot that wserv's 0.4 s
abort timeout never causes), so wserv never aborted it and never repainted
its region. Fix, from `Direct.CPP` and `RDirect.CPP`: hold the access;
on abort stop writing; ws32 restarts the client at once from a `CIdle` with
the region it has *now* -- empty behind the menu, two rectangles under a
popup -- so the blit and the blank keep to that region (`dsa_box`, four
clip modes, `NOTE_DSA_RGN`/`NOTE_DSA_BOX`); and because **neither game's
Restart calls StartL** (measured, `R105`), the port restarts the access
itself when the game has not. Both titles' windows are the whole panel once
the status pane has gone, so the clip changes nothing in the foreground.

## 9a. Leaves inside the game's TRAPs (G6FLT 31600 / 31604)

**Symptom.** `R106-108`. `G6FLT` with `TExcType 0` (`EExcGeneral`) and no
frame, `TTrap::Trap` the last import, in Asphalt 1's audio-stream reopen
after a foreground cycle. Rare: round 105 played 3,000 frames without it.

**Cause.** On 9.x `User::Leave` is `throw XLeaveException` and TRAP is a
try/catch (`us_trp.cpp`, `e32cmn.h`). The port had faked the game's EKA1
`TTrap::Trap` (return 0) and `UnTrap` (no-op), so a leave inside a game
TRAP was a C++ throw with no catch it could reach: the unwinder cannot pass
the game's GCC98r2 frames or the port's, so `__cxa_throw` ends in
`std::terminate` -> `User::RaiseException(EExcGeneral)` -> the installed
exception handler -- which is why the catcher saw a bare type and never a
kernel frame. `SetAudioPropertiesL` left on the reopen; the game's code at
0x14398 was written to catch it and retry at 8 kHz.

**Fix.** `gate6.s` `gate6_trap_enter`/`gate6_trap_longjmp` and the trap
bridge in `gate6.cpp` (`TrapHandler`, `gate6_trap_*`): the game's
`TTrap::Trap` is a real setjmp (registers, sp, lr, and `aResult = 0` on
entry, as EKA1 did -- E359), `UnTrap` pops, and the port installs its own
`TTrapHandler` per thread wrapping the real `TCleanupTrapHandler`, laid out
like it (`iCleanup` at offset 4, because `CleanupStack::PushL` casts the
installed handler -- E358). `Leave` forwards to the original, calls
`XLeaveException::GetReason` on a dummy to balance `Exec::LeaveStart`, and
longjmps into the innermost game trap before 9.x throws. Framework TRAPs
keep their try/catch. Bench knob `FORCE_LEAVE_AT` proves the round trip
(E360). Lesson: **a faked primitive is a promise the port has to keep
somewhere**; "only code that leaves can tell" was true, and it told.

**How it was found.** Three instrument rounds: 023's frame scan matched a
false window (`R107`); 024 recorded the raw entry value and stack instead
of interpreting, and the raw words named `User::HandleException`'s frame.
Record first, interpret offline.

**Still open.** A leave with no game trap open, thrown from code reached
through game frames (a game callback that leaves without trapping), is
still a terminate, as it always was.

## 10. Where the bench and the phone disagree

Known gaps in EKA2L1, each of which hid a bug above: thread stacks far
apart (4); any stack size accepted (4); a bad handle returned instead of
`KERN-EXEC 0` (4; `EKA2L1_STRICTHANDLE`); duplicate thread names accepted
(4); SIS integrity not checked (2); no status pane painted (6); nothing
ever covers the window, so no DSA abort, no focus loss, no view deactivation
(8, 9) -- drive the framework from inside instead (`FORCE_*` knobs); a
client-initiated DSA cancel completed twice (5, patched). The bench's
frame rate varies 20% run to run (`E350-E351`): a frame count is a
regression signal only against the ticks.

## 11. The instrument, which is half of every fix

- **The box** (`g6box-<stem>.dat`): a fixed block of the latest state,
  rewritten in place; survives any death. `readbox.py --game`.
- **The ring log** (`g6box-<stem>.log`): a frozen head and a ring behind it;
  the end of a run is always on file. `readlog.py --game`.
- **The build stamp**, the code size, the launch counter, the tick clock.
- **The fault catcher** (8), **the scheduler dump** (5), **the worker
  probe** (4), **the heap walk** (7), **the ruler and the keypad picker** (6).
- **Bench knobs** that make EKA2L1 do what the phone does from inside the
  process (`FORCE_*`), all 0 when shipping.
- `emurun.sh` writes every bench run into `ROUNDS.md` as a `TODO` row and
  `checkrec.py` refuses to let one stay; `rules.py` prints the four rules.

## 12. One (6r58): the fourth title's first fifty stops

Each of these is a class of bug, not a One quirk: check a new title for all
of them before its first round.

**TInt64 is a class on EKA1 and a `long long` on 9.x.** `E465-E467`. The
game imports `TInt64::operator*`, `/`, `+=`, `GetTInt`, `GetTReal`, and the
`TDes::Num(TInt)` family now takes `TInt64`. Fix: the class answered locally
(`gate6_tint64_*`, 32-bit halves, no runtime helper), signature thunks
(`KIND_SEXT1`, `KIND_ZEXT1_RADIX`). Key MANUAL entries by the **7.0 def
spellings**, not the 9.x ones: a key that does not match is silently unused.

**`RFs::SetDefaultPath` panics on 9.x** (FSInsecCli 1, `cl_insecure.cpp`).
`E468-E475`. A no-op is not enough: drive-less names resolve against the
session path, which on 9.x starts at the private directory. Fix: keep the
path, `RFs::SetSessionPath` on every `Connect` (`gate6_fs_connect`). **Every
thread's** `Connect`: resolve SetSessionPath once on the main thread and keep
the address -- a worker's `RLibrary::Lookup` through the main thread's handle
fails, and One's part loader then looked for `Male.ppd` on C: (`E511-E512`).

**Objects 9.x made bigger overflow the game's stack.** `E472`. `TEntry`
(+8) and `TVolumeInfo` (+cache fields). Fix: old-size stand-ins and copy-back
hooks (`gate6_fs_entry`, `gate6_fs_volume`).

**Deleting a 9.x object through the old vtable word.** `E471-E473`. The game
`delete`s `CDir`/`CFileMan` via word 2 with in-charge 3; on EABI that word is
`Extension_`. Fix: `old_deletable`, a full vtable copy with word 2 the
deleting destructor. The reverse: `CleanupStack::PushL(CBase*)` of a game
object runs EABI word 1, which is 0 on a GCC98r2 vtable -- `gate6_pushl_cbase`
pushes a TCleanupItem running the old destructor (`E477-E478`).

**Old `MStreamBuf` slot numbers in the game's code.** `E477-E478`, `E512-E513`.
`ldr ip, [ip, #0x28]` is old slot 8, `DoSeekL`; on the 9.x object it is EABI
slot 10. Unseeked reads: zlib `Z_DATA_ERROR`, then a 17 MB `AllocL` from a
garbage count and `G6FLT 34200`. Fix: `GAME_CODE_PATCHES` to `+0x20`. Scan
**every** `+0x28` call whose object is loaded from `[RReadStream]`, not only
those in functions that call estor directly -- the first scan missed four.

**Eight-byte structures returned by value.** `E474`, `E516`. GCC98r2 returns
`TPtrC` in r0:r1; EABI takes a hidden pointer in r0. `KIND_SRET8`, and the stub
must move the arguments up with `this`: the first version put `this` over
`Left(int)`'s length. `TDesC::Left/Right/Mid` are struct returns too (Ashen
imports `TDesC8::Mid`).

**Threads found by name.** `E478-E482`. The port renames threads `g6wN`; the
game's `RThread::Open(name)` fails and its audio thread "dies". Fix: retry with
`FullName()` + `::g6wN`.

**The card is per title.** `E488`. Codewave checks the card CID, which is the
dump's MMC-ID (`GAME_CARD_CID`).

**32-bit pixels, and the buffer on every poll.** `E489-E490`, `E507-E510`. One
draws EColor16MU (`GAME_SRC_BPP 32`) and calls `UserSvr::ScreenInfo` every
frame; the hook replaced the address only on the first poll, so past the splash
the game wrote the real panel (the streak band) while the port posted the
splash. Fix: hand back the port's buffer on every poll.

**Keys never arrived: a Cancel the port dropped.** `E493-E503`. One wraps
`RTimer` in a `CActive` of its own; its `Cancel` was dropped as a stray (vptr
in the image), the object stayed active, `RTimer::Close` then `Deque`'s real
Cancel waited forever in `User::WaitForRequest`, swallowing every signal.
Found by the emulator's own traces (key shipper, event queue, request
semaphore, the stack at the wait). Fix: `GAME_CANCEL_OWN_OBJECTS`.

**The wrapped CTimer's state never reached the game's object.** `E514-E515`.
`if (iActive) Cancel(); After(t);` read the game's +8 while the wrapper was
pending: `E32USER-CBase 42`. Fix: `GAME_TIMER_MIRROR`.

**The fight-start rasteriser fault was another title's patch.** `E516-E527`.
Image `0x349a4`, the reciprocal table read far past its end from one wild
vertex. The table at `0x101b98` (entry n = 2^30/n) had entry 6602 reading
`0xE1A00000`: Asphalt 2's `kNop` at `0x1082c0`, applied to every title. Found
with range probes on the clipped vertices' projected x, which dumped the table
entry. Fix: gate the write to Asphalt 2's UID. Lesson: **a hard-coded image
offset belongs to one image**; every one must carry the title it was found in
(UGT and Ashen escaped only because the offset lies past their code).

**A double passed by value lands two registers apart.** `E531`, `E535-E536`.
`TRealX::TRealX(double)` and `TDes::AppendNum(double, TRealFormat&)`:
GCC98r2 passes the double in r1:r2 (high word first in the FPA order), EABI
aligns it to r2:r3 and the next word goes to the stack. Forwarded plainly they
read garbage. Fix: `KIND_DBL1` (`PASSES_DOUBLE` in `gen_shim.py`). The bench
self-test `GAME_FPA_SELFTEST` calls every float and double import once through
the game's slots with known answers (30 helpers, 9 Math functions): run it on
any title that imports them.

**Open: the fighters never move.** `E527-E554`. The fight runs (timer, rounds,
menus) but neither fighter animates after FIGHT. Ruled out: keys, clock,
TInt64, the arithmetic layer, audio position, round state (2 = fighting).
What the probes show: both fighters keep the round-start animation index 0
(`0x586f0`, by design) and never get another -- the anim-end transition at
`[fighter+0x2b08]`, which `0x9dd98` applies through `0xa0d04`, is never
scheduled. The AI keys its move map (`data/script/torro.aici`, 153 records,
built correctly) on the current animation, finds nothing for 0, and never
decides; the button logic sits behind the same scheduler. Next: the anim
stepper that should schedule the transition.
