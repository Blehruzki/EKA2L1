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
Avkon (58 on the N95 -- round 87; 56 was the hardcoded guess -- 48 on a 5320) and the picture laid out below it; the
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
client-initiated DSA cancel completed twice (5, patched); **no round robin
between two ready threads of one priority** (patched, round 133: EKA2L1
refreshed a thread's timeslice whenever it was preempted, so any
higher-priority wakeup inside 20 ms kept the running thread's slice full and
its equals starved -- a same-priority spinner made +0 in the main thread's
busy second, and every worker stream fell silent while the main thread was
busy, `E683-E686`; EKA2 refreshes it only on block or rotation,
`nkern/sched.cpp`, see SYMBIAN.md). Audio, measured on the N95 by ngtest (round 135): the phone never
reports `KErrUnderflow` (the bench's stream patch does after 500 ms and
stops the stream), keeps ~375 ms queued by `Position` (the bench 100 ms),
and copies the first buffer ~93 ms after it is written (the bench at once);
SYMBIAN.md has the numbers. A title that runs dry is therefore *stopped* on
the bench and *still open, starved* on the phone. The bench's
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

### 12.x A minimize that dies at the focus-lost event (round 137)

**Symptom.** One build 011 on the N95: several minimizes and returns fine,
then one minimize KERN-EXEC 0 at once. The log ends on the port's
`WS EVENT a` record -- not the Suspend-refusal note that followed it in
every other minimize since build 007, nor the foreground event.

**Cause (the best-supported reading).** The protection's `RThread::Suspend`
on focus lost carries a word that is no handle -- 0, 2, 5, 7 so far -- and
build 007's test refuses it. A hooked import is not traced, so a Suspend
that *passes* leaves no record at all; a garbage word that happens to look
well-formed goes to the kernel, which panics the main thread. The gap in the
log is exactly that call.

**Fix (build 012).** Every Suspend/Resume is written down with its handle and
caller offset. The port keeps the thread handles the game actually holds
(every `RThread::Create` and `Open` result, minus every `RHandleBase::Close`);
a call from the decrypted code chunk -- where the protection is -- on any
other word is refused. The game's own code keeps the old test: the strict
rule refused a loader-thread Resume on a handle the port never saw handed
over and stalled the game (E724). Confirmed on the bench E725; the N95 is
round 138.

**Still open from round 136:** two deaths a few beats *after* a mid-fight
minimize, in a worker's `Close` (box: last import 288, no record in the log).
The sound thread's Closes in round 137 were all its own thread-owned timer
handle. Build 012 carries the Close record, so the next one names itself.

### 12.y The pause menu stays undrawn after a return until a key (rounds 138-139)

**Symptom.** One builds 012-013 on the N95: back from a minimize with the
game paused, the panel sometimes shows the fight scene with no pause menu
over it; the next scroll draws only the item it selected, and the header
("GAME PAUSED") may never come back. Round 138's first video, frame by frame
(2 fps): returns at 5.5, 15.5, 24, 47, 89.5 and 95 s whole; at 32, 71.5 and
85 s the scene alone.

**What the log says.** The game draws nothing while away when paused (round
139: no update asked between the abort and the return in either paused
return; the fight and a load *do* draw while away, 4-24 updates, dropped)
and exactly one frame after its own `StartL` on the return. The image says
what that frame is: `0x44eac` takes a rectangle, `0x44fa0` starts the DSA if
the game's own flag (+0x58) is clear and the object reads inactive, then
copies the rectangle from its back buffer to the screen (`0x18240`: the
whole buffer when the rectangle covers the screen, else that part) and calls
`Update` on the DSA's device with the DSA's region. The menu is drawn in such
rectangles over a buffer the game expects to persist. The port blits the
whole buffer every time, so a panel showing the scene without the menu means
**the game's own buffer lost the menu** -- and build 013's re-posts (0x57AA)
fired twice after all seven returns of round 139 and changed nothing, as
they could not. Why the game's post-return frame sometimes carries the menu
and sometimes not is still inside the game (its AbortNow/Restart handlers
were not found; the observer is the screen object +0x30).

**Fix (build 014).** The port keeps the picture as it was at AbortNow and,
at the first frame back (after a `StartL` or a server restart with the whole
region), compares the game's buffer with it (0x57AB). When the game drew
nothing while away and the frame lost something, the snapshot goes back into
the game's buffer before the blit (0x57AC); the game's later rectangles then
land on the picture they were drawn for. A game that drew while away keeps
its frame. The bench cannot lose the menu (E729/E730: a menu comes back
whole), so E731 blackens the frame back by hand (`BENCH_SPOIL_FRAME`) to
exercise the restore. 
**Round 140 overturned it.** Build 014 on the N95: eight returns, and the
comparison says the frame back differs from the AbortNow snapshot only in
the bottom ten rows (seven times) or is a full redraw (once); the menu band
is identical before and after -- and the panel shows no menu after any of
the eight, now every time. The game's buffer is not where the menu is lost;
the restore only reverted the game's bottom strip. The bench's dumps
(E734-E736) show text in general goes through the game's buffer, but the
pause menu was never caught paused there. Two readings remain -- the pause
menu drawn through the DSA graphics context straight into the frame buffer,
which every whole-buffer blit of ours paints over; or our blit after a
return landing in a buffer the panel is not showing (two frame buffers,
the address read once) -- and build 015 instruments both: the frame
buffer's band summed after each blit and at each beat (0x5C0E), the
ScreenInfo address checked every frame and followed (0x5C1F), HAL's
display address (0x5C1E), and dumps of both buffers at AbortNow and of
the frame buffer after the first blit back (`C:\g6code-6r58.bin`,
`dump32.py`). Restore and re-posts off.

**Round 141 settled it, and it was the port's own StartL.** The dumps say
the frame buffer is one buffer, the address never moves, and after a return
nothing but the port writes it (the band checksum reads SAME at every beat)
-- what the panel showed was the port's blit, and the menu, when it came,
came through the game's buffer. It was in neither buffer at AbortNow because
the game had not drawn it: these minimizes were from the running fight, and
One pauses itself on losing the screen and draws the menu on the return. The
log splits the returns: the good ones have the game's `StartL` one record
after the foreground event, inside its handler; the bad ones have the
handler run without it and the `StartL` ~350 records later, from the frame
timer. The frame function (`0x44fa0`) starts the DSA only when the shadow's
+8 -- the real object's active word -- reads 0, and when it reads active
with the game's own flag clear it draws nothing. The port's `StartL` at the
minimize's restart (0x57A7, since round 60) left the real object active on
an empty region; whether that request had completed by the handler's frame
was timing. A real N-Gage never starts the DSA behind the game's back.

**Fix (build 016).** `START_FOR_THE_GAME 0`: the port does not start the
DSA for the game; after a restart the game did not answer, the screen is
nobody's (`dsaIdle`, clip NONE, 0x57AE) until the game's own `StartL`.
Bench E739-E740; settled by round 142.

### 12.w The fight glitch: two screens ticking, the keys dead (rounds 130-142)

**Symptom.** Some *sessions*, from their first fight on: the picture cuts
every two or three frames between two camera shots of the same fight, the
fighter does not answer the keys, pause and minimize still work, every later
fight in that session is as bad, and only a restart clears it (round 142's
video and words: "I can even exit and enter a new fight during the same
gameplay and it will still be bugged. I have to restart the app"; round 130
called it "cut back to intro shots"). An earlier draft of this entry said
"never the first fight" -- wrong, and the user's memory was right: in every
N95 log (rounds 139-143) the archive is read exactly once per session, at the
first fight's load, and the later fights (their threads appear, no new
`one.cwa` open, no new probe batch) reuse what that read produced. So a
session's fate is rolled once, at its first fight, and inherited; that is also
why the bench's one-fight-per-launch runs were the right test and no
second-fight case was left unexercised. Reproduced on the bench from round 143
(E746 on).

**Cause.** Round 132 made a Stop from the sound thread an owed one -- the
real `CMdaAudioOutputStream::Stop` had deadlocked under the game's channel
mutex against the main thread -- and the owed Stop was carried out only at
the stream's next Open. The real Stop is what delivers
`MaoscPlayComplete(KErrCancel)` (round 135: inside Stop, on the N95), and
One's screen flow waits for it. The intro's voice sample is stopped under
the mutex at the fight's start; when the fight opens no other sound, the
complete never comes, the intro screen never ends, the fight screen comes
up over it, both tick, and the keys go to the one underneath. Round 142's
log: the deferred-Stop counter (5A0D) read N=6 done=5 for 12,300 records,
the whole glitched fight, where every other owed Stop was carried out
within a beat or two. The bench's next Open always came a beat later.

