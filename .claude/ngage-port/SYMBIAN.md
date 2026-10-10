# Symbian, from the sources — not from memory

Everything here was read out of Symbian's published source or its SDK
documentation, with the file or page named. Nothing in this file is inferred
from EKA2L1, and nothing is recalled. **Check here before asserting anything
about how the platform behaves**, and add to it rather than re-deriving.

`toolchain/port/getsources.sh` clones the repositories. They are not in this
repository (they are large and not ours), and a container that has lost them
can get them back in a few minutes.

## Panic codes

| panic | meaning | source |
|---|---|---|
| **KERN-EXEC 0** | "the Kernel cannot find an object in the object index for the current process or current thread using the specified object index number (the raw handle number)". A handle that was never opened, was closed, or belongs to another thread. The reference's own example: `RLibrary::Lookup()` panics with it if not preceded by a successful `RLibrary::Load()`. | SDK, *System panic reference*, KERN-EXEC |
| **KERN-EXEC 3** | an unhandled exception — usually an access violation | same |
| **KERN-EXEC 44** | a bad message handle, or `RHandleBase::iHandle` corrupted | same |

A device **panics** on an unresolvable handle. It does not return an error.
EKA2L1 returns `KErrBadHandle`, or nothing at all from the void executive
calls — see the emulator's CLAUDE.md.

## Request completion across threads

`User::RequestComplete` signals **the current thread, always**. From
`kernel/eka/euser/us_func.cpp`:

```cpp
EXPORT_C void User::RequestComplete(TRequestStatus * &aStatus,TInt aReason)
	{
	*aStatus=KRequestPending;
	RThread().RequestComplete(aStatus,aReason);
	}
```

`RThread()` default-constructs to `KCurrentThreadHandle`, so this can never
raise KERN-EXEC 0 and can never signal another thread. The documentation says
the same: "This function is used to complete an asynchronous request
originating in the same thread as the code that is currently executing. If a
request originates in another thread, then executing code must use
`RThread::RequestComplete()`." Both games import `User::RequestComplete` and
neither imports `RThread::RequestComplete`.

## The app UI vtable

`CCoeAppUi : public CBase`, so an EABI vtable is two destructors and
`CBase::Extension_`, then each class's virtuals in declaration order:
`CCoeAppUi` (`cone/inc/COEAUI.H`), then `CEikAppUi`
(`uikon/coreinc/EIKAPPUI.H`), then `CAknAppUi`.

| slot | method |
|---|---|
| 0, 1 | destructors (complete, deleting) |
| 2 | `CBase::Extension_` |
| 3 | `InputCapabilities` |
| **4** | **`HandleWsEventL`** — once per window-server event, so it dominates any count |
| 5 | `PrepareToExit` |
| 6 | `HandleScreenDeviceChangedL` |
| 7 | `HandleKeyEventL` |
| **8** | **`HandleForegroundEventL(TBool)`** |
| 9 | `HandleSwitchOnEventL` |
| **10** | **`HandleSystemEventL(const TWsEvent&)`** |
| 11 | `HandleApplicationSpecificEventL` |
| 12 | `SetAndDrawFocus` |
| 13 | `HelpContextL` |
| 14 | `FrameworkCallsRendezvous` |
| 15 | `CCoeAppUi_Reserved_2` |
| **16** | **`CEikAppUi::ConstructL`** |
| 17 | `HandleModelChangeL` |
| 18, 19 | `ProcessCommandParametersL` (two overloads) |
| 20 | `ApplicationRect` |
| **21** | **`StopDisplayingMenuBar`** |
| 22 | `SetFadedL` |
| 23 | `ReportResourceChangedToAppL` |
| **24** | **`HandleCommandL(TInt)`** |
| 25 | `ProcessMessageL` |
| 26 | `OpenFileL` |
| 27 | `CreateFileL` |
| 28 | `HandleError` |
| 29 | `HandleResourceChangeL` |
| 30 | `Exit` |
| 31 | `MopSupplyObject` |
| 32 | `MopNext` |
| 33 | `ValidFileType` |
| 34, 35 | `Reserved_3`, `Reserved_4` |

**The check:** the port has had `SLOT_UI_CONSTRUCT = 16` since gate 5, found
empirically, and the header's declaration order puts `CEikAppUi::ConstructL`
there independently. Slot 4's count (63, 173 — once per ws event) and slot
8's (twice when backgrounded, once when not) agree as well.

`CActive` for comparison: 0, 1 destructors, 2 `Extension_`, **3 `DoCancel`**,
4 `RunL`, 5 `RunError` — which the port already encodes as `NEW_DOCANCEL = 3`.

## Cancelling an active object

From `kernel/eka/euser/cbase/ub_act.cpp` and `ub_tim.cpp`:

```cpp
EXPORT_C void CActive::Cancel()
	{
	if (iStatus.iFlags & TRequestStatus::EActive)
		{
		DoCancel();
		User::WaitForRequest(iStatus);
		iStatus.iFlags &= ~(TRequestStatus::EActive | TRequestStatus::ERequestPending);
		}
	}

EXPORT_C void CTimer::DoCancel()
	{
	iTimer.Cancel();
	}
```

Two things follow and the port got both wrong before reading this.

**`DoCancel` must make the outstanding request complete**, because `Cancel`
then waits for it. A `DoCancel` that does not is a thread blocked forever in
`User::WaitForRequest`.

**`CTimer::DoCancel` is an executive call on an RTimer handle.** A `CTimer`
whose `ConstructL` never ran has no such timer -- `iTimer.CreateLocal()` is
in `ConstructL`, not the constructor -- so its handle is zero, and an EKA1
object's handle word holds whatever that layout left there. Either way the
kernel refuses it: **KERN-EXEC 0**.

