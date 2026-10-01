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