**What it is not (build 017's one-shot, refuted).** Build 017 armed a
`CPeriodic` in the sound thread to carry the owed Stop out there (0x5A0E/0x5A0F;
bench E741). It was first read as the cause when the bench threw "dead" fights
(E746, E747): the fighter stands still for 60 s, no hit lands, the keys do
nothing. But the dead fight recurs with the one-shot **off** (E748-E760,
E769-E771) and with the fake-complete on or off, under every sound knob tried.
The audio Stop is not the discriminator.

**What it is (the one.cwa read).** Every bench fight splits cleanly in two, and
the split is exact across 17 fights (E746-E771): a **live** fight reads the
whole `one.cwa` archive -- 15-16 chunks of 0x800, the copy-protection's card
write-probe fired 408 times -- and a **dead** fight reads 4 KB (2 chunks), probes
once, closes the file (RFile::Close, efsrv 300, from code+e8680) and runs the
fight without its data. 408 probes <=> live in 6 of 6; 1 probe <=> dead in 11 of
11. The N95 logs show the same: rounds 140 and 142 (live) read 408 times, 141
and 143 (dead, the glitched ones) read once. So the glitch is the archive
failing to load, not the sound.

**Where the split is decided.** A 12M-entry PC trace of the main thread (the
emulator's `EKA2L1_PCTRACE`, armed at the stream-read wrapper code+f35b0) is
bit-identical between a live run (E763) and a dead one (E761) for the first
~23,000 block entries of the read, then parts **inside the game's own
obfuscated decompressor** -- at the state dispatch code+d233c, or the per-byte
transform loop code+b6408, depending on the run. No port shim sits between the
last shared block and the divergence: the registers the port hands the read
(f35b0: the RFile, the descriptor, a 0x400 request) are identical in both, and
so are the decompressor's inputs (code+36dc4/b3f90). The branch is the game's
own, on a value it computes. The two remaining non-deterministic inputs to that
loop are the emulator's timing (User::TickCount, read through code+b8738 by the
cca10/ccacc interval routine the reader leans on) and `Math::Random`; a
read-before-write of the decompressor's working buffer (uninitialised heap,
which the emulator fills unlike hardware) is the other candidate and is not yet
ruled out. It is neither. Each bench fight is a fresh process, so its heap is
zero-filled on first fault; an uninitialised read would be deterministic
(zero) and give the same outcome every launch, yet the outcome varies. And
two dead runs diverge from *each other* at different points in the same
decompressor loop (E761/E764), so the deciding value is consumed repeatedly
and changes run to run.

**Root cause (confirmed): the wall-clock tick.** The decompressor leans on
an interval routine (cca10/ccacc) that reads `User::TickCount` through
code+b8738 and mixes it (with `Math::Random`) into the loop's control flow --
a timing-adaptive loader. EKA2L1 serves `User::TickCount` from the host wall
clock: `tick_count` in `src/emu/kernel/src/svc.cpp` divides
`ntimer::microseconds()`, and that is `basic_teletimer_micro::microseconds()`
in `src/emu/common/src/time.cpp`, which returns real elapsed microseconds
since start. So the tick the game reads jitters with host load every launch,
and the loader lands live or dead by luck. The real N95 is intermittent for
the same reason -- its own timing-sensitive loop -- just in a narrower band,
so the user hits it only now and then.

Proof: `EKA2L1_DETTICK=<us-per-read>` swaps a deterministic virtual clock in
(same file). The outcome becomes a clean function of the step --
small steps stall the boot (E772-E774, E780/E781), 2000 bails (E775), and
10 ms per read reads the archive whole on every one of three launches
(E776-E779) where wall-clock launches split 6:11 live:dead. Timing, not
memory, decides it.

**Fix direction (no build yet).** The deterministic per-read clock is a
diagnostic, not the fix: it distorts frame pacing (the live runs do ~2%
fewer records) and stalls boot at small steps. The right fix is a steady,
emulated-time tick -- one that advances in proportion to emulated CPU work
rather than host wall time, as a device's 64 Hz system tick does in lockstep
with its fixed-rate CPU. That keeps the loader in the full-read band and is a
genuine emulator-fidelity improvement, not a game-specific hack. It is a
global change to how `User::TickCount` / the fast counter advance.

**Built (r143): `EKA2L1_CYCLETICK`.** The guest tick SVCs now read a monotonic
count of emulated instructions (fed from the dyncom core) over a configured
instructions-per-microsecond rate, instead of host wall time; the host event
scheduler keeps its own wall clock, so the change is confined to what the guest
reads. Off by default (wall clock); indexed in `EMU_PATCHES.md`. At 50 instr/us
the bench reads the archive whole on every launch and plays full fights live
(motion ~23; E787-E789 and E795-E798, 7/7 live), where wall-clock launches split
6:11. The live/dead outcome is non-monotonic in the rate (50 and 5000 live, 200
and 1000 dead; E782-E794), so it is not a plain elapsed-time threshold, but it is
now *stable* at each rate -- the coin flip is gone.

**What this is and is not.** It fixes the **bench**: a deterministic emulator
that reads the archive reliably, plus a reliably-dead regime (200 instr/us) to
test against. It does **not** fix the user's N95 -- the phone runs the game
natively, not on EKA2L1, so its own timing-sensitive loader is untouched. The
phone fix must be port-side: the gate6 shim already wraps the game's euser
imports, so it can wrap `User::TickCount` (euser 1137) to keep the loader in the
live band on real hardware. The cycle tick gives that work a reproducible bench
-- set 200 instr/us for a guaranteed-dead load, add the shim, confirm it flips to
live.

**The mechanism, found (r143).** The reader seeds a Mersenne Twister from
`User::TickCount` at its construction: code+c5d28 reads the tick, hashes it
(a bijective multiply chain), and passes it to `init_genrand` at c58b0 -- the
624-word state at obj+0x9c4, `mti` 0x270 at +0x9c0; the generator's lazy
default seed is a custom constant, 0xac2ddf7b. The bytes the reader then
decodes depend on that seed, including the Uint32 count at code+5be4c of how
much more to load: with the seed the 50 instr/us bench gives (tick 0x12c) it is
0x19765 and the archive reads whole; with 200's (tick 0x4b) it is 0 and the
reader closes after 4 KB. Everything else that looked like a suspect is not:
the interval check (cca10/ccacc) is clean in every run, live or dead -- its
budget is 45 s (E821/E822 registers); `Math::Random` only enters when that
check trips; the two other construction-time reads (ab594, ab5e0) discard
their result; and FastCounter and NTickCount are never reached -- the game
imports neither, statically or by lookup, and NTickCount does not exist in
its ABI. During the load the main thread is the only one reading TickCount
(815 reads = two interval stamps per underflow). So the seed read is the one
tick that enters the data, on the bench and on the phone alike.

**Fix (build 018): `GAME_TICK_SEED_LR` / `GAME_TICK_SEED_VALUE`.** The game's
TickCount import is diverted through an lr-passing thunk, and the one read
whose return address is code+c5d2c is answered with 0x12c -- a seed the bench
has read the archive whole with every time -- while every other TickCount read
stays real, so pacing and the interval checks are untouched. No window, no
counter, no clock arithmetic: one read, identified by where it came from.
Wall-clock bench, no CYCLETICK, where launches split 6:11 before: **6/6 live**
boot+load (E823-E828) and the full-fight batch (E829-E832). **Confirmed on the
N95, round 144: six fresh sessions, six clean fights.** Closed. The earlier window
shim (E801-E820) is gone; its lesson -- a tick leap faults the game's timers
-- is why the substitution is a plausible small tick, not a marker value.
### 12.aa The arenas' ambience is silent: TLex16::Val(TReal64&) in the wrong word order (round 144)

**Symptom.** Round 144, build 018: on the Himalayas map (arena file
`kyberpass`) the wind does not blow during a fight; blows, grunts and music are
fine. Every arena has ambience -- the boxing gym's water drops and siren,
tatami's drums, the rooftop's wind and wings -- and none of it had ever played
in the port. Nobody had noticed until the wind.

**Cause.** Ambience is data: `data/arenas/<arena>.fx` is UTF-16 text, one
`REPE <period s> <weight> <sample.noi>` line per sound, and the game's scheduler
plays each sample about every <period> seconds. The loader (code+0x45450..)
parses the two numbers with euser's `TLex16::Val(TReal64&)` (import 413). The
9.x euser writes the double through the reference in EABI word order, low word
first; the GCC98r2 game reads a double high word first. So "4.0"
(0x40100000_00000000) came back as 0x00000000_40100000, a denormal near zero,
and "0.1" as 0x9999999A_3FB99999, a huge negative -- and the scheduler never
fired. The port already re-orders doubles passed by value (`TRealX(double)`,
`AppendNum(double)`), returned (`GetTReal`) and the `Math::` by-reference
family (`M_D2D`); a *parsed* double written through a reference was the one
shape left. Blows never go through `Val`, which is why the fight had its hits
and not its wind. Not the port's audio path: the phone's logs show every
`MaoscPlayComplete` as -3 (Stop), no policy error, and the game mixes all
effects into one 16 kHz stream.