This port substitutes a 9.x `CTimer` for the game's 7.0s one by running the
constructor over allocated memory and driving the request from the game's own
completions. For such an object the right `DoCancel` is neither the game's
nor the real one: it is to complete the request, which is what the contract
above actually asks for.

**And "complete the request" means *if it is still outstanding*, not always.**
That took a second reading, and build 015 shipped the first one.

`TRequestStatus` is two words (`e32cmn.h`): `iStatus` then `iFlags`, whose
bit 0 is `EActive` and bit 1 `ERequestPending`. `CActive` is `CBase` (a vptr)
then `iStatus`, so in a `CActive` the status word is at **+4** and the flags
at **+8**. `TRequestStatus::operator=` (`e32cmn.inl`) sets `ERequestPending`
when `KRequestPending` is assigned and clears it for anything else, so the
status word alone says whether a completion is outstanding.
`KRequestPending` is `-KMaxTInt` (`e32const.h`).

```cpp
// us_func.cpp
EXPORT_C void User::RequestComplete(TRequestStatus*& aStatus, TInt aReason)
	{
	*aStatus = KRequestPending;
	RThread().RequestComplete(aStatus, aReason);
	}

// us_exec.cpp -- note that it always takes one signal off the semaphore
EXPORT_C void User::WaitForRequest(TRequestStatus& aStatus)
	{
	TInt i = -1;
	do { i++; Exec::WaitForAnyRequest(); } while (aStatus == KRequestPending);
	if (i) Exec::RequestSignal(i);
	}
```

`RThread::RequestComplete` (`epoc/arm/uc_exec.cia`) swaps the caller's pointer
to NULL, stores the reason through the old one, and **signals the thread's
request semaphore**. So each completion puts one signal on the semaphore and
each `WaitForRequest` takes exactly one off.

That is the whole accounting, and it decides what `DoCancel` may do. If the
request is still `KRequestPending`, nothing has signalled and `DoCancel` must
complete it or the wait never returns. If it has already been completed --
which for this port is the normal state between frames, because the game arms
its timer with `SetActive()` followed immediately by its own
`User::RequestComplete` -- a signal is already waiting and `DoCancel` must do
nothing: completing again leaves a second signal that no active object will
claim. `CActive::SetActive`'s own documentation names that case:

> E32USER-CBase 46 panics may occur if an active object is set active but no
> request is made on its TRequestStatus, or vice versa. [...] This panic is
> termed a 'stray event'.

`SetActive` also panics **E32USER-CBase 42** if the object is already active
and **49** if it was never added to a scheduler.

## How EKA2 delivers an exception to a user-side handler

From `kernel/eka/kernel/arm/ckernel.cpp` (`Exc::Dispatch`) and
`kernel/eka/euser/epoc/arm/uc_exe.cia` (`_E32Startup`):

The kernel does **not** call the handler installed with
`User::SetExceptionHandler`. For a user-mode fault in a thread whose handler
covers it, it pushes a frame on the *user* stack (`PushExcInfoOnUserStack`,
low address first) --

    word 0  TExcType         word 3  iFaultStatus     word 5..20  r0 .. r15
    word 1  iExcCode         word 4  iCpsr            (18 sp, 19 lr, 20 pc)
    word 2  iFaultAddress

