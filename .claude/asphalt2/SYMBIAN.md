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

Round 96 showed a *second* backgrounding path in which slots 10, 21 and 24 do
not fire at all and the game cancels its own frame timer instead. Both end in
KERN-EXEC 0. Do not assume which one a given report is.

## Direct screen access

From the window server's own `Direct.CPP` and the SDK's DSA example:

- The server panics a client for DSA misuse (`EWservPanicDirectMisuse`) — a
  WSERV panic, not KERN-EXEC.
- Drawing after an abort and before the restart "will cause a temporary
  deadlock (since the client will be waiting for WSERV to make the requested
  window rearrangement, and WSERV will be waiting for the client to
  acknowledge that the DSA has aborted)". A hang, not a panic.
- `Restart` updates the clipping region and the example calls `StartL()` from
  inside it.
- `CWsDirectScreenAccess`'s region-sync timer is `KRegionSyncTimeoutMicrosec`
  = 0.1 s and only clears the frozen region. It does not kill the client.

## Sources

Cloned by `toolchain/port/getsources.sh`:

- `SymbianSource/oss.FCL.sf.mw.classicui` — cone, uikon, Avkon
- `SymbianSource/oss.FCL.sf.os.kernelhwsrv` — euser and the kernel
- `SymbianSource/oss.FCL.sf.os.graphics` — the window server

Documentation: the S60 3rd/5th edition C++ developer library (system panic
reference, class reference) and the Symbian Developer Library, both mirrored
at `docs.huihoo.com` and `devlib.symbian.slions.net`.