**Fix (build 019, shared).** `IMPORT_LEX16_VAL_REAL` (gen_shim HOOKS, euser
1199) and the `M_LEXVAL` shape in `kFpaMath`: `gate6_fpa_lexval` round-trips
the value -- what the game holds goes in swapped, the result comes back swapped
into the game's order -- so a `Val` that fails and leaves the value alone still
reads back as it was. Bench, one fight on the same arena: silence 63% -> 25%,
the long silent runs halved, mean level 2.6x, the gaps between blows filled
(E833/E834); the Math-hook count at install 9 -> 10. The first six parses are
logged (NOTE_FPA 0xF1.., then the two words) but the parsing thread's records
reach no log on the bench, so the audio is the bench's proof and the phone the
final one. A title without the import has the hook absent (65535) and is
unaffected.

### 12.ab The arena's ambience stops after a minimize and return (round 145)

**Closed on the N95, round 146 (build 020).**

**Symptom.** One build 019 on the N95: the wind plays through a fight until
the game is minimized and brought back; from then on the fight has its blows
and no ambience, for the rest of the fight. The game's own pause (the menu
from a key) does not do it.

**Bench.** Reproduced with the modelled minimize (`BENCH_MINIMIZE_TICK` /
`BENCH_RESTORE_TICK`) and CONTINUE from the pause menu (E837: the worker's
stream 69% zero samples after the return against 0-1% with the ambience),
absent across the game's own pause (E838: 29%), unchanged with the port's
background mute off (E839: 73%). The first two bench runs were confounded
by the attack key, which is also the menu's select (E835, E836).

**Cause.** The game's own foreground handling. On focus lost the sound
manager (code+0x48d54) requests the effects stream off and gives back every
decoded sample: 0x1216c deletes each entry of the player's sample array and
resets it. After the return -- with the resume, some 1,600 records after the
foreground handler on the bench, not inside it -- it loads the two fighters'
sets again (0x47ffc, 0x481d4) and the arena's set (0x483f4) **only for
entries whose sample id is still -1**. The arena's REPE entries -- parsed from
`data/arenas/<arena>.fx` into an RArray the manager keeps at +0x58, 0x64
bytes each: TBuf<32> name, type at +0x48, period and weight, the id at
+0x5c, last fired at +0x60 -- kept the ids they resolved at the fight's
start, which now name slots past the end of the rebuilt array. The
scheduler (0x458b4: every <period> seconds, a Math::FRand draw against the
weight) goes on firing, play(id) finds nothing. Both logs show it: after the
return the game opens loading.noa and the 42 fighter files again, and not one
of the arena's (ship, shipwave, sea, wind, seagull on the bench's harbour;
wave0/1, seagull1/3 on the phone's). Whether the N-Gage's own task switch did
the same is not known; the code is the game's.

**Fix (build 020, One).** `ambience_forget` (gate6.cpp) runs at focus
gained, before the game's handler: through the game's TLS object
(`GAME_GLOBAL_FN`), the manager (`GAME_SOUNDMGR_OFF`) and the list
(`GAME_AMBIENCE_LIST`), read in the 9.x euser's RArrayBase layout (iCount,
iEntries, iEntrySize), it sets every entry's id to -1, so the game's own
reload resolves the arena's set along with the fighters'. The manager keeps
the list pointer after a fight ends, so nothing is written unless the list
looks like one: entry size 0x64, a sane count, and in every entry a TBuf<32>
name, a type of 0 or 1 and an id small or -1. The ids stay -1 until the
game's reload: that is the state the parser leaves them in until the fight's
start resolves them, and putting the old id back as soon as the handler
returned undid the fix (E842), because the reload comes later. Codes 0x5A30
(no list), 0x5A31 (size, count), 0x5A32 (ids forgotten). E840 read the
entry size at the wrong word (iKeyOffset) and declined; E841: the arena's
seven files loaded again after the fighters', 24% zero samples after the
return against 69%.

### 12.ac Ashen: the sound stops in chapter 1, and Continue hangs the game (round 147)

**Symptom.** Ashen build 009 on the N95: the sound stops after a while in
chapter 1; later, Continue at a chapter-2 checkpoint hangs the game (the
view server then closes it). One hour's log, 130,850 records.