-- then sets `r15 = iReentryPoint` (the process's E32 entry point), `r4 =
KModuleEntryReasonException` (4, `u32std.h`), clears the Thumb bit, and
returns to user mode. eexe's `_E32Startup` compares r4 against 4 and calls
`User::HandleException(sp)`, which runs the installed handler. If the thread
faults again with `KThreadFlagLastChance` set, or has no handler, the kernel
panics it **KERN-EXEC 3** (`K::PanicKernExec(ECausedException)`).

So a hand-written entry point that dispatches on r4 == 0 only never runs its
handler, however correctly it was installed: the exception arrives as a
thread start with the fault frame where `SStdEpocThreadCreateInfo` should be.
This port's `_start` did exactly that until build 017, and every KERN-EXEC 3
it has ever reported went through it. `User::ExceptionHandler()` returns the
installed pointer, which is how the entry point finds its way back to the
handler without any writable data of its own.

`TExcType` (`e32const.h`): `EExcGeneral` 0, `EExcAccessViolation` 9,
`EExcDataAbort` 23. `iExcCode`: 0 prefetch abort, 1 data abort, 2 undefined
instruction. The mask bits for `SetExceptionHandler`: `KExceptionFault`
0x10 is the one a bad pointer raises. The emulator (EKA2L1) delivers only
software exceptions (`User::RaiseException`) to a handler, calling it
directly with the type in r0; a bad memory access there never reaches user
code, so the phone path can be reviewed but not run on the bench.

**Exec numbers are the ROM's, not the source tree's.** The RM-409 euser's
stubs (disassembled) use `svc 0x5A` for `SetExceptionHandler`, `0x59` for
`ExceptionHandler` and `0x54` for `ResetInactivityTime`; the kernel test
`e32test/system/execinfo.cpp` in the published (later) source numbers them
0x80, 0x7F and 0x48. Go through euser's exports, never a raw `svc`.

## Window-server events on losing the foreground

From `classicui/lafagnosticuifoundation/cone/src/COEAUI.CPP` and
`COEMAIN.CPP`: `CCoeEnv::RunL` reads one `TWsEvent` and calls
`iAppUi->HandleWsEventL(event, control)` (app UI vtable slot 4). For
`EEventFocusLost` (10) / `EEventFocusGained` (11) that calls
`HandleForegroundEventL(aForeground)` (slot 8) and then `SetFocus` on the
top focusable control, which is the control's `FocusChanged`. `TWsEvent` is
`TInt iType; TUint iHandle; TTime iTime; TUint8 iEventData[]` (`W32STD.H`),
so the type is the first word of the argument. Avkon's own
`CAknAppUi::HandleWsEventL` (`AknAppUi.cpp`) swallows
`KAknFullOrPartialForegroundLost/Gained` before anything else runs.

## The active scheduler runs the highest-priority ready object, always

`CActiveScheduler::DoRunL` (euser/cbase/ub_act.cpp) walks `iActiveQ`, a
`TPriQue` ordered by `CActive::iLink.iPriority`, and runs the first object
that is active with a completed request. It never rotates: a lower-priority
object that is ready stays ready for as long as any higher-priority object
keeps completing. `EPriorityIdle` is -100, `EPriorityLow` -20,
`EPriorityStandard` 0, `EPriorityUserInput` 10, `EPriorityHigh` 20
(e32base.h).

S60 brings an application up in stages, and the last ones are idle-priority
active objects queued during construction to run "once the app is idle". A
game whose frame timer re-completes every frame at priority 0 starves them
for the whole run; they run for the first time when the game pauses --
on losing the foreground -- against a state minutes older than the one they
were queued for. Round 102 measured exactly this on an N95: one object,
ready at the startup walk, ready a hundred frames later at FocusLost, ready
at ForegroundLost, dispatched at the fault. The port's remedy is to give its
frame-timer wrapper a priority below `EPriorityIdle`, so the frame loop runs
exactly when nothing else is ready, which is what a well-behaved
application's does.

The queue can be read from inside the process: `CActiveScheduler::Current()`,
`iActiveQ` at +8 (head next, head prev, `iOffset` which reads 12 =
`CActive::iLink`), each link's object at link - 12, with `iStatus` at +4 and
`iFlags` at +8 (bit 0 active, bit 1 request pending). An object with bit 0
set and `iStatus != KRequestPending` is ready; the one being run has bit 0
just cleared.

## A view deactivation reaches the app UI through a mixin, and a constructor sets its vtable

What S60 does to an application that loses the foreground, beyond
`EEventFocusLost`: the view server deactivates the application's view.
`CVwsSessionWrapper` (viewcli.dll) receives the event on an active object
and calls `MVwsSessionWrapperObserver::HandleViewEventL`; cone's
`CCoeViewManager::HandleViewEventL` (coevwman.cpp) turns `EVwsDeactivateView`
into `DoDeactivation`, which calls every registered
`MCoeViewDeactivationObserver::HandleViewDeactivation`. `CAknAppUi::ConstructL`
(AknAppUi.cpp) registers the app UI itself, and `CAknAppUi::HandleViewDeactivation`
is one line: `iAvkonEnv->CloseAllIntermediateStates()`, which deletes and
re-creates a `CIdle` and closes any menus or popups registered with `CAknEnv`
(nothing in the tree but aknenv.cpp registers any).

The observer pointer cone stores is the mixin subobject, `this + k`. A
mixin with nothing but a vtable pointer is four bytes, laid out after the
primary base in declaration order; `class CAknAppUi : public CAknAppUiBase,
MEikStatusPaneObserver, public MCoeViewDeactivationObserver` puts the status
pane observer at k - 4 and the view deactivation observer at k. **Only the
class's own constructor writes those vtable pointers.** avkon exports
`CAknAppUiBase`'s constructor (217) and `vtable for CAknAppUi` (3820) but not
`CAknAppUi`'s constructor, so an object assembled from those two has the
primary vtable and none of CAknAppUi's mixins: cone's first deactivation
reads a zero as a vtable, which is a data abort on address 0 with the
observer in r0, pc in cone and lr in euser's `RPointerArrayBase::At`.

The offset k is in avkon itself: the export table lists `non-virtual thunk
to CAknAppUi::HandleViewDeactivation`, and a non-virtual thunk is
`subs r0, #k` followed by a branch to the method (on the RM-409 ROM, def
4030 is `subs r0, #0x5c; b ...`). Def indices for the other thunks (4021,
3826) land on `bx lr` on that ROM, so they are not usable; the status pane
observer is placed by the declaration order above. A deactivation can be
driven from inside the process with `CCoeAppUi::DeactivateActiveViewL`
(cone 224): the view server sends the event back through the same path, on
the emulator too.

## Avkon shuts applications down through HandleCommandL

`CAknAppUi::HandleSystemEventL`, from `uifw/AvKon/src/AknAppUi.cpp`:

```cpp
case EApaSystemEventShutdown:
    ...
case EApaSystemEventSecureShutdown:
    StopDisplayingPopupToolbar();
    CAknEnv::RunAppShutter();
    break;
```

and the shutter, from `uifw/AvKon/src/aknshut.cpp`:

```cpp
appUi->StopDisplayingMenuBar();
appUi->HandleCommandL(EEikCmdExit);
```

So **slot 10 then slot 21 then slot 24 is the system telling an application
to quit** — two consecutive calls in the source, two consecutive slots in the
log. When the app does not close, the shutter reports to
`ROomMonitorSession`: this is the low-memory mechanism, and a fat background
process is what it is for.

**The end (red) key is the same command by another road.** `CAknAppUi::HandleWsEventL`
(`AknAppUi.cpp`):

```cpp
case KAknUidValueEndKeyCloseEvent:
    {
    CEikonEnv* env = iEikonEnv;
    if ( env && !env->IsSystem() ) // System apps are not closed
        {
        // Close or hide the application.
        TWsEvent event;
        event.SetType( KAknShutOrHideApp );
        event.SetTimeNow();
        CAknAppUiBase::HandleWsEventL( event, aDestination );
        }
    break;
    }
```

