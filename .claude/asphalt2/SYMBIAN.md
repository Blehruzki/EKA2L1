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