**What the log says.** The main thread's waits on the sound thread's
semaphores (the port's `gate6_sem_wait`, NOTE 836): the first thread's
start-up handshake signalled in one slice (4712), a join signalled (15130),
then a join at 43163 that was **never signalled** -- the port gave up after
two seconds and let the game carry on, which closed the semaphores under a
live thread. From there: a second sound thread whose ready signal never
came (46308), another join never signalled (80069), the third
`RThread::Create` refused **KErrAlreadyExists** (84165: the first thread
still exists), and the final join at 130848, where the log ends.

**Cause.** Ashen's sound thread (code+0xb470c) writes one 1 KB buffer per
`MaoscBufferCopied` (0xb4b18, on 0 or -10 only) and restarts after
`MaoscPlayComplete(KErrUnderflow)` (0xb4a7c -> 0xb49a8); its periodic is
cancelled once the chain runs (0xb4c7c), so between callbacks it has nothing
of its own, and the exit flag the main thread sets (0x72988: bit 0x800 of
the sound manager's word at +0x54) is read only from a callback. The N-Gage's
stream reported `KErrUnderflow` when it ran dry; **the N95's never does**
(ngtest, round 135, SYMBIAN.md); the emulator's does after 500 ms. So one
copy that comes back with an error -- a buffer the audio policy threw away
arrives as `KErrAbort` -- and the thread writes nothing more, the stream
drains silently, the thread is deaf, and the main thread's join waits for a
signal that cannot come. On the bench, modelled with the emulator's own
underflow swallowed and one copy made `KErrAbort` (E846), the music stops at
that copy and the join at New Game times out exactly as the N95's did.

**Fix (build 012, Ashen).** `GAME_MDA_UNDERFLOW_TICKS 32`: a timer the port
makes in the stream's own thread at its first write (`mda_underflow_watch`,
every 100 ms) tells the game `MaoscPlayComplete(KErrUnderflow)` once a
written stream has had nothing queued for 32 ticks (500 ms, the emulator's
own figure), which is what the N-Gage's stream did for it. The game then
writes again, or, with the exit flag set, stops its scheduler and signals the
join. On the same model (E847) the music returns within a second of the
aborted copy and the join at New Game completes. Codes 0x5A36 (watch
armed), 0x5A35 (underflow told); bench knobs `BENCH_DROP_UNDERFLOW` and
`BENCH_ABORT_COPY_AT`, 0 when shipping. Off for a title without the define:
One's streams are the main thread's and the sound thread's own protocol
(12.w), and were not changed.

**Found on the way.** Every title's build but One's had been broken since
build 019 (`IMPORT_LEX16_VAL_REAL` existed only in One's import table), so
the first three bench runs of this round ran a stale binary and read a stale
log as their own (E845 says how it was caught: the binary's date). The
import tables are regenerated for all five.

### 12.ad Ashen: "Game Deck Memory Full" -- no save is written (round 147; closed by observation, round 148)

**Symptom.** Ashen build 009 on the N95: saving at a checkpoint or changing
the options ends in "Game Deck Memory Full"; no save appears.

**What is known.** The bench saves options (E445, E848: `C:\System\Apps\6R21\options.dat`,
788 bytes, the size the N95 read at its own start). The N95's log has every
`RFile::Open` of the save slots (`savegame01..04.sav`: -1 at the first
scan, then -14 `KErrInUse` on slot 01 at every later scan) and **no Size of
a written file in the whole hour**, so the save never reached the game's
write routine (0xb56b8); the open path either found no free slot (0xb240c,
four slots of twelve bytes, a flag byte each) or had its `RFile::Replace`
fail in the wrapper (0xb5824) -- and `Replace`, `Write`, `Flush` and
`MkDir` were not traced, so the log cannot say which, nor what holds
`savegame01.sav` open. Build 012 traces them and records each Replace's
result and drive letter (NOTE 698). Not fixed: the next N95 log is what
names it.

**Closed by observation (round 148).** Build 012 saves on the N95, the
options too, with nothing done to the save path but the tracing. The
failure went with 12.ac: the save was asked for on a thread the game had
given up on. Which call failed is not established.

### 12.ae `G6FLT 28812` at launch on every install without the original `6r58.app` (round 149)

**Symptom.** One build 021 from the zip, or from the bundled package, on
any phone that never had the card dump copied to it: "Application closed:
One G6FLT 28812" a second after the title appears. The user's own N95,
which has the dump's `6r58.app` beside the install, never showed it.

**Cause.** One's HandleForegroundEventL (image 0x4353c, through 0x436c4)
asks `RFs::Entry` for `\system\apps\6R58\6R58.app` at every foreground
gain and quits when it is not there: `PrepareToExit()` on its own app UI
(old slot 2, which resolves to avkon's CAknAppUi::PrepareToExit and reads a
9.x member the old-layout object has not got -- the red key's death, 12.z)
then `CEikAppUi::Exit()`. Every package since build 002 carries the image as
the scrambled `6r58.bin`, so the file the game asks for is not there. The
opens had the `.app`-to-`.bin` rewrite since Asphalt 2; `Entry` did not.
And the game also reads its own image back, end to end in 8 KB pieces,
through that rewrite: with Entry fixed alone the bench died later, at
image 0x1fc910, on the 32 scrambled bytes (E854).

**Fix (build 022).** `gate6_fs_entry` applies `on_the_real_drive`; the
game's read of its own image through the renamed open has the scrambled
bytes put back (`image_read_fix`, NOTE 697); and PrepareToExit on the
game's own app UI is answered with nothing (`GAME_PREPARE_EXIT_NOOP`,
0xE819), so a quit path, should the game take one, is an exit and not an
abort. Bench E852 reproduced the death on a `.bin`-only tree, E853 the clean
exit, E855 the build running clean. Hardware: pending.

**Lesson.** The bench tree and the user's phone both had the original
`.app`; the shipped layout was never run anywhere before round 149. Rule 4's
"what state did the bench never enter" includes the install layout.

### 12.af A Nokia N73 (S60 3.0) dies in the ROM at launch, two records after the sound thread is opened by name (round 154)

**Symptom.** Build 022 on an N73: the title never appears; the log ends in a
data abort reading 0 at a ROM pc, right after `THREAD OPEN -1, -1`. The
same package runs on an N95 8GB.

**Cause (round 155).** The N73 loads the image at 0x7DA00000; the N95 at
0x4600000. The port's descriptor reader (`des_text`) rejected any pointer
at or above 0x10000000, and One's thread names are literals in the image,
so on the N73 the name the game opened by read as nothing, no made thread
matched, and the game's RThread stayed unopened -- its handshake then
dereferenced nothing. Round 154 blamed the full-name open on 3.0; build 023
answered the open from the port's own duplicate handle (0x0Dsshhhh) and
died identically, because that path sits after the name match.

**Fix (build 024).** `user_ptr`: an address from 0x400000 up to the
pseudo-handles counts as the process's, in `des_text`, the probes, the
box's file name and `sane_ptr` (the scheduler dump, whose RunL addresses
the N73's 0xF8000000 ROM had failed too). 023's duplicate answer stays.
Bench E863; the N73 pending.

### 12.ag The N73 dies in its first frame, right after `SetClippingRegion` (rounds 156-159; fixed on the bench, build 028)

**Symptom.** One 024-026 on an N73: past the thread open, the game reaches its
screen setup and dies reading 8 off a null pointer at a ROM pc, the next thing
after `CFbsScreenDevice::SetAutoUpdate` and `CFbsBitGc::SetClippingRegion`.

**What is known.** StartL left a gc, device and region of the same shape as
the bench's (0x57AF). With the N80 (S60 3.0) firmware on the bench (round 158):
3.0's StartL is instruction-for-instruction 3.2's -- it makes the screen device
(bitgdi 247) and activates the context on it (bitgdi 148) -- and 3.0's
`SetClippingRegion` stores the region without touching the device. The read
`ldr r0, [gc, #0x70]; ldr r0, [r0, #8]` (the context's device, then +8) is
bitgdi's, in the export after `SetClippingRegion`, and matches the fault.

**What the bench cannot do.** EKA2L1 runs the N80 firmware up to StartL, where
3.0 opens the LCD driver (`GenericLcd_Lcd`) and makes kernel calls 0x82 and 0xA
the emulator lacks (E872). Clearing the device word on the 3.2 bench does not
reproduce the death (E874): One's 3.2 frame never reads it.

**Build 027.** After StartL, a context with no device is activated on
StartL's device with bitgdi 148 (0x57B0; works on the bench, E873); 026's
dumps (0x57AF, 32 words of the context) and the `SetClippingRegion` wrapper
(0x5E7C) stay. A guess at the fix, and a measurement either way.

**Cause (round 159).** Not bitgdi: `CCoeControl::DrawNow`, which One calls
once (image 0x43748) on its own old-layout control. S60 3.0's cone keeps a
control's flags behind a pointer at +0x2C (`ldr r0,[this+0x2c]; ldr r0,[r0,#8]`);
3.1 and 3.2 keep them as a plain word at +0x24. The old object has a zero at
+0x2C, so 3.0 reads address 8 and 3.2 reads a word that happens to pass. With
the emulator taught 3.0's LCD driver (kernel calls 0x82 and 0xA, and
`GenericLcd_Lcd`'s controls read off the ROM's own LDD), the N80 bench
reproduces it exactly: E878's pc and lr sit at -0x785 and +0x720E from the
DrawNow entry in r12, the same offsets as the N73's fault box.

**Fix (build 028).** `DrawNow` (old cone 53) is in `DIVERTS`: the map thunk
swaps the game's control for the wrapper, as for `ActivateL` and
`SetExtentToWholeScreen` (12.x's E281 is the same bug on another method). E881:
the fight runs on the N80 firmware at 26 FPS; E882: 3.2 unchanged. Build 027's
context repair (0x57B0) stays; it is harmless and was not the cause. Waiting on
the N73.

**Round 160.** Build 027 on the N73: the identical fault (pc 0xf8b602d4, lr
0xf8b67c67, r12 at DrawNow's entry), the 0x57B0 repair never fired. 027 was
built before the cause was known; 028 is the build that addresses it.

### 12.z The red key dies G6FLT 38212 (rounds 138-139)

**Symptom.** The end key during a fight: "Application closed: One G6FLT
38212" (round 138's first video; round 139's log ends there).

**Cause.** Avkon turns the end key into `KAknUidValueEndKeyCloseEvent` and,
for an application that is not a system one, `KAknShutOrHideApp`
(`AknAppUi.cpp`, `HandleWsEventL`), which reaches the wrapper as
`HandleCommandL(EEikCmdExit)`. One's handler (`0x439ac`) answers 0x100 with a
virtual call on its own app UI -- old slot 2, a ROM function on the
old-layout object -- and then `CEikAppUi::Exit()`. The virtual call dies
first: data abort reading 0x80 off a null pointer at a ROM pc (0x82cf1d20
on the N95; the fault handler's 382 is the frame loop's RequestComplete, the
last traced import, not the caller). Round 138 read the same code as round
136's mid-fight death; it is not.

**Fix (build 014).** `gate6_ui_command` answers `EEikCmdExit` itself: flush
the record, `User::Exit(0)` (0xE818), as `gate6_appui_exit` answers the
`Exit()` the game would have reached. Nothing else is on the game's path.

## 11a. Found by ngtest, the test app (round 134, bench and N95)

**The ninth thread to trap: E32USER-CBase 66.** `E692`. The trap bridge
(section 9a, `E358`) kept its per-thread handlers in a table of eight,
never emptied when a thread ended and matched by address. The ninth thread
to trap got no handler, its TRAP marked nothing, and its first
`CleanupStack::PushL` panicked `EClnPushAtLevelZero`; a new thread's
cleanup handler allocated where a dead thread's had been would also have
matched the dead entry. Fix: no table -- the installed handler is ours
exactly when its vtable is `c->trapVt` (`E693`).

**The twentieth stream: a jump to the callback's offset-to-top.** `E693-E694`.
Each `CMdaAudioOutputStream::NewL` built a stream proxy and a callback
proxy out of the spare arena, about a kilobyte, and nothing gave it back.
When the arena ran out the platform got the game's callback bare, and the
first `MaoscOpenComplete` jumped to entry zero of a GCC98r2 vtable: the
mixin's offset-to-top (-24 in ngtest; the -4 of the comment at `CB_PROXY`
in One's case). One makes a stream more than once a session. Fix: the
deleting destructor gives both proxies to a free list, the last one held
back a free because the destructor returns through its trampoline.

**A shared table on a dead thread's heap: G6FLT nn12 at `delete cleanup`.**
Round 134 on the N95 (`E704-E716`). `user_allocz` takes from the calling
thread's heap, and the CTrapCleanup stand-in's vtable (`c->cleanupVt`) was
made by the first thread to call `CTrapCleanup::New` -- always a game
worker. When that worker ended, EKA2 freed its heap (`DThread::
CloseCreatedHeap`, kernel/sthread.cpp), and every later `delete cleanup`
read its vtable from an unmapped page: EExcPageFault, type 12. The
old_deletable tables had the same exposure (One's part loader is a worker).
EKA2L1 kept dead heaps readable, so the bench never saw it until it was
taught to free them. Fix: `lasting_allocz`, from the spare arena. Rule: a
table cached in the context is built from memory the process owns.

**The main thread read a dead worker's stream.** Same round. `gate6_cb_call`
logged the state of `c->mdaProxyObj` -- the *last stream made*, a worker's --
on every OpenComplete and PlayComplete, so once that worker was gone the
main stream's next Stop faulted the main thread. Fix: describe the stream
that is calling back, found by its callback proxy.

**An inline `TRgb` is the wrong colour on 9.x.** `E696`. The 6.1 header
packs `r | g<<8 | b<<16` (alpha 0), 9.x `r<<16 | g<<8 | b | 0xff000000`
(`gdi.inl` in both): a brush colour built in the game's code shows with red
and blue swapped. No title is known to draw with one; noted for the next.

**A redraw asked for from a RunL never reached ngtest's screen** (`E696-E698`:
`DrawNow`, then `DrawDeferred`; `Draw` ran once, at start). Not pursued:
ngtest closes itself at DONE instead. A window-gc title that repaints only
on request would show it.

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
deleting destructor. Not covered: anything the game makes itself with `new`
and a 9.x constructor -- a worker's `delete` of its own `CActiveScheduler`
dies KERN-EXEC 3 with the scheduler's queue empty (ngtest, `E679-E682`). No
title does it today; one that does needs the constructor import wrapped. The reverse: `CleanupStack::PushL(CBase*)` of a game
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

**The fighters never moved: a game class's vtable read by 9.x code.**
`E527-E562`. The fight ran (timer, rounds, menus) but neither fighter left
its round-start animation, so the AI -- which keys its move map on the
current animation -- never decided, and the button logic sat behind the same
scheduler. The animation table, `torro.bin` out of the pak, was all zeros:
One reads it with 9.x `RReadStream` through its **own** `TStreamBuf`
subclass (`0x36e70`, vtable `0x152940`), and 9.x calls slot k at vptr + 4k
while a GCC98r2 vptr is the vtable's start, two header words early. So
`DoReadL` ran slot 0, `MStreamBuf::DoRelease`, and read nothing. Fix:
`GAME_VTABLE_SHIFTS` moves the slots down two words in place. Found by
following the frozen state back one step at a time with range probes:
animation index never stored, its end event due but its op a no-op, the op
table empty, the loader's buffer empty after `ReadL`. Lesson: **any game
class that 9.x code calls virtually needs an EABI-shaped vtable** -- the
mixin callback shift (`CB_SHIFT`) was the same bug; check every class the
game derives from a 9.x base whose virtuals 9.x invokes (stream buffers,
observers, callbacks).

**The first phone run: three bad handles the bench forgave.** `R125`,
`E567-E569`. The N95 showed KERN-EXEC 0 then `G6FLT 40812`. The bench had
run the same build for hours, because EKA2L1 declines a bad handle quietly;
`EKA2L1_STRICTHANDLE=2` panics like a phone and found all three:
- the sound thread used a main-thread library handle (the push hook's
  `RLibrary::Lookup`): the thread died, and the main thread later read the
  sound object it never made, the G6FLT. Fix: every function a hook may need
  on another thread resolved at load (`pushItemFn`, `fullNameFn`,
  `setSessionPathFn`);
- the protection's four-byte `TRequestStatus` at sp+4, with its RLibrary at
  sp+8 that 9.x euser's `iFlags` write corrupted. Fix: its dynamic
  `RThread::Logon` and `User::WaitForRequest` get an eight-byte status of
  the port's (`gate6_thread_logon`);
- four CTimers held as one pair: the name entry's destructor ran 9.x
  `CTimer::~CTimer` on its old-layout object at the fighter's SAVE. Fix:
  `GAME_MULTI_TIMER`, a table of pairs, with `~CTimer` and `DoCancel` sent to
  the stand-in. It is also why the fighter's save (`6R58.prf`) was never
  written.
Lesson: **run every new title under `EKA2L1_STRICTHANDLE=2` before its first
hardware round.** One run would have saved this one.


**Round 126: four reports from the second phone run, fixed in build 003.**
`R126`, `E574-E577`.
- *KERN-EXEC 3 in `g6w2`, then KERN-EXEC 0 in One, at the VS fight's
  loading screen.* g6w2 is the loading-screen thread (spinner, `loading.noa`),
  created with the 8 KB stack the game asks for; the first loading screen ran
  on the same thread and survived. The fault's pc was not recorded (the
  exception handler is installed on the main thread only), so the cause is a
  **guess**: a stack overflow, since on EKA2 the 9.x client code and the
  port's hooks run on that stack too, and EKA2L1 runs the servers host-side.
  Fix: `stack_thunk` raises any request below `STACK_RAISE` (0x8000). The
  KERN-EXEC 0 after it is read as consequence. Round 127 confirms or not.
- *Holding C did nothing.* One gets its keys through the app UI's
  `HandleKeyEventL` (`gate6_ui_keyevent`), not the control's
  `OfferKeyEventL`, and nothing ticked the hold because One's frame loop is
  an active object of its own. Fix: `hold_key`, shared by both paths, and the
  100 ms hold timer started from the app UI path.
- *The picture sits a few pixels off, as Asphalt 1's did on the other side.*
  The N-Gage frame buffer's 32-byte header, which is 8 pixels at 32 bpp.
  `GAME_SRC_ORIGIN 8`, measured on a frame dump (E575).
- *The music hiccups.* Suspected: the log. Round 126's log ran at about
  131,000 records with a write and a flush to the card every block, on the
  main thread. Three-quarters were noise: NOTE_SCREEN nine times a frame,
  `TTrap::Trap`/`UnTrap`/`User::AllocL` traced on every call, and the
  allocator census wrapping Asphalt 2's import **indices** -- in One index 409
  is `TTrap::UnTrap`, so every UnTrap logged six records. All three cut. A
  suspect, not a proof: round 127 says whether the music is smooth.
Lesson: **a list of import indices is one title's.** Two arrays of Asphalt 2's
numbers outlived the move to (DLL, ordinal) keys in E280.

**Round 127: the VS fight still dies, and three things found looking.**
`R127`, `E581-E585`.
- *KERN-EXEC 3 in the loading thread, again, at 32 KB.* It dies in the
  animation loader (image 0x5b0a8, `script\torro.anm`, 296 files), the box's
  ring says (it holds every thread's traced imports; the log only the main
  thread's). The same loader runs on the main thread at the fighter creator and
  survives; the bench runs it on the loading thread and survives. Not yet
  explained. Build 004 gives game threads the main thread's 64 KB, and **the
  port's exception handler on every game thread** (installed at its first
  traced import): the fault frame goes into the context and the main thread's
  heartbeat logs it (`NOTE_WRK_FAULT`, 717), and the thread panics G6FLT with
  the last import, so the next death names its instruction.
- *A double free between threads in the port's own quarantine.* `gate6_free`
  read a ring slot and wrote it back in two steps; two threads preempted in
  between both freed the cell that was in it. The 9.x heap locks itself; the
  ring did not. One's loading thread shares the main thread's heap. Fix: the
  slot is exchanged with `swp`, the cell and its deallocator in one word.
- *No file was ever closed.* `RFsBase::Close` was mapped to
  `RHandleBase::Close`, which closes word 0 of an RFile -- the parent session's
  handle, which 9.x marks KHandleNoClose inside a subsession, so the kernel
  refused it. Every file every title opened stayed open on the file server; on
  the bench One's fighter save lost the tail of its host stream and the game
  deleted it as corrupt at the next launch (its zlib answered Z_BUF_ERROR).
  Fix: efsrv `RFile::Close` (ordinal 300, disassembled: CloseSubSession(0x1c)),
  in gen_shim's map and in the dynamic lookup. Found by three range probes on
  the save's descriptors and a temporary log in the emulator's file server.
Lessons: **a port-wide structure written from a hook is written by every
thread** -- check each for a read-then-write; and **a mapping by name is a
claim about the object**: RFsBase::Close on a session and on a subsession are
different calls.

**Round 128: silent fights, and a frame loop the port did not own.**
`R128`, `E590-E592`. One's frame loop is a self-completing active object of
the game's own (constructor 0x264e8, priority -1; it kicks itself at 0x26564
with `User::RequestComplete` and `SetActive`). The 9.x scheduler always runs
the highest-priority ready object (SYMBIAN.md), so for the length of a fight
nothing below -1 in the main thread ran: the audio stream's open-complete
arrived only at the pause (26,000 records after the Open), and the fight had
no sound. It is round 102's starvation again; the Asphalts' fix was the port's
own frame timer at -101, which One does not use. Fix: `GAME_AO_PRIORITIES`
rewrites the priority a named `CActive::CActive` call site passes (the
wrapper reads the return address); One's loop goes to -101. The bench never
starved, so the bench can only show the change is harmless. Lesson: **a title
with its own frame loop needs the priority check the port's timer gets for
free** -- look for the self-completing object (`User::RequestComplete` on its
own iStatus, every frame) in a new title's first log.

**Round 129: sound in the fight, a skip that spoils it, and a theory dropped.**
`R129`, `E597-E617`. The priority change (round 128) worked: the stream opens
at once and the fight has sound. Three findings from looking further:
- *A count can mix two objects.* Stream calls counted by thread said the sound
  thread drove the main thread's stream -- 1,097 writes -- and a marshal was
  built to hand them over. It marshalled nothing: the sound thread makes its
  **own** stream for the menus, and all its calls came before the main
  thread's existed (E616). Count per object, not per slot, before building on
  a count. The marshal came out.
- *A probe that never fires is not evidence.* Range probes planted in the
  audio writer (0x1195c) never reported though the code ran there (E607-E609);
  the dispatch's caller, logged from the proxy, settled where the calls came
  from. Cause of the probe's silence not found.
- *The bench is blind to a stall it cannot tell from idling.* The skip that
  spoils a fight on the phone could not be split on the bench (E599-E612).
The minimize: the bench walks the whole chain, view server included
(`BENCH_DEACTIVATE_TICK`, from the hold timer, for a title without the
port's timer) and survives; the phone's log stops after the port's DSA
restart because nothing after it was flushed. Build 006 flushes from every
abort on.

Round 130. *Deliver the real event, not its consequence.* The bench's
window-to-back sends no focus event, so the minimize had never run the
code the phone ran. Synthesizing `EEventFocusLost` through `HandleWsEventL`
(`BENCH_FOCUSLOST_TICK`) reproduced it on the first try (E621): the copy
protection, from decrypted code in a local chunk, calls `RThread::Suspend`
on handle 5 through the game's own import table. Five is no handle; a device
panics KERN-EXEC 0. Build 007 refuses the call (E624-E625). The flush armed
at focus lost was a guess and changed nothing (E622) -- kept as an
instrument, not as a fix.
*A hang leaves no record unless something else is awake.* ViewSrv 11 means
the main thread stopped; its own heartbeat cannot report that. The watchdog
thread does (E627-E628).

Round 131. *A worker's log line is no evidence either way.* The mute ran on
the sound thread, and `log_event` drops a worker's records (WORKER_LOG 0), so
the first bench run of it (E638) could not have shown it working. Counted
instead, and the counts logged by the main thread (E639-E640). Before
reading an absence, ask which thread would have written the record.
*One dump a launch is spent by the first stall.* The heartbeat stops while
the game is minimized, so build 007's single dump went on a minimize and the
hang after it went unrecorded. A slot per stall.
*The bench knobs that ride the hold timer need the timer running.* One
starts it only on a held C; E636 delivered nothing. The knobs start it
themselves now.

Round 132. *Two threads, one lock, one call that waits.* One's freeze was
a deadlock the stall dump named whole: the sound thread inside the real
CMdaAudioOutputStream::Stop, holding the game's channel mutex; the main
thread waiting for that mutex. On the N-Gage no audio call waited on another
thread; on S60 that Stop does not come back while the main thread is stuck.
The port cannot change the lock, so it moves the call: a worker's Stop is made
from the stream's next callback, where the worker holds nothing.
*Registers beat records for a hang.* Three rounds of logs ended at the same
place and could not say why; one dump with RThread::Context of every thread
and the main stack said it in one reading.
*Model the phone's sequence, not the symptom.* The bench minimize first
modelled an empty region and showed nothing wrong (E653); the phone's own log
had a partial region instead, and that shape is the one the fix answers.

## 13. Colin McRae 2005 (6r66): an AirPlay engine, from E883

The fifth title, and the first that is not an N-Gage application at all:
its `.app` is a launcher, and the game is an Ideaworks3D AirPlay engine
(`6r66.nax`, inflating to an `LXCE` image; `lxce.py`) that the port lays out
and runs in a thread of its own (`GAME_ENGINE_LXCE`). Most of what it hit is
EKA1 against EKA2 in the engine's own code, where no import can be swapped.

### 13.a A null read (0x14) in avkon while the app UI is built (E883)
`CEikAppUi::BaseConstructL(ENoAppResourceFile)` leaves avkon's members
unset. Fixed as every title's base construction is: avkon 2924 with
`ENoScreenFurniture`.

### 13.b "Already Active" (E-32 E11) at the engine's first request (E884-E886)
The engine's inlined `SetActive` and destructor check test the word after
`iStatus` -- on EKA1 the next member, on EKA2 the request's own `iFlags`.
Three code words now test and set bit 0, `EActive`, as 9.x does.

### 13.c Writing 0x28 through a null `this` (E886-E890)
Found by PC trace (the object was fine on entry; a callee-saved register came
back zero) and then by write watch: 9.x `RWindow` is 24 bytes, the engine's
stack temporary 8, and the constructor, `BeginRedraw(TRect)` and `EndRedraw`
wrote the rest over saved r4-r7. Those four run on a full-size copy. The
first watch was void: its 400-write cap filled with stack traffic (E889).

### 13.d A null renderer, `vptr` read off 0 (E892)
The engine builds its renderer only for EColor4K or EColor64K from
`GetDefModeMaxNumColors`. Answered EColor4K, the mode the port reads a
16-bit buffer as.

### 13.e The engine waits for good on its own thread (E909)
Its sound-thread shutdown opens the thread by name; the open fails (the port
renamed it) and the port answers from `wdThr` -- indexed by the name's
number, while the port's own `g6eng` holds slot 0. Colin got a duplicate of
itself. Each named thread now records the slot its Create filled.

### 13.f The engine exits at once, tearing its window down (E910-E913)
Its I3D shared memory, `I3D_SHARED_MEMORY_COLIN`, was the launcher's to
make: finding none, the engine (participant 1) makes it and sets its own
state to 5, exit. The port makes the chunk before the engine starts, header
3/0x100, state 1 (`engine_shared_memory`). The teardown it showed was
behind 13.g's crash: a GCC 2.x deleting destructor called through an EABI
vtable.

### 13.g The emulator itself segfaults (E917-E920; two rows misread first)
"Black" screenshots with no emulator window in them were a dead host process.
gdb: `graphic_context::clear` on a null window. The engine called its window
gc's `Activate` and `Deactivate` at GCC 2.x slots +0xd8/+0xdc, which are
`Clear()` and `Clear(TRect)` in 9.x's vtable (read out of both ROMs' ws32).
Sixteen code words move every gc call and the five ROM-object deletes to
9.x's slots; EKA2L1 now panics WSERV 9 for a command on an inactive gc, as
the S60 3.x server does, instead of crashing.
*A screenshot with no window in it is not a black screen.* Measure the whole
frame, not the panel, before reading the panel.

### 13.h White, then black under a magenta band (E921-E934)
The window-gc path showed white on the bench (the window server has the
bitmap, uploads it, and composes white -- not settled). The flip is made
always direct (E931). Then the magenta band: white 4K pixel pairs written
into the panel's own 32-bit buffer, because the `ScreenInfo` hook read a
`TPtr8`'s maximum length as its pointer and returned early. `EPtr`'s pointer
is the third word.

### 13.i "The bench display stops following the frames" (E935-E944) -- a misreading
The frames were shown all along: E946 caught the engine's IDEAWORKS3D!
splash on the panel, and gdb found no host thread stuck. The shots had kept
landing on white phases, and a white frame with an unchanged FPS figure is
the same bytes, whole window and all.
*Identical screenshots are a reading of when the shots were taken, not proof
of a frozen display.* Sample densely before concluding a freeze.

### 13.j White after the splash: the launcher's mailbox (E948-E951, corrected E962)
After its splash the engine posts 3 in the I3D block's mailbox (0xe4) and
spins for an answer above 3, 5 meaning cancel (0x449214). E948 read this as
"the engine started its front end, `6r66_2.app`, and waits for it", and
E951 as "the front end's choices are needed". Both wrong: the front end
never touches 0xe4 (its block reads are +0xd4 and +0xdc), and the engine
starts it only from script opcodes not on the single-player path (E962,
from the binary; a hooked StartApp never fired). The mailbox is the
launcher's, participant 2 -- the N-Gage launcher the port replaces. **Fix:**
the port answers 4 two seconds after the ask (`GAME_ENGINE_LAUNCHER`, which
began as the bench stand-in `GAME_ENGINE_FRONTEND_STUB`). The fault after the
answer is 13.k's, not missing front-end state.
*Lesson: when a hypothesis names a party ("the front end"), find in the
binary who reads the word before porting the party. The front end cost six
rounds (E952-E958) on a path single player never takes -- though its port
is kept, in `games/colin2`, for the multiplayer side.*

### 13.k The interpreter jumps into nothing (0x185B18C, E951, E963-E965)
After the mailbox, the engine thread (renamed COLIN) faults reading
0x185B18C at 0x459e24: a block interpreter (0x459d68) whose blocks link to
each other by the low 24 bits of their address, rebuilt as
`[0x560b98] | link`, plus 16 MB below a limit (the image's pointer 0x3cc214,
relocated). The base word is never written (EKA2L1_WATCH, E964): on the
N-Gage the engine loaded under 16 MB and a zero base was right. Here it
loads at 0x4700000, and 0x485B18C decodes as 0x185B18C. Its setter exists
-- 0x45b0b0, `base = limit & 0xFF000000` -- and nothing in the image calls
it. **Fix:** the port calls it once, relocated, before the engine thread
starts (`GAME_ENGINE_INIT 0x45b0b0`). The engine then reaches its own main
menu, attract demo and races (E966, E976).
*Lesson: code that packs pointers into fewer bits was written for an
address map; an image that never calls its own fix-up still carries it.
Search the image for the store before writing one.*

### 13.l Keys go nowhere (E967-E971)
The engine reads input through its own RWsSession and focusable window
group (`Construct(2, ETrue)`), as it did in a process of its own. Sharing
the port's process, the wrapper app UI's root group held the focus.
**Fix:** the wrapper's group declines the focus
(`RWindowGroup::EnableReceiptOfFocus(EFalse)`, ws32 148) once the engine's
group is in its block slot (the launcher's watcher). Declined earlier, in
ConstructL, it left no focusable group and EKA2L1's window server segfaulted
(E968; a phone allows it). The watcher's samples confirmed the engine's
group focused (E970).
*Harness trap, already in E131 and missed: `xdotool search --name EKA2L1 |
head -1` is Qt's selection-owner window and swallows every key. Use
`--onlyvisible` and `windowactivate`, as `holdtest.sh` does. Three rounds
(E967-E970) tested keys that never left the harness.*

### 13.m The front end quits by itself (E958)
Run alone, 6r66_2.app finds no I3D block, makes one, and being participant
3, not 2, sets the state to 5 and exits through a one-tick timer whose
observer is CEikAppUi::Exit (+0x1b950, +0x50c0). Not a bug: it is only ever
meant to open a block the engine (or launcher) made.

### 13.n A GCC 2.x delete on a ROM object (E957)
The front end deletes its CPeriodic the EKA1 way, vtable slot +8 with 3 in
r1; on an EABI vtable slot +8 is CBase::Extension_, which wrote through r2.
**Fix (shared):** objects returned by `CPeriodic::NewL`, `CBufFlat::NewL`
and the `CDesC8/16ArrayFlat` constructors get a per-class shadow of their
vtable whose slot +8 is an adapter: r1 <= 3 goes to the EABI deleting or
complete destructor, anything else to the real Extension_ (`gate6_shadow`).
For CBase-derived classes the slots past +8 line up between the two ABIs,
so only +8 needs it. Inert on the Asphalts, whose timer is the port's own
(E977).

### 13.o The front end's first stops (E952-E956)
`games/colin2` loads 6r66_2.app with the ordinary loader (`GAME_DIR_CHARS`
6r66: its folder is not its stem). Four stops, each fixed in the shared
layer: `CEikonEnv::CreateBitmapL("*")` -- the application's own store, here
the loader's -- sent to the game's `.mbm` on its drive (E952), on
`CCoeEnv::Static()` rather than the control's old +8 word (E953); the old
iScreen read with N-Gage MGraphicsDeviceMap slots, so `GAME_SCREEN_FONTS 1`
(E954); `Cba()` and `StatusPane()` on an app UI built with
ENoScreenFurniture, answered with one hidden stand-in control whose every
slot does nothing (E955-E956, `gate6_hidden_furniture`).

### 13.p "The throttle does nothing" (E976) -- a misreading, and the controls (E978-E981)
E976 held Up and 5 and read 0-2 MPH off the HUD, so the throttle went down as
unknown. Its shots during the 5 hold show the car carried from the gantry
onto the stage: I read the digits and not the picture. Settled by
measurement rather than more guessing: the engine keeps a 256-byte
scancode -> engine key table (object + 4, filled by 0x4a6654; EKA2L1_WATCH
over all memory narrowed to that PC listed every entry, E978), tracks each
key's down/up state (0x4a6660), and the game script remaps it twice. Then
each candidate held in a stage: **5 throttle, 7 brake and reverse, the
arrows steer, either softkey pauses** (the softkeys are taken before the
table, though the right one maps to W), 1 resumes from the pause menu.
*Lesson: CLAUDE.md's rule-4 list says "a picture is not a measurement";
the converse holds too -- one number read off a picture is not the
picture.*

### 13.q "No sound" -- there was sound all along (E982-E986)
Colin's sound was listed open only because nothing on the bench had listened.
With the paced capture backend (`EKA2L1_AUDIO_CAPTURE_DIR`, used since E675)
the engine's stream is there: 16 kHz mono from 10-13 s on. E982 read it as
broken -- 97% of the energy under 300 Hz, the same every second -- and E983's
GAME_FPA_DOUBLES 0 changed nothing. The capture's transients sit on a 111 ms
grid (sixteenths at about 135 BPM) under pitch-dropping kicks and a bass
line: a bass-heavy track, correctly paced. Throttle on and off in a stage
moves the 1.2-4 kHz band by 3-8x and back (E985): the engine note is mixed
in. The path is the engine's own: a sound thread (0x5009d8), NewL straight
to the 9.x CMdaAudioOutputStream (the port's MDA proxy is never entered,
E984's RDebug trace), Open on an empty package, SetAudioPropertiesL(16000,
mono) in MaoscOpenComplete, a 40 ms CPeriodic feeding it; a PC trace showed
OpenComplete once and BufferCopied 141 times (E986).
*Lessons: "the spectrum is low" is not "the audio is wrong" -- check the
rhythm before the pitch. And a static reading of a vtable (E985 read the
callback table as GCC 2.x and expected a crash) loses to one PC trace.
Every title's captures carry an empty 44.1 kHz stereo `stream00`; it is not
the title's.*

### 13.r Hold C dead on an engine title (E991-E993)
The picker hears C through the wrapper's key handlers; once the engine's own
window group took the focus (13.l) none reached them -- E991, no step, no
cfg. **Fix:** the engine's RWsSession::GetEvent (old ws32 118) is wrapped:
a key event with C's scan code goes through hold_key, and one the hold acted
on becomes EEventNull (`gate6_engine_get_event`). It runs on the engine's
thread, so the clock is the main thread's 100 ms hold timer and the save is
deferred to it (`cfgSavePending`): cfg_save's file handle is the main
thread's. E992: six steps in a 4 s hold, saved; E993: read back at launch.
*Lesson: a fix that moves where input goes moves every feature that listens
for input. The focus hand-over (E969) broke this and nothing said so until
the rule was tested by hand.*

### 13.s QUIT leaves a white screen, then three crashes (E994-E1001)
Yes on QUIT posted 6 in the I3D mailbox and spun for the launcher to clear
it; unanswered, the game hung white. Answered (the launcher part clears 6),
the engine's teardown ran into three bugs, one after another:
1. **Its DSA delete read a null vtable** (0x4a8044, KERN-EXEC 3): the game
   holds the port's old-layout stand-in for the CDirectScreenAccess, which
   had no vtable -- no title had deleted it, the Asphalts leave by
   User::Exit. **Fix (shared):** the stand-in gets a vtable whose every slot
   deletes the real object and forgets it (`gate6_dsa_shadow_delete`).
2. **CMsvSession deleted the GCC 2.x way** (+8 on a ROM CActive:
   CActive::Extension_ writing 0): 13.n's bug on a factory the shadow list
   lacked. **Fix (shared):** the list widened -- CMsvSession::OpenSyncL,
   CWsScreenDevice's constructor, CApaWindowGroupName::NewL (deleted so at
   startup too), CFileMan, CApaCommandLine, the SDP pair -- behind a check
   that the class's slot +8 is CBase's or CActive's Extension_.
3. **The check refused them all** (E999): a DLL's vtable reaches euser's
   Extension_ through its own veneer, `ldr pc, [pc, #-4]`. The check looks
   through one.
Then the engine finishes -- save written, block at state 5, "everyone
exits" -- and **the launcher part ends the process on state 5** (0xE81E);
the 6-then-1 path the engine also has is kept. E1001: a clean exit 0.
*Lesson: a path no run had taken (the game's own exit) held three latent
bugs in shared code. Walk every exit the game offers before a round.*

### 13.t The release had no engine image (E990)
The package shipped `6r66.lxe` only because a bench run had left it in the
game folder; from a card dump it would have shipped none and the phone would
have had nothing to run. **Fix:** build_release.py inflates it from
`<stem>.nax` for an engine title, and refuses a `.lxe` in the tree that is
not that inflation. E990 installed the package with the emulator's installer
on an emptied E: and C: and played.


### 13.u Installed on C:, "Error loading game data" (E1005-E1007)
The E: fiction (the game told it lives on E:, its names put back on the real
drive) covered RFile's Open, Create and Replace, which is how the .app
titles reach their files. Colin's engine reaches its data through estlib --
fopen and wfopen on `E:\system\apps\6r66\*.dz`, mkdir and unlink -- and names
its folder to RFs::SetSessionPath, MkDirAll and Modified. Installed on C:,
every data open failed. **Fix (shared):** those seven calls are translated
too (`gate6_fopen` and the rest; the C-library names get their own buffers,
since the engine calls them on its own thread). E1006 plays from C:, E1007
quits from C:. The Asphalts pick up the fopen and MkDirAll hooks; on E: they
pass through (E1021, E1023).
*Lesson: the bench installs to E:, so a fiction about E: is never tested
there. Install on the other drive before a round.*

### 13.v Back from an app switch: black, and deaf (E1008-E1015, E1019)
Nothing on the bench takes the foreground, so `GAME_BENCH_SWITCH_AT` does what
another application and apparc would: another group with a full-screen window
in front, then the wrapper's group back at ordinal 0 (as
TApaTask::BringToForeground does). Two bugs, one after the other:
1. **The keys went to the other app** (E1009). The wrapper's group declines
   the focus (13.o), so bringing it back left the engine's group behind. **Fix:**
   the launcher part hands back. When the wrapper's group arrives at ordinal
   0 and the focus is not the engine's, the engine's group goes to the front
   (SetWindowGroupOrdinalPosition, as the N-Gage launcher and the engine's
   own 0x49fb78 do). It fires once per arrival: OrdinalPosition counts only
   among groups of the same priority (WINBASE.CPP), so a high-priority note
   can leave the wrapper reading 0.
2. **The picture stayed black** (E1010-E1014). The engine drew on, but its
   DSA region had emptied when covered, and the bench's window server sent
   no abort or restart, so the region never came back. **Fix:** the hand-back
   sets a kick, and the engine's next post (on its own thread) cancels the
   DSA if active (CActive::Cancel, euser 1088), starts it, and reads the
   region afresh (`dsa_kick`). E1015 and E1019 come back to the menu, and
   the keys work.
On a phone the abort and restart do come, and the engine's own Restart
calls StartL (E1016, by a bench knob that runs both on the engine's thread:
`GAME_BENCH_DSA_ABORT_AT`). E1013's reading that it would not was wrong.
*Lesson: when the bench cannot take the foreground, ask the window server to
do it from inside the process (as with 13.k's view deactivation).*

### 13.w The kick and the game's Restart: two StartLs on one DSA (E1017-E1018)
On a phone the hand-back's kick can land between ws32's RunL (the abort
acknowledged) and the CIdle that runs the game's Restart. The Restart's StartL
then finds the DSA running. wserv answers a Request for a running session
with **EWservPanicDirectMisuse** (nonnga Direct.CPP), so the game dies.
`GAME_BENCH_DSA_ABORT_KICK` reproduces that order; on the bench the engine
then called StartL on every frame (E1017). **Fix (shared):** `gate6_dsa_startl`
answers a StartL on an active DSA without a second Request (0x57A2, "G6D").
E1018 is clean. The other titles never take the guard (E1020-E1023).
*Lesson: a fix that starts something behind the game's back must ask what
the game will start next. Round 141 was the same question.*

### 13.x Build 002 dies at launch on the N95: `g6eng KERN-EXEC 0`, then stuck, then `G6FLT 32212` (round 161, E1031-E1046)
**Symptom.** The engine thread starts and is gone before it draws: the block
stays at state 1, no frame is posted, the screen hangs as if loading, and End
gives a fault on the main thread's way out. Thirty bench runs had passed.
**Cause.** Two kinds of handle a phone refuses and EKA2L1 lets through (root
CLAUDE.md: the emulator returns an error where EKA2 panics KERN-EXEC 0):
1. **The main thread's handles on the engine's thread.** The engine asks for
   its screen on its own thread, and the port's answer (the first-time branch
   of the screen query) took the status pane down, asked Avkon for the main
   pane and read the picture-mode file -- `RLibrary::Lookup` on the main
   thread's avkon handle, and `RFile::Open` through its file-server session.
   On the bench the lookups simply failed (so the pane was never hidden and
   the inset was the default 56); on the N95 the first one killed the thread.
2. **`RSystemAgent` built on the stack and closed** (engine 0x4a6b60). 9.x has
   no System Agent and the shim answered its whole library with a no-op, so
   the constructor never zeroed the handle; the word was whatever the stack
   held -- a clock value after the first key -- and `RHandleBase::Close` gave
   it to the kernel. Hidden behind the first: it fires at the first key.
**Fix.** (1) `engine_start` takes the pane down, asks Avkon and reads the
cfg on the main thread before the engine's thread exists; `status_pane_off`,
`avkon_inset` and `cfg_read` refuse to run on any other thread, and the
screen query applies the cached answers (the inset is now Avkon's 48).
(2) `RSystemAgent::RSystemAgent()` (sysagt 18) is answered as RHandleBase's
constructor, `iHandle = 0` (`LOCAL_HANDLE_CTOR`, shared: Colin's front end
and Ashen import it too), so the Close is the null-handle no-op.
**Found and judged with `EKA2L1_STRICTHANDLE`:** =1 named the handles (E1031),
=2 reproduced the phone exactly (E1032), and the fixed build is clean under =2
through start, keys, a drive, QUIT, hold C and its read-back, and an app switch
(E1036-E1041), and from the zip on fresh drives (E1046). `emurun.sh` now runs
every bench with =1 and prints the count.
*Lesson: the bench's leniency is a known gap, written in the root CLAUDE.md,
and a probe for it existed. A build goes out only after a run under
`EKA2L1_STRICTHANDLE=2`.*

### 13.y Build 003 on the N95: no keys, drawn over every app, End fatal (round 162, E1047-E1057)
**Symptom.** The menu draws and no key works; switching to another app leaves
the game's picture on top of everything; End leaves it up until about a
demo's start, then the main thread dies at rounds 101-102's frame (pc
0x807344c6). Every lenient bench run had played.
**Cause.** Two, the first hiding everything else:
1. **An EKA1 one-word TRequestStatus spilling into the next field.** The
   engine keeps its window-server event status at object +0x50 and its own
   stop flag at +0x54. EKA2's `EventReady` sets the status pending through
   `TRequestStatus::operator=`, which also sets ERequestPending in the second
   word (+0x54); the kernel's completion writes the status word only
   (`DThread::RequestComplete`, sizeof(TInt)), so the flag stays 2 and the
   engine's wait loop (0x4a7308) returns at once: it never reads an event,
   never runs an active object (the DSA's abort is never answered, so wserv
   gives up and the engine draws over everything), never sees End. EKA2L1
   cleared the bit on every completion, so the bench worked.
2. **End answered by nobody.** The engine-mode app UI took CAknAppUi's
   HandleCommandL, which ignores EEikCmdExit; Avkon's shutter came later and
   died in cone, as in rounds 101-102.
**Fix.** (1) `gate6_engine_event_ready` wraps the engine's EventReady and
puts the word after the status back as it was. (2) The engine-mode app UI's
HandleCommandL is `gate6_ui_command`: EEikCmdExit flushes and leaves (0xE818).
**Found and judged with `EKA2L1_KERNREQ=1`,** a new emulator switch that
completes requests as EKA2 does: it reproduced the dead keys (E1047), the fix
cleared them (E1048), `GAME_BENCH_ENDKEY_AT` sent Avkon's own close event to
show End ignored (E1049) and then answered (E1050), the battery is clean under
it and STRICTHANDLE=2 (E1051-E1054), One and Asphalt 2 -- phone-proven -- run
under it unchanged (E1055-E1056), and build 004 from the zip layout (E1057).
`emurun.sh` now runs every bench with it.
*Lesson: E885 found this class (EKA1's one-word TRequestStatus) in the
engine's active objects and patched them; a raw, polled status was the same
bug where no CActive was looking. And an emulator that is kinder than the
kernel hides it: when the bench and the phone disagree, find what the
emulator does that the platform source does not.*

### 13.z Build 004 on the N95: the sound stutters, and closing from the task switcher dies (round 163, E1058-E1068)
**Stutter.** The engine's feeder (0x50157c) is One's writer over again
(round 132): it writes `clamp(target - (written - Position), min, max)` with
one buffer in flight at its 40 ms timer -- target 94 ms, at most 47 ms a write,
12 ms at least (E1060-E1061, by register trace and `EKA2L1_WATCH`). Against the
N95's `Position`, which reports nearer the speaker than the N-Gage's did, a
94 ms cushion that refills only 1.18x faster than it plays leaves gaps.
**Fix:** `GAME_MDA_POSITION_LEAD_US 150000`, the knob One got for the same
writer (the engine's stream does pass through the port's MDA proxy: E1062,
where E984 had read the opposite). The bench shows no gap added and the music
clean where the run without it dropped out (E1063-E1065); the size is a guess
against One's 100 ms.
**Death.** A system shutdown event (`EApaSystemEventShutdown`, the task
switcher's close) reached `CAknAppUi::HandleSystemEventL`, which runs Avkon's
app shutter; its RunL died in cone before reaching HandleCommandL -- rounds
101-102's frame, pc 0x807344c6. **Fix:** `gate6_engine_sysevent` answers the
event itself: flush and leave (0xE81F). `GAME_BENCH_SHUTDOWN_AT` sends the
event as apparc does; the bench's shutter survives (E1066), the answer comes
first (E1067).
*Lessons: when a title reports what another one already had, read that
title's entry first -- One's knob was the fix. And a reading in the record
(E984: "the engine never goes through the proxy") is a measurement of its day;
re-measure before building on it.*