In the log: `WS EVENT type 101f87f0` (the close event's UID), then app UI
slots 11 and 10, 21, 24 — `HandleCommandL(0x100)`. The game's own handler
runs from there (round 139: One's dies in it; `gate6_ui_command` now answers
the command itself).

## Direct screen access: who starts it

`CDirectScreenAccess::StartL` is the client's call; the server never makes it.
After an `AbortNow` the object is inactive until the client's own `StartL`,
and a client that starts it on a condition of its own (One's frame function,
`0x44fa0`: start only when the object reads inactive, draw nothing when it
reads active and its own flag is clear) sees exactly two states on a device.
The port's `StartL` on the game's behalf (rounds 60-140, 0x57A7) made a third
-- active, on an empty region, pending -- and round 141 found the game's
pause menu skipped whenever the return landed in it. Measured on the N95
(round 141): the frame buffer is one buffer, `UserSvr::ScreenInfo` and
`HALData::EDisplayMemoryAddress` (78) both 0xcb400000, the address never
moving; the window server draws into it while the game is away (the task
switcher is in the dump) and nothing but the client's blits touch it after
a return (the band checksum, 0x5C0E).

## The audio stream: what Stop delivers, and what running dry does

Measured, not read: on the N95 `CMdaAudioOutputStream::Stop` takes 49-53 ms
and delivers `MaoscPlayComplete(KErrCancel)` *inside* the call, on the
calling thread (ngtest, round 135); a stream whose writer stops writing
plays on and never reports `KErrUnderflow` (same round). On the emulator
the same stream reports `MaoscPlayComplete(KErrUnderflow)` by itself about
ten records after the writer's last buffer (E743, the sound thread's log,
721-723). A game whose screen flow waits for the complete -- One's intro
does -- therefore behaves on the bench as if every Stop were answered, and
on the device only when the Stop is really made or the complete really
sent (round 143).

Round 96 showed a *second* backgrounding path in which slots 10, 21 and 24 do
not fire at all and the game cancels its own frame timer instead. Both end in
KERN-EXEC 0. Do not assume which one a given report is.

## Direct screen access

From `windowing/windowserver/nonnga/SERVER/Direct.CPP` (the server side) and
`CLIENT/RDirect.CPP`:

A client that calls `CDirectScreenAccess::StartL` is given its window's
visible region (`DrawingRegion()`): on the N95, the whole 240x320 for an app
whose status pane is gone, `(0,58)-(240,320)` for one whose pane is still
visible. Drawing outside it is drawing on windows wserv believes it owns.

When the screen changes hands wserv runs `AbortNow` on the server object:
it signals the client (`ETerminateRegion` for a window change, a global
reason otherwise), copies the client's visible region into `iFrozenRegion`,
and waits **0.4 s** for the client's acknowledgement -- which the client
library gives from `CDirectScreenAccess::RunL`, calling the application's
`MDirectScreenAccess::AbortNow` and then, through a `CIdle`, `Restart`. If
the acknowledgement does not come, wserv `Abort()`s the session and carries
on; nothing reboots. Either way the frozen region ends with
`CancelFrozenRegion` -> `Screen()->ScheduleRegionUpdate(&iFrozenRegion)`:
**wserv repaints the region the DSA client was drawing in.** A session the
client cancelled itself (`Cancel` while running: `Terminate1`, `Terminate2`,
`ETerminateCancel`) gets no such repaint; `CorrectScreen` -- an `Invalidate`
of the region -- runs only for a session that was aborted by timeout and
then cancelled.

So an application that writes the frame buffer must keep its DSA session
for as long as it writes: that is what makes wserv aware of the region, stop
the client before it redraws it, and repaint it afterwards. The port
released its session on the first frame from round 60 to build 020, and the
last frame it wrote stayed on the panel after every switch, under whatever
wserv did not know it had to repaint.

**The restart comes at once, with a smaller region.** `CDirectScreenAccess::RunL`
calls the application's `AbortNow`, acknowledges (`Completed()`), and
starts a `CIdle` whose callback is the application's `Restart` -- not when
the window comes back to the front, but as soon as the scheduler is idle,
seconds before the user returns. An application whose `Restart` calls
`StartL` again (the documented pattern) is granted the window's visible
region *as it is now*: empty while another application covers it, part of
it while a popup or the task list does. The abort that brings the window
back is the same mechanism in reverse: the region changes
(`CWsWindow::PossibleVisibilityChangedEvent` -> `IsAbortRequired`,
WINDOW.CPP), the session is aborted and restarted, and the new region is
the whole window again. So a client must clip every write to
`DrawingRegion()` and draw nothing for an empty one; a client that draws
the whole window on every restart paints over the menu the moment its
window is sent behind it. The region is an `RRegion` in screen coordinates:
`iCount`, `iError`, `iAllocedRects`, `iGranularity`, then the pointer to
its `TRect`s.

**A second StartL on a running session kills the client.** `StartL` makes a
new `RDirectScreenAccess::Request` and `SetActive` every time (nonnga
`CLIENT/RDirect.CPP`); the server's `CWsDirectScreenAccess::Request`
(`SERVER/Direct.CPP`) accepts a session only when it is idle or completed,
and otherwise panics the client `EWservPanicDirectMisuse`. So nothing may start
a DSA that its owner's `Restart` will start again: the restart runs from a
`CIdle` after `RunL`, and a start that lands in between is the second
(E1017; the port's guard is `gate6_dsa_startl`).

**`OrdinalPosition` counts only among equals.** A window's
`OrdinalPosition()` skips siblings of higher ordinal priority
(`CWsWindowBase::OrdinalPosition(EFalse)`, `SERVER/WINBASE.CPP`); the
`FullOrdinalPosition` variant counts them. So a window group reading 0 is not
necessarily in front: a high-priority group (a global note, an alert) can be
over it.

**A request's completion writes the status word only.** `DThread::RequestComplete`
(kernel/eka/kernel/sthread.cpp) copies `sizeof(TInt)` to the client's
`TRequestStatus`; the second word, `iFlags`, whose `ERequestPending` bit
`TRequestStatus::operator=(KRequestPending)` sets on the client side
(e32cmn.inl), is cleared only by the active scheduler as it dispatches
(cbase/ub_act.cpp). So an EKA1 binary with a one-word status followed by a
field of its own finds that field ORed with 2 after every request, for good
(round 162; EKA2L1 cleared it on completion until `EKA2L1_KERNREQ=1`).

## Leaves, TRAP and the trap handler on 9.x

From `kernel/eka/euser/us_trp.cpp`, `us_exec.cpp`, `cbase/ub_cln.cpp`,
`include/e32cmn.h`, `e32std.h`, `e32base.h`:

- `User::Leave(aReason)` is `Exec::LeaveStart(); pH = GetTrapHandler();
  if (pH) pH->Leave(aReason); throw XLeaveException(aReason);`. `TRAP` is
  `try { MarkCleanupStack(); body; UnMarkCleanupStack(); } catch
  (XLeaveException& l) { r = l.GetReason(); }`. `GetReason()` is
  `Exec::LeaveEnd(); return iR;` -- the kernel counts threads mid-leave to
  defer code-segment unloads, so every LeaveStart wants a LeaveEnd.
- `TTrapHandler` is `{ vptr }` with virtuals `Trap()`, `UnTrap()`,
  `Leave(TInt)` in that order and no virtual destructor. `MarkCleanupStack`
  calls `Trap()`, `UnMarkCleanupStack` calls `UnTrap()`. The handler is per
  thread (`User::SetTrapHandler`, `User::TrapHandler`).
- The default handler, `TCleanupTrapHandler`, is `{ vptr; CCleanup*
  iCleanup; }`; `Trap()` is `iCleanup->NextLevel()`, `UnTrap()`
  `PreviousLevel()`, `Leave()` `PopAndDestroyAll()`. **`CleanupStack::PushL`
  and friends cast whatever handler is installed to this class and read
  `iCleanup` at offset 4** (`cleanup()` in ub_cln.cpp): any replacement
  handler must keep that word.
- `TTrap::Trap(TInt&)` does not exist in a `__LEAVE_EQUALS_THROW__` build
  (uc_trp.cia); the class is kept for layout: `iState[16]`, `iNext`,
  `iResult`, `iHandler`. EKA1's `Trap` set `aResult = KErrNone` on the first
  pass and returned 0; a leave returned into it with a nonzero result.
- An exception no catch can take (the unwinder fails on a frame without
  tables) reaches `std::terminate`, which on this platform ends in
  `User::RaiseException(EExcGeneral)` -> `User::HandleException(&type)` ->
  the thread's exception handler, called with the `TExcType` alone. That
  is how an uncaught leave shows up as a `TExcType 0` with no frame.

## VA_LIST, and the vtable of a class with two polymorphic bases

From `kernel/eka/include/e32def.h`, `kernel/eka/euser/us_des.cpp`,
`graphicsdeviceinterface/gdi/inc/GDI.H`, `windowing/windowserver/inc/W32STD.H`:

- `VA_LIST` is `typedef TInt8 *VA_LIST[1]` unless the compiler's headers
  define `__VA_LIST_defined` first, which the EABI builds (RVCT, GCCE) do:
  there it is the compiler's `va_list`, one pointer passed by value. A
  GCC98r2 caller passes its one-element array as the array's address, so a
  9.x `TDes16::FormatList(fmt, VA_LIST)` called from old code receives the
  address of the word that holds the va pointer, and reads the caller's
  stack as the arguments (round 116, E435). Variadic functions such as
  `TDes16::Format(fmt, ...)` are unaffected: both ABIs leave the trailing
  arguments in r2, r3 and on the stack. The old array's one element is
  what 9.x wants.
- `%s` (us_des.cpp, `case 's'`) takes one pointer word from the list and
  reads a zero-terminated string of the descriptor's character width at it;
  `%S` takes a `TDesC*`. Neither reads a `{text, length}` pair.
- `class CGraphicsDevice : public CBase, public MGraphicsDeviceMap`: two
  polymorphic bases, so an object has two vptrs, CBase's at +0 and
  MGraphicsDeviceMap's at +4. In the EABI (Itanium) layout a virtual that a
  derived class overrides from the non-primary base gets a slot of its own
  in the primary table as well, appended in the derived class's declaration
  order after everything inherited through the primary chain. For
  `CWsScreenDevice : public CBitmapDevice, public MWsClientClass` that is:
  0-2 CBase, 3-12 CGraphicsDevice's ten (DisplayMode .. GetPalette), 13-20
  CBitmapDevice's eight (GetPixel, GetScanLine, AddFile, RemoveFile,
  GetNearestFontInPixels, GetNearestFontToDesignHeightInPixels,
  GetNearestFontToMaxHeightInPixels, FontHeightInPixels), then 21-24 the
  twips/pixel conversions, 25 GetNearestFontInTwips, 26
  GetNearestFontToDesignHeightInTwips, 27 GetNearestFontToMaxHeightInTwips,
  28 ReleaseFont. The table at +4 is MGraphicsDeviceMap's own: 0-1 its
  destructor, 2-5 the conversions, 6-8 the three twips getters, 9
  ReleaseFont, each entry a thunk that expects the +4 subobject as `this`.
  A slot counted on paper is confirmed by what comes back: the font's
  `HeightInPixels` after the call (E437, E438).
- `MWsClientClass` (W32STD.H) is `{ TInt32 iWsHandle; RWsBuffer* iBuffer; }`,
  handle first, and it is the base of every window-server handle class:
  `RWsSession`, `RWindowGroup`, `RWindow`, `RDirectScreenAccess`. A window
  object's second word is therefore the session's buffer, the same pointer
  the session itself holds, which is how a window can be told from any
  other word in a control without knowing the control's layout (round 120,
  E451 having compared the first word and matched nothing). Private
  layouts measured on one ROM are measurements of that ROM: an N91 (S60
  3.0) keeps `CCoeControl::iWin` elsewhere than 0x28 and gives
  `CDirectScreenAccess` one more word before its window reference than
  3.1 and 3.2 do.

## File server, streams and structure returns (One, round 125)

- **`RFs::SetDefaultPath` panics the caller on 9.x**: `FSInsecCli` 1, by
  design (`userlibandfileserver/fileserver/sfsrv/cl_insecure.cpp`). The
  per-session replacement is `RFs::SetSessionPath` (efsrv eabi @ 44). A new
  session's path starts at the process's private directory on the system
  drive, so a drive-less name resolves there unless the path is set.
- **`TEntry` and `TVolumeInfo` grew.** 9.x `TEntry` adds `iSizeHigh` and a
  reserved word (8 bytes); `TVolumeInfo` adds to `TDriveInfo` and appends the
  cache fields (`f32file.h`). An EKA1 caller's stack buffer is too small.
- **`MStreamBuf`'s virtuals keep their declaration order** but EABI drops the
  two GCC98r2 header words: old offset `+0x28` (slot 8, `DoSeekL`) is EABI
  `+0x20` (`s32buf.h`).
- **A `TPtrC` returned by value** (8 bytes) comes back in r0:r1 under GCC98r2
  and through a hidden result pointer in r0 under EABI, `this` in r1 and the
  arguments after it: `TParseBase::Name()` and friends, `TDesC::Left/Right/Mid`.
- **`CActive::Cancel`** calls `DoCancel` and then `User::WaitForRequest(iStatus)`
  (`euser/cbase/ub_act.cpp`): a request that can no longer complete -- its
  timer handle closed -- leaves the thread in that wait for good.
- **`CActive::SetActive`** panics `E32USER-CBase 42` (`EReqAlreadyActive`,
  `e32panic.h`) on an object already active.

## Handles a phone refuses and EKA2L1 lets through (One, round 125)

**A library handle belongs to the thread that loaded it.** `RLibrary::Load`
ends in `RLoader::LoadLibrary`, which sets `info.iOwnerType=EOwnerThread`
before it asks the loader (`kernel/eka/euser/us_ksvr.cpp`, line 5201). So
`RLibrary::Lookup` from another thread through that handle is an unknown
handle: KERN-EXEC 0 on a device. The port's own handles are loaded on the
main thread, so a hook that can run on a worker must use a function address
resolved there, never the handle (E567).

**`TRequestStatus` is eight bytes on EKA2**: `TInt iStatus; TUint iFlags;`,
with `EActive = 1` and `ERequestPending = 2` (`kernel/eka/include/e32cmn.h`,
around line 2119). Its `operator=` writes both: `KRequestPending` sets bit 1
of `iFlags`, anything else clears it (`kernel/eka/include/e32cmn.inl`, line
2684). EKA1's was four bytes, so old code that keeps something right after a
`TRequestStatus` on its stack has that word rewritten by 9.x euser (E568).

**The bench.** EKA2L1 answers an unknown handle with an error code, not a
panic. `EKA2L1_STRICTHANDLE=1` logs each one with the guest PC and LR,
`=2` panics the thread as a device would. Run a new title under `=2` before
its first hardware round.

## The N-Gage SDK itself (round 133)

`github.com/razvang-dev/Nokia-N-Gage-SDK-Toolchain` (cloned to the scratchpad,
not this repository: the SDK is Nokia's) carries the Series 60 6.1 SDK the
N-Gage titles were built with -- the headers, the ARMI import libraries, and
the period compiler, GCC `2.9-psion-98r2` (`arm-epoc-pe-g++`), runnable on this
Linux host. Three things follow, each checked:

- **The old ordinals, by name.** Each ARMI `.lib` member holds its ordinal in
  `.idata$5` (`0x8000nnnn`) beside the mangled name; read out, they are the
  N-Gage export tables -- cone 317, euser 1,679, eikcore 290, ws32 357, fbscli
  155, efsrv 231, avkon 2,274 -- where EKA2L1's `epoc6.def` lists names
  without ordinals and falls short (cone 309, euser 1,646). Every hard-coded old
  ordinal in `gen_shim.py`'s HOOKS that was spot-checked names what the port
  says it does (euser 746, 858, 1122, 954, 1454, 1613; cone 8, 223, 226; bitgdi
  23, 105, 137; efsrv 18, 142, 168; avkon 63; apparc 3, 13).
- **The old layouts, compiled rather than inferred.** A probe built with the
  period compiler (`#define private public`, offsets into a static array, read
  from the `-S` output) gives: CActive 24 bytes, iStatus at 4;
  CDirectScreenAccess 96 bytes, iGc 0x18, iScreenDevice 0x1c, iDrawingRegion
  0x20 (the OLD_DSA_* the port measured from ROMs); TMdaAudioDataSettings 44
  bytes, iSampleRate 0x1c, iChannels 0x20.
- **The audio API as the games saw it.** `mdaaudiooutputstream.h` declares the
  stream's virtuals in 9.x's order (SetAudioPropertiesL first), and
  `mda/common/audio.h` the enums: 0x100 is 16000 Hz, 0x02000000 mono. One's Open
  package (E664) is a TMdaAudioDataSettings with iSampleRate and iChannels 0.

Use it before reading a layout or an ordinal off a game's code.

## Timeslices: who gets a fresh one (round 133)

`kernelhwsrv/kernel/eka/nkern/sched.cpp`: `TimesliceTick` counts the running
thread's `iTime` down and asks for a reschedule at zero; `RotateReadyList`
moves the thread to the back of its priority's list and gives it a fresh
`iTimeslice`; `TScheduler::Remove` -- a thread blocking -- gives it a fresh one
"for next time". Nothing else does. A thread preempted by a higher priority
keeps what it had left and stays at the head of its list, so two busy
threads of one priority alternate at the slice (`EDefaultUserTimeSliceMs`,
20 ms, `kern_priv.h`) however often something above them wakes. EKA2L1 gave a
fresh slice on every preemption, so they did not alternate at all; patched in
`thread_scheduler::switch_context` (`E684-E685`).

## A thread's heap dies with it (round 134)

`CreateThreadHeap` (`common/heap_hybrid.cpp`) gives a thread with heap sizes
its own `$HEAP` chunk; the first allocator a thread switches to is recorded
as `DThread::iCreatedAllocator` (`ExecHandler::HeapSwitch`, sexec.cpp); and
when the thread dies `DThread::CloseCreatedHeap` (kernel/sthread.cpp) drops
one reference (`RUserAllocator::Close`) and, at the last, closes every handle
in the heap's handle list -- the chunk goes, and anything left in it is an
unmapped page. A heap shared with `RThread::Create(..., RAllocator*)` was
`Open`ed by the new thread (`UserHeap::SetupThreadHeap`, up_utl.cpp), so it
survives. Anything one thread allocates for others to use belongs on a heap
that outlives it.

## The N95's audio stream, measured (ngtest, rounds 134-135)

From `CMdaAudioOutputStream` on the N95, 16 kHz mono, 1,600-sample buffers,
build 002 (round 135) with every phase three or two times over:

- Open completes in 40-62 ms; the first `MaoscBufferCopied` comes ~93 ms
  after the first `WriteL` (the bench: 0-22 ms).
- Fed as fast as it copies, the stream keeps **5,760-6,400 samples
  (360-400 ms)** between what was written and what `Position` reports
  played, taking its first buffers almost at once (copy #5 at ~230 ms). The
  bench keeps 1,600.
- `Stop` takes 49-53 ms and calls `MaoscPlayComplete(KErrCancel)` from inside
  itself; `Position` reads 0 afterwards. A worker thread's `Stop` takes
  29-35 ms **whether the main thread is idle or busy** across it.
- **The stream never reports `KErrUnderflow`.** Restarted by `WriteL` after a
  Stop and starved for five seconds (three times), or opened fresh, given one
  buffer and starved (twice): no `MaoscPlayComplete` at all. EKA2L1's stream
  patch reports -10 after 500 ms of starvation and stops the stream
  (`KWaitBufferTimeInMicroseconds`, src/patch/mediaclientaudiostream).
- One or two streams playing cost the main thread nothing measurable.
- Threads of one priority share the CPU: a spinner made ~2.46 M per 100 ms
  in every sample while the main thread kept ~60% of its solo rate.

## Thread priorities, and the Bluetooth security 9.x moved (Colin, round 166)

**A relative priority becomes an absolute one through a table**
(`kernel/eka/kernel/sthread.cpp`, `ThreadPriorityTable`, indexed by process
priority and thread priority). In a **foreground** process: MuchLess 10,
Less 11, Normal 12, More 13, MuchMore 14, **RealTime 22**. The system server
rows run Normal at 21 and More at 24 (23 with
`SYMBIAN_CURB_SYSTEMSERVER_PRIORITIES`); a High process (sysap's class) runs
Normal at 19. Anything above 24 needs ProtServ and is capped without it, so
22 is allowed to any application -- and is above every application thread and
above sysap. An application thread at EPriorityRealTime that stops blocking
takes the phone with it, power key included.

**`User::RequestComplete` touches the word after the status.** It is
`*aStatus = KRequestPending; RThread().RequestComplete(aStatus, aReason);`
(`euser/us_func.cpp`), and the assignment is `TRequestStatus::operator=`,
which sets ERequestPending in the second word. `RThread::RequestComplete` is a
bare executive call (`epoc/arm/uc_exec.cia`; on RM-409, euser 1790: ldrex/strex
of the pointer, then the call), and the kernel writes the status word only.
So for a one-word EKA1 status with something of its own after it, complete
through `RThread::RequestComplete` on `KCurrentThreadHandle`.

**The highest an application thread may go, and ending yourself.** An
absolute priority passes `RThread::SetPriority` unchecked up to
EPriorityAbsoluteHigh (500, absolute 23, kern_priv.h); only the
EPriorityAbsoluteRealTime values need ProtServ (sexec.cpp,
`ExecHandler::ThreadSetPriority`). A relative priority above 24 is capped
without it (sthread.cpp). `RProcess::Kill` on a process of the same security
zone -- itself -- needs no capability (server.cpp, `ExecHandler::ProcessKill`;
PowerMgmt only for another's). On RM-409: euser 1786 SetPriority, 1320 Kill.

**Capabilities are checked by the phone's kernel and servers, not by the
emulator.** The exe header's TCapabilitySet (offset 0x88, two words; bit n is
`TCapability` n, e32capability.h: NetworkServices 13, LocalServices 14) is
what every protected call is checked against at run time, whatever the
installer accepted. Bluetooth sockets (esock on BTLinkManager, L2CAP, RFCOMM)
need LocalServices -- the SDK's documentation, not yet a measurement here
(round 168 is the test). The port's loader declared none until build 010.

**Signing, as the N95 enforces it** (rounds 168-169). An unsigned package
installs while no exe in it declares a capability; one that declares
LocalServices is refused, "Required application access not granted". A
self-signed package may carry the user-grantable capabilities (the SWI
policy's user set: LocalServices, NetworkServices, ReadUserData,
WriteUserData, Location, UserEnvironment) -- the SDK's account, to be
confirmed by build 011. The SISController checksums are not checked: every
package before build 011 carried stale ones and installed. Signature layout:
Ensymble 0.29 `sisfile.py` (RSA/SHA-1 over the controller's contents, the
chain between the install block and the data index).

**Before 9.3 the bitmap heap lock is global** (fbs docs, "Heap Locking in
the Font and Bitmap Server"; the 9.3+ source in symbiansource keeps
`LockHeap` only as `BeginDataAccess` and says why). Large bitmaps lived in one
global heap the Font and Bitmap Server defragmented, moving their data; the
server waited on a global mutex before anything that could trigger that
(allocating a large bitmap, from any process, could), and `LockHeap` /
`UnlockHeap` -- and `TBitmapUtil::Begin` / `End`, which call them -- were
Wait and Signal on it. So on S60 3.0 and 3.1 (9.1, 9.2) a thread between
Begin and End holds up every bitmap user in the phone, the window server
included. 9.3+ (S60 3.2 and later) has no such lock.

**Why the N95 refused LocalServices (round 171, from the installer's own
source, oss.fcl.sf.mw.appinstall).** The signed range is what mksis signs:
`CSecurityManager::SignedSize` sums Info, Options, Languages,
Prerequisites, Properties, [Logo] and InstallBlock, each header + length +
padding, and `CSignatureVerifier` checks RSA PKCS#1 v1.5 over SHA-1 with a
DigestInfo -- openssl's own format. And "Required application access not
granted" is `EUiCapabilitiesCannotBeGranted`, raised by
`installmachine.cpp` only when a package asks for a system capability, or
for a user capability while `AllowGrantUserCapabilities` is false; unsigned
and self-signed packages are one trust class
(`ESisPackageUnsignedOrSelfSigned`). The reference policy
(`swiconfig/swi/swipolicy.ini`) is `AllowUnsigned = false`,
`AllowGrantUserCapabilities = true`, user capabilities NetworkServices
LocalServices ReadUserData WriteUserData Location UserEnvironment. The
user's N95 installs unsigned packages, so its policy is not the reference
one, and it refused LocalServices unsigned and self-signed alike: it grants
no user capability (or not that one) to an uncertified package. A stock
phone does the opposite -- refuses every unsigned package, grants
LocalServices to a self-signed one.

**Bluetooth sockets need LocalServices** (oss.fcl.sf.os.bt: the Bluetooth
SAP checks it for every protocol under it -- avctpsap.cpp says so --
and SDP's net database checks `KLOCAL_SERVICES`, sdpnetdb.cpp). There is no
Bluetooth path on S60v3 for an executable without it.

**Bluetooth socket option numbers moved between 6.1 and 9.x** (round 171).
N-Gage SDK `caseinc/BT_SOCK.H`: `KSolBtLM` 0x1011, `enum TBTLMOptions
{ KLMGetACLLinkCount, KLMGetACLLinkArray }` and `enum TBTLMIoctls
{ KLMDisconnectACLIoctl, KLMSetPacketTypeIoctl, KLMWaitForSCONotificationIoctl }`.
9.x `bluetoothclientlib/inc/lmoptions.h`: `TBTLMOptions` opens with
`ELMOutboundACLSize, ELMInboundACLSize, KLMGetACLHandle, KLMGetACLLinkCount,
KLMGetACLLinkArray, ...` -- the level kept its number and the names their
meaning, but every value moved. An N-Gage binary's numbers have to be
translated, not forwarded.

**9.x's btmanclient has no `RBTSecuritySettings`.** Its 84 exports (RM-409,
EKA2L1's `epoc9.def`) carry RBTMan, the registry and comm-port settings;
`TBTServiceSecurity` lives in bluetooth.dll on 9.x, and a listener's security
is given with `TBTSockAddr::SetSecurity(TBTServiceSecurity const&)` before the
bind. An N-Gage title that registers security through `RBTSecuritySettings`
has nothing to call; its listener keeps 9.x's default security.

## SDP on 9.x: the builder's slots, and RSdpDatabase's size (Colin, round 172)

- **`MSdpElementBuilder`'s twelve virtuals are in the same order on 6.1 and
  9.x** -- BuildUnknownL, BuildNilL, BuildUintL, BuildIntL, BuildUUIDL,
  BuildBooleanL, BuildStringL, BuildDESL, BuildDEAL, StartListL, EndListL,
  BuildURLL (oss.fcl.sf.os.bt, `btsdp/inc/btsdp.h`; MSEB_ExtensionInterfaceL
  is not virtual). Only the vtable layout differs: GCC 2.x puts slot k at
  +8+4k, EABI at +4k. `CSdpAttrValueList` is `CSdpAttrValue` (a CBase, vptr
  only) then the builder mixin, so the builder is at +4 on both.
  `StartListL` returns this, `BuildDESL` a new child list (NewDESL(this),
  appended), `EndListL` the parent passed to NewDESL (`SDPAttrValue.cpp`).
  9.x's own code calls none of these slots on a list it builds: the record
  reaches the database through `CAttrEncoderVisitor`, the primary vtable.
- **`RSdpDatabase` on the RM-409 ROM is 0x18 bytes**, measured by
  disassembly: the constructor (sdpdatabase 41) writes a vtable at +0, zero
  at +4 and +8 (the subsession, SubSessionHandle read at +8) and at +0x14;
  `UpdateAttributeL(.., CSdpAttrValue&)` (33) and CreateServiceRecordL's
  helper (37) `delete iBuffer` at +0x14 and store the new one there; Close
  (40) frees it. A 6.1 caller's RSdpDatabase is 0x10 bytes (Colin's engine:
  +8 to +0x18 of its SDP object, its own fields after). `RSdp` is one word on
  both (ordinal 141 writes +0 only).
- **The SDP server checks LocalServices** (sdpnetdb's `KLOCAL_SERVICES`); on
  the bench it is the ROM's real server, so a package without the capability
  gets -46 from it there as on a phone (E1106).

## Sources

Cloned by `toolchain/port/getsources.sh`:

- `SymbianSource/oss.FCL.sf.mw.classicui` — cone, uikon, Avkon
- `SymbianSource/oss.FCL.sf.os.kernelhwsrv` — euser and the kernel
- `SymbianSource/oss.FCL.sf.os.graphics` — the window server

Documentation: the S60 3rd/5th edition C++ developer library (system panic
reference, class reference) and the Symbian Developer Library, both mirrored
at `docs.huihoo.com` and `devlib.symbian.slions.net`.
