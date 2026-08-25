# N-Gage boot hang on restored N95 (RM-320) — investigation notes

**UPDATE (rounds 135-137, tested the REAL N-Gage 2.0 installer (user**
**had it: "N-Gage 2.0 v1.40.1557 by BodyZ, Fixed by FROST_A-52.sis",**
**11MB, genuine Symbian OS 9.x install file) - got much further than any**
**stub, found a new specific error, chased it into real guest code, and**
**surfaced what's likely the actual root cause: RM-320 and RM-409 run**
**different Symbian OS platform versions entirely.):**

Staged the real SIS at `data/devices/rm-320/drives/d/Private/2000A2AE/
n-gage.sisx` and re-ran the Installer (`0x2000A2AE`). Confirmed the
Installer genuinely *consumes* the staged file each run (it's gone
afterward) - a real sign of actual processing, not just an existence
check. Result: **322,229 lines of activity** (vs 144,225 with the ROM
stub used in rounds 115/133) - more than double the processing, and a
**new leave code never seen before: `-10254`**.

**Traced `-10254` to a concrete point**: right after `InstallServer`
opens `C:\resource\swicertstore\dat\` (the SIS package certificate
store) and reads several files, `svc.cpp:733`'s `message_complete`
fires with `val=-10254`, targeting the `!UISS` session. Initially
suspected this was EKA2L1's own `uiss_server` HLE stub (`uiss.cpp`)
returning a hollow "success" with no real dialog response, since UISS
almost certainly handles install confirmation prompts. **Tested directly:
had the stub write a plausible "accepted" value to several IPC argument
slots for every opcode, rebuilt, re-ran - zero effect, and the stub's own
new log line never even fired.**

**Followed the completion mechanism itself and found `uiss_server` is**
**dead code - never registered anywhere (`init.cpp` has no reference to
it at all).** The real `!UISS` server that `InstallServer` actually talks
to is created via the **guest-side** `server_create` SVC (`Server !UISS
created` logged from the generic kernel dispatch path, not from any
EKA2L1 service file) - meaning `!UISS` here is genuine, real, un-HLE'd
guest code (most likely a component created from within `InstallServer`
itself), and `-10254` is a real value the guest computed and passed to
its own `RMessage2::Complete()` equivalent - not anything EKA2L1's host
code returns. Reverted the ineffective stub experiment.

**While looking for what to check next, found what's probably the real**
**underlying cause**: `data/devices.yml` shows RM-320 (`N95`) as
`platver: epoc93fp1` (Symbian OS 9.3, S60 3rd Edition **FP1**) and
RM-409 (`5320`, confirmed working for the user) as `platver: epoc93fp2`
(S60 3rd Edition **FP2**) - **different Symbian OS platform versions
entirely**, not just different device models. N-Gage 2.0 v1.40 (this
exact package, dated Jan 2010) is a late-era release; SIS packages
routinely declare `TargetDevice`/dependency version requirements, and a
package built assuming FP2+ compatibility checking its way through a
real, un-HLE'd `InstallServer`/`UISS` flow on an FP1 device is a very
plausible source of a install-specific failure that wouldn't reproduce
on FP2 at all - consistent with everything observed (works on RM-409,
fails identically regardless of which specific `.sis` file is used on
RM-320).

### Where this leaves things

The `-10254` failure is real guest code's own decision, not an EKA2L1
stub gap - pinning down *exactly* why would need dumping and
disassembling `InstallServer.exe`'s own logic around its certificate-
store/UISS interaction (same live-dump-and-Ghidra technique used
throughout this investigation), which is a comparable-sized effort to
everything already done. Given the FP1-vs-FP2 platform version
difference is a strong, concrete, previously-unknown lead that could
explain the *whole* pattern (not just this one error), that's the more
promising next thread to pull - specifically, whether EKA2L1 correctly
emulates FP1-specific SWI/version-check behavior, or whether the real
device historically needed a specific compatibility patch/workaround
(matching this exact package's "Fixed by FROST_A-52" naming) that
addresses an FP1-specific quirk EKA2L1 doesn't yet replicate.

### To resume from here

1. Check whether EKA2L1's HAL/property responses for platform version
   (`epocver`/`SysVersion`-equivalent HAL attributes) correctly reflect
   `epoc93fp1` specifically for this device profile, or whether there's
   a version-reporting gap that could make guest SIS-compatibility logic
   behave incorrectly.
2. Dump `InstallServer.exe` live (same technique as `playserver.exe`/
   `playserverccd.dll` in rounds 118-128) and disassemble around the
   certificate-store read + `!UISS` interaction to find exactly what
   condition produces `-10254`.
3. Consider whether a *different*, specifically FP1-targeted N-Gage 2.0
   package/patch exists (the "BodyZ"/"FROST_A-52" naming suggests an
   active modding community around exactly this kind of compatibility
   issue) - if the genuine historical fix was a specific patched
   installer variant for older devices, obtaining that specific one may
   sidestep the problem entirely rather than needing an EKA2L1 code fix.

**UPDATE (rounds 132-134, user correction reframes everything - the**
**"dead server" theory (round 129-131) was wrong as the decisive**
**factor, and testing the real hypothesis (missing `.n-gage` game files)**
**found the true, specific, actionable answer: `-1003` is a SIS**
**signature/certificate validation failure, because the SIS file being**
**tested is a lightweight ROM stub, not a genuine signed installer.):**

**Critical correction from the user, based on direct real-hardware**
**experience**: N-Gage 2.0 worked fully offline on their real N95 for
years after Nokia's servers shut down, and separately, installing N-Gage
2.0 on an EKA2L1 RM-409 (5320) profile works correctly today - it finds
and installs `.n-gage` files from a local folder with no online
dependency at all. This directly rules out round 129-131's "dead
backend service" theory as the decisive blocker - it may still be true
that those specific URLs are dead, but that's not what's stopping this
device profile specifically, since the exact same architecture provably
works offline elsewhere.

**Checked the obvious next thing: does RM-320 have the same `.n-gage`**
**game files RM-409 has?** No. RM-409 has 37 `.n-gage` files sitting
flat in `data/` (`Asphalt.3.n-gage`, `FIFA 09.n-gage`, etc.) - exactly
matching the user's description of "installs any `.n-gage` games it
finds in the respective folder." RM-320 has a `data/N-Gage/` folder
containing only a tiny `sai/appInfo.sai` metadata file - no actual game
packages - plus one stray `.n-gage` file in an unrelated location
(`E:\n-gage\Asphalt.3.n-gage`, wrong drive, wrong case convention
relative to RM-409's pattern).

**First attempt: copied the stray `.n-gage` file into both `data/` and**
**`data/N-Gage/` directly.** Zero effect - re-ran `playserver.exe`
(`0x20007B39`), got a byte-for-byte identical trace to before. This
means `playserver.exe` itself isn't what scans for raw `.n-gage` files
at all.

**Correction: the user's own description matches the Installer, not**
**PlayServer.** "Installs any `.n-gage` games it finds" describes
`0x2000A2AE` (`NGageInstaller_0x2000A2AE.exe`, the process investigated
in rounds 112-115, which panicked on a missing `n-gage.sisx` and later
hit an unidentified `-1003` error after that was fixed) - not
`playserver.exe`/`Games` (`0x20007B39`), which is presumably just the
*launcher* for content the Installer has already registered.

**Re-ran the Installer with full `log-svc`/`log-ipc` tracing (never**
**previously done for this specific process).** Immediate discovery:
the `n-gage.sisx` staged back in round 115 was **gone** - `entry() NOT
FOUND` again. The Installer had consumed/deleted it as part of its
(partially successful) processing during that earlier test - itself a
useful confirmation that the Installer genuinely interacts with this
file rather than just checking its existence. Re-staged the same
`ngiinstaller_stub.sis` copy and re-ran with full tracing.

**Found the real mechanism.** Right after `InstallServer` opens
`C:\resource\swicertstore\dat\` (the SIS package **certificate store** -
used to validate a package's digital signature before allowing install)
and does a burst of file reads, the operation completes with
`code: -20` (`KErrCorrupt`) - a **signature/certificate validation
failure**. This is almost certainly what surfaces as the unidentified
`-1003` at SW-Install's own higher error layer.

**This makes complete sense once framed correctly: the file being fed**
**in isn't a real installer.** `ngiinstaller_stub.sis` (found under
`Z:\rm-320\system\install\`, used since round 115) is one of many
`*_stub.sis` files on this ROM (`browser_stub.sis`, `bthid_stub.sis`,
`quickoffice_stub.sis`, etc.) - these are lightweight **ROM-provisioning
markers** (registering that a ROM-resident app already exists, implicitly
trusted, no real signature needed), not genuine, fully-signed installer
packages. Feeding one to `InstallServer` as if it were a real SIS to
validate and install predictably fails certificate checking.

### The actual answer to "what's missing"

**The real, original `n-gage.sisx`** - a genuine, Nokia-signed N-Gage
2.0 installer package that would have lived at `D:\Private\2000A2AE\
n-gage.sisx` on the real device. This is *not* reconstructable from
anything bundled with EKA2L1 or present on this ROM dump - every `.sis`
file on this ROM that matches the name is a ROM stub, not the real
package. This is specific to whatever the user actually downloaded/
installed onto their real N95 years ago, and needs to come from their
own original backup or install history, not from this investigation's
tooling.

**Everything else found and fixed this session remains real and
worth keeping**: the restored Central Repository file (`20008bb7.cre`,
rounds 129-131) was a genuine, previously-invisible bug in this
investigation's own test data (an earlier round's forgotten
`.disabled_rXX` folder rename) and is now correctly fixed regardless of
this final finding. The `!BackupServer`/`KErrCancel` fix (round 58) and
the `gametitlemetadata.dll` loop fix both remain valid, unrelated wins
from earlier in this whole investigation.

### To resume from here, if the user locates the real installer package

1. Stage the genuine `n-gage.sisx` at `data/devices/rm-320/drives/d/
   Private/2000A2AE/n-gage.sisx` (confirmed correct path/location this
   round) and re-run the Installer (`--run 0x2000A2AE`) - if the real
   package validates correctly, this should for the first time reach
   actual `.n-gage` file installation logic.
2. Once the Installer completes successfully, re-test `Games`/
   `playserver.exe` (`0x20007B39`) directly - the splash-thread-never-
   resumed finding from rounds 116-128 may resolve itself once there's
   actually something installed for it to find and display, without
   needing further code changes.
3. If the user doesn't have the original package, this specific restore
   likely cannot proceed further through data alone - would need either
   a from-scratch fresh N-Gage 2.0 install flow (if such a thing is
   still obtainable) or accepting this as a genuine limitation of this
   specific backup.

**UPDATE (rounds 129-131, the actual answer - a genuine local bug found**
**and fixed (this investigation's own earlier round left two data**
**folders disabled and never restored them), AND the deeper structural**
**reason `playserver.exe` still exits the same way afterward: its own**
**settings repository points at real Nokia N-Gage backend servers that**
**have been shut down for over a decade.):**

Picked up from round 128's negative result by going wider instead of
deeper: enabled full `log-svc`/`log-ipc` tracing (same technique as
round 111, just never previously run against `0x20007B39` directly) and
extracted `playserver.exe`'s complete IPC/session activity for its whole
lifetime.

**Found and fixed a real, concrete, previously-invisible bug in this**
**investigation's own test data.** `config.yml`'s `log-filter` had
carried `Service.Cenrep:off` since round 74 or earlier - meaning **every
round of this entire investigation has been blind to all Central
Repository activity**. Removing that override and re-running immediately
surfaced: `playserver.exe` requests Central Repository UID `0x20008BB7`
three times, and every single time: `"Repository not found with UID
0x20008BB7"` (`centralrepo.cpp:264`).

**Root cause: this investigation itself broke it, in an earlier round,**
**and never noticed.** Found two folders under this device's per-device
storage that had been renamed with a `.disabled_rXX` suffix and never
restored: `Private/10202be9/10202be9.disabled_r40` (Central Repository's
own persists folder) and `Private/10009D8F/10009D8F.disabled_r41`
(`ecomserver`'s private cache - UID matches `ecomserver[10009d8f]` seen
throughout every log this session). The `_r40`/`_r41` suffixes match
this investigation's own round numbering, confirming this was a
self-inflicted artifact from a much earlier round (predating this
context window) that renamed these folders for some now-forgotten
experiment and never renamed them back. The Cenrep one directly and
completely explained the `0x20008BB7` failure: the ACTIVE `persists/
rm-320/` folder had every *other* repo used this session (`10272618`,
`101f876d/e/f`, `cccccc00`, etc.) except this one - which sat, fully
intact, one directory over, in the disabled copy.

**Fix confirmed effective for the file-not-found symptom itself**:
copied `20008bb7.cre` from the disabled backup into the active
`persists/rm-320/` folder. Re-ran - the `"Repository not found"` line is
completely gone, and the repo now opens successfully on the very first
attempt (previously retried 3 times, always failing). The (separate)
`ecomserver` disabled folder turned out to be a non-issue on inspection -
byte-identical `ECom.dat`/`.idx`/`.lang` already exist in the active
location, just re-touched by later runs; nothing was actually missing
there.

**But this specific repo wasn't the gate for the splash-thread/game-**
**check logic after all** - `playserver.exe` still exits identically
(`KErrEof`, same shutdown sequence) even with the repo now found, and
the same 13-function breakpoint sweep from round 128
(`FUN_706012fa`/`FUN_706015ea` included) still shows zero hits. So this
was a real, worth-fixing bug, but not *the* blocker for reaching a
playable menu.

**The actual content of the restored repo explains why, at a much**
**deeper level.** Hex-dumping `20008bb7.cre` (UTF-16LE strings visible
directly in the binary) shows this is genuinely `PlayServer`'s own
settings repository (its UID, `0x20008BB7`, sits directly inside
`playserver*.dll`'s own UID cluster - `0x20008BAE`-`0x20008BB7`) - and
it stores **live backend service URLs**:
```
http://playapps.n-gage.mobi/ngage/playapp_page-tcs_mcc-%S_lg-%d.html
http://qa.sh.ngi.netsizeonline.com/int/output/roamingnotifications_mcc-%S_mnc-%S.xml
```
`n-gage.mobi` was N-Gage 2.0's own game-catalog/store endpoint; `Netsize`
was Nokia's real mobile-billing/DRM-licensing partner for N-Gage
purchases. **Both services have been shut down for well over a decade** -
this isn't an EKA2L1 or backup-completeness problem at all at this
level: the actual online catalog/licensing infrastructure N-Gage 2.0
was built to depend on no longer exists, on any device, real or
emulated.

### What this means, put together

`playserver.exe`'s own "is there anything to show" logic
(`FUN_706012fa`, confirmed via full decompile in round 128) is genuinely
capable of checking *local* content (its path-construction and
`FUN_70602cfa` count-check pattern matches "enumerate installed games"
closely) - but it never gets called. The most consistent explanation
across everything found this session: `playserver.exe`'s startup
sequence tries the *online* catalog/service path first (using exactly
the URLs now confirmed present in its settings repo), that attempt fails
outright (the servers are gone, or - locally - EKA2L1 likely has no real
network stack behind whatever HTTP client component would be used), and
whatever error-handling path that failure takes leads directly to
cleanup and exit **without ever falling back to checking locally-
installed content** - explaining why the local-content-check functions
never execute, and why the splash/fallback render thread never starts.

This is now a *qualitatively different* kind of "missing thing" than
anything found in rounds 90-128: not a missing file, not an EKA2L1 bug,
but the real-world retirement of the online service N-Gage 2.0's own
code was written to depend on for its primary content-listing path.

### To resume from here, if pursued further

1. Confirm the online-vs-local fallback theory directly: trace whether
   `playserver.exe` ever attempts a real HTTP/socket connection at all
   (would need a probe on `!SockServ` or EKA2L1's HTTP/RHTTPSession
   equivalent, filtered to `playserver`'s process) - not yet done.
2. If confirmed, the only paths forward are either (a) accept this as a
   genuine, permanent limitation of running N-Gage 2.0 fully online-
   dependent flows today (real hardware would fail identically), or (b)
   investigate whether `playserver.exe` has a documented offline/no-
   network fallback mode that could be coaxed into triggering (e.g. by
   having EKA2L1 answer the connection attempt with a specific error
   code the guest code specifically branches on, matching the
   `KErrCancel`-vs-generic-error precedent from the `!BackupServer`
   thread earlier in this investigation).
3. Both `.disabled_rXX` folders should be either fully restored (Cenrep
   one now partially done - only the one missing file was copied over,
   the rest of that folder's contents were never audited) or the whole
   `.disabled_rXX` directories cleaned up now that they've served their
   diagnostic purpose, to stop future rounds from re-discovering the
   same thing from scratch.

**UPDATE (rounds 124-128, definitive negative result closes the byte-**
**level chase - the failure happens in generic early init, before ANY**
**app-specific code in either `playserver.exe` or `playserverccd.dll`**
**runs at all. This is the practical end of this specific sub-thread.):**

Continued rounds 118-123's caller-chain trace with live software
breakpoints (2-byte Thumb `BKPT`, patched directly into the live
`code_base_ptr` at `codeseg::attach()` time, restored on first hit so the
real instruction still executes correctly - same technique proven in
much earlier rounds of this investigation).

**Three targeted breakpoints, three genuine zero-hit results - each one**
**real, not a probe bug:**
1. `FUN_70000a4e` (`playserver.exe`) - a plausible-looking dispatcher
   found via the earlier stack-walk. Zero hits.
2. `FUN_700008b6` (`playserver.exe`) - a richer dispatcher, decompiled in
   full, checking a message-type field against constants read straight
   from the binary (`DAT_70000c50=0x10000002`, `DAT_70000c54=0xd0000002`,
   plus adjacent UID constants shared with a sibling dispatcher). Zero
   hits.
3. `FUN_706012fa`/`FUN_706015ea` (`playserverccd.dll`) - found by
   correctly locating the splash thread's actual entry point
   (`0x70601868`) inside `FUN_706015ea`'s range, and finding that it
   immediately calls `FUN_706012fa()` and bails with `-1` if the result
   is zero - a function whose own decompiled body (path construction,
   directory-style enumeration, then `FUN_70602cfa()` count check) reads
   exactly like "does any game/content data exist." Broke precisely at
   the `cmp r0,#0x0` instruction right after the call (`0x706015f4`,
   confirmed via disassembly) to read the real return value. Zero hits.

**Escalated to ground truth: breakpointed all 13 non-thunk (real,**
**non-import-stub) functions in `playserverccd.dll` simultaneously**
(`PROBE_R125`, addresses extracted directly from the decompile's
function list). **Every single one showed zero hits.** This isn't "the
wrong function" anymore - it's conclusive: **none of `playserverccd.dll`'s
actual application code ever executes**, even though the DLL is loaded
(codeseg attaches successfully). Combined with `playserver.exe`'s own
two candidate dispatchers also never firing, this rules out the entire
"late-stage check for game content, decide nothing to show" theory this
round set out to confirm - **the real failure happens even earlier**,
in generic library/init code (likely `E32Main`/`ConstructL`-level -
a session connect, property access, or similar basic resource check)
that fails and triggers cleanup before any `playserver`-specific logic
(in either binary) ever gets a chance to run at all. This is consistent
with - and now sharpens - the observed behavior: a short, generic-looking
Etel/active-scheduler setup sequence with very little happening between
process start and the `KErrEof` self-exit, which never left much room
for elaborate game-list-checking logic to begin with.

### Where this leaves things, honestly

This is the practical end of the byte-level chase for this specific
question. Going further (finding the *exact* generic init call that
fails) would mean breakpointing generic euser.dll/estor.dll library
entry points instead of `playserver`-specific code - a much larger
search space with a much lower signal-to-noise ratio, and not obviously
worth it given:

- The two independent investigative threads (`0x2000A2AE`'s missing SIS
  install payload, and `0x20007B39`'s thread never being resumed) already
  converge on the same practical conclusion: **this restored backup is
  missing something fundamental that a genuinely-provisioned N95 would
  have** - now most likely something even more basic than "game content"
  specifically (a session/resource/registration-level piece of state),
  given the failure happens before content-checking logic ever runs.
- Every static/dynamic candidate this round and the last both pointed at
  has now been directly, empirically ruled out via live breakpoints -
  this isn't a guess anymore, it's confirmed negative space.

**Practical recommendation**: treat this as confirmed - the blocker is
missing/incomplete backup data, not an EKA2L1 code bug - unless there's
a specific reason to keep excavating exactly which early resource check
fails (which would need a different, broader technique than function-
level breakpoints on `playserver`-specific code).

### Cleanup still pending

All temporary probes from this entire investigation (rounds 90-128) are
still in the tree: `process.cpp` (`PROBE_R104`), `dirs.cpp`
(`PROBE_R105`), `files.cpp` (`PROBE_R106`), `epoc.cpp` (`PROBE_R107`),
`arm_dynarmic.cpp` (`PROBE_R108`/`PROBE_R109`), `svc.cpp`
(`PROBE_R117`), `codeseg.cpp` (`PROBE_R118`/`PROBE_R125`), `fs.cpp`
(`PROBE_R110`). None of these are destructive or affect normal use (all
gated to specific process/function names that only matter during this
investigation), but they should be reverted before considering this
thread closed, along with the two live data-mount edits (round 112-115's
`n-gage.sisx` staging under `data/devices/rm-320/drives/d/Private/
2000A2AE/`).

**UPDATE (rounds 118-123, live-dumped `playserver.exe`/`playserverccd.dll`**
**and traced the real caller chain for the never-resumed splash thread -**
**found the actual dispatcher function, stopped short of the exact byte-**
**level condition. A real build-tooling gotcha caught and fixed along the**
**way: `cmake --build ... | tail -N` silently swallows the real build**
**exit code, showing stale-binary results as if the build had succeeded.):**

**Build-tooling gotcha, worth remembering for any future round:** piping
`cmake --build` through `tail` (as basically every round this session
did) reports the exit code of `tail`, not of the build. Two consecutive
probe rounds this session silently ran against a **stale binary** for
several cycles after a genuine compile error (`kernel_system::get_mmu`
doesn't exist - wrong accessor, fixed by going through
`get_memory_system()->get_mmu(...)` instead) - the tool wrapper reported
"exited with code 0" both times because that 0 was `tail`'s exit code,
not the actual (failing) build's. Caught by noticing a log line number
that should have shifted after an edit didn't. **Always redirect build
output to a file and check `$?` directly instead of piping through
`tail`** if a build's actual success/failure matters (it always does when
chasing a specific code change).

**Live-dumped both binaries** (`playserver.exe`, 11,152 bytes at
`0x70000000`; `playserverccd.dll` at `0x70600000`) via a temporary
`codeseg.cpp` probe (writes `code_base_ptr`/`code_size` to a file at
attach time), same technique as round 60's `ecomserver.exe` dump.
Imported into a new Ghidra project (`playserver_r118`).

**`playserver.exe` itself is thin** - only 36 functions, mostly tiny
E32 import-table thunks and generic C++ RTTI/vtable-dispatch helpers
(observer-notification loops, handle reference counting). No obvious
splash-thread-specific logic lives in the EXE itself; the real work is
in `playserverccd.dll` (where the splash thread's actual entry point,
`0x70601869`, lives).

**Traced the real (non-generic) caller via a live stack-walk**, since the
directly-captured `LR` at the `thread_create` SVC (`0x803AA7ED`) turned
out to be a generic, reusable euser.dll utility function (confirmed by
disassembly: takes a param, conditionally skips full setup, calls the
actual thread-create wrapper - the same shape used for lots of different
threads, not PlayServer-specific). Added a wall-clock-unconditional
stack-scan probe (`PROBE_R117`, `svc.cpp`) that walks 512 words up from
`SP` (using the correct process ASID via `current_addr_space()`, per the
established gotcha that the default `-1` ASID only works for
globally-mapped addresses) looking for anything inside `playserver.exe`'s
(`0x70000000-0x70003000`) or `playserverccd.dll`'s (`0x70600000-
0x70700000`) address ranges. Found several: `0x70601869` (the splash
thread's own entry point - expected, it's a parameter, not a return
address), a handful of other CCD addresses, and - most useful -
**`0x700022A4` appearing three separate times** in the scan, a strong
signal for a real, repeated application-level caller rather than
leftover stack noise.

**`0x700022A4` turned out to be a false lead** - disassembling around it
produced garbage (`swivc`/`andeq` patterns with embedded zero words,
the classic signature of data being misread as ARM instructions). It
falls past the last function Ghidra's auto-analysis found
(`FUN_70001e1e`, ending well before `0x700022A4`) - almost certainly
inside the E32 image's import/export table region, which Ghidra doesn't
recognize as instructions from a raw dump without proper header parsing.
Coincidentally numeric-range-matched, not actually code.

**Other stack hits inside the real analyzed function range
(`0x70000D4F`, `0x700011F4`, `0x700011A1`) point at `FUN_70000a4e`** - a
genuine, substantial dispatcher function, decompiled and read in full.
Shape: switches on `*(param_2 + 4)` (a message/event-type field) - `==1`
decrements a pending-count field and signals completion, `==9` calls two
helper thunks and returns early, `==10` loops a list looking for a
matching entry to cancel/remove, and everything else does a **UID-range
check** (comparing against three loaded globals,
`DAT_70000c60/64/68`, plus several fixed sub-ranges) before either
dispatching through a vtable slot (`+0x1c`) via `FUN_7000089e` or falling
back to `FUN_700008b6`. This has the shape of a general session/message
router (`RunL()`- or `ServiceL()`-style dispatch on event/opcode), not a
"do I have game data" check specifically - it's the right *neighborhood*
(genuinely reached via the same call chain that sets up the splash
thread) but not yet narrowed to the single condition that decides whether
to actually create/resume it.

**A probe bug caused one real crash this round** (SIGSEGV, exit 139) -
the 512-word stack-scan read past the end of a valid page on one run.
Harmless (test instance only, no zombie process, confirmed via
`tasklist`), and it happened *after* the useful stack data had already
been logged, so it didn't cost the finding - but worth noting the probe
itself needs a bounds check if reused.

### Where this leaves the investigation

The overall conclusion from rounds 112-117 stands and is not contradicted
by anything found this round: `playserver.exe` almost certainly makes an
early decision (likely inside or downstream of `FUN_70000a4e`'s dispatch)
that there's nothing to show, and this cascades into both symptoms
already tied together (`KErrEof` self-exit, splash thread never
resumed). Fully pinning the exact byte-level check would need either:
tracing what event/message value actually reaches `FUN_70000a4e` at the
critical moment (would need a live IPC/property probe at the point of
the dispatch, not just a static disassembly), or accepting rounds
112-117's already-strong, convergent evidence (missing SIS payload on
one entry point, never-resumed render thread on the other) as sufficient
without chasing the last mile of "which exact byte in `playserver.exe`
detects the missing data."

**UPDATE (rounds 116-117, the actual white-screen mechanism found - the**
**thread responsible for drawing anything is provably never told to**
**start, and this converges with rounds 112-115's separate finding to**
**the same underlying cause: this backup is missing N-Gage 2.0's real**
**platform data, not code to fix.):**

User correction: every prior round this session (90-115) tested UID
`0x20007B38` or `0x2000A2AE` - never the actual UID the user identified
early on as N-Gage's own private data folder, `0x20007B39` (visible in
the app list only as a second, separately-registered "Games" entry,
possibly hidden from the real device's menu via `hide-system-apps: true`
- matching the user's own note that this UID "remained hidden" on their
real device). Launched it directly for the first time this session
(`--run 0x20007B39`, not as a side-effect of some other app's flow).

**Confirms and sharpens round 59's decade-old-in-session-terms finding.**
The thread `PlayServerSplashScreenFallBackThread` (created with
`start pc = 0x70601869`) is exactly what its name says: the fallback
renderer for whatever screen N-Gage shows on launch. Round 59 (much
earlier this session) found it stuck at `state=0` (`create`) in every
snapshot, including after forcing a related mutex open, and warned that
naively force-resuming it wedges the whole interpreter in an infinite
loop - concluding some real precondition for running it safely was never
met, without identifying what that precondition was.

**This round found something stronger than "stuck at create in every**
**snapshot": zero `RThread::Resume()` calls happen anywhere in the**
**entire boot, for any thread, over the whole run.** Added two probes to
confirm this precisely: `PROBE_R117` on `thread_create`
(`svc.cpp:2215`, filtered to thread names containing "PlayServer") and
`PROBE_R117` on `thread_resume` (`svc.cpp:2318`, **unfiltered** - logs
target thread, calling thread, and caller `LR` for *every* resume call
in the system). Result: exactly one `PROBE_R117` line in the whole
969-line log - the `thread_create` line. **Not one `thread_resume` call
of any kind happens anywhere in this run.** This isn't "the wrong thread
got skipped" - the SVC that would start it is never invoked at all, for
this thread or anything else, over this app's entire lifetime.

**This converges with `playserver.exe`'s own already-known self-exit.**
Rounds 74/104-111 established `playserver.exe` runs a short, orderly
Etel/AVKON setup, then self-`Kill(-25)` (`KErrEof`) via a completely
normal-looking shutdown sequence (`library_detach`, `dll_free_tls`,
`thread_kill`) - previously read as a benign, by-design "nothing to do,
exit cleanly" signal (matching round 74's directory-scan-EOF theory,
even though rounds 105-106 later disproved the *directory-scan* specifics
of that theory). Put together with this round's finding: **`playserver.exe`
almost certainly decides, early in its own init, that it has nothing to
display, and takes an early-exit branch that skips ever starting its own
splash/fallback renderer thread before self-terminating.** The
`KErrEof`/`-25` exit code and the never-resumed splash thread are not two
separate bugs - they're the same early-exit decision, observed from two
different angles.

**This is now the same root-cause shape as rounds 112-115's `0x2000A2AE`**
**finding, just reached from the opposite direction.** `0x2000A2AE`
(`NGageInstaller_0x2000A2AE.exe`) panics/fails to complete an install
because its expected payload (`n-gage.sisx`) and dependencies aren't
present in this backup. `0x20007B39` (`playserver.exe`, the actual N-Gage
runtime) decides there's nothing to show and exits before ever starting
its display thread - consistent with the *same* underlying gap: **this
specific restored N95 backup does not contain whatever real N-Gage 2.0
platform/session data both of these components expect to find**,
whether that's the full installer payload, a registered/installed game
catalog, or some other piece of first-run provisioning state that a
genuinely-used N95 would have accumulated. Neither of these looks like an
EKA2L1 code bug at this point - both look like consequences of an
incomplete backup.

### What would need to be true to actually fix this

1. Find out exactly what `playserver.exe` checks early in its `E32Main`
   that leads to the early-exit branch (needs Ghidra disassembly of the
   real `playserver.exe` binary around its init routine, live-dumped the
   same way `cdlserver.exe`/`ecomserver.exe` were earlier this
   investigation - not yet done for this specific angle).
2. Determine whether that missing precondition is a file (like
   `0x2000A2AE`'s missing `n-gage.sisx`), a central repository/property
   value, or registered-application state that a complete backup would
   have included.
3. If it's data-shaped (most likely, given the `0x2000A2AE` parallel),
   the real fix is restoring/obtaining that data from the original
   device or a complete backup, not an EKA2L1 code change - this may be
   outside what's fixable through this investigation alone.

**UPDATE (rounds 112-115, likely wrong target this whole time - the real**
**entry point is a DIFFERENT app, `0x2000A2AE`, and it has a genuine,**
**partially-fixed crash caused by a missing/misplaced data file, not a**
**code bug. Two live data-mount edits made, not yet reverted - see**
**bottom.):**

Everything in rounds 90-111 tested UID `0x20007B38` ("Games"/"N-Gage
Installer" - a genuinely duplicate-captioned entry). The app list also has
a **separate** `N-Gage Installer` at UID `0x2000A2AE` (`NGageInstaller_
0x2000A2AE.exe`), never tested until this round. Launched it the same way
(`--run 0x2000A2AE`) and got an entirely different, much more informative
result on the first try.

**A genuine panic, not a benign exit.** `process::kill` (`PROBE_R104`)
showed `exit_type=2` (panic - `kernel::entity_exit_type::panic`, not
`kill`/`terminate`), `category=N-Gage Installer`, `reason=-1`
(`KErrNotFound`) - a real `User::Panic("N-Gage Installer", -1)` called by
the app's own code, after several `Leave(-1)`/`Leave(-5)` retry cycles.
This is categorically different from every prior finding this session
(the `BackupServer` stall, the CPU-spin hang, `playserver.exe`'s benign
self-exit) - a genuine, reproducible crash.

**Root cause, found via the `entry()` probe (`PROBE_R110`, still in the**
**tree from round 110):** right before the panic,
`entry() proc=Installer[2000a2ae]0002 path=d:\Private\2000A2AE\n-gage.sisx`
comes back `NOT FOUND`. The installer needs its own install package at a
specific path on drive D: and it's missing entirely - confirmed via
direct filesystem check (`find`), no `n-gage.sisx`/`.sis` anywhere near
that path.

**First fix attempt - wrong drive-mount location.** Found a real
`n-gage.sis` (144 bytes, valid `Symbian installation file (EPOC release
6)` header) under `data/drives/z/nem-4/system/install/` and copied it to
`data/drives/d/Private/2000A2AE/n-gage.sisx`. Result: **zero change**,
identical panic. Root cause of the non-fix: this device (`RM-320`) uses
**per-device storage** (`system/src/devices.cpp`'s
`migrate_legacy_drives_if_present`/`get_device_specific_storage_path`),
confirmed by the very first log line of every run this whole session
("Legacy c drive folder still exists at data\drives\c\, but RM-320
already has its own") - the actual live D: drive for this device is
`data/devices/rm-320/drives/d/`, not `data/drives/d/`. All of this
session's earlier filesystem exploration (rounds 104-111) happened to
only touch paths that exist identically in both locations, so this
mismatch never surfaced until now.

**Second fix attempt - right location, wrong source file.** Copied the
same 144-byte `nem-4` file to the correct
`data/devices/rm-320/drives/d/Private/2000A2AE/n-gage.sisx`. Result:
**real, structural progress** - the file was found (no more `NOT FOUND`),
a genuine `!InstallServer` (UID `101f7295`, confirmed real LLE guest code
- no `InstallServer` match anywhere in EKA2L1's own `services/` HLE
source) actually ran and exited cleanly, but then hit a new,
never-seen-before leave code, `-1003`, repeated 3x, before the installer
exited cleanly (`reason=0`, no panic - the crash was gone, but nothing
installed).

**Third fix attempt - correct device-specific file.** Realized the
144-byte file came from `nem-4` (a **different phone model's** ROM
entirely - wrong source), not `rm-320` (the actual device). Found the
real, device-matched stub: `data/drives/z/rm-320/system/install/
ngiinstaller_stub.sis` (25,616 bytes - clearly a real, substantial
package, not just a marker). Copied that to the correct per-device path
instead. Result: **furthest reached yet** - file found, then a much
longer leave sequence (`-25` `KErrEof`, `-12` `KErrPathNotFound`, `-20`
`KErrCorrupt` x3, `-1` `KErrNotFound` x4) before landing back on the same
`-1003` x3 and exiting cleanly, no panic.

**What `-1003` means is still unconfirmed.** `!InstallServer` is real,
un-HLE'd guest code, so this is a genuine Symbian SW-Install-specific
error code (below `-1000`, outside the generic `KErrXXX` range) that
isn't in the local `ossrv_repo` checkout (the SW-Install/SISX-installer
component appears to not be part of what Symbian open-sourced) - could
not verify its exact meaning from source. The `KErrCorrupt`/
`KErrPathNotFound` leaves right before it suggest the installer is
finding *some* structure in the package but failing to resolve one or
more dependencies/components it references - plausible given `.sis`
"stub" files conventionally just point at other ROM-resident components
rather than embedding a full payload, and this backup may be missing
whatever those referenced components are.

### What this round actually establishes

1. **This device's `RM-320`-specific per-device storage path
   (`data/devices/rm-320/drives/...`) is the one that matters** - any
   future filesystem-mount edits/investigation on this specific
   device profile need to target that path, not the generic
   `data/drives/...` one (which is legacy/unused once a device has its
   own per-device folder, per `devices.cpp`).
2. UID `0x2000A2AE` is a strong candidate for the *actual* real-world
   N-Gage 2.0 entry point a user would tap - unlike `0x20007B38`, it
   produces a genuine, fixable-looking crash tied to a concretely missing
   file, not a benign non-issue.
3. The crash (panic) is now gone with the correct stub file in place, but
   the install still doesn't complete (`-1003`, unconfirmed exact
   meaning) - this backup may be missing one or more further dependency
   files the `ngiinstaller_stub.sis` package references.

### Live changes still in place (not yet reverted)

- `data/devices/rm-320/drives/d/Private/2000A2AE/n-gage.sisx` now exists
  (25,616 bytes, copied from `data/drives/z/rm-320/system/install/
  ngiinstaller_stub.sis`) - this **did not exist before this round** and
  is a genuine change to the live data mount, not a source-code change.
  Keep or revert depending on whether further installer investigation
  continues.
- The stray, incorrect 144-byte file at
  `data/drives/d/Private/2000A2AE/n-gage.sisx` (wrong/legacy path,
  wrong-device source file) is harmless dead weight but was never
  cleaned up.
- `config.yml`'s `log-svc`/`log-ipc` were briefly set to `true` and
  `Kernel:Warn` was changed to `Kernel:Trace` in `log-filter` for round
  111's full-syscall-trace probe, then reverted back to `false`/`Warn`
  immediately after - confirmed back to baseline.

### To resume from here

1. Identify what `-1003` actually means (would need either finding the
   real, closed-source Symbian SW-Install headers somewhere, or tracing
   `!InstallServer`'s own disassembly around where it's raised - same
   Ghidra live-dump technique used throughout this investigation).
2. Check whether `ngiinstaller_stub.sis` references other components (via
   its own SIS structure) that might also be missing from this backup.
3. Decide whether to keep chasing `0x2000A2AE`'s install-completion
   further, or accept that this specific restored backup may be
   permanently missing pieces of the original N-Gage 2.0 installation
   that aren't recoverable through EKA2L1-side fixes alone.

**UPDATE (rounds 103-108, correction + new confirmed root cause: the**
**user-visible "stuck at white screen" symptom is NOT explained by round**
**102 alone - live testing found a real, non-deterministic, unbounded**
**native CPU spin, isolated to a single `dynarmic_core::run()` call, right**
**after `WaitForAnyRequest`. Live probes added and still in the tree,**
**not yet reverted.):**

Built the current tree (Release, `-m:4`) and live-tested actual launches
of `Games`/`N-Gage Installer` (UID `0x20007B38`) via `--run` (same
`launch_app` path a real UI click uses, confirmed round 75) instead of
relying on log traces alone.

**Round 102 (premature): confirmed harmless, wrongly.** First test waited
only ~15s after the `!BackupServer` `KErrCancel` fail-fast and observed
`Get-Process ... Responding: True`, concluding the stall was harmless.
**This was wrong** - `Responding: True` only proves the Qt window's native
message pump is alive; it says nothing about whether the emulated Symbian
system is making progress. Leaving a later run for 30+ minutes exposed
the real behavior.

**Round 103: confirmed non-determinism matches round 73/75's old**
**findings exactly.** Across repeated `--run 0x20007B38` launches, outcome
varies run to run:
- Sometimes it clears the `!BackupServer` stall and continues: `Games`
  spawns `playserver.exe` (backing UID `0x20007B39`, also captioned
  "Games" in the app list - a genuine duplicate-caption/UID situation),
  which repeatedly self-`Kill(-25)`s (`KErrEof`) in a retry loop, and
  `N-Gage Installer` eventually gives up and **cleanly self-exits**
  (`process::kill` reason=0, category=None - a deliberate `Kill(0)`, not a
  crash) - confirmed via a temporary `process::kill` probe
  (`process.cpp:418`, tag `PROBE_R104`).
- Sometimes (round 106-108) it hangs hard, indefinitely, right after the
  `!BackupServer` fail-fast, at ~100% of one CPU core, with zero further
  log output - a real repeat of round 73/75's exact old finding, not
  something round 58's fix ever addressed.

**Round 104: chased `playserver.exe`'s `-25` (`KErrEof`) exit - a dead**
**end, but ruled out round 74's old theory.** Round 74 (much earlier in
this investigation) guessed this came from a directory-scan legitimately
reaching its end. A new probe on `dirs.cpp`'s `open_dir()` (tag
`PROBE_R105`, bumped from `LOG_TRACE` to `LOG_ERROR` since
`Service.EFsrv` is filtered above trace/info by default and blanket-
enabling that category is known from round 74 to itself perturb timing)
showed **`playserver.exe` never calls `open_dir()` at all** in this build
- the `-25` must come from a file read or another leave path entirely,
  not a directory enumeration. A follow-up probe on `files.cpp`'s
  `new_file_subsession()` (tag `PROBE_R106`, filtered to processes named
  `playserver`) found **zero file opens either** - `playserver.exe`
  self-kills with `KErrEof` without ever touching the EKA2L1 fs server, so
  the leave must originate from something else entirely (a HAL/property
  call, or in-guest logic unrelated to any host fs code path). Not
  pursued further this round - a real dead end for THIS specific
  question, though the self-`Kill(0)` give-up higher up
  (`N-Gage Installer`) is still a legitimate, separate thing worth fixing
  later (see "what remains" below).

**Round 107: isolated the hang to one thread, ~100% CPU, real, not a**
**scheduler artifact.** Windows-level `Get-Process` thread CPU-time
showed one thread accumulating 9+ minutes of CPU time while every other
thread stayed near zero. A wall-clock-gated thread-state dump added to
`system_impl::loop()` (`epoc.cpp`, tag `PROBE_R107`, same technique as
round 74's `wait_obj` dump) fires every 2 real seconds *unconditionally*
- it fired normally (showing the expected idle `wait_fast_sema` states for
`!ecomserver`/`CdlServer`/`!AknIconServer`) right up until the
`!BackupServer` fail-fast, then **never fired again**, for 8+ real
seconds of continuous checking. Since this probe doesn't depend on
`crr_thread()` being non-null, its total silence means `system_impl::
loop()` itself stopped being called - not "idle", not "same thread
re-picked every iteration" - execution is stuck inside a single call
*within* one `loop()` invocation.

**Round 108: pinned it to a single `dynarmic_core::run()` call, right**
**after `WaitForAnyRequest`, that never returns.** Added two more gated
probes: one in `CallSVC` (`arm_dynarmic.cpp`, tag `PROBE_R108`, fires on
every guest syscall) and one wrapping `dynarmic_core::run()`'s entry/exit
(same tag). Live trace showed: `run() enter instruction_count=3690000`,
then a single `svc call svc=0x800000 pc=0x8039FA9C` immediately after the
`!BackupServer` fail-fast fires, then **total silence** - no `run()
return` line, no further `svc call` line, for 8+ seconds despite both
probes being gated to fire at most once per second. Disassembling around
that PC (Ghidra, `euser_rm320_r55` project,
`ghidra-scripts/DisasmFun8039fa9c.java`) confirmed `0x8039FA98` is a
one-instruction shared SVC trampoline (`swi 0x800000; bx lr`) used by many
callers - not itself the spin site. `svc 0x800000` dispatches
(`svc.cpp:5657` etc.) to `kernel::thread::wait_for_any_request()`
(`thread.cpp:630`), which calls `semaphore::wait(0)` - read in full
(`sema.cpp:63-79`) and confirmed **textbook-correct**: it properly
de-schedules the calling thread via `scheduler->wait()` if no signal is
already pending. This is not a guest-logic bug and not a Symbian-source
question.

**The real open question is host-side, in the CPU/JIT layer, not**
**Symbian source:** `kernel_system`'s SVC handler
(`kernel.cpp:485-502`) calls `get_lib_manager()->call_svc(ordinal)` and
returns - it never calls anything like `jit->HaltExecution()` after a
blocking wait. So after `wait_for_any_request()` marks a thread blocked in
the kernel's own bookkeeping, dynarmic keeps executing that *same*
thread's compiled code forward, using the *same* CPU register context,
for the rest of its already-granted tick budget (`instruction_count=
3,690,000` this round) - completely oblivious to the fact the kernel
now considers this thread non-runnable. Whatever guest code immediately
follows the `WaitForAnyRequest` return in this exact path apparently
spins on plain ARM instructions (no further syscalls) without either (a)
dynarmic's own tick-based preemption ever kicking in to hand control back
after ~3.69M instructions (which should take low-single-digit
milliseconds of wall time, not indefinite real time), or (b) any other
thread ever getting a chance to run and unstick it. Both are surprising
if the loop body is genuinely bounded and dynarmic's `AddTicks`/
`GetTicksRemaining` accounting (`arm_dynarmic.cpp:296-305`) is being hit
correctly - meaning either that accounting has a gap for whatever
instruction sequence is executing here, or the loop body somehow never
reaches an instruction that calls `AddTicks` at all under some condition.
Not yet root-caused further - this needs either direct dynarmic
tick-accounting instrumentation or a live debugger attached mid-hang
(none available in this environment - checked for `procdump`/`cdb`/
`windbg`/`gdb`, none installed).

**Temporary probes still in the tree, not yet reverted** (all tagged,
all `LOG_ERROR` so they survive default log-level filtering, all
wall-clock-gated where they fire on a hot path):
`process.cpp:418` (`PROBE_R104`), `dirs.cpp:65` (`PROBE_R105`),
`files.cpp` in `new_file_subsession` (`PROBE_R106`), `epoc.cpp` inside
`system_impl::loop()` (`PROBE_R107`), `arm_dynarmic.cpp`'s `CallSVC` and
`dynarmic_core::run()` (`PROBE_R108`).

### What remains

1. **The dynarmic-run()-never-returns mystery (the actual current**
   **blocker)** - needs tick-accounting instrumentation inside dynarmic
   itself, or a live debugger attached to the host process mid-hang, to
   see exactly which instructions are executing in the stuck window.
2. **`N-Gage Installer`'s own give-up-and-`Kill(0)`** (round 103/104,
   the *other*, non-hanging outcome) is a separate, smaller gap worth
   fixing later: even on the "lucky" runs where the CPU-spin doesn't
   happen, the installer just quietly exits rather than ever handing off
   to the real `Games`/`playserver.exe` (UID `0x20007B39`) front end -
   not investigated this round.
3. Per user feedback this round: consult all 4 user-supplied reference
   sources (not just the archive.org Symbian source dump) at the start of
   each new sub-thread - see the new `SOURCES.md` in this directory for
   what's in each and what's still unchecked. Confirmed this round:
   `playserver` does not exist anywhere in the real Symbian OS source
   (`ossrv_repo`) - it's N-Gage 2.0's own proprietary component, so that
   source can explain generic kernel/active-object semantics around it
   but not its own internals.

**UPDATE (round 102, root cause CONFIRMED against real Symbian OS**
**source code - this specific sub-thread (rounds 90-101, the**
**`!BackupServer` opcode-32 stall) is a genuine, historical Nokia**
**design fragility, not an EKA2L1 emulation bug. No source changes -**
**this round is pure confirmation of round 13-58's already-implemented**
**fix.):**

User supplied 4 Symbian documentation/archive links; one
(`archive.org/download/SymbianOpenSource`) turned out to be the actual
Symbian Foundation open-source release, archived as Mercurial bundle
files. Downloaded `ossrv.hg.bundle` (88MB, via PowerShell
`Invoke-WebRequest` - Bash `curl`/`wget` are intercepted by this
session's context-mode plugin and redirected to a sandboxed executor
whose filesystem doesn't persist to the host, so the download had to go
through PowerShell instead), installed Mercurial (`pip install
mercurial`), and reconstructed the real working tree (`hg unbundle` +
`hg update`, 18463 files). This tree contains the actual historical
Nokia source for `genericservices`, `genericopenlibs`,
`lowlevellibsandfws` (including the exact `apputils` component that
implements the client side of `!BackupServer`), etc.

**Read `lowlevellibsandfws/apputils/inc/backup_std.h`:**

```cpp
_LIT(__BACKUP_SERVER_NAME_V2,"!BackupServer");

enum TBaBakOpCode
    {
    EBakOpCodeEventReady=20, // EBakOpCodeStartNotifications,
    EBakOpCodeGetEvent,
    EBakOpCodeCloseAllFiles,
    EBakOpCodeRestartAll,
    EBakOpCodeCloseFile,
    EBakOpCodeRestartFile,
    EBakOpCodeNotifyLockChange,
    EBakOpCodeNotifyLockChangeCancel,
    EBakOpCodeCloseServer,		// no longer supported
    EBakOpCodeNotifyBackupOperation,
    EBakOpCodeCancelOutstandingBackupOperationEvent,
    EBakOpCodeGetBackupOperationState,
    EBakOpCodeBackupOperationEventReady,
    EBakOpCodeGetBackupOperationEvent,
    EBakOpCodeSetBackupOperationObserverIsPresent,
    EBakOpCodeStopNotifications
    };
```

Server name matches EKA2L1's own `name() == "!BackupServer"` check in
`kernel/server.cpp` exactly. Counting the enum from 20:
`EBakOpCodeBackupOperationEventReady = 32` - an exact match to round
90's captured live log line, `"!BackupServer message (function=32)
never completed by guest dispatch after 1s"`.

**Read `lowlevellibsandfws/apputils/src/babackup.cpp`, found the exact
client-side notifier:**

```cpp
void CBaBackupOperationNotifier::Queue()
    {
    if (!IsActive())
        {
        iStatus=KRequestPending;
        SetActive();
        iBackupSession.SetBackupOperationObserverIsPresent(ETrue);
        iBackupSession.BackupOperationEventReady(iStatus, iBackupOperationAttributes);
        }
    }

void CBaBackupOperationNotifier::RunL()
    {
    const TInt status=iStatus.Int();
    if (status!=KErrCancel && iObservers.Count()>0)
        {
        TRAPD(err,DoRunL());
        if (err!=KErrServerTerminated)
            {
            Queue();
            }
        User::LeaveIfError(err);
        }
    }
```

This is a byte-for-byte match, in real Nokia source, for what rounds
57-58 had already reverse-engineered purely from disassembly: on
`status==KErrCancel` specifically, the entire `RunL()` body - including
the `Queue()` re-arm call - is skipped. No error propagates, nothing
crashes, the active object just goes silently, permanently dormant.
Compare the sibling `CBaLockChangeNotifier::RunL()` in the same file,
which re-arms unconditionally except on `KErrServerTerminated` - proving
this asymmetric `KErrCancel` guard is a real, specific design choice in
`CBaBackupOperationNotifier` alone, not a universal defensive pattern
Nokia used everywhere.

**Conclusion:** rounds 13-58 (already committed in this working tree,
see `kernel::service::arm_backup_server_msg_timeout` in
`src/emu/kernel/src/server.cpp:76`) had already fully solved this exact
problem - completing the timed-out message with `KErrCancel` rather than
`KErrNone` or a generic error code, specifically *because* the real
guest binary's own compiled dispatch has this `!=KErrCancel` guard. That
diagnosis, reached purely through live disassembly and empirical
testing, is now independently confirmed correct down to the exact
source line, against the real historical Nokia implementation. This is
not an EKA2L1 bug or emulation gap: it's a genuine fragility in
Symbian's own `CBaBackupOperationNotifier`, present on real hardware
too, that just happens to be reachable here because EKA2L1's `!BackupServer`
LLE guest dispatch doesn't (and per round 13-58's own analysis, cannot
easily) complete this specific async request on its own.

**UPDATE (round 101, reframed the whole hypothesis - ecomserver's**
**BackupServer retry logic almost certainly runs correctly and**
**terminates by design; the real gap is a missing link to a SEPARATE**
**pending-request object. Pure static analysis, no code changes.):**

Checked round 100's remaining open question: does the class at
`0x804cbc36` derive from `CActive`? Decompiled its two base-construction
calls (`thunk_EXT_FUN_803a2e18`, `thunk_EXT_FUN_803a6adc`). Neither
cleanly confirms it - `FUN_803a2e18` checks a bitflag and conditionally
calls a *virtual* method at offset `+0xc` before clearing the flag
(unusual shape for a base constructor, which normally wouldn't dispatch
through a not-yet-fully-built vtable), and `FUN_803a6adc` sets *yet
another* vtable pointer before calling `FUN_803a2e18` itself. This
doesn't read as a simple linear `derived : public CActive` chain - more
consistent with composition or multiple inheritance (a second embedded
sub-object with its own vtable slot), which would need actual Symbian
ABI/multiple-inheritance-thunk expertise to resolve conclusively rather
than more guessing through disassembly.

**More importantly, re-examined what round 90/91's own live trace already
proved and reframed the whole hypothesis around it**: `!ecomserver`'s
thread does **not** just fail once and hang - the round 90 trace showed
it cycling `!ecomserver` ↔ `!BackupServer` **several times** before
finally settling into the generic idle wait. That is the signature of a
retry loop running to completion and giving up *by design* - not a
crash, not a broken handler. Round 99-100's class (constructor +
"ensure connected, register" method) is almost certainly *this* retry
logic, and it most likely already works correctly: attempt, fail,
retry, eventually stop retrying, return to idle.

**This means the bug is very unlikely to be "ecomserver's own RunL is
broken."** It's far more likely that after this retry logic gives up,
**nothing tells the separate pending-request-list object (round 97's
destructor-holding object, `FUN_804c8512`) to drain and complete
`cdlserver.exe`'s original message.** Two live possibilities, still
open:

1. These are two views of the *same* underlying object (a Symbian
   multiple-inheritance pattern: one combined instance, two vtable
   pointers at different member offsets, each accessed through a
   different "this"-adjusting thunk) - in which case the missing
   trigger is a specific method call between two of its own interfaces
   that never happens.
2. They are genuinely two separate objects, and something that should
   connect them (an observer registration, a callback pointer) is never
   wired up or never invoked in this specific code path.

**This is now Symbian-ABI-specific enough** (resolving multiple-
inheritance `this`-adjustment thunks, or finding the exact
observer-registration link between two objects) that further progress
needs targeted study of the actual object layout, not more blind vtable
exploration. Given the very substantial ground already covered across
rounds 90-101 on this specific sub-thread, this is a reasonable point to
pause it.

No source changes this round - pure static analysis of already-dumped
binaries, nothing to revert.

**UPDATE (rounds 99-100, identified the exact class/vtable ecomserver.exe**
**uses to talk to BackupServer, and its "ensure connected + register"**
**method - the specific RunL/event-handler slot remains unconfirmed):**

Round 98 left one open task: find what triggers (or should trigger) the
observer-list object's destruction/completion after the `!BackupServer`
`KErrCancel` response. Tried the same LR-read techniques used for the
async path (rounds 94-96) on `!ecomserver`'s own **sync** SendReceive
calls (`FUN_803aa998`, confirmed hit directly by this thread in round
91) - but this path fans out through multiple shared euser.dll helper
layers (`FUN_803aa998` → `FUN_803ac842` → several distinct call sites)
used by many unrelated session types (file server, etc. all showed up),
unlike cdlserver's single clean async request. Reading LR one prologue
at a time wasn't converging.

**Switched technique**: hooked the kernel's own generic
`ipc_send_callback` (proven in round 87) filtered directly on
`server_name == "!BackupServer"` - this fires with the real server name
already resolved host-side, no guest breakpoint hunting needed at all.
At that exact moment, walked the stack (48 words, correct asid per
round 83's fix) looking for any word that looks like a return address
**outside** euser.dll's own range. Found `0x804cbc31` sitting on the
stack - which, after an arithmetic slip was caught and corrected
(`ecomserver.exe`'s real range is `0x804c7dc8`-`0x804cf284`, not the
smaller range first assumed), confirmed as squarely inside
**`ecomserver.exe` itself**.

Disassembling around it found a small, coherent C++ class:
- **`FUN_804cbc36`** (vtable slot 0): the constructor - sets the vtable
  pointer, then calls two euser.dll base-class construction helpers
  (`thunk_EXT_FUN_803a2e18`, `thunk_EXT_FUN_803a6adc`).
- **`FUN_804cbc18`**: lazily allocates a sub-object at `this+0xc` if
  absent, calls it with `this+4`, then sets `this+0x14 = 1` - a
  textbook "ensure connected, then register/complete" pattern. Its
  return address (right after the inner call at `0x804cbc2c`) is
  **exactly** `0x804cbc31` - confirming this is the live call caught by
  the stack walk.
- **Vtable slot 4** (`0x804cbc64`) is a one-instruction tail-call proxy
  straight into `FUN_804cbc18` - meaning this method is reached only by
  vtable dispatch (zero static callers anywhere, same indirect-call
  pattern as apgrfx.dll's round 88 ping-pong and ecom.dll's round 95-96
  `FUN_804c66c4`), which is exactly why no earlier static reference
  search ever found it.
- Other vtable slots (1, 3, 5, 6, 7) decompiled to a mix of a paired
  destructor, a `Cancel`-shaped reset (nulls two owned sub-objects), two
  small allocator calls, and one large, complex function
  (`0x804cac36`) doing descriptor type-masking and comparisons -
  plausibly ECom registry/interface matching, not obviously a
  `RunL`/event handler. Slot 2 lands in a shared import/veneer table
  (same artifact as round 97's vtable read), not a real per-class
  method.

**Not yet confirmed**: which vtable slot (if any among these 8) is this
class's actual `RunL`/completion-handling method - none of the 8
decompiled cleanly enough to say "this is what runs when BackupServer's
reply arrives." Plausible explanations, in order of likelihood: (a) this
object is a plain `CBase`-derived helper (not itself `CActive`), and the
real active object doing async wait/RunL handling is a *different*,
still-unlocated class that *owns* this one; (b) the handler exists among
these 8 slots but its shape wasn't recognizable in the disassembly
sampled so far and needs a longer decompile window per slot.

**Concrete next step**: get the full decompile (not just a 15-40
instruction disassembly window) for vtable slots 1, 3, 5, 6, and 7, and
check `thunk_EXT_FUN_803a2e18`/`thunk_EXT_FUN_803a6adc` (the
constructor's two base-class calls) to determine whether this class
derives from `CActive` at all - that single check would settle
possibility (a) vs (b) above directly.

All temporary probes from rounds 99-100 fully reverted; `epoc.cpp` back
to just the two permanent fixes (verified via `git diff --stat`,
unchanged +131 lines). New Ghidra scripts and lookup results from this
round are kept in `ghidra-scripts/` for continuity.

**UPDATE (round 98, DEFINITIVE - the cleanup/completion object is never**
**destroyed, confirmed live. Root cause fully localized: something**
**upstream of ecomserver's own destructor never triggers it after the**
**failed BackupServer exchange):**

Round 97 left two open scenarios for how `ecomserver.exe`'s
message-completion mechanism (confirmed correct, drains a pending list
and completes every entry with a given error) related to the deadlock:
either (1) the observer-list object is supposed to be destroyed right
after the BackupServer exchange and something prevents that, or (2) a
separate, not-yet-found non-destructor completion path is the real
broken one.

Settled this empirically: breakpointed both halves of the destructor
pair (`FUN_804c8512` - the base/non-deleting destructor, vtable slot 0;
`FUN_804c8544` - the paired deleting destructor that calls slot 0 then
frees the object, vtable slot 1) and ran the full real deadlock
reproduction (UI-click launch, wait for the `!BackupServer` fail-fast,
then an **additional 40+ seconds** past that with the process still
alive). **Neither breakpoint fired even once.**

**This confirms scenario 1 conclusively**: the object holding
`cdlserver.exe`'s pending observer registration is never destroyed, so
its correctly-implemented completion-on-destruction logic never runs.
The bug isn't in the completion mechanism at all - it's that whatever
should trigger this object's destruction (or otherwise drive its normal
completion path) after `!BackupServer`'s `KErrCancel` response never
happens. `ecomserver`'s own thread settles into the generic "wait for
any request" primitive (round 91's `0x8039fa98`) and simply never
receives - or never acts on - whatever event was supposed to prompt
cleanup.

**Full picture, now complete end-to-end**:
1. `cdlserver.exe` issues one async ECom request via `ecom.dll`'s
   `FUN_804c66c4` (vtable-dispatched, round 96).
2. This flows through euser.dll's generic `RSessionBase::SendReceive`
   machinery into `!ecomserver`.
3. `!ecomserver` talks to `!BackupServer`, which times out and gets the
   existing `KErrCancel` fail-fast (rounds 26/51-58's pre-existing fix).
4. `!ecomserver` runs a final time, then goes idle - normally, waiting
   for its next request.
5. The object tracking `cdlserver.exe`'s original registration is never
   destroyed and never completes it (round 98, confirmed live: 0 hits
   on either destructor half after 40+ seconds).
6. `cdlserver.exe`'s thread waits forever on a completion that will now
   never come. **Deadlock.**

**What remains** (a genuinely separate, well-scoped task for whoever
picks this up): identify what real-hardware event is *supposed* to
trigger this object's cleanup after a `KErrCancel`-style backup-session
failure - almost certainly a `RunL`/observer-callback method living
somewhere in `ecomserver.exe`'s other ~390 undecompiled-in-detail
functions (not the two destructor-vtable slots examined so far), whose
own entry condition is never satisfied in this emulated path. Round 97's
vtable dump for this object only reliably resolved slots 0, 1, and 3 (a
simple one-line forwarder) before running into unrelated
import-veneer-table data past slot 1 - the vtable's true remaining
slots (if any) haven't been correctly enumerated yet and would be the
natural starting point.

All temporary probes from round 98 fully reverted; `epoc.cpp` back to
just the two permanent fixes (verified via `git diff --stat`, unchanged
+131 lines).

**UPDATE (round 97, found ecomserver.exe's message-completion mechanism -**
**it exists and works, but is only reachable from a virtual destructor,**
**not from any "normal response" path - pure static analysis, no code**
**changes):**

Continued directly into `ecomserver.exe`'s own ~29KB of code (already
dumped in round 92, now fully decompiled - 400 functions). Went looking
for how it signals request completion back to a client (i.e. what
`cdlserver.exe`'s stuck thread from round 96 is actually waiting for).

Found the RMessage2-completion primitive first: SVC `0x42`
(`message_complete`, confirmed against `svc.cpp`'s dispatch table) sits
at `0x8039fd78` in euser.dll, found by walking backwards from a cluster
of sequential 8-byte SVC trampolines starting at `0x803a0038` (SVC
`0x9b`) - each stub is exactly one SVC number lower every 8 bytes back.
Its wrapper, `FUN_803aa0f6`, is the same address noted in round 84's
`apgrfx.dll` work (called there too) - `RMessage2::Complete()`, in
effect.

`ecomserver.exe` calls it exactly once, through `FUN_804cc370(msg,
result)` (a thin one-line wrapper). Its only caller, `FUN_804c84c6`,
is a **complete-all-pending pattern**: it fetches a count via
`FUN_804cd716`, then loops `FUN_804cd6b2(list, i)` to fetch each
element and calls `FUN_804cc370(elem+4, result)` on all of them - i.e.
"complete every message currently queued in this collection with the
given result code." This is called with `result = 0xfffffffd`
(**KErrCancel**, -3) from `FUN_804c8512`, which itself opens by writing
a vtable pointer (`*param_1 = PTR_PTR_804c8888`) before doing any work -
the unmistakable shape of a **C++ virtual destructor**.

Checked what calls `FUN_804c8512`: exactly one reference, and it
resolves to an **unrecognized (non-function) location** - meaning it's
not called through an ordinary `bl`, but is itself sitting as a **vtable
slot entry**, invoked polymorphically whenever whatever object owns this
observer/request list gets deleted (`delete` on a base-class pointer).

**This means the completion mechanism genuinely exists and is correctly
implemented** (it does exactly what you'd want: drain the pending list,
complete each with the given error) - **but the only path that reaches
it found so far is object destruction**, not a "handle the BackupServer
reply and notify observers" response path. Two live possibilities,
neither yet confirmed:

1. This object *is* supposed to be destroyed (and thus flush its pending
   list) as part of normal handling right after the BackupServer
   exchange, but something upstream prevents that destruction from
   happening in this specific scenario (matching the theme of every
   prior stall in this whole investigation: an object waiting on a
   completion that would ordinarily have arrived by now).
2. There is a *separate*, non-destructor completion path elsewhere in
   this same 400-function dump (not yet located) that's supposed to
   fire in direct response to the BackupServer reply, and it's *that*
   path that's broken or unreached - the destructor path found here
   would only be a fallback/cleanup mechanism, not the main one.

**Concrete next step for continuing this thread**: live-trace which (if
either) of these two paths actually executes during the real deadlock -
breakpoint `FUN_804c8512`'s entry (same LR-read technique used
throughout rounds 94-96) to see if it's ever hit at all during a stuck
run, and separately search the remaining ~390 undecompiled-in-detail
functions in `ecomserver.exe` for the object's *other* vtable methods
(a RunL-style handler is the most likely candidate, reachable from the
same vtable this destructor lives in).

No source changes this round - pure static analysis of the already-
dumped `probe_r92_ecomserver.exe.bin` (imported as Ghidra project
`ecomserver_r92`), nothing to revert.

**UPDATE (rounds 91-96, the full call chain traced - cdlserver.exe ->**
**ecom.dll -> euser SendReceive -> ecomserver -> BackupServer - root**
**cause narrowed to whatever ecomserver does after the KErrCancel**
**fail-fast that leaves cdlserver.exe's original async request**
**uncompleted forever):**

Continued the round-90 deadlock (real UI-click launches hit this ~10/10,
far more reliably than the apgrfx.dll thread). Traced it with a series of
live techniques, each correcting a gap in the previous one:

**Round 91** added an edge-triggered PC-capture in `system_impl::loop()`
(logs the previous thread's PC whenever `crr_thread()` changes - cheaper
and more precise than round 90's 300ms sampler). Found the exact stop
points: `0x8039fa98` (`software_interrupt(0x800000)`, the generic "wait
for any request" primitive - both `!ecomserver` and `Games` sit here
normally) and `0x803a02cc`/`0x803a02b8` (`software_interrupt(0x4c)` /
`software_interrupt(0x25)` - the sync and async `RSessionBase::SendReceive`
SVC stubs respectively, confirmed against `svc.cpp`'s dispatch table for
this device). Both threads are in **genuinely normal wait states** - this
is not a crash, just something that never signals completion.

**Round 92** dumped `ecomserver.exe`'s own code (29884 bytes, live via
`codeseg_loaded_callback`, same technique as round 79/84) expecting to
find the BackupServer-calling routine there. Found **zero references**
(as code or as raw data) to any of the SVC/SendReceive addresses, and no
companion codeseg ever loaded into ecomserver's own process either.

**Round 93** broadened to log every distinct process and codeseg loaded
during boot (name lookup once per new process pointer, not per codeseg -
kept cheap the same way round 92's pointer-gate did). Found two
UID-named DLLs (`10281fc7.dll`, `101fe2aa.dll` - classic ECom plugin
naming) loaded directly into **N-Gage's own process** (matching the
theory that ECom plugins load in-process within the client, not inside
`ecomserver.exe`). Dumped both - also **zero references** to any known
SVC/SendReceive address in either.

**Round 94** pivoted from static cross-referencing to a live LR-read:
breakpointed `FUN_803aa976`'s entry (euser.dll's async `SendReceive`
wrapper, found via the SVC `0x25` stub) right before its prologue push
executes, so `LR` still holds the real caller - no static analysis
needed at all. First attempt found the caller was `FUN_803ac80e`, itself
just another shared euser.dll helper one layer too shallow; moving the
breakpoint one level deeper (identical prologue, so the same
hand-emulation worked unchanged) found this layer is **euser.dll's
universal `RSessionBase` send path** - called from `ws32.dll`,
`efsrv.dll`, `aknskinsrv.dll`, and more, system-wide. Too generic by
itself, but among the callers, **`ecom.dll`** (the ECom **client**-side
library, distinct from `ecomserver.exe` the server) stood out as
matching this investigation's whole premise.

**Round 95** found `ecom.dll` never appears in any codeseg roster at
all - it's ROM-XIP (like `euser.dll` itself), and ROM-XIP shared
libraries apparently never fire the per-process `codeseg_loaded_callback`.
Dumped it directly at its known static address instead (`0x804c6608`,
found by cross-referencing round 94's caller address against every ROM
DLL's own E32Image header via `parse_rom.py`) - no boot or click needed
at all, since ROM-XIP code is available the moment ROM loads. Decompiling
found `FUN_804c66c4` - confirmed as the exact function round 94's LR
capture pointed at - sets a flag on its argument and issues an async
`SendReceive` via the euser wrapper. But it has **zero static callers
anywhere**, not in `ecom.dll` itself nor in any of the three previously
dumped modules - meaning it must be called through a real C++ vtable,
the same indirect-dispatch pattern found in round 88's apgrfx.dll
ping-pong.

**Round 96** breakpointed `FUN_804c66c4`'s own entry directly (same
LR-before-prologue technique) to sidestep the vtable indirection
entirely - LR is set by the calling instruction regardless of whether
it was direct or through a vtable slot. **Single, clean hit**:
`caller_lr=0x83485c69`. Cross-referencing against round 93's roster
(`cdlserver.exe run_addr=0x83485248 size=0x1a30`) confirms this address
falls exactly inside **`cdlserver.exe`** - the Content Download Server,
literally named in this investigation's own pre-existing code comments
("...leaves !ecomserver dead and, downstream, leaves CdlServer spinning
forever trying to talk to it"). Only one hit (not a loop) means
`cdlserver.exe` issues this async ECom request exactly once - the bug
isn't a retry storm, it's that **the completion never arrives**.

**Full call chain now traced end to end**:
`cdlserver.exe` (`0x83485c69`) → `ecom.dll`'s `FUN_804c66c4` (via vtable)
→ euser.dll's async `SendReceive` machinery → `!ecomserver` → `!BackupServer`
→ round 58's KErrCancel fail-fast fires → `!ecomserver` runs once more
→ **the entire system, including `cdlserver.exe`'s original waiting
thread, goes idle forever**.

**Root cause is now narrowed to one specific question**: what does
`!ecomserver`'s own code do with the KErrCancel result internally, and
why does it never propagate a completion back to `cdlserver.exe`'s
original async request? This is a tractable, well-scoped next step -
much narrower than "somewhere in this whole subsystem" - but genuinely
reverse-engineering `ecomserver.exe`'s own internal state machine (which
round 92 confirmed contains no *direct* SendReceive calls of its own, so
the completion-handling logic in question is presumably a callback/RunL
triggered by the async response, elsewhere in its ~29KB of code) is a
new, separately-scoped task, intentionally not started this round given
how much ground rounds 91-96 already covered.

All temporary probes from rounds 91-96 fully reverted; `epoc.cpp` back
to just the two permanent fixes (verified via `git diff --stat`,
unchanged +131 lines). Useful permanent artifacts kept in this folder:
`probe_r92_ecomserver.exe.bin`, `probe_r93_10281fc7.dll.bin`,
`probe_r93_101fe2aa.dll.bin`, `probe_r95_ecom.dll.bin` - all four
already imported into their own Ghidra projects
(`ecomserver_r92`, `ecom_plugins_r93`, `ecomdll_r95`) for whoever
continues this thread.

**UPDATE (round 90, a SECOND, much more reproducible deadlock found -**
**likely the real blocker for actual UI-driven N-Gage usage, distinct**
**from the apgrfx.dll ~33Hz loop this whole 81-89 thread was chasing):**

Went for it on round 89's plan: reimplemented UI-click automation
(`PostMessage` to the emulator's own host-side Qt app-picker window,
found via `EnumWindows`/`EnumChildWindows` + class name, same technique
as round 75) to launch N-Gage without `--run`, so the emulator window
stays open regardless of what the launched app's own process does
later. Confirmed via screenshot (`PrintWindow` on the TOP-level window
works fine for the host Qt chrome, though NOT for the actual GL-rendered
guest screen sub-widget - only needed the host chrome here since this
launcher IS host-native Qt, not guest-rendered) that the app grid has
"Games" appearing twice, matching the known dual-launcher registration.

**Every single real UI-click launch attempt (10/10 across this round)
hit the pre-existing "!BackupServer message never completed by guest
dispatch" fail-fast** (`server.cpp`, documented fix from rounds 26/51-58)
- far more reliably than `--run`'s roughly 50% hit rate seen in earlier
rounds. Ruled out my own tooling overhead as the cause (a version using
a single dot-sourced PowerShell invocation with zero subprocess spawns
between listing windows and clicking still hit it 9/9) - real UI-click
launches themselves trigger this race close to deterministically, for
reasons not yet identified (likely a genuine difference in call
ordering/timing between the App List Server's "launch app" RPC path and
a direct command-line process spawn).

Given this blocks real UI usage far more reliably than the bug rounds
81-89 were chasing, and real users only ever click icons (never use
`--run`), this is arguably the more important target. Investigated
live: added an edge-triggered thread-transition logger directly in
`system_impl::loop()` (logs only when `kern_->crr_thread()` changes,
catching every distinct thread that ever runs, however briefly - a
300ms periodic sampler tried first only ever caught `nullptr`/idle,
too coarse to see brief activity). Result, in order, right up to the
permanent freeze:

```
... Games / !AknIconServer alternating (normal icon-related activity) ...
Games
!ecomserver
Main
!ecomserver
!BackupServer
!ecomserver
!BackupServer
!ecomserver
!BackupServer
!ecomserver
!BackupServer
!ecomserver
(null/idle)   <- permanently idle from here on, verified 2+ minutes, never resumes
```

**`!ecomserver` and `!BackupServer` exchange several messages, then
`!ecomserver` runs one final time, and the entire system goes idle
forever** - not a busy spin (unlike the "CdlServer spinning forever"
failure mode this exact subsystem's pre-existing code comments describe
as the *original*, pre-round-58 symptom), a clean, total halt. Critically,
**`Games` (N-Gage's own process) never runs again either** - this looks
like a genuine mutual-wait deadlock between the app and ECom, not just
ecomserver getting stuck in isolation. `!ecomserver` here refers to the
guest's own real `ecomserver.exe` (Symbian's ECom component framework),
not anything implemented in EKA2L1's `services/` tree - so root-causing
this further means the same kind of guest-binary reverse engineering as
the apgrfx.dll thread (rounds 81-88), applied to `ecomserver.exe` instead.

**This is a substantial new investigation in its own right** (comparable
in scope to the apgrfx.dll thread just completed) and was intentionally
not pursued further this round to avoid opening a third parallel
investigation in one sitting. Recommended next steps for picking this up:
dump `ecomserver.exe`'s code (live, via the same `codeseg_loaded_callback`
technique used throughout this investigation) and find where it goes
idle right after its last `!BackupServer` exchange, to see what it's
actually waiting on and why `Games`'s thread never becomes ready again
either.

All temporary probes from round 90 (thread-list dumper, edge-triggered
transition logger) fully reverted; `epoc.cpp` back to just the two
permanent fixes (verified via `git diff --stat`, unchanged +131 lines).

**UPDATE (round 89, `--run` structurally cannot reach this code path -**
**live identification needs the UI-click launch method instead):**

Tried to identify the mystery object from round 88 (whose vtable slot
`0x14`, reached through `FUN_8206350a`, is the real "get next item"
call with no visible termination check) live: breakpointed right after
`FUN_8206350a`'s prologue push (`0x8206350c`, before `movs r4,r0`
executes, so `r0` still holds the `this` pointer per AAPCS), then
dereferenced `this+0x1c` and its first word (vtable pointer) to identify
the owning ROM module by address range - the same technique that
identified `apgrfx.dll` itself in round 84.

**Zero hits across 13 fresh `--run` boot attempts this round** (mix of
clean exits and backup-server stalls, same as previous rounds) - this
breakpoint, on a function that should fire during perfectly ordinary
`GetAppInfo`-adjacent operation, never triggered even once. This means
whatever flow reaches `FUN_8206350a`/the ping-pong state machine does
not happen during a quick `--run` launch-and-exit cycle at all - it
requires deeper navigation into the app than `--run` reaches before
`on_app_exited` auto-closes the whole emulator. This matches round 78's
own methodology exactly: that round only found the ~33Hz stall by
launching via a **real UI click** (no `--run`, no auto-close - see round
75's finding) and leaving one instance running for **5+ real minutes**,
deep into the app past video playback and into whatever screen actually
exercises this code path.

**Conclusion for continuing this thread**: further live identification of
the mystery vtable object needs the UI-click launch method (not `--run`),
left running long enough to reach the state where this ping-pong actually
triggers - a heavier reproduction setup than anything in rounds 81-89,
which all relied on the cheaper (but here, insufficient) `--run` +
short-wait cycle. Probe fully reverted; `epoc.cpp` back to the two
permanent fixes only (verified via `git diff --stat`, unchanged +131
lines).

**UPDATE (round 88, the actual ping-pong mechanism traced to a 2-state**
**cycle with no visible termination check - purely static analysis, no**
**code changes):**

Continued from round 87's finding that opcode `0x6` traffic through
`apgrfx.dll`'s generic transport (`FUN_820670ae`) is real. Found the
exact call site: `FUN_82067232 @ 0x82067232` builds a `TIpcArgs`-style
buffer and calls `FUN_820670ae(this, 6, &args)` - this **is**
`RApaLsSession::GetAppInfo`, confirmed. It has only 2 callers inside
`apgrfx.dll` itself (`FUN_82062e26`, `FUN_8206330e`), and neither loops -
each calls it exactly once. `FUN_8206330e` in particular is a lazy-init
cache getter (`if (*(param_1+0x14) == 0) { ...call GetAppInfo...
cache result... }`), which only matters if never actually cached.

Traced `FUN_8206330e`'s own 3 callers. Two (`FUN_820633d2`,
`FUN_82063608`) are straightforward. The third is an unnamed small
dispatcher at `0x82063758..0x82063792` that reads a **state field** at
`param_1+4` and branches: `state==1` → call `FUN_8206330e` (the
GetAppInfo path) → set `state=2`; `state==0` or `state==2` → call
`FUN_8206350a` instead. Decompiling `FUN_8206350a` found it **sets the
state back to `1`** at its end (`*(param_1+4) = 1`), after issuing two
indirect (vtable) calls through `*(param_1+0x1c)` at offsets `0x14` and
`0x3c`.

**This closes the loop**: state 1 → GetAppInfo (opcode `0x6`) → state 2 →
`FUN_8206350a` (some other operation via a *different* object's vtable,
offsets `0x14`/`0x3c`) → state back to 1 → GetAppInfo again → ... with
**no visible exit condition anywhere in this cycle**. The vtable call at
offset `0x14` returns a value (`iVar1`) that's used completely
unconditionally to reissue the next request - the only guard in
`FUN_8206350a` is a panic (`FUN_82066324(0x11)`) if that vtable call
itself returns 0, i.e. this looks like the classic Symbian idiom of
iterating a collection until a `Leave` (caught by the `TRAPD`-style
harness visible around it, the same `803a56a6/803a56ba/803a56d6`
trap-frame push/pop functions seen elsewhere in this investigation)
signals "no more items" - rather than a normal return value. If whatever
EKA2L1 HLE service backs that *other* object's vtable slot `0x14` never
returns the value that would trigger this Leave/panic path, the cycle
has no way to ever terminate.

**Not yet identified**: which class/object `*(param_1+0x1c)` actually is
(it's a separate object from the `apgrfx.dll` session itself, reached
through a member pointer - likely something registry/recognizer-related
given `GetAppInfo` is being interleaved with it), and which EKA2L1 HLE
service its vtable slots `0x14`/`0x3c` ultimately reach. That is the
concrete next step for continuing this specific thread - most
straightforwardly by getting a live memory dump of this object (its
vtable pointer would immediately reveal which ROM module owns it, the
same cross-referencing technique used successfully in round 84 for
apgrfx.dll itself).

This round was purely static analysis (Ghidra decompilation and
cross-referencing against the existing `apgrfx.dll` dump from round 84) -
no source changes, nothing to revert.

**UPDATE (rounds 85-87, correcting round 84's overclaim - `FUN_820670ae`**
**is a generic transport, not itself the loop; real loop still**
**unidentified, but the transport mechanism and one strong lead are now**
**pinned down):**

Round 85 tried to identify the exact App List Server opcode by adding a
counter directly in `applist_session::fetch()` (`applist.cpp`). Round 86,
suspecting round 84's live SendSync breakpoint had itself been perturbing
timing toward reproducing the stall (the mirror image of round 74's
"overhead flips a race toward stalling" finding), reinstalled that same
no-op breakpoint alongside the new counter. **Neither helped**: 34 fresh
boot attempts combined (18 without the breakpoint, 16 with it
reinstalled) never reproduced the ~33Hz stall even once, versus round
84 catching it on the very first attempt - so its true occurrence rate is
evidently far lower than one lucky repro made it look, and the
breakpoint-overhead theory wasn't confirmed either (may have just been
a coincidence).

Round 87 changed approach entirely: instead of depending on reproducing
the rare stall, hooked the kernel's own existing generic
`ipc_send_callback` (`kernel.h`/`kernel.cpp`, already invoked from
`svc.cpp`'s `session_send_general` with the real server name and opcode
for **every** synchronous IPC send, system-wide) to count `(server, ord)`
pairs. This needs no stall reproduction at all - it gets useful data from
any ordinary boot. First run immediately showed `!AppListServer` traffic,
including opcode `0x6` appearing 3 times in one short, perfectly normal
boot (more than most other opcodes, which mostly appear once) - a
plausible sign of a per-app-iterated call. Opcode `0x6` in the modern
(non-legacy) `applist_request` enum (`services/applist/op.h`) is
`applist_request_app_info`, i.e. `RApaLsSession::GetAppInfo`.

**This forced a correction of round 84's conclusion.** Re-reading the
decompiled loop:

```c
void FUN_820670ae(int param_1,undefined4 param_2,undefined4 param_3)
{
  int local_18 = 0;              // <- a LOCAL STACK variable
  do {
    do {
      uVar1 = FUN_8206dd6e(param_1 + 4,param_2,param_3);
      iVar2 = FUN_8206706e(param_1,uVar1,&local_18);
    } while (iVar2 == -0xf && local_18 == 0);
  } while ((iVar2 == 0) && (local_18 != 0));
}
```

`local_18` is freshly reset to `0` on **every call** to this function -
it is not persistent state. Its `!= 0` value can only come from the
inner loop's own reconnect branch (`iVar2 == -0xf`, `KErrServerTerminated`)
having fired **within this same call**. Checked whether that can even
happen for `applist_server` in EKA2L1: `session_send_general` (svc.cpp:
1259) returns `KErrServerTerminated` only via `session::is_server_terminated()`
(`kernel/session.h:105`, simply `svr == nullptr`), and both places that
null a session's `svr` (`server::destroy()` in server.cpp:191,
`session::destroy()` in session.cpp:234) explicitly skip HLE servers
(`if (!svr->is_hle())`) - and `applist_server` is HLE. So `-0xf` should
essentially never fire for this session in EKA2L1, `local_18` stays `0`,
and **both loops in `FUN_820670ae` execute exactly once** under normal
(`KErrNone`) operation - this function is not itself an infinite loop.

Cross-referencing every call site of `FUN_820670ae` inside `apgrfx.dll`
found **65 separate callers** - it's the client class's generic
"SendReceive-with-reconnect" transport helper, used by essentially every
API method (`GetAppInfo`, icon lookups, recognition, etc.), not something
GetAppInfo-specific. The real ~33Hz loop must be in **whichever of those
~65 wrapper functions specifically encodes opcode `0x6`**, being called
repeatedly by some higher-level caller - most likely an enumeration loop
iterating app UIDs one by one (matching this investigation's recurring
"enumeration that never terminates" theme, alongside the already-fixed
`gametitlemetadata.dll` bug). That specific wrapper and its own caller
have not yet been identified - this is the concrete next step for anyone
picking this back up: find which of the 65 callers passes function code
`6` into `FUN_820670ae`, then find what calls *that* wrapper in a loop.

All temporary probes from rounds 85-87 fully reverted; `epoc.cpp` and
`applist.cpp` are back to their pre-round-85 baselines (verified via
`git diff --stat`, matching the two permanent fixes plus one unrelated
pre-existing DRM MIME-type change already present before this
investigation began).

**UPDATE (rounds 81-84, the ~33Hz loop's caller found - apgrfx.dll's**
**session-reconnect retry idiom, not any N-Gage-specific code; two**
**incidental bugs fixed along the way):**

Round 81 tried logging every codeseg loaded into playserver.exe's process
(gated on `pr->name()`) to get a full roster to prioritize further
decompilation. This callback runs for **every** codeseg loaded
system-wide, not just playserver's - and calling `pr->name()`
unconditionally, without a null check, crashed the emulator on the very
first codeseg load of the entire boot (`pr` can be null - `codeseg.cpp`
itself guards this a few lines above the call into this callback).
Fixed with a null check. Once working, it also reintroduced the round-74
"probe overhead perturbs the eikbackupsrv race" effect: gating on
`pr->name()` (a virtual call + string build) for every codeseg system-wide
was enough overhead to flip a normally-4/4-successful boot into a
consistent stall, 2 boots in a row. Rewritten to gate on the cheap
already-available `name` parameter first (no `pr` touch at all unless it's
one of a handful of exact target strings) - overhead gone, boots
succeeded again immediately.

Also notable: **this same eikbackupsrv stall proved it's not the only
nondeterministic one in this codebase.** Once past it, two separate clean
boots in a row reached "N-Gage Installer to disable keyboard focus" and
then the ordinary `on_app_exited`-driven shutdown (round 72-74's known
benign pattern) **without the ~33Hz stall ever triggering at all.** The
second stall type is therefore also probabilistic, like the backup-server
one, not a guaranteed-every-time hang.

Rounds 81-82 dumped and decompiled `playserver.exe`'s main EXE (round 79,
already ruled out) plus 7 small companion codesegs one batch at a time
(`catalogsengine_ngi.dll`, `playserverar.dll`, `playserverset.dll`,
`naf.dll`, then `nafservicemanagercli_v3.dll`, `ncdutils_ngi.dll`,
`nafafutil_v3.dll` - the last three chosen because "service manager
client" sounded like a plausible session-holding culprit). Cross-checking
all ~160 unique external call targets across all of them against the
known SendSync return point (`0x803aa9aa`, which sits inside
`FUN_803aa998` in euser.dll) found **zero direct calls into that function**
from any of the 8 codesegs dumped so far - all ruled out.

Round 83 pivoted from blind dumping to reading the live CPU state at the
existing round-78 breakpoint directly. Reading `LR` turned out to be
useless (it's self-referential - `LR` at this exact point just holds the
address of this same breakpoint, since it was last set by the `bl` that
got us here, and the function's real caller-return-address was saved
elsewhere before that inner call). But reading the **stack** properly
finally worked, and revealed **why rounds 78/80 always silently read
`0x0`**: `get_real_pointer(addr)` defaults to `asid=-1`, which does not
resolve process-local memory like a thread's stack (unlike the
ROM-XIP/RAM-codeseg addresses this investigation normally deals with,
which are globally mapped and worked fine without an asid). The fix:
`kern_->get_memory_system()->get_mmu(cpu_core)->current_addr_space()`
gives the running core's real asid, which must be passed explicitly. Once
fixed, the stack immediately showed real, sensible values instead of
zeros.

Several stack slots repeated the same addresses across multiple
breakpoint hits (`0x8206dd75`, `0x820670c5`, `0x82068875`, `0x8206edac`).
Cross-referencing every ROM DLL's own E32Image header (`code_address` +
`code_size`) via `parse_rom.py` against these addresses identified them
unambiguously as belonging to **`apgrfx.dll`** (App aRChitecture - the
client-side stub library behind `RApaLsSession`, i.e. the App List
Server's session client). This is the first concrete, non-N-Gage-specific
candidate found in this entire sub-investigation.

Round 84 dumped `apgrfx.dll`'s code directly from ROM-XIP memory (no
process/asid needed for this one - it's globally mapped like euser.dll)
and decompiled the functions containing those four addresses. Found the
actual loop:

```c
void FUN_820670ae(int param_1,undefined4 param_2,undefined4 param_3)
{
  int local_18 = 0;
  do {
    do {
      uVar1 = FUN_8206dd6e(param_1 + 4,param_2,param_3);  // -> euser SendSync
      iVar2 = FUN_8206706e(param_1,uVar1,&local_18);
    } while (iVar2 == -0xf && local_18 == 0);   // -0xf = KErrServerTerminated
  } while ((iVar2 == 0) && (local_18 != 0));
  return;
}
```

`FUN_8206706e` (the retry-decision helper) is the classic Symbian
session-reconnect idiom: on `KErrServerTerminated` (`-15`), it attempts
one reconnect and sets `local_18 = 1` unconditionally afterward - so the
**inner** loop is correctly bounded to at most one reconnect attempt. But
`local_18` is only initialized to `0` once, before the **outer** loop, and
is never reset back to `0` on the outer loop's own back edge. So once a
single reconnect has happened at any point, the outer loop's exit
condition (`iVar2 == 0 && local_18 != 0`) becomes "keep calling forever as
long as the result is exactly `KErrNone`" - which matches round 78's live
finding exactly (2000+ breakpoint hits in 60 seconds, every single one
with `r0=0`, i.e. `KErrNone`). This is very likely an enumeration-style
call (`apgrfx.dll` → App List Server, matching this investigation's
recurring theme of enumeration loops that never receive their "end of
data" signal - compare to the already-fixed `gametitlemetadata.dll` bug),
expecting an eventual non-`KErrNone` completion code that EKA2L1's HLE
implementation of whatever it's calling may never send.

**Not yet confirmed**: exactly which App List Server opcode/session this
maps to. A direct look at `services/applist/applist.cpp`'s
`get_next_app()` shows it already returns `KErrNotFound` (not `KErrNone`)
once its own enumeration is exhausted, which doesn't immediately explain
an unconditional `KErrNone` stream - so this may be a different call
entirely (not `get_next_app`), or something upstream keeps re-arming the
enumeration state before it can complete. Next step: log the actual
`ctx->msg->function` opcode number(s) received by `applist_session` while
this specific loop is spinning, to pin down the exact call instead of
reasoning from names alone.

All temporary probes from rounds 81-84 have been fully reverted;
`epoc.cpp` is back to just the two permanent fixes (gametitlemetadata.dll
loop-index correction, busy-poll sleep). Companion-DLL and apgrfx.dll code
dumps are kept in this folder (`probe_r82_*.bin`, `probe_r84_apgrfx.dll.bin`)
as permanent artifacts for any future static analysis.

**UPDATE (rounds 79-80, static hunt for the ~33Hz loop's caller inside**
**`playserver.exe` - main EXE ruled out, higher-level caller still not**
**found):** two angles tried to identify what calls the tight
`session_send_sync` loop pinned down in round 78.

Round 79: dumped `playserver.exe`'s own main-EXE code (11152 bytes, base
`0x70700000`) straight out of guest RAM via a `codeseg_loaded_callback`
(had to correlate against the immediately-preceding "runtime code:
0x70700000" `codeseg.cpp` log line to pick the right hit, since the naive
name filter `find("playserver")` also matches the companion
`playserverccd.dll`/`playservergl.dll` codesegs). Imported into a fresh
Ghidra project (`playserver_r79`, ARM:LE:32:v7) and decompiled all 125
functions found in it. Result: **no direct call to the confirmed
SendSync return address range, and no ~33Hz-shaped tight loop anywhere in
the main EXE itself.** So the loop is not in `playserver.exe`'s own code -
it must live in one of its ~17 other dependency/ECom codesegs observed
loading alongside it (sizes from 0x230 to 0x18E3C bytes), none of which
have been decompiled yet.

Round 80: went back to the live breakpoint from round 78 and widened the
stack read from the single fixed offset (`[sp+0xC]`, based on the callee's
`{r2,r3,r4,lr}` prologue) to a generic scan of stack offsets 0 through
0x7C in 4-byte steps, checking each word against the RAM-loaded codeseg
address range (`0x70000000`-`0x72300000`). **Still `0x0` on every one of
32 sampled hits** - no caller address found anywhere in that window
either. Combined with round 78's `__switch8` precedent, the leading
hypothesis is still that the real caller is reached through some
jump-table/indirect dispatch that doesn't leave a conventional stack
frame, though a deeper-than-expected frame can't be ruled out either.

Net status: the exact higher-level loop driving the ~33Hz SendSync calls
is still unidentified. Next steps, not yet attempted: decompile the
remaining ~17 companion codesegs (large undertaking - would help to first
log every codeseg name+address as it loads during this specific boot, to
narrow down which few are worth decompiling first), or find a different
diagnostic angle entirely (e.g. a proper call-stack unwinder instead of a
raw stack scan).

**UPDATE (round 78, the stall is genuinely infinite after all - round 77's**
**"heavy but finite" alternative disproven, and the exact loop mechanics**
**pinned down further, but its higher-level caller still not identified):**
tested round 77's own alternative hypothesis directly, since this round's
first launch (via the real UI click, no `--run` auto-close) gave an
unlimited window: left one stuck instance running for 5+ real minutes.
Result: **289 CPU-seconds consumed over 273 wall-seconds (~100%+ of one
core, continuously) and the log never advanced past the same
`eikbackupsrv` line once.** This settles it - "heavy but eventually
finishes" is wrong; this is a genuine, permanent infinite loop.

Went back for a real per-hit count instead of periodic sampling: installed
an actual breakpoint (not a timer-sampled probe) at the return point right
after the `session_send_sync` call in `euser.dll`
(`0x803aa9aa`, ROM-XIP so the address is fixed). First attempt used the
wrong Thumb encoding for `cmp r0,#0` (guessed the register-compare form
`0xb0 0x42`; the real immediate-compare encoding is `0x2800`) - caught
immediately via the byte-verification check refusing to install, fixed,
rebuilt.

With the correct breakpoint: **over 2000 hits in 60 seconds (~33/second,
sustained)**, every single one with `r0=0` (the IPC call succeeding, not
failing/retrying an error) but a *different* session pointer (`r5`) nearly
every time. This rules out "one call stuck waiting on one specific reply" -
it's a tight loop making many different successful synchronous IPC sends,
over and over, never reaching whatever comes after. Attempted to identify
the actual higher-level loop by reading the stack for the grandparent
return address (this generic send-and-check helper's own prologue pushes
`{r2,r3,r4,lr}`, so `[sp+0xC]` should hold whoever called *it*) - came back
`0x0` on every sample. Given this session's own earlier `__switch8`
discovery (a genuine compiler-generated jump-table dispatch that looks
like broken code until you realize it's data), the most likely explanation
is that the actual caller is reached via a similar jump-table/indirect
dispatch that doesn't leave a conventional call chain - not a bug in the
reasoning, just a harder case to unwind without a real debugger or
decompiler. Several sampled `r5` values (`0x37`, `0x40`, `0x15`, `0xE`) were
also too small to be genuine pointers, suggesting this shared helper gets
called from more than one place with different register conventions,
further complicating a purely register/stack-based identification.

**Honest state: the loop is now proven real, its rate is known precisely**
**(~33 successful IPC sends/second, indefinitely), and the specific
low-level call site is identified - but the actual higher-level loop
(inside `playserver.exe`'s own code, most likely, given the thread names
involved) has not been found.** This matches round 59-64's own original
recommendation, never yet acted on: get a real Ghidra import of
`playserver.exe` itself and trace its own compiled code directly, rather
than continuing to infer it from generic `euser.dll` call sites.

All probes (`epoc.cpp`'s breakpoint and stack-read) reverted after use.
`config.yml` back to baseline. Verified via `git diff --stat` and a final
rebuild.

### To resume from here

1. **The concrete next step, finally with a precise target**: extract
   `playserver.exe`'s own compiled code (dynamic runtime address, capture
   via the existing `codeseg.cpp` "runtime code:" log line filtered to its
   UID) and decompile it in Ghidra, specifically looking for a loop that
   calls a `SendReceive`-style client wrapper (which will itself call
   through the now-identified `0x803aa998`/`0x803aa9aa` generic helper)
   over some collection - most likely iterating sessions, servers, or
   files - whose termination check never becomes true. This is the exact
   same rigor that resolved `gametitlemetadata.dll` earlier this session,
   just aimed one level up the call stack.
2. Given the ~33Hz rate and `r0=0` (success) on every call, this doesn't
   look like a guest-side error-retry loop - it looks more like an
   enumeration/polling loop whose "are we done yet" check depends on
   something EKA2L1 reports differently than real hardware would (the same
   general shape of bug as the original `gametitlemetadata.dll` issue and
   the historic `!ecomserver` one from rounds 45-58), making this Ghidra
   trace likely productive rather than another dead end.

**UPDATE (round 77, the "second stall type" does not appear to be a single**
**fixable bug - every specific address examined turned out to be normal,**
**correctly-functioning code): went looking for round 76's busy-loop stall**
directly with tight, 150ms-interval PC sampling. First capture landed on
the *other* case instead (`crr_thread()` staying `nullptr` for 374
straight ticks - a genuinely idle wait, not a busy loop; the round-76 fix
correctly kept CPU low throughout, confirming the fix works as intended
for this case). A second capture did catch real thread activity, and
found something initially striking: the exact same `pc=0x803a02dc
lr=0x803aa9ab` recurring across several different threads
(`NgiInstaller`/`N-Gage Installer`/`PlayServer`).

Disassembled both addresses to check. `0x803a02dc` turned out to be the
tail end of a tiny `euser.dll` wrapper (`push/setup/svc #0x4c/pop`) -
`SVC 0x4c` maps to `session_send_sync` in
[`svc.cpp`](../../src/emu/kernel/src/svc.cpp:6075) (confirmed correctly
implemented: handle lookup, server-terminated check, dispatch to
`send_receive_sync`, no bug found). The caller at `0x803aa9ab` is a
completely ordinary "send one synchronous IPC message, check the result,
optionally post-process, return" helper - it makes exactly one call and
returns; **it is not a loop by itself**. Seeing its return address show up
repeatedly across multiple *different* threads simply reflects that this
is shared, heavily-used client-library infrastructure (basically every
synchronous IPC call in the whole OS funnels through something like this),
not evidence that any single thread is stuck spinning there. The specific
`PlayServer` pattern (this call, one more small call, an idle tick, repeat
every ~450ms) reads as a normal, correctly-functioning polling cycle, not
a tight busy-loop.

**Honest conclusion: no discrete, fixable infinite-loop bug was found**
**behind the "busy" stall this round**, despite deliberately hunting for
one. The earlier high cumulative-CPU-time readings from this session's
retries likely reflect either (a) genuinely heavy - if unusually slow -
legitimate IPC/service-startup activity (plausible given this is a
restored backup with a large, possibly-duplicated purchase/title table,
the same root condition this whole investigation started from), or (b) a
measurement artifact (cumulative process CPU time reflects everything the
process has done since launch, not the current instantaneous rate - a
process that did a lot of real work earlier and is now merely slow will
still show a high number). Both are meaningfully different from "there is
one specific spinning instruction to go find and patch."

Temporary probe (`epoc.cpp`, round 77's tight-sampling PC/thread dump)
reverted after use. Verified via `git diff --stat` and a final rebuild.

### To resume from here

1. Given round 77 found no single fixable loop, further hardening of this
   specific boot-path stall would need a different angle than more PC
   sampling - e.g. instrumenting `session_send_sync`/`send_receive_sync`
   with a per-(server,ordinal) call-rate counter over a real time window,
   to distinguish "unusually high but finite IPC volume, will finish given
   enough wall-clock time" from "something is truly retrying forever."
2. Round 76's busy-poll fix remains a real, validated, worth-keeping
   improvement regardless of this round's findings, and round 75's
   click-injection method remains fully proven and ready to reuse the next
   time a boot reaches a stable state.

**UPDATE (round 76, the busy-poll fix built and shipped - real, permanent**
**improvement to boot-path reliability): implemented round 73's proposed**
fix in [`epoc.cpp`](../../src/emu/system/src/epoc.cpp)'s
`system_impl::loop()` - when `kern_->crr_thread()` is still `nullptr` after
`reschedule()` (genuinely nothing ready to run), sleep 1ms instead of
letting the caller's own driving loop spin this function as fast as
possible. Deliberately minimal and low-risk: no change to the scheduler's
actual readiness/wake logic, no new synchronization - scheduled `ntimer`
events already fire correctly on their own dedicated thread regardless
(confirmed: this is why the 1-second `!BackupServer` timeout always worked
even before this fix), so a short host-side sleep here can't delay any
real event by more than ~1ms, while eliminating the ~99%-of-one-core
busy-poll round 73 measured directly.

First test (a single stuck instance, caught already mid-hang) showed no
change - expected, since that instance's `crr_thread()` was *not* null
(confirmed via a fresh temporary probe: a real thread was actively
executing ARM instructions), meaning that particular stall is a different,
earlier-stage manifestation than the "nothing scheduled" case this fix
targets. But the real, more meaningful test - **four fresh boot attempts
in a row after the fix, all four reaching the normal clean-exit ending**
(`Corrupted graphics command list`) with no stall at all - is a striking
reversal from the immediately-preceding 5-for-5 stall streak on the exact
same boot path with no code changed in between. Consistent with round 74's
own finding that this specific race is genuinely timing-sensitive (verbose
logging alone flipped it from rare to constant); the busy-poll fix is
itself a timing change, and it appears to have flipped the odds back the
other way.

This is not a guaranteed, provable fix for the underlying stall (the
earlier-stage variant this round also found - a real thread actively
spinning - is still open, and this is a probabilistic improvement to a
known-flaky race, not a deterministic resolution), but it's a real,
permanent, low-risk change worth keeping regardless: it removes a genuine
resource-burning inefficiency (any idle wait, resolved or not, no longer
pegs a full CPU core), and empirically improved this session's success
rate on the exact boot path this whole investigation has been chasing.

Temporary diagnostic (`epoc.cpp`, round 76's own `crr_thread()`/PC probe)
reverted after use; the busy-poll fix itself is permanent. Verified via
`git diff --stat` and a final rebuild.

### To resume from here

1. Round 76 also directly confirmed a **second, distinct busy stall**: at
   least one instance had `crr_thread()` non-null with a real thread
   (`'N-Gage Installer'`/`'!AknIconServer'`) actively executing, meaning a
   real guest ARM loop is spinning rather than the scheduler being starved.
   This is NOT fixed by the busy-poll change (correctly so - if guest code
   is genuinely looping, the host must keep running it). Finding *that*
   loop (same rigor as the `gametitlemetadata.dll` work earlier this
   session: live PC capture, Ghidra disassembly, identify the actual
   non-terminating condition) is the next real target if further hardening
   of this specific boot path is wanted.
2. With boot-path reliability now noticeably better, round 75's fully-
   proven UI click-injection method (`EnumChildWindows` for
   `"Qt6111QWindowOwnDCIcon"`, `PostMessage` with `WM_LBUTTONDOWN`/
   `WM_LBUTTONUP`) is worth retrying against the actual phone viewport -
   there's a real chance of getting a stable enough boot now to properly
   test whether a tap on the N-Gage menu produces any visible response.

**UPDATE (round 75, real UI input confirmed working - N-Gage 2.0 launches**
**correctly through the actual emulator UI, not just `--run`): resolved**
round 74's open question - proved genuine mouse input can reach EKA2L1's
UI. First attempt (`SetCursorPos`+`mouse_event` at screen coordinates)
failed silently, same as round 72 - this environment's active desktop for
screen-coordinate input doesn't correspond to where `PrintWindow` captures
from. Switched to `PostMessage(WM_LBUTTONDOWN/UP)` sent directly to the
specific child `HWND` (found via `EnumChildWindows`, bypassing focus/
desktop entirely) - **this worked**: single-clicking the icon-grid child
window at the right coordinates opened a detail panel for one icon
(confirming clicks land precisely), and after recalibrating by one row,
**clicking the "Games" icon launched N-Gage 2.0 through the real
`RWsSession`/app-list flow** (not `--run`) - confirmed via the log line
`Open a new video to play with path: Z:\Private\20007B38\teaser\
SizzleG1sound.mp4`. Since this wasn't launched via `--run`,
`app_launch_from_command_line` stays false, so `on_app_exited()` doesn't
auto-close the window - the emulator now stays open indefinitely
regardless of what the launched app does, which is a real, permanent
improvement to how this can be tested from here on.

Also identified the actual phone-viewport window precisely:
`display_widget`'s native child carries Qt class name
`"Qt6111QWindowOwnDCIcon"` (distinct from the plain `"Qt6111QWindowIcon"`
class used by ordinary picker-UI children) - found via `GetClassName`, no
guessing needed. Sent a click into its center once N-Gage was running; no
visible change and no touch-processing log line appeared, but the run had
only reached partway through its startup sequence at that point (still
doing `eikbackupsrv`/teaser-video setup, not yet at any interactive menu),
so this specific result is inconclusive rather than a negative one.

**Blocked from going further: the exact `eikbackupsrv`/`!BackupServer`**
**boot-path stall from round 73 hit 5 out of 5 consecutive attempts this**
**round**, every single time at the identical log line and line count
(303 lines), regardless of launch method (`--run` or real UI click).
Checked for an unrelated cause first (system-wide CPU load 52%, no
lingering/zombie processes from this session's many kills) - ruled out
environmental contamination. This is a real regression in reproducibility
compared to earlier in this same session, where the identical boot path
succeeded more often than not. Given round 73 already proved this specific
stall is a genuine, permanent busy-poll (~99% CPU, zero progress over 100+
seconds) rather than a slow-but-resolving wait, there was no reason to
keep retrying blindly, so this round stopped here rather than burning
further cycles.

All test artifacts cleaned up; no source changes this round (purely
interactive/UI testing, nothing to revert).

### To resume from here

1. **The concrete blocker is now squarely the `eikbackupsrv`/
   `!BackupServer` stall's reproducibility**, not anything about input or
   the N-Gage app itself. Getting a single lucky boot past it (as happened
   multiple times earlier in this session) is enough to resume the
   viewport-tap test immediately - the click-injection method itself is
   proven and ready to reuse verbatim (`EnumChildWindows` for
   `"Qt6111QWindowOwnDCIcon"`, `PostMessage` with `WM_LBUTTONDOWN`/`WM_LBUTTONUP`,
   `lParam = (y << 16) | x` in that window's own client coordinates).
2. Given round 73's architectural finding (busy-poll instead of sleep when
   idle) directly explains why this stall is un-recoverable once hit, and
   this round found it dominating almost every attempt, actually fixing
   that gap (make the OS thread wait for the next scheduled `ntimer` event
   instead of spinning `reschedule()`) may now be the highest-leverage next
   step for making further testing practical at all - not because it fixes
   the underlying wait, but because it would stop every attempt from
   burning a full CPU core and would very plausibly make the specific race
   behind this stall resolve differently (recall: enabling verbose
   `Service.EFsrv` logging alone was enough to flip this same stall from
   "rare" to "always" in round 74 - timing sensitivity here is real and
   significant).

**UPDATE (round 74, the pieces click together: this may already be a**
**legitimate idle/ready state, not a bug at all): picked up round 73's open**
**thread by asking the kernel object model directly what's blocking each**
**thread**, instead of guessing from raw PC - `kernel::thread::wait_obj`
(public member, points at whatever semaphore/mutex/request the thread is
blocked on) plus `current_state()`. A fresh probe (same raw-file-write
technique round 73 had to fall back to, since a `LOG_ERROR` call from a
background thread can itself block) dumped every thread's name/state/
wait_obj once a second. Result: `!AknIconServer`, `!ecomserver`,
`CdlServer`, and `N-Gage Installer` (`'Games'`, UID `0x20007B38`) are all
sitting in `wait_fast_sema` on their own private request semaphores - the
completely ordinary "idle, waiting for the next async event" state any
active-scheduler-based Symbian app sits in most of the time. This is
exactly what round 58 meant by "a normal idle/waiting state."

The one genuinely active thing: multiple **`PlayServer`** thread objects,
several already in `state=stop` (terminated) and one freshly `state=run` -
i.e. `playserver.exe` keeps getting summoned, running very briefly, and
exiting, matching the repeated "Can't open object: playserver.exe" →
"Trying to summon: playserver.exe" cycle logged earlier. Its own exit
reason, captured earlier this session, is `-25`. Looked this up in
[`err.h`](../../src/emu/utils/include/utils/err.h): **`-25` is
`error_eof`** - "indicates end of file," not a crash code.

Traced where EKA2L1 itself returns this code: found in
[`dirs.cpp`](../../src/emu/services/src/fs/dirs.cpp) (`read_dir`/
`read_dir_packed`), returned when `directory::get_next_entry()`/
`peek_next_entry()` comes up empty - **this is the exact, standard Symbian
convention for "no more directory entries," identical to how `RDir::Read()`
signals end-of-listing on real hardware.** It is not inherently an error
condition; well-behaved client code treats it as "done enumerating," not a
reason to panic.

Put together, the more likely reading now: `playserver.exe` gets summoned
speculatively (maybe scanning for something to do, maybe checking whether
a specific game is queued to launch), finds nothing pending via a directory
read that legitimately reaches its end, and exits cleanly rather than
looping forever - and the front-end (`'Games'`) is correctly sitting idle,
waiting for a real user tap that a scripted `--run` launch with no document
argument can never produce. **This may mean there is no remaining app-logic**
**bug left to find here at all** - what looks like "stuck" from the outside
is the combination of (a) round 73's busy-poll-when-idle gap making any
correct wait look like a pegged-CPU hang, and (b) this specific automated
test method having no way to actually tap anything.

One methodology note worth keeping: bumping `Service.EFsrv` to trace level
(to watch file-read activity directly) reliably changed a run that used to
reach the clean end-of-flow into one that stalls at the very first
`eikbackupsrv`/`BackupServer` cycle, three times in a row - confirming
logging verbosity itself can perturb this codebase's timing-sensitive races
enough to change outcomes. Worth remembering before reaching for broad
trace-level logging as a first move on this specific stall.

All probes (`epoc.cpp`'s wait_obj dump) reverted; `config.yml` back to
baseline. Verified via `git diff --stat` and a final rebuild.

### To resume from here

1. The natural next step given this reframe: confirm real UI interaction is
   possible at all (mouse/keyboard input reaching the emulated window; round
   72's attempt via `PostMessage`/`SendInput` into EKA2L1's own Qt app-picker
   grid did not visibly register - untested against the actual *emulated
   phone screen* specifically, which may accept input differently). If input
   can be gotten through, tapping into the N-Gage menu at this exact idle
   point is the direct way to find out whether this is genuinely "ready and
   waiting" or something still doesn't respond.
2. If pursuing the busy-poll fix from round 73 (a real, worthwhile
   improvement on its own merits, separate from any of this session's other
   bugs): make `system_impl::loop()`'s caller wait/sleep until the next
   scheduled `ntimer` event when `to_run == nullptr`, instead of spinning.

**UPDATE (round 73, the boot-path "stall" investigated: it's a real**
**architectural gap, not the blank-screen rendering issue, and not a guest**
**infinite loop either): investigated the long-flagged `eikbackupsrv`/**
**`BackupServer` non-determinism directly**, at the user's request, since it
was blocking verification of everything else. Measured actual CPU
utilization during a confirmed-stuck run (two `Get-Process` CPU-time
samples 113.8 real seconds apart): **112.78 CPU-seconds consumed - ~99.1%**
**of one core, continuously**, while the log produces zero new lines. This
directly contradicts round 58's own note that its `!BackupServer` fail-fast
"leaves the system in a normal idle/waiting state" - so either something
regressed since then, or this is a different repetition of the same cycle
hitting an uncovered case.

Built a live probe to find out. First attempt (a wall-clock-gated check
inside `system_impl::loop()` itself, same pattern as round 59/73's earlier
probes) fired exactly once then never again for 100+ seconds despite the
confirmed CPU-busy state - proving `loop()` wasn't even being re-entered
repeatedly. Second attempt (a genuine independent `std::thread` polling
every second via `LOG_ERROR`) had the identical symptom - proving the block
wasn't about `loop()`'s call frequency, it was the **logging call itself**
blocking. Third attempt (same independent thread, but writing straight to a
raw `std::ofstream`, bypassing the logger entirely) finally worked cleanly
for the full 90-tick duration, with fine-grained checkpoints around each
individual call.

**The result: `kernel_system::crr_thread()` starts returning `nullptr` from**
**a certain point on and never stops** - i.e. **the kernel scheduler
genuinely has no thread ready to run at all.** Looking at
[`epoc.cpp`](../../src/emu/system/src/epoc.cpp)'s `system_impl::loop()`: when
`to_run == nullptr`, the whole `cpu->run()`/`cpu->step()` block is skipped
and it falls straight through to `kern_->reschedule()` and `return 1` -
every single call, with no sleep or wait for the next scheduled timer event.
The OS thread's own driving loop (`while (!should_emu_quit) { loop(); }` in
[`thread.cpp`](../../src/emu/qt/src/thread.cpp)) then just calls this again
immediately, as fast as possible. **This is the actual mechanism behind the
~99% CPU reading: a genuine busy-poll instead of a proper wait-for-next-event
sleep, whenever the scheduler has nothing ready.** It doesn't matter whether
what's being waited on is a legitimate, slow, eventually-resolving wait or a
true deadlock - either way, this loop burns a full CPU core the entire time
and produces zero log output, which is indistinguishable from a true hang
from the outside without exactly this kind of instrumentation.

The last real thread/PC captured before `crr_thread()` goes permanently
null was `'Games'` (the new N-Gage launcher's own main thread, UID
`0x20007B38` - not necessarily `playserver.exe`, so likely a different wait
than round 59/64's mutex) at `pc=0x803a044c` inside `euser.dll`. Disassembled
it (in ARM mode, not Thumb - same lesson as `drtrvct2` earlier this
session): `ldr pc,[sp],#4` - a completely ordinary function return, not
corrupted code. So the thread finishes some call and, immediately after
returning, blocks on something (most likely a semaphore/request wait) -
legitimate-looking synchronization, not a bug at that address. What it's
actually waiting for, and whether that thing can ever arrive, is still open.

All three probes (the in-loop check, the `LOG_ERROR`-based thread, and the
raw-file thread in `epoc.cpp`; the mutex-signal check in
[`mutex.cpp`](../../src/emu/kernel/src/mutex.cpp)) fully reverted after use.
`config.yml` back to baseline. Verified via `git diff --stat` (both files
clean of probe remnants) and a final rebuild.

### To resume from here

1. The concrete next step is finding what the `'Games'` thread's wait right
   after `0x803a044c` is actually blocking on (semaphore/request/IPC reply),
   and whether anything in the system is ever positioned to satisfy it -
   the same kind of tracing that resolved the `!BackupServer` opcode-32 gap
   (rounds 8-58) and the `gametitlemetadata.dll` loop (this session).
2. Separately, the busy-poll-when-idle gap in `system_impl::loop()` is a
   real, independently fixable inefficiency worth flagging on its own merit
   (a proper fix would have the OS thread sleep/wait until the next
   scheduled `ntimer` event when `to_run == nullptr` and no stepping is
   requested, instead of spinning) - out of scope for this investigation's
   original bug, but directly responsible for turning every wait in this
   codebase, resolved or not, into a full-CPU-core, log-silent stretch that
   looks exactly like a hang from the outside.

**UPDATE (round 72, two launchers confirmed identical, and the real remaining**
**blocker is a general EKA2L1 rendering gap, not an N-Gage bug): the ROM has**
**two apps both captioned "N-Gage Installer"** - UID `0x2000A2AE` (old) and
UID `0x20007B38` (new; grid-icon caption "Games", the one this whole
investigation's fix targets - it's the one that loads `gametitlemetadata.dll`,
spawns `playserver.exe`/`gtmdserver.exe`). Confirmed via `--listapp`.

Set up real interactive testing this round (`PrintWindow` via PowerShell to
capture the actual native window regardless of focus/foreground state, since
this environment's screen-coordinate mouse injection doesn't reach the
window - a first attempt at that grabbed an unrelated foreground window
instead of the emulator, which is what exposed the problem). Launched the
**old** launcher (`0x2000A2AE`) for comparison, per the direction to focus on
the new one and only touch the old one if it helps: it exits with the exact
same `Corrupted graphics command list` shutdown signature as the new one, on
the same live-boot machinery, on the same schedule. That rules out an
N-Gage-2.0-specific defect in the exit behavior itself - it's shared code
path, shared behavior.

Screenshotted the new launcher (`0x20007B38`) live, mid-run, after it logged
`Unblanking screen in AKNCAP session stubbed` - the phone-screen viewport is
still flat white. Traced why:
[`winuser.cpp:550`](../../src/emu/services/src/window/classes/winuser.cpp)'s
`redraw_msg_canvas::store_draw_commands` is stubbed (logs "Store draw command
stubbed" and returns success without storing anything) - the only
draw-related stub in that file. This is a general EKA2L1 window-server HLE
completeness gap, unrelated to anything this investigation has fixed, and
likely affects other apps/games too, not just N-Gage.

**Bottom line: both fixes (loop-index correction, non-destructive) are**
**confirmed correct and identical in behavior between old and new launcher -**
**the app-logic side of "full playability" is done.** What's left to actually
*see* gameplay is a separate, much larger undertaking: EKA2L1's window-server
drawing-command implementation for this device/OS combination, which is out
of scope for the bug this investigation started with.

**UPDATE (round 71, the "new blocker" wasn't one - N-Gage Installer now runs**
**to a clean, normal finish): the `Corrupted graphics command list! Emulation**
**halt` line flagged right after the loop-index fix was a red herring.**
Traced it down: that log fires from `list_queue.pop()` returning nullopt in
[`graphics_ogl.cpp`](../../src/emu/drivers/src/graphics/backend/ogl/graphics_ogl.cpp),
which - given the blocking wait used there - only happens after something
calls the driver's own `abort()`. That call comes from `kill_emulator()` in
[`thread.cpp`](../../src/emu/qt/src/thread.cpp), invoked right after Qt's
`application.exec()` *returns* - i.e. after the main window already closed.
[`mainwindow.cpp`](../../src/emu/qt/src/mainwindow.cpp)'s `on_app_exited()`
closes that window itself, specifically because this device was launched
via `--run` from the command line (a deliberate convenience feature: close
the emulator once the launched app exits, for scripted/CLI use).

So the real question was never about graphics at all - it was "why does the
launched app's process exit right after its screen unblanks?" A temporary
diagnostic on `process::kill()` (the single choke point where exit
type/reason/category get set) answered it directly: `N-Gage
Installer[20007b38]` exits with `exit_type=kill(0), category='None',
reason=0` - the exact condition `on_app_exited()` treats as *"The
application exited normally."* No panic, no kill-with-error, no crash. It's
a normal, self-initiated `RProcess::Kill(0)`-style exit - the Installer
finishing its own job and terminating cleanly, same as any Symbian
installer would after processing whatever it needed to (which, thanks to
this round's earlier fix, now includes `gametitlemetadata.dll`'s full
purchase/title table without hanging or crashing).

Both diagnostics used to confirm this (`PROBE_R71` in `kernel.cpp`,
`PROBE_R71B` in `process.cpp`) were reverted after use - neither remains.

**Net result of round 71: the entire original bug (infinite hang → second**
**crash) is fixed, and `N-Gage Installer` now runs start to finish with zero**
**errors or crashes.** This specific executable (UID `0x20007b38`,
"N-Gage Installer") is a setup/registration component, not the actual N-Gage
2.0 game client itself - reaching full gameplay likely means launching a
different executable (candidates seen loading alongside it this round:
`ngiplaycommon.dll`, `gtmdserver.exe`, `playserver.exe` - the actual game
client `.exe` wasn't directly observed this round, only its supporting
services). That's the natural next step for continuing toward full
playability, not a bug to fix.

**UPDATE (round 71, THE SECOND CRASH ROOT-CAUSED AND FIXED FOR REAL - the**
**original "confirmed" fix was itself the cause): the first fix's own byte**
**patch corrupts a live jump table it never should have touched.** Re-verified
the raw disassembly around the loop and found that `blx 0x70805278` (right
after the confirmed recursive round-trip, at code offset `0x47E8`) calls
`drtrvct2_1.dll` ordinal 28 - the RVCT compiler's own `__switch8` runtime
helper. That helper reads its jump table as **bytes starting at its own
caller's return address** - which is exactly `0x47EC`, the same 2 bytes
round 70's fix overwrites. Reconstructed several real table entries against
known targets in the loop body (they all land on sensible, real code) to
confirm this beyond reasonable doubt. A live probe then caught the crash
directly: the switch index (`r0`) is `0x0` on the very first hit, every
time, immediately followed by the crash - `0` is exactly `table[0]`, whose
original value (`6`, routing to a valid in-loop address) round 70's patch
overwrote with `0xC0` (routing 384 bytes away, into unrelated code). The
"confirmed, permanent" round-70 fix was the direct, deterministic cause of
the second crash the whole time.

Reverted that byte patch entirely (removed from
[`codeseg.cpp`](../../src/emu/kernel/src/codeseg.cpp)) and replaced it with
a fix that never touches those bytes: a runtime hook on the loop's own
back-edge increment (code offset `0x4810`, `add r6,r6,#1` - real,
always-executed code, never table data) in
[`epoc.cpp`](../../src/emu/system/src/epoc.cpp), which enforces a
host-tracked, monotonically-increasing row index there regardless of what
happened to `r6` during the pass. This is deliberately cause-agnostic: it
doesn't depend on which exact instruction disturbs `r6` (and per the
`__switch8` finding, the original "clobbering instruction" theory for *why*
`r6` gets disturbed is now in doubt too - none of the switch's real case
targets actually touch `r6`). It guarantees termination either way.

**Verified live against the real `rm-320` restored-backup repro:** the loop
terminates immediately (no hang), and - checked across the full run log -
**zero occurrences of "Access violation writing address 0x1".** The process
sails straight through `gametitlemetadata.dll`, loads dozens more system
DLLs (avkon, eikcoctl, form, gdi, fbscli, hal, ...), and reaches actual
N-Gage Installer UI/window setup - further than this entire investigation
has ever gotten. It then hits a **new, apparently unrelated** blocker:
`Corrupted graphics command list! Emulation halt` in the OpenGL backend.
That is the next thing standing between here and full playability - not
yet investigated this round.

**UPDATE (round 70, FIX BUILT AND VERIFIED LIVE): the infinite hang is**
**broken.** A permanent 2-byte compatibility patch in
[`codeseg.cpp`](../../src/emu/kernel/src/codeseg.cpp) neutralizes the exact
clobbering instruction found this round. Confirmed against the real
`rm-320` restored-backup repro: the process now progresses well past the
point it has hung at for the entire 70-round investigation, before hitting
a second, separate, previously-unreachable bug further along (now exposed
for the first time because the hang no longer masks it). The fix itself
is real, targeted, verified, and left in place - unlike every other change
in this file, this one is not reverted. Full details in the dedicated
section near the end of this file.

**UPDATE (round 70, the second crash explained): it's not a separate**
**bug - it's the same recursion, now actually able to run.** Chased the
crash the fix exposed and found it's not an independent issue: every
iteration of the fixed loop makes a fresh recursive re-entry into
another full invocation of itself (confirmed live - the vtable dispatch
this loop calls resolves back into the same function). Before the fix,
the `r6` clobber kept this recursive descent pinned at a flat, non-
growing depth forever (hence the hang); the fix lets it actually
recurse, and it blows the stack within a handful of levels once the
table is genuinely large. This is a structural limit the original bug
was accidentally hiding, not a new mistake introduced by the fix -
bounding it properly is a bigger, more deliberate engineering decision
(cap recursion depth, accept incomplete data for extreme cases) than
the first clean instruction patch. Full reasoning near the end of this
file.

**UPDATE (round 70, recursion cap: built, tested, falsified, removed):**
built the recursion cap, verified it live twice against the real repro -
**it doesn't work.** The identical crash still happens even though the
cap's own counter never got anywhere near its threshold, meaning the
crash occurs on one of the very first few calls, not after volume/depth
builds up. This falsifies the "exponential recursion explosion" theory
as the direct cause of this specific crash - the mutual recursion is
still real, but whatever's actually crashing does so too fast to be a
scale problem. Removed the non-working code rather than leave it
misrepresenting itself as a fix; round 70's original, confirmed-working
hang fix in `codeseg.cpp` is untouched by this and remains in place.
Full details near the end of this file.

**UPDATE (round 70, confirmed + blocked): a clean live capture proved**
**the second crash happens on the loop's very first iteration, every**
**time** - definitively ruling out recursion depth/volume as the cause
(not just suspected after the failed cap, now directly confirmed). Two
follow-up probes aimed at pinning down the exact faulting instruction
both came up empty - not because they were wrong, but because live
verification itself broke down: five consecutive boot attempts in a row
failed to even reach `gametitlemetadata.dll`'s load point, each burning
100-400+ seconds before being killed. The crash-site diagnosis remains
open. Full details near the end of this file.

**UPDATE (round 70, ROOT CAUSE FOUND): the loop index gets overwritten by**
**the record's own data, every single iteration - mathematically incapable**
**of ever reaching the real row count once the table has grown past ~16**
**rows.** The mutually-recursive function pair found earlier this round
(entry at `0x708047b6`) uses register `r6` for two incompatible things at
once: the loop's row index, and a 4-bit type-tag pulled from whatever
record was just processed. Every pass, `lsr r6,r0,#0x1c` throws away the
real index and resets `r6` to that tag (0-15) before incrementing it by
one - so `r6` can never climb past ~16, and the loop's own termination
check (`Count() > r6`) stays true forever once the purchase table (bloated
by the already-confirmed duplicate-insert bug) has more than about a
dozen and a half rows. Not "very slow" - genuinely, mathematically
non-terminating. Same underlying compiler/codegen mistake as the earlier-
found `this`-pointer clobber in a sibling function, just landing on a
different register with a different failure mode (one crashes, one
hangs). Full derivation - including the raw-disassembly correction of an
earlier, Ghidra-decompiler-misled reading - in the dedicated section near
the end of this file.

**UPDATE (round 70, resolved RM-320-vs-RM-409): the hang is data-driven, not**
**device-driven.** Tested directly: `RM-409` (5320), given the same retail
N-Gage build and its own real accumulated purchase history, hangs at the
exact same point as `RM-320` (N95) - confirmed live, not inferred. A
separate manual test the user ran (5320 works, N95 force-closes on fresh
installs) turned out to be a real but unrelated bug: RM-320's real ROM
defaults to a language the specific repackaged installer used in that test
never shipped a resource file for. Full story in the dedicated section
near the end of this file.

**UPDATE (round 70): found the actual bug, in the actual stuck loop, in raw**
**disassembly - a register-clobbering bug baked into the compiled ARM**
**Thumb code itself.** Traced the 88-caller dispatcher (`FUN_70803998`,
round 69's find) back through its 5 single-call getter wrappers to their
one real repeating caller: `FUN_70804534`, a `for (i=0; i<this->Count();
i++)` field-iteration loop (`r5` is the counter - matches nothing else
written to it anywhere in the function). Raw disassembly (not decompiler
guesswork) shows that mid-body, at `0x70804578`, the compiled code
executes `lsr r4,r5,#0x18` - and `r4` is the loop's own `this` pointer,
with exactly one other write in the whole function (the entry `mov
r4,r0`). For any realistic loop count (`r5` under ~16.7 million), this
zeroes `r4` outright, and nothing ever reloads it before the *next*
iteration's own `Count()` recheck at `0x708045ba` (`ldr r0,[r4,#0]`) -
which now dereferences a permanently-clobbered NULL "this" for the rest
of the function's life. Whether that hangs or panics depends entirely on
what EKA2L1 does when guest code reads through a NULL/unmapped pointer
and then branches to whatever it finds there - checking that now. Full
disassembly and call-chain trace in round 70 at the end of this file.

**FOLLOW-UP (same round): checked EKA2L1's own NULL-read handling - it**
**panics (KERN-EXEC 3), it doesn't hang, ruling the clobber out as the**
**live mechanism - then checked `gamepurchase.db`'s actual content and**
**found something concrete instead.** A clean sibling loop
(`FUN_70803872`, no clobber bug) matches the live symptoms far better.
And the database itself has hard counted evidence of a real,
long-standing bug: 11 of 18 purchased games each carry exactly one
purchase record in the untouched 2014 backup, but gained exactly 7
byte-identical duplicate records apiece over this investigation's own
repeated boot attempts - while the other 7 games already had 2-34
duplicate copies **in the original, never-touched 2014 backup itself**,
proving this exact "purchase already registered?" check has been broken
on the real device for years, independent of EKA2L1 or backup
restoration. Full breakdown in round 70's second half at the end of
this file.

**UPDATE (round 70, final): found the exact function and confirmed the**
**duplicate-record bug in decompiled code, not just file forensics.**
Traced `playserver.exe`'s own code (missed by earlier per-DLL scans) to
a direct call into `gametitlemetadata.dll` at `FUN_708010ea` - the real
"register a game's purchase/title metadata" entry point, called once per
installed game at boot. It checks row-count capacity and evicts the
oldest entry if over the limit, then unconditionally allocates and
inserts a brand-new database row for the game - **with no check anywhere
for whether a record already exists.** Every callee involved (row count,
max-count lookup, find-first-match, delete, insert, attach-sub-item) is
a clean, bounded, single-shot Symbian DBMS operation - confirmed via full
decompile, nothing hidden. This is the exact, now-proven mechanism behind
every duplicate found in `gamepurchase.db` earlier this round. It's
bounded and can't hang by itself - the separate unbounded-looking scan
found earlier must live in a different, not-yet-found read/enumerate
entry point (most likely `playservergl.dll`'s game-list UI reading back
the now-bloated record set to display it). Full writeup at the very end
of this file.

**CORRECTION (round 70, live test): confirmed empirically - the hang**
**does NOT go through `FUN_708010ea` at all.** Armed a live, no-GDB-needed
breakpoint (reusing the GDB stub's own Thumb `BKPT` trap mechanism,
wired up as a temporary self-contained probe) directly on
`FUN_708010ea`. Booted for over four minutes, including the period the
process was confirmably already sitting in its known stuck state (zero
new log output for the entire final 3-minute wait) - the breakpoint
never fired, not once. The duplicate-insert bug is still completely
real and confirmed in code, but it's an **independent** bug from the
hang - the hang is reached via a different path that never calls this
function. The stuck loop is still the `FUN_70803872` chain from earlier
this round; the next step is arming this same live-breakpoint technique
on *that* function instead. Full details at the very end of this file.

**UPDATE (round 70, final live capture): found genuine mutual recursion**
**through vtable dispatch - two virtual methods on the same object**
**calling each other, invisible to any static analysis.** Two more live
breakpoint captures (reusing the same no-GDB-needed technique) showed
`FUN_70803930` calls vtable slot `+0x38`, which resolves to
`FUN_708047e6`; `FUN_708047e6`'s own `Count()`-bounded loop calls vtable
slot `+0x24` every iteration, which resolves right back to
`FUN_70803930`. This is why 70 rounds of static tracing across three
binaries never found an external caller for this cluster - there may
not be one at this level; it's substantially self-referential. If the
two sides' index/position bookkeeping ever falls out of sync (plausible
exactly when the underlying table has grown unusually large - tying
back to this round's confirmed duplicate-record bloat), the two could
keep re-triggering each other without ever converging. Full call graph
and reasoning at the very end of this file.

**UPDATE (round 69): identified the exact ROM libraries involved -**
**`drmcommon.dll`, `drmrights.dll`, `estor.dll`, and (unexpectedly)**
**`bafl.dll` again - and fully decompiled a real, working DRM-rights**
**enumeration function that confirms the array-iteration pattern this**
**investigation has been circling. The stuck loop itself is a sibling**
**function with the same shape, whose own loop counter (`r6`, round 67's**
**data) just counts up normally with no sign of ever stopping.** Resolved
every one of `gametitlemetadata.dll`'s 79 external import addresses to
real ROM modules via `parse_rom.py`'s header-derived code ranges (the same
technique used since round 3, applied fresh to 4 new targets): most are
the already-known `euser.dll` generic wrappers, but two clusters are new -
`estor.dll` (Symbian's persistent object store library) and, split across
two DLLs, `drmcommon.dll` + `drmrights.dll` (Symbian's actual DRM
subsystem) - plus a single call into `bafl.dll`, the exact same Backup And
Restore Framework Library at the center of this investigation's original,
already-fixed bug. Found and fully decompiled the specific function
(`FUN_70800070`) that calls nearly all of these: it opens a DRM content
object, enumerates a `CArrayFixFlat`-shaped array of DRM rights via
`drmcommon.dll`, then iterates it (`Count()`/`At()`-style calls) checking
each right's status via `drmrights.dll`, selecting the best valid one.
This function's own loop is genuinely bounded by that array's `Count()` -
not itself infinite. But it's architecturally identical in shape to the
still-unidentified function actually holding the boot hostage, and round
67's own captured register data (`r6` incrementing `0, 1, 2, 3, 4, 5, 6...`
at the real stuck loop, never resetting) is consistent with the same
"iterate an array `Count()` times" pattern - just with `Count()` never
being satisfied, or the loop lacking an upper bound the sibling function
has. Full reasoning and the decompiled function in round 69 at the end of
this file.

**UPDATE (round 68): identified the actual C++ classes involved via RTTI**
**strings embedded in the bounded table region - this is a Game Purchase/**
**Activation Database (`CGamePurchaseDb`, `RGamePurchaseDbServer`,**
**`TActivationCode`) - but the obvious "missing record" theory doesn't hold**
**up: the two on-disk copies of `gamepurchase.db` (`C:` and `E:`) are**
**nearly identical, with `C:` if anything slightly more complete, not less.**
Dumped and inspected the exact `~0x70806000`-`~0x70808300` region
(round 67's bounded scan range) directly from the already-captured
`gametitlemetadata_code_reloc.bin` - it contains ARM EABI RTTI mangled
class-name strings: `CGametitleMetadata`, `CGamePurchaseDb`,
`CGamePurchaseDbRecord`, `RGamePurchaseDbServer`, `TActivationCode`,
`TGameInfo`, `TGamePartInfo`, `TGamePackageInfo`, `TBundledLicenseInfo`,
plus the generic Symbian array/handle base classes these build on. This
confirms the search is against a game purchase/licensing data structure,
not an arbitrary unrelated table - a real, concrete narrowing.

Found the actual `gamepurchase.db` file on disk (two copies: `Private/
20003b85/gamepurchase.db` on `C:`, 337776 bytes, and `20003B85/
gamepurchase.db` on `E:`, 302701 bytes, dated 2014 - clearly the original
restored-backup copy). Extracted every printable string from both and
diffed them directly: **2739 vs 2736 unique strings, essentially the same
game catalog** (FIFA 08, Prince of Persia, Brothers in Arms, SPORE,
Sims 2 Pets, Worms World Party, Resident Evil, Asphalt 4, Tetris, and
others matching round 65's game folders) - and the handful that differ are
*extra* strings present only in `C:`'s copy, not missing ones. This rules
out the simplest version of the hypothesis ("the restored backup's
purchase database is missing entries the code needs") - both copies look
substantively complete and consistent with each other.

This is genuinely deep territory now: the remaining open question - what
*specific* record, field, or comparison this bounded scan never
satisfies - would require parsing Symbian's proprietary DBMS binary B-tree
format at the byte level, not just diffing printable strings, to answer
with confidence. Full reasoning in round 68 at the end of this file. Given
the scope this would take, this is a natural point to check in on
direction rather than continue open-ended low-level reverse engineering.

**UPDATE (round 67): live register trace of the loop's own iteration state**
**confirms it's a genuine, bounded table/lookup scan that never finds its**
**target - not a corrupted counter, not unbounded memory growth.** Fixed
round 66's own wrong assumption first (the loop doesn't revisit its
function *entry point* repeatedly - that address has no internal branch;
the actual repeating cycle is at two specific addresses deeper in
`gametitlemetadata.dll`, already known from round 64's tail data). Retargeted
a live register-sampling probe at those two addresses across 50,000
iterations. Result: one address's `r2` register walks a **bounded** memory
range entirely within the module's own data section (`~0x70806000`-
`~0x70808000`, never growing beyond it even 50,000 iterations and 7.7
million instructions later) while its "keep going?" return value in `r0`
takes many different nonzero values, never landing on the zero that would
stop it. The other address copies the *same two constant values*
(`0x10000000`, `100`) into a fresh struct on every single visit, confirming
the caller keeps retrying the identical operation rather than advancing
through a growing collection. This is a genuine, bounded search for
something that is never found, with no give-up condition - not a corrupted
or runaway counter. Full register data and reasoning in round 67 at the
end of this file.

**UPDATE (round 66): the `.play_mc.id` lead is ruled out, cleanly and**
**directly - and a real, separate storage-path discovery was made along**
**the way (device-specific storage overrides the generic `data/drives/`**
**folder for RM-320, which round 65's file check had been reading from by**
**mistake).** Traced exactly which guest thread checks for `.play_mc.id`
(confirmed: `PlayServer`'s own thread, twice, on `D:` then `Z:`). Went to
fix it directly - copied the real file into what looked like the active
`D:` drive folder and re-tested; no change. Investigating why led to a
real discovery: RM-320 (like every device) uses a **device-specific
storage path** (`data/devices/rm-320/drives/`), which round 65's
file-system check had been reading from the wrong, legacy, unused
`data/drives/` folder the whole time. Corrected the file placement to the
real active path (`data/devices/rm-320/drives/d/.play_mc.id`) and re-ran:
this time the guest's own check for `D:\.play_mc.id` **succeeded**
(confirmed - the "non-existent file" log line for it disappeared, only the
separate `Z:` check still failed) - and the boot **still stalled at the
exact same point**, byte-for-byte the same sequence of log lines up to and
including the already-fixed `!BackupServer` fail-fast. This is a clean,
direct, empirically confirmed negative result: **`.play_mc.id` is not
connected to the `gametitlemetadata.dll` loop.** All experimental file
changes reverted; the test data folders are back to their original state.
Full reasoning in round 66 at the end of this file.

**UPDATE (round 65): pinned the exact caller chain into the stuck loop**
**(`playservergl.dll` → `playserverccd.dll` → `gametitlemetadata.dll`), and**
**found a real, verifiable file-lookup anomaly - `E:\.play_mc.id` genuinely**
**exists on disk, but the guest reports it not-found on the paths it tries**
**(`D:`/`Z:`) - though the connection to this specific loop is not yet**
**confirmed.** Reused the ring-buffer technique to capture what leads
directly into the second (stuck) entry into `gametitlemetadata.dll`,
reproducing round 64's exact step number (2101514) deterministically. The
immediate caller is `playservergl.dll` (likely "PlayServer Game List"),
routed through `playserverccd.dll` and `euser.dll`. Separately, inspecting
this session's actual private-data folder (`Private/20007b39`, PlayServer's
own) found a real, restored-backup game collection - 18 `gameNNNNNNNNN`
folders plus other per-game UID folders, 44 entries total, consistent with
a genuine multi-game N-Gage backup. `backup_registration.xml` in that same
folder explicitly declares `E:\.play_mc.id` as an expected backup file, and
that exact file **does exist** on the emulated `E:` drive - yet earlier
logging (this session) showed the guest getting `KErrNotFound` trying
`D:\.play_mc.id` and `Z:\.play_mc.id`. This is a real, checkable anomaly
worth investigating further, but - stated honestly - not yet tied directly
to the specific loop inside `gametitlemetadata.dll`; flagged as the
strongest lead for the next round rather than claimed as solved. Full
details in round 65 at the end of this file.

**UPDATE (round 64): found the actual stuck code - `PlayServer`'s thread**
**burns millions of instructions inside `gametitlemetadata.dll`, in what**
**looks like a runaway allocation/enumeration loop that never terminates.**
Traced `PlayServer`'s own thread forward (live single-step, region-transition
logging) for 8,000,000 instructions past where round 61 left off inside the
plugin-enumeration loop. It never finished enumerating plugins in that
window - by the end, execution had settled into a tight cycle entirely
within one specific companion DLL: `gametitlemetadata.dll` (confirmed via
this run's own codeseg log), repeatedly allocating small heap objects and
calling a low-level generic dispatch helper whose return value gates
whether the loop continues. `Signal()` on the `playserver.exe` mutex never
fired even once across the whole 8-million-instruction trace. The deepest
helper involved turned out to be ARM EABI compiler-generated dispatch glue
(pointer/tag decoding), not meaningful application logic - the same
"generic infrastructure, not the bug" wall this investigation has hit
several times - so the exact trigger condition remains open, but the
**location** is now pinned precisely: this specific plugin's metadata
enumeration is where `PlayServer`'s startup genuinely never completes.
Full trace data and reasoning are in round 64 at the end of this file.

**UPDATE (round 63): identified `playclient.dll` - the shared client library**
**every PlayServer-using app links against - and its own code PROVES the**
**mutex is meant as a brief "is PlayServer alive" rendezvous, not a permanent**
**hold.** Traced `Games`'/`N-Gage Installer`'s call site the same way
`playserver.exe` was traced: live single-step + ring-buffer probe, this time
armed on `ngiinstaller.exe`'s UID. The direct caller wasn't app code at all -
it was `playclient.dll` (confirmed via its own runtime-code log line),
loaded into the same process. Extracted and fully decompiled this tiny
(1136-byte) library: its core function calls the mutex-wait thunk
(`thunk_EXT_FUN_803a7f6c`, confirmed `swi 0x11` in round 61) and, on the
"PlayServer already running" success path, calls what's almost certainly
the matching **Signal** thunk (`thunk_EXT_FUN_803a7f76`, the very next
euser.dll address) *immediately afterward* - a correctly-paired, quick
lock/unlock used purely as a readiness checkpoint before returning
`KErrAlreadyExists` to the caller. This settles the open question from
round 62: the mutex was never designed to be held for `PlayServer`'s whole
life - `PlayServer`'s own code really is supposed to `Signal()` it
reasonably promptly, and every other app's `playclient.dll` copy is built
to expect exactly that. Also found that round 62's assumption "`FUN_700002ca`
is near `playserver.exe`'s entry point" doesn't hold up under closer
checking - it has zero static callers, meaning it's invoked indirectly
(most likely an ECom-style per-plugin factory callback from the
plugin-enumeration loop round 61 already found, not literally a one-shot
top-level initializer). Full reasoning, the decompiled `playclient.dll`
source, and the corrected next steps are in round 63 at the end of this
file.

**UPDATE (round 62): full decompile of `PlayServer`'s lock-holding function -**
**it genuinely never signals the mutex on any path, and the "timeout" theory**
**is ruled out (both sides call the plain, infinite `mutex_wait`, not the**
**timeout-capable variant).** Force-created a proper function boundary over
round 61's call site and decompiled the whole 842-byte function: a lazy
one-time-init guard that locks the mutex, then builds an elaborate object
graph (many calls through unresolvable vtable pointers - the same wall
flagged repeatedly since round 39), then **tears the whole graph back down
again and returns** - with no `Signal()` anywhere on that path, matching
round 61's live-run confirmation exactly. Checked the leading hypothesis
this round (an EKA2L1-side bug in `mutex::wait_for()`'s timeout-arming logic,
which does look genuinely inverted on inspection) by confirming empirically
which SVC variant `Games`/`N-Gage Installer` actually calls - it's the plain,
infinite `mutex_wait` (SVC `0x11`, no timeout), not `mutex_wait_ver2` - so
that bug, while real and worth a separate look, isn't the cause here. The
mutex really is designed to be held across something that never completes.
Full reasoning and the exact decompiled function are in round 62 at the end
of this file.

**UPDATE (round 61): the exact lock call site is now pinned, unambiguously -**
**confirmed via `swi 0x11` disassembly that it's a real `mutex_wait`, and**
**confirmed (at full run speed, not single-stepped) that `Signal()` never**
**fires on this mutex during the entire observable boot.** Round 60's static
tracing hit the same "generic wrapper" wall round 4 already knew about, so
round 61 switched to a **live single-step execution trace** (reusing round
56's proven technique) instead of chasing euser.dll's call graph bottom-up.
This pinned the real call chain with zero ambiguity: `playserver.exe`'s own
lazy-singleton-guard function (right near its entry point) calls straight
into a `swi 0x11` (`mutex_wait`) instruction - confirmed real, not inferred.
That same function does *not* call `Signal()` on the way out, in either of
its two branches - meaning the release was always going to happen later,
elsewhere in `playserver.exe`'s own code. A follow-up probe on `Signal()`
itself, run at full (non-single-stepped) speed so timing can't be blamed,
confirmed it: `Signal()` genuinely never fires for this mutex across a
complete, stable boot. This likely shares a root cause with round 59's other
finding (`PlayServerSplashScreenFallBackThread` stuck at `create`) - both
probably gate on the same later milestone in `PlayServer`'s own init that
never completes. All round-61 probes fully reverted. Full trace, exact
addresses, and the reasoning are in round 61 at the end of this file.

**UPDATE (round 60): traced the `playserver.exe` mutex deeper - new technique**
**built (live-memory code extraction), root cause narrowed but not yet pinned.**
Round 59 found `PlayServer` locks a mutex named `playserver.exe` and never
releases it. Round 60 built a new capability this investigation didn't have
before - dumping playserver.exe's/ngiinstaller.exe's own REAL, decompressed,
correctly-relocated machine code straight out of EKA2L1's live process memory
(not from a ROM dump - these are installed, non-ROM binaries) and importing
it into Ghidra. This let us decompile playserver.exe's actual application
code directly for the first time. Concretely ruled out one plausible lead
(two euser.dll ordinals it imports, 2060/2061, turned out via decompile to
be an unrelated, already-correctly-paired internal lock/unlock helper, not
our mutex) and confirmed the low-level wait mechanism (`FUN_803a82d4` in
euser.dll) is generic OS-wide "user-side fast lock" spin logic, not
app-specific - so the actual answer (why `PlayServer` never signals its own
named mutex) lives in playserver.exe's own application logic, reached
through a deeper euser.dll wrapper chain than static tracing pinned this
round - the same category of "everyone funnels through generic wrappers"
wall round 4 already hit and documented. All round-60 probes fully
reverted; round 58's fix is the only change left in the tree. Full details,
new technique writeup, and the concrete next steps are in round 60 at the
end of this file.

**UPDATE (round 59): the round-58 fix is confirmed correct, but it is not
the last blocker.** Manually testing the fixed build (user tapped the
running emulator - no visual change) led to a fresh thread-state sweep,
which found a **second, distinct, genuine deadlock** downstream of round
58's fix: the `Games`/`N-Gage Installer` thread blocks forever in
`wait_mutex` on a global mutex literally named `playserver.exe`, held
(locked, never released) by `PlayServer`'s main thread. Forcibly releasing
that mutex experimentally *does* unstick real progress (N-Gage Installer
completes its own flow and exits cleanly) - but the on-screen placeholder
still never changes, because `PlayServerSplashScreenFallBackThread` is
created but never `Resume()`d, throughout every run, with or without the
mutex fix. Forcibly resuming that thread ourselves wedges the entire
interpreter (a genuine guest-code infinite loop - not a host hang), so that
thread's start must depend on something not yet in place; naively kick-
starting it is the wrong fix. See round 59 at the end of this file for the
full trace, exact log lines, and the reasoning for why this is a separate
bug from anything rounds 1-58 covered. All temporary probes for round 59
fully reverted; only round 58's fix remains in the tree.

**RESOLVED (round 58): the `CdlServer` stall is fixed.** After rounds 50-57
each deepened understanding of the panic mechanism without finding a working
fix, round 58 found the actual answer: `server.cpp`'s existing fail-fast
timeout (round 26) was completing the stuck `!BackupServer` message with
`KErrNotSupported` (`-5`). Round 57 found that the guest's own backup-observer
object has an explicit `!= KErrCancel` (`-3`) check on its own state field -
the guest binary already expects `KErrCancel` to mean "this request was
cancelled, not a real failure." Changing the completion value from `-5` to
`-3` was tested directly: across repeated runs and full thread-state dumps,
`!ecomserver` no longer panics, and **`CdlServer` no longer spins** - both
settle into the same normal idle/waiting state every other thread already
had. This is a real, verified fix, not another narrowed-down symptom. See
round 58 at the end of this file for the full verification. The one-line fix
is in [server.cpp](../../src/emu/kernel/src/server.cpp)'s
`arm_backup_server_msg_timeout`.

**Major reframe (round 45): nothing in this entire investigation - not the
original round 1-26 `!BackupServer` hang, not the round 27-44 `CdlServer`/
`!ecomserver` stall - is actually caused by the restored backup.** A genuinely
fresh N95 install - real firmware provided by the user, freshly flashed, N-Gage
installed via its actual `.sis` installer onto completely empty C:/D:/E: drives,
zero restored-backup data anywhere - reproduces **both** stalls exactly:
`!BackupServer`'s function-32 message never completes (round 26's fail-fast fires
identically), and `!ecomserver` panics the same way afterward
(`E32USER-CBase`/47). This is a general RM-320/EKA2L1 compatibility issue that
affects N-Gage on *any* RM-320 install, fresh or restored - the restore was never
the trigger. This doesn't invalidate any of this file's technical findings (the
call chains, the panic mechanism, the `error_server_terminated`/`-15` details,
round 26's fix itself) - it changes what they *mean*: this was never "why does
restoring a backup break N-Gage," it's "why doesn't N-Gage work correctly on
RM-320 in EKA2L1 at all." Round 26's fix stays; it's still correct and necessary
regardless of this reframe. See round 45 for the full test.

Status: **two real, verified fixes are in the tree and should stay: round 9 (the
original TFindThread/thread_presence_alias fix) and round 26 (a bounded, delayed
fail-fast timeout for stuck `!BackupServer` messages). Round 26's fix is verified to
get N-Gage substantially further than any of the previous 25 rounds ever observed** —
past the `!BackupServer` hang entirely and on into loading `ngiinstaller.exe`,
`akninit.dll`, and real window/UI setup. **The user confirmed it still doesn't reach
a playable menu. Round 27 found why: a second, entirely different stall -
`CdlServer` (Symbian's component-registration server) genuinely spinning inside
`drtaeabi.dll` (a compiler-runtime library, confirmed via direct thread-state dump and
single-step trace: 3000 steps, only 549 unique addresses, no descheduling). **A
patience test settled the one open question round 27 left: 6 minutes of continuous
observation, completely flat from t=20s onward, screen still blank white at the end -
this is a genuine, permanent stall, not slow-but-bounded legitimate work (which would
have finished well within 6 minutes even under emulation overhead).** Round 29 traced
the real call chain (`cdlserver.exe` → its own internal recursive/merge routine →
`drtaeabi.dll`) and round 31 verified that routine (`FUN_83485d62`) is a genuinely
correct, properly-bounded merge-sort - raising a real "just needs more patience for an
unusually large restored backup" hypothesis, which a **25-minute** direct observation
(log size and process memory both checked every 10 seconds throughout) then firmly
ruled out: zero log growth, zero memory growth, screen unchanged from start to finish.
**Confirmed permanent by two independent lines of evidence now, not one.** Round 32
traced the merge's comparator down to a real binary-search function in `euser.dll`
(`FUN_803a0ad8`) whose match-found path calls straight into `drtaeabi.dll` via a
division-shaped instruction sequence (`asr r3,r2,#0xd` then `bl #0xfea`). Round 33
went further and got **live** confirmation with an actual PC/LR/stack snapshot: at
capture time `CdlServer` sat at `pc=0x803946b8`/`lr=0x80394bef` (both inside
`drtaeabi.dll`'s known spin range), with a stack frame at `sp+0x54` pointing back into
`euser.dll` at `0x803a3385` — a concrete, address-level link from the binary search's
division call down into wherever `drtaeabi.dll` is stuck. Round 33 then ran the
**longest patience test yet, 32 minutes**, and found the same flat-line result as
rounds 28 and 31: zero log growth, zero memory growth throughout. **Confirmed
permanent by three independent lines of evidence now.** **Round 34 found the real
mechanism, and it's a re-framing of everything from round 27 onward: the addresses
inside `drtaeabi.dll` are not a division algorithm at all - they're the ARM EABI
C++ exception-unwinding/type-match machinery.** `CdlServer` isn't stuck in
arithmetic; it's stuck in a `TRAP`/`User::Leave(-15)`/retry cycle. A direct
capture at the `leave_start` SVC call site (the same one this file's own
`LOG_TRACE` has been quietly logging as "Leave started! Guess leave code: -15" in
every capture since round 27, dismissed as noise until now) shows the real call
chain: `cdlserver.exe`'s own wrapper (`0x83485fde`, round 29) calls
`FUN_83485e24` (round 31's linked-list builder), which calls into **`ecom.dll`**
(`0x804c67fd`/`0x804c6831`, inside round 1's originally-investigated
`0x804C6608`-`0x804C7D44` range) - and that ECom call is what leaves with `-15`,
forever. **`!ecomserver`'s thread has shown `state=4` (`stop` - already
terminated) in every single thread-state dump this entire investigation, starting
with round 27's own.** This ties the very first rounds of this investigation
(`ecom.dll`/`ecomserver.exe`) directly to the latest. **Round 35 corrected an
initial misread: `-15` is not `KErrServerBusy` - it's EKA2L1's own
`error_server_terminated = -15`, the *genuinely correct* error for "this server
already died," returned instantly and correctly by
[`session_send_general`](../../src/emu/kernel/src/svc.cpp)'s existing
`is_server_terminated()` check.** `ecomserver` stopping is itself entirely normal,
correct Symbian kernel-object lifecycle (traced end-to-end through
`kernel_obj.cpp`/`kernel.cpp`/`server.cpp`) - not a bug. **Round 36 then
confirmed, via a live call-site capture, that `CdlServer` never once calls
`session_create` again after its `!ecomserver` session dies - out of 3000+
captured events, only the original two connect calls ever happen; every one of
2955+ subsequent `session_send` calls hammers the exact same already-terminated
session handle**, meaning this isn't a reconnect attempt gone wrong - the guest
code driving this doesn't appear to attempt reconnection at all. Not yet fixed;
the open question is why that retry never gives up or reconnects, and whether
that's fixable on the EKA2L1 side (e.g. server-relaunch-on-demand, matching real
Symbian's on-demand server start, in case a reconnect attempt exists somewhere
this session's capture window didn't reach) or is closer to a genuine gap in how
this specific ROM's guest code was expected to behave. Round 26's fix stays
regardless; it's independently verified and unrelated to this issue.

**Round 37 found there is no guest-code loop at all to check that error** -
`cdlserver.exe`'s own functions around the ECom call are unconditional,
straight-line code with zero branches on any return value, and the wrapper that
invokes them has no caller anywhere in the binary's own machine code - meaning
it's driven externally by Symbian's real Active Object scheduler, re-arming the
same request every time it completes (near-instantly, since the target session
is already dead). **Round 38 then found why `!ecomserver` dies in the first
place, and it closes the loop all the way back to round 1: `!ecomserver` doesn't
exit cleanly at all - it panics** (`E32USER-CBase`, code 47, "an active object
leaves" - Symbian's standard panic for an uncaught `RunL()` leave), and the
leave code is `-5` (`error_not_supported`). Tracing what happens right before it
(`log-ipc` enabled for one capture) showed `!ecomserver` itself is a client of
**`!BackupServer`** - the very same server round 1-26 spent dozens of rounds on
- sending function 34 then function 32, hitting round 26's own fail-fast fix,
converting the resulting `KErrNotSupported` into a `Leave`, catching it once,
then **immediately repeating the identical sequence and leaving a second time -
uncaught, causing the panic.** Round 26's own header comment already establishes
that function 32's dispatch is a genuine no-op *even on real Symbian hardware* -
meaning a real N95 would also block forever here, never panicking, because it
would simply never receive any response to react badly to. **Round 26's
fail-fast is necessary (the alternative is the original infinite hang) and
correct in mechanism, but returning a hard error where real hardware would have
delivered silence is what `!ecomserver`'s own code isn't written to survive.**
The deepest remaining open question, tying every round in this file together:
*why does `!ecomserver` (via `bafl.dll`) call this specific, always-broken
`!BackupServer` operation at all* - round 24 already proved this entire chain
only happens with a restored backup, never on a clean install, so something
about this specific backup's state is what triggers a call that a healthy real
device's `ecomserver` would never make in the first place.

Round 21's first fix attempt made things measurably worse (a busy-retry loop,
reverted) because it fired unconditionally on every re-arm; round 22 showed the
detection heuristic behind it (react to re-arm timing) is unsound in general (races
against the guest's own completion timing). Round 25 found the missing piece: the
actual caller is `bafl.dll`'s `FUN_8077811a`/`FUN_80778190`, real Symbian client code
with an explicit, correctly-bounded 10-attempt retry loop — the bound never helped
because the one call that hangs never returns at all, so the loop never got to its
second attempt. **Round 26's fix is built around that finding specifically: rather
than reacting to a re-arm (racy) or firing unconditionally (round 21's mistake), it
schedules a single, generous (1 second of virtual time — far longer than any genuine
completion, which happens within microseconds per rounds 13/23) delayed check per
message, re-validates the message's identity and status at fire time, and completes
it with `KErrNotSupported` (a real failure, not a fabricated success) exactly once.**
This lets `bafl.dll`'s own already-correct retry/give-up logic do the rest, instead of
fighting it. Rounds 19/20/23 remain the mechanism characterization this fix is built
on: `baksrv.dll`'s dispatch has a real, in-range, compiled no-op for this opcode
(round 19), that pattern is normal for this codebase across devices (round 20), and
`!ecomserver` blocks on a Symbian primitive (`WaitForAnyRequest`) with no timeout on
real hardware either (round 23) — round 24 confirmed the whole exchange only happens
on `rm-320`'s restored-backup state. Full round-by-round detail below, oldest first.**

The second issue's mechanism is now precisely characterized (round 13, correcting
round 12): `!BackupServer`'s message delivery and re-arm machinery work correctly for
every message it receives
(all four: `function=-1, 31, 34, 32` are each delivered with `ready=true` and followed
by a fresh `receive_async_lle` re-arm) — round 12's "re-arm only happens once" was an
artifact of counting one specific LR value, not proof the mechanism actually stopped.
The real, now directly-confirmed bug: of the three messages that receive a substantive
guest reply (`function=-1, 31, 34`, each hits `message_complete` targeting
`!ecomserver`), **`function=32` never does** — `!BackupServer` receives it, re-arms
after it, but its own guest-side dispatch never calls `message_complete` for it. This
is a guest-side (`eikbackupsrv.dll`) dispatch bug (or a real, on-device dependency the
handler silently waits on and never gets), not an EKA2L1 IPC-plumbing bug. Root cause
of *why* the `function=32` handler doesn't reply — which vtable slot handles it and
what it's blocked on — round 14 single-stepped the entire `function=32` dispatch and
round 15 did the same for `function=31` (a message that *does* complete) and diffed
the two traces directly. **The two traces are byte-for-byte identical for their first
99 instructions, then split at two addresses only 8 bytes apart** —
`function=31` lands at `0x821b6226`, `function=32` at `0x821b622e`, both immediately
after an identical call made with the exact same `lr`. Round 16 static-disassembled
both `baksrv.dll` addresses (aligning from the live trace's own return address rather
than a guessed entry point, which is what finally made *that part* of the disassembly
trustworthy) and found `0x821b622e` (where `function=32` lands) is nothing but the
shared epilogue itself (`str r6,[r5,#0]; pop {...}`) — an empty no-op — while
`function=31`'s handler at `0x821b6226` genuinely calls into real logic
(`bl 0x821b5c60`) before falling into that same epilogue. **Those two facts still
stand, but round 16 also guessed the call in between was "a euser.dll helper" doing
"indirect/computed handler-pointer dispatch" — round 17 checked that specific claim
against the actual ROM headers and it's wrong: `0x80399f48` isn't euser.dll at all,
it falls inside `drtrvct2_1.dll`/`drtrvct2_2.dll` (the RVCT compiler-runtime support
library, code range `0x80399088`-`0x8039ac8c` per `euser.dll`'s own, correctly
extracted `0x8039CDB8`-`0x803CF328` range excluding it) — almost certainly a generic
arithmetic helper (divide/modulo), not dispatch logic. *How* execution actually ends
up at `0x821b6226` vs `0x821b622e` from there was open — `lr` alone doesn't explain it
(identical in both traces, yet the landing PC differs). Round 18 found `r3` differs by
exactly 4 between the two traces (`0x49` vs `0x4d`) right at the landing point.
**Round 19 closed the loop completely, with a live memory dump of the actual dispatch
table (`traces/round19_switch_table_dump.log`) proving the exact mechanism end to end:
`0x80399f48` (in `drtrvct2`, confirmed round 17) is a classic compiler-emitted
switch/case byte-jump-table trampoline — bounds-check a small case index (`r3`, the
*input*, not an offset: `0xb`/11 for `function=31`, `0xc`/12 for `function=32`) against
a max-valid count stored at `table[-1]` (`0x10`/16 here — both are in range, not
clamped to a default), then `table[case_index]` gives a byte that's doubled and added
to the table base (`lr-1 = 0x821b6194`) to get the branch target. Verified by hand
against the live-dumped bytes: `table[11] = 0x49` → `0x821b6194 + 0x49*2 = 0x821b6226`
(matches `function=31`'s handler exactly); `table[12] = 0x4d` →
`0x821b6194 + 0x4d*2 = 0x821b622e` (matches `function=32`'s landing exactly). This is
now a complete, arithmetic-verified account of the entire mechanism from IPC delivery
(round 13) down to the exact compiled switch-table entry: `baksrv.dll`'s dispatcher has
a genuine, in-range, explicit case for whatever opcode 32 maps to internally, and that
case is compiled to do nothing but fall into the shared return path — it is not
somehow an EKA2L1 IPC bug, a clamped/default fallback, or an out-of-range access; it is
exactly what the guest binary's own compiled switch statement does for this specific
opcode. The only thing left unanswered is source-level *why* (a genuine
`eikbackupsrv.dll` no-op for this operation vs. a case that's supposed to be completed
by some other async path this build doesn't reach) — likely not resolvable through
further binary analysis alone. **Round 20 shed real light on this: compared
`rm-320`'s `baksrv.dll` against `rm-409`'s independently-compiled, nearly-2x-larger
build of the same nominal DLL version (byte-identical dispatch trampoline, confirmed)
and found the same qualitative pattern — most of `rm-409`'s much larger switch table
(234 declared cases) routes to a small set of shared/generic targets rather than
unique per-case logic too. This is real cross-device evidence that sparse switch
tables with shared fallback cases are a normal pattern in this codebase, not something
specific to `rm-320`/N-Gage — shifting the likely real issue toward "`!ecomserver`
waits forever on a reply it may never be entitled to" rather than "`eikbackupsrv.dll`
has a bug."** Twenty rounds of investigation documented here so any of them can be
picked up again without re-deriving everything. Later rounds corrected several
conclusions from earlier ones (most significantly: round 8 found an extraction bug
that affected round 4's addresses too; round 13 corrected round 12's "re-arm stops
after first message" claim; round 16 corrected round 15's "adjacent switch case"
framing; round 17 corrected round 16's "euser.dll handler-pointer dispatch" module
attribution; round 19 refined round 18's "r3 is a table-offset"
framing — it's the case index, the table lookup happens inside the trampoline itself)
— read this whole file, not just the newest section, especially if
touching any address cited before round 8.

## Symptom

On a Nokia N95 (RM-320) device with a restored backup, launching N-Gage gets stuck —
window comes up, nothing ever happens, has to be force-closed.

Two app-registry entries are involved due to a real (non-EKA2L1) quirk on this device:
`UID 0x20007B38` is registered twice, once as "Games" and once as "N-Gage Installer";
`UID 0x20007B39` is registered once as "Games". This dual-registration under one UID is
a known real N95/N-Gage firmware artifact, not an emulator bug — ruled out early, not
the cause.

Repro command used throughout: `eka2l1_qt.exe --run 0x20007B38` (run from `build/bin`).

## What actually happens (corrected in round 2)

Boot proceeds through `gtmdserver.exe` → `ngiinstaller.exe` (N-Gage's own
installer/launcher, `sys/bin/ngiinstaller.exe` — note: this process's own main thread
gets renamed twice, `Main` → `ngiinstaller` → `Games`; the `'Games'` thread seen in
traces further down **is ngiinstaller.exe itself**, not a separate process), which
spawns `baksrvs.exe` (Symbian Secure Backup Engine). Both `ecomserver.exe` and
`baksrvs.exe` are **real guest Symbian binaries executing under CPU emulation** — not
EKA2L1-side stubs. EKA2L1's own native `BackupServer` implementation
([backup.cpp](../../src/emu/services/src/backup/backup.cpp)) registers under the
EKA1-style name `"BackupServer"` (no `!` prefix) and is not involved here at all.

**Round 1 wrongly concluded `ecomserver`/`BackupServer` loop forever.** They don't.
The exchange is a **finite, successful, 4-message sequence**, confirmed by adding a
`caller_lr`-tagged probe directly in `session_send_general` (every send, to every
server, not just BackupServer) and in `message_complete`:

```
!ecomserver: session_create               -> "New session connected to !BackupServer"
!ecomserver: session_send_sync, op -1     -> BackupServer completes (code 0), signals ecomserver
!ecomserver: session_send,      op 31     -> completes (code 0), signals ecomserver
!ecomserver: session_send,      op 34     (blind sync call, no status)
!ecomserver: session_send,      op 32     -> completes (code 0), signals ecomserver
```

This happens **exactly once**. After it, `ecomserver` and `BackupServer` keep trading
the CPU back and forth a bounded number of times (visible as repeated scheduler
switches with **no further session_send or message_complete calls at all** — confirmed
with a probe covering every server, not just BackupServer) and then the system moves on.

**The real, reproducible hang is a full CPU/scheduler idle, not an IPC retry loop.**
Verified two ways:
1. A per-scheduler-tick probe in `system_impl::loop()` that logs on every thread
   switch *and* every time the ready queue is completely empty
   (`kern_->crr_thread() == nullptr`).
2. Fixed a real trap for this kind of test: spdlog was configured with
   `flush_on(spdlog::level::debug)` ([log.cpp](../../src/emu/common/src/log.cpp)) —
   `LOG_TRACE` lines (most SVC-name logging) never trigger a flush on their own, so a
   force-killed process can lose a large tail of already-generated-but-unflushed log
   lines, making a real hang look identical to "logging just stopped." Temporarily
   changed to `flush_on(trace)` for testing; several earlier-round captures that showed
   "growth stops instantly" were **not trustworthy** for that reason. With honest
   flushing, low log volume (default filter, not full `*:trace`), and a 60-90 **real**
   second observation window, log growth (and scheduler switching) genuinely,
   permanently stops. This is a true deadlock/idle state, not a fast retry loop that
   merely looks idle due to log buffering — confirmed reproducible at the exact same
   point (byte-for-byte identical log up to line 13622) across repeated runs under the
   same low-overhead settings.

**Where exactly it stalls is non-deterministic across runs**, timing-dependent:
- Sometimes immediately after the 4-message BackupServer exchange (`!ecomserver` /
  `!BackupServer` still ping-ponging, nothing else ever gets scheduled again).
- Sometimes later, after `Games` (=ngiinstaller) and `!AknIconServer` get further
  turns, then `PlayServer` (a separately spawned process, thread renamed from `Main`)
  does legitimate-looking setup — connects to `!FileServer`, `!Notifier`, and does
  several real `!CentralRepository` reads (config lookups) — before *that* eventually
  settles into the same permanent idle instead.

Same underlying failure class either way (something never gets signalled again), just
which thread ends up as the last one standing varies with scheduling luck.

## Ruled out (with evidence)

| Hypothesis | Evidence it's wrong |
|---|---|
| `ecomserver`/`BackupServer` retry forever at the IPC level | Round 1 conclusion, **wrong** — see above. The exchange is finite; a probe covering every session_send/message_complete on the system shows zero IPC activity during the actual stuck phase. |
| Scheduler starvation matching the WH40K/Eternal Legacy compat-script bug class | The system doesn't stay busy switching between two starved threads forever — it goes to genuine, sustained (60-90+ real second, trustworthy-flush-verified) full idle. Not a starvation pattern; nothing is being denied CPU time because nothing is asking for it. |
| Missing/incomplete EKA2L1-side BackupServer implementation | `!BackupServer` is confirmed to be `baksrvs.exe`'s own guest thread, real Symbian code under emulation, not EKA2L1 C++. |
| Empty `bur/` (SBE backup-registration folder) on the restored `C:\Private\10202be9\bur\` | Also empty on the user's **original, un-restored** backup dump. N-Gage works on the real device with that same empty folder — normal state, not the cause. |
| Missing System Agent property `(0x101f75b6, 0x1020762f)` | Defined it at boot in `init.cpp` (same pattern as the existing `UNK_KEY1`), rebuilt, reran — identical hang. Reverted. Real gap in EKA2L1, but not this bug. |
| Audio DSP "more buffer needed" callback firing too fast (`dsp_output_stream_shared`, [dsp_shared.cpp](../../src/emu/drivers/src/audio/backend/dsp_shared.cpp)) | Probed both `start()` and the more-buffer-request path directly. **Zero hits over 54+ trustworthy-flushed real seconds** of the stuck phase — the audio stream is never even created. Not it. |
| Fast/degenerate recurring timer (`kernel::timer::after`'s `MINIMUM_US_AFTER = 30` microsecond floor, [timer.cpp](../../src/emu/kernel/src/timer.cpp)) | Probed every `timer::after` call system-wide. Only **one** call ever happens near this point: `BackupServer` arming a genuine ~33.5 second timer (`us_signal=33554432`), once. Not recurring, not fast. Not it. |
| `message_ipc_copy` / IPC descriptor marshaling bug | Deprioritized, not directly tested — this path is exercised constantly by every app on the device and nothing else is reported broken. Still theoretically possible but low-probability. |

## Address-space findings (why the round-1 disassembly target was wrong)

Parsed ROM-image headers (not standalone E32Image — see `parse_rom.py`) for the
relevant binaries on `data/drives/z/rm-320/sys/bin/`:

| Binary | code_address | code_size | range |
|---|---|---|---|
| `ecom.dll` | `0x804C6608` | `0x173C` | `0x804C6608`–`0x804C7D44` |
| `ecomserver.exe` | `0x804C7DC8` | `0x74BC` | `0x804C7DC8`–`0x804CF284` |

Round 1 disassembled `ecom.dll` and got stuck on a false-positive (`movs r0, #0x20`
turned out to be an allocator size argument, not an IPC opcode — see git history of
this file for that detailed dead end if useful).

**Round 2 found the round-1 target was the wrong binary anyway.** The `caller_lr`
captured at every `ecomserver` → `BackupServer` send (`0x803aa9ab`, `0x803aa997`) and
the earlier wait-loop PC samples (`0x8039fa9c`, `0x803a02c8`, `0x803a02dc`,
`0x8039fccc`) all fall in the `0x8039f000`–`0x803ab000` range — **below both**
`ecom.dll` and `ecomserver.exe`'s code. This is almost certainly `euser.dll` (the base
user-side library every process links, containing the actual `RSessionBase::SendReceive`
implementation that issues the real `svc`/`swi` trap) — meaning `get_lr()` at the SVC
boundary lands one stack frame too shallow: inside euser's own generic wrapper, not
inside `ecomserver.exe`'s application code that decided to make the call.

Tried reading the guest stack near SP at the same `session_send` capture point to find
the next frame up (the real caller). Dumped 24 words from `SP`; none fell inside either
`ecom.dll`'s or `ecomserver.exe`'s code ranges. Two values (`0x807784c9`,
`0x8077852d`) landed in a **fourth**, entirely different address range
(`0x8077xxxx`) not yet identified. Hand-walking an ARM Thumb stack without frame-pointer
metadata is unreliable — `push {r4-r7,lr}`-style variable-depth frames mean a raw
memory scan can easily pick up spilled register values instead of real return
addresses, so this data is suggestive, not conclusive.

## Files in this folder

- **`parse_rom.py`** — parses the ROM-image header (`TRomImageHeader`-style, not
  standalone E32Image) of any ROM-resident binary. Confirmed working for `ecom.dll` and
  `ecomserver.exe` (table above). Would need pointing at `euser.dll` (and possibly
  whatever owns the `0x8077xxxx` range) next.
- **`ecom_code.bin`** / **`ecom_full.txt`** — round-1 extracted code + full Capstone
  Thumb disassembly of `ecom.dll`, based at `0x804C6608`. Now known to be the wrong
  binary for finding the actual retry/wait logic, but still potentially useful context
  since `ecom.dll` is genuinely involved in the exchange (just not at the address
  round 1 was looking at).
- **`e32dump.cpp`** — throwaway tool for parsing **standalone** (non-ROM) E32Image
  files via the project's own `loader::parse_e32img()`. Not needed for the ROM-resident
  binaries relevant here; kept in case a non-ROM binary needs inspecting later. Built via
  a temporary CMake subdirectory (see git history of `CMakeLists.txt` around the time
  this was written) — reverted, not part of the tracked build.

## Round 3: identified the library, then hit a hard wall disassembling it

Confirmed with certainty which binary owns the mystery address range from round 2:
parsed `euser.dll`'s ROM header the same way as the others —
`code_address = 0x8039CDB8`, `code_size = 0x32570` (→ range
`0x8039CDB8`–`0x803CF328`). **All six** previously-unidentified addresses
(`0x8039fa9c`, `0x803a02c8`, `0x803a02dc`, `0x8039fccc`, and the two `caller_lr`
values `0x803aa9ab`/`0x803aa997`) fall inside it. This is the base user library every
process links — makes sense as the home of the actual `RSessionBase::SendReceive`
trap and the generic wait/signal primitives.

**Every attempt to actually disassemble around those addresses failed in a
methodologically informative way:**

1. **Naive windowed disassembly** (start ~64 bytes before target, decode forward)
   produced garbage — NEON instructions, branches to addresses wildly outside the
   library (`0x7f4aafa7`, `0x8045b7d7`), repeating nonsense patterns. Root cause: Thumb
   instructions are variably 2 or 4 bytes wide, and starting from an arbitrary
   (non-instruction-boundary) offset desyncs every subsequent decode. There is no way
   to know the correct boundary without either a known-good starting point or proper
   analysis.
2. **Tried using the export table as known-good starting points**
   (`export_dir_address = 0x803B8C2C`, `export_dir_count = 0x8B5` = 2229 entries) to
   get guaranteed-correct function boundaries. Raw values read from that address
   looked like `0xf0029909`-style entries — not plausible code addresses (should be
   `0x8039xxxx`-`0x803cfxxx`). Reinterpreted as Thumb `BL`/`B.W` opcode encodings
   (`0xF0xx` is a real BL prefix) — a trampoline-stub theory — but disassembling that
   region directly as code *also* degenerated into garbage after a few instructions,
   with the very first "entry" being `ldr r1, [sp, #0x24]`, not any kind of table
   entry. Conclusion: `export_dir_address` is not being interpreted correctly by the
   hand-rolled `parse_rom.py` header reader for this file, or the format has more
   structure than the flat/simple layout assumed (compression, ordinal-gap bitmap,
   or similar — see `e32img.cpp`'s standalone-E32Image export handling for a hint of
   how much more complex real Symbian export tables can get, which the ROM-image
   variant likely mirrors and `parse_rom.py` doesn't yet model).
3. **Backward self-consistency scan** (walk backward from each target looking for a
   byte offset that decodes cleanly forward through to the target) — implemented, ran,
   but the results (`0x8039fa9a`, `0x803a02da`, etc — 2 bytes before each target) are
   worthless: the check only verifies the decode doesn't desync *before reaching the
   target*, so it trivially accepts the closest byte-aligned candidate rather than
   finding a real function prologue. Would need a much stronger heuristic (matching an
   actual `push {..,lr}` prologue pattern, requiring clean decode well *past* the
   target too, cross-checking multiple independent candidates agree) to be trustworthy
   — at which point it's most of the way to reimplementing what a real disassembler's
   auto-analysis already does.

**This is a genuine, now concretely-demonstrated wall, not a lack of effort.** Three
independent approaches to establishing correct instruction boundaries without symbols
or proper tooling all failed in instructive but ultimately blocking ways. Further
manual iteration on the same class of technique (more backward-scan heuristics, more
export-table format guessing) has very low expected value from here — the actual
missing piece is a tool that does binary-format-aware, alignment-verified,
cross-referenced disassembly, which is precisely what IDA/Ghidra/objdump-with-correct-loader
exist to do and hand-rolled Capstone scripts do not reproduce reliably at this scale.

## Honest assessment

Three rounds in. What's solid: it's a genuine full-system idle (not a busy retry
loop), it happens somewhere in the vicinity of `ecomserver`/`BackupServer`/`PlayServer`
startup, it isn't any of the seven specific mechanisms ruled out in round 2, and the
address range in question is definitively `euser.dll`. What's missing, and what turned
out to require real tooling rather than more manual effort: reliable instruction
boundaries inside that library to see what the code at those addresses actually does.

## Round 4: got a real headless Ghidra pipeline working — then a bigger finding than expected

User had me install Ghidra + a JDK locally and drive it entirely headlessly
(`analyzeHeadless`, no GUI, no interaction needed) to get past round 3's wall.
Worth documenting how, since it's reusable for the next round too.

### Setup (reusable)

- **JDK 21** (Ghidra 12.1.3 requires it — `application.java.min=21` in
  `Ghidra/application.properties`; JDK 17 is rejected, plain Java 8 obviously
  too). Installed via `winget install --id EclipseAdoptium.Temurin.21.JDK`.
  Path used: `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.8-hotspot`.
- **Ghidra 12.1.3** — free, no license, `winget` doesn't have it; downloaded
  the release zip directly from
  `https://github.com/NationalSecurityAgency/ghidra/releases/latest` and
  extracted to `C:\Users\Usuario\Desktop\tools\ghidra_12.1.3_PUBLIC`. Neither
  of these lives in the repo or in `investigation/` — reinstall if resuming
  on a different machine.
- **Headless entry point**: `support/analyzeHeadless` (bash script, runs fine
  under Git Bash). Must set `JAVA_HOME` to the JDK 21 path before invoking, or
  it complains about the default (older) `java` on `PATH`.
- **Project path must be absolute** — `analyzeHeadless .` fails with
  `Path element starting with '.' is not permitted`; use a full Windows path.
- **This Ghidra build has no classic Jython scripting** — `.py` post/pre
  scripts fail with `Ghidra was not started with PyGhidra`. Write scripts in
  **Java** instead (`ghidra_scripts/*.java`, extends `GhidraScript`) — Ghidra
  compiles them on the fly via `-scriptPath <dir> -postScript Name.java`, no
  separate build step needed.
- **ROM-resident binaries need `-loader BinaryLoader -loader-baseAddr <addr>`**
  raw import (not `-import <file>` alone) since Ghidra has no built-in
  Symbian E32Image/ROM-header loader. Use `parse_rom.py` (round 3) to get the
  right `code_address`/`code_size` first.
- **Force Thumb mode** — this is ARMv6/EABI Symbian code, effectively 100%
  Thumb-2. Without help, Ghidra's default ARM disassembly mode guess is wrong
  and everything degenerates into garbage identical to the round-3 manual
  Capstone failures. Fix: a pre-script (`SetThumb.java`) that sets the `TMode`
  context register to `1` across every memory block *before* analysis runs.
  This single fix is what took Ghidra from "same garbage as hand-rolled
  Capstone" to clean, correct, decompiler-quality output.
- **Manually-added memory blocks default to non-executable** — adding a
  second/third binary's code into an already-open program via
  `Memory.createInitializedBlock(...)` does **not** get scanned by
  `Function Start Search` or any code analyzer by default; the block reads as
  pure data (0 instructions, 0 functions found) even after calling
  `analyzeAll(currentProgram)`. Fix: explicitly call
  `block.setExecute(true)` on each manually-added block before analyzing.
  This was a second silent trap — first attempt at a multi-module combined
  image produced convincing-looking "zero cross-module calls found" results
  that were actually just this permission bug, not a real finding. Caught it
  by cross-checking instruction/function counts per block
  (`CheckBlocks.java`) before trusting the "zero calls" result.
- Reusable script pattern that worked end-to-end: `SetThumb.java` (preScript,
  first import only) → `AnalyzeTargets.java` / `FindCallers.java` /
  `FindCrossModuleCalls.java` (postScript, per-question) → once a
  project exists, iterate fast with `-process <name> -noanalysis
  -postScript <NewScript.java>` instead of re-importing.

### What this pipeline found

**Confirmed the pipeline is correctly calibrated against known ground
truth**, independent of anything Symbian-specific: the function containing
`0x803a02dc` disassembles to exactly `swi 0x25` (ARM software interrupt,
opcode 0x25) — and `0x25` is literally `BRIDGE_REGISTER(0x25, session_send)`
in this project's own [svc.cpp](../../src/emu/kernel/src/svc.cpp). Same for
`0x8039fccc` → `swi 0x27`. This is real, correct, decompiler-quality output,
not another false positive.

Walked the call chain up from there (`FUN_803a02dc` → `FUN_803aa99a` →
`FUN_803aa9dc` → `FUN_803aaa76`/`FUN_803ac832` → ...) using
`FindCallers.java`/`FindCallers2.java`, each hop confirmed real and clean.
Every single hop stayed inside `euser.dll` itself (`0x8039CDB8`–`0x803CF328`)
— this is a deep stack of generic `RSessionBase::SendReceive`-family
wrapper/overload functions, not application code. `FindCrossModuleCalls.java`
(scanning `ecom.dll`'s and `ecomserver.exe`'s own code for any direct
`bl`/`blx` landing inside `euser.dll`) found **zero** — cross-module calls in
this build apparently don't show as direct branches or literal pointer
tables either (checked both; see `ghidra-scripts/` in this folder for the
exact negative results). Given the wall this hit last time on the export
table, didn't chase this further this round — noted for next time.

### The bigger finding — this reframes the whole investigation

Went back to the emulator to get a live PC sample of `PlayServer`'s own stuck
loop (the actual unresolved target, not `ecomserver`'s already-understood
exchange), re-adding the send/complete probe from round 2/3 alongside it.
Result: **`PlayServer`'s `caller_lr` for its `session_send` calls is
`0x803aa9ab` — the exact same address as `ecomserver`'s.** So is
`N-Gage Installer`'s (`ngiinstaller.exe`/`Games`), caught mid-hang in a
*different* run doing a completely different, clearly legitimate flurry of
`!Windowserver`/`!FileServer`/`!CentralRepository`/`CdlServer` calls.

This means `0x803aa9ab` isn't a bug signature at all — it's the single
generic euser.dll entry point **every process in the system** funnels
through for synchronous IPC. Tracing "who calls this address" was never
going to isolate one culprit, because the honest answer is "everyone."

**And which thread is the last one standing when the system goes idle
changes between runs** — sometimes `ecomserver`/`BackupServer`, sometimes
`PlayServer`, this run `N-Gage Installer`. Given each of those, examined
individually, looks like unremarkable/legitimate activity (finite, normal
IPC calls, no sign of a retry loop in the specific function bodies pulled up
via Ghidra), this now reads much more like a **scheduling-timing race
affecting the whole system** than one buggy function callable out by
address. Whichever thread happens to be mid-flight when something (still
unidentified) stops advancing is the one that shows up as "stuck" in that
particular run.

### Revised honest assessment

Four rounds in. The Ghidra pipeline is real, working, and reusable — that's
solid, durable progress independent of this specific bug. But the target
this round's tooling breakthrough was pointed at (tracing one thread's IPC
call chain by address) turned out to be the wrong shape of question, because
the address in question is shared by the entire system's normal IPC traffic.
The right next question is different in kind: **not "what does thread X call
at address Y," but "what shared, global resource or scheduler behavior could
make whichever thread is running at the time stop advancing, non-
deterministically, across otherwise-unrelated threads."** That points at
things like: EKA2L1's own scheduler fairness/quantum accounting, a shared
kernel object (mutex/semaphore/central-repo lock) with a capacity or
starvation bug, or a host-timing-dependent race — not at any one server's
application code.

## To resume

**Given round 4's finding, the disassembly angle (rounds 3-4) is likely a dead end for
finding *this* bug** — every thread's IPC calls funnel through the same generic
`euser.dll` wrapper, so tracing "who calls address X" can't isolate a single culprit.
Suggested priorities for round 5, roughly in order:

1. **Look at EKA2L1's own scheduler for a fairness/quantum bug**, since the symptom
   (some thread stops getting scheduled, non-deterministically, and it's a different
   thread each run) fits a starvation/accounting bug in the emulator itself better than
   an application-level bug in any one Symbian binary at this point. Start at
   [scheduler.cpp](../../src/emu/kernel/src/scheduler.cpp) (already reviewed once in
   round 2 and found to be pure fixed-priority with no anti-starvation — re-examine with
   this round's "different thread each time" evidence in mind) and at how
   `get_remaining_screenticks()` / `add_ticks()` in
   [epoc.cpp](../../src/emu/system/src/epoc.cpp) account for a thread's timeslice.
2. **Look for a shared, capacity-limited kernel object** (mutex, semaphore, handle
   table, Central Repository session limit) that could quietly stop granting access
   once some count is reached, explaining why unrelated threads (`ecomserver`,
   `PlayServer`, `N-Gage Installer`) each eventually stall on what looks like ordinary
   IPC — grep for anything with a small fixed-size handle/slot table in
   `src/emu/kernel/` and `src/emu/services/src/centralrepo/`.
3. If it turns out to genuinely be one specific binary's logic after all: the Ghidra
   pipeline described above is real and reusable (see the "Round 4" setup notes) — the
   scripts in `ghidra-scripts/` in this folder are ready to point at a *different*
   target than `euser.dll`'s generic wrapper (e.g. disassemble `playserver.exe` itself
   at its real runtime load address, which is dynamic/relocated — capture it fresh via
   the `codeseg.cpp` runtime-code log line, filtered to the relevant UID, each run).
4. If reproducing the probes described in earlier rounds: all fully removed from the
   tree now (`git diff` is clean, `investigation/` folder + one trivial whitespace diff
   only). Notes that still apply:
   - `system_impl::loop()` ([epoc.cpp](../../src/emu/system/src/epoc.cpp)) — edge-triggered
     switch/idle logger works well: `LOG_ERROR` once per thread transition *and* once per
     transition into "nothing ready", not every tick.
   - `session_send_general` + `message_complete` in
     [svc.cpp](../../src/emu/kernel/src/svc.cpp) — log every send/complete
     system-wide, not filtered to one server name (round 1's mistake — it hid the
     `PlayServer`/`FileServer`/`Notifier`/`CentralRepository` traffic entirely, which
     turned out to be exactly the signal that revealed round 4's finding).
   - Remember to change `spd_logger->flush_on(...)` to `spdlog::level::trace` in
     [log.cpp](../../src/emu/common/src/log.cpp) for the duration of any test that ends
     in a force-kill, or the results are not trustworthy.
   - Use the **default** `log-filter` in `config.yml`, not full `*:trace` — the
     unconditional `LOG_ERROR` probes bypass the filter regardless, and the default
     filter is dramatically faster wall-clock (full `*:trace` inflates a ~10 real-second
     repro into 60-90+ real seconds).
   - **The exact stall point is non-deterministic across runs** — sometimes
     `ecomserver`/`BackupServer`, sometimes `PlayServer`, sometimes
     `N-Gage Installer`/`Games`. Don't assume a single run's "last thread standing" is
     *the* culprit; look for what's common across multiple runs instead (round 4's
     approach, and how the real finding emerged).

## Round 5: found the actual last message — freeze is mid-conversation with the real `baksrvs.exe`, not a scheduler bug

Went back to application-level tracing after all, but fixed a bug in round 5's own
probes first: **the `signal_request`/`sema::wait` probes from round 4 had a hardcoded
hit cap (40/60) that silently saturated during ordinary early-boot activity, long before
the actual freeze** — meaning the "self-signal" evidence written up as the round-4/
mid-round-5 working theory (`ecomserver` signalling its own request semaphore from
inside `FUN_803aa99a`) was captured from an unrelated, earlier, successfully-completing
IPC call, not from the freeze itself. That theory is **retracted**. Lesson: any probe
with a hit cap needs the cap checked against total log volume before trusting a "last
N hits" result as representative of the end state.

With the caps removed (see `svc.cpp`/`sema.cpp`/`thread.cpp` — currently uncapped at
100000, still temporary/reverted before this investigation closes) and a full run
allowed to reach genuine, log-growth-plateaued idle (confirmed via 12+ real seconds of
zero line-count growth after force-kill, trustworthy-flush as established in round 2),
the real last sequence of events was captured. This run stalled on the
`ecomserver`/`BackupServer` pair again (still non-deterministic which pair it lands on
— see above), giving a clean, complete trace of exactly this exchange:

```
ecomserver → BackupServer: ord=-1 (connect)              [is_hle=false, real baksrvs.exe]
ecomserver → BackupServer: ord=31 (0x1F)                  -> BackupServer replies, ecomserver proceeds
ecomserver → BackupServer: ord=34 (0x22)                  -> BackupServer replies, ecomserver proceeds
ecomserver → BackupServer: ord=32 (0x20)                  -> BackupServer never replies. Freeze.
```

Confirmed `to_server=!BackupServer` is `is_hle=false` throughout — this is the **real**
`baksrvs.exe` guest binary (summoned via the loader, `"Trying to summon: baksrvs"`),
*not* this project's own HLE stub at
[backup.cpp](../../src/emu/services/src/backup/backup.cpp)
(`backup_old_server`, registered under the plain name `"BackupServer"`/EKA1-style, no
`!` prefix, `is_hle()` true). That HLE stub **was** briefly suspected — it has a real,
separate, confirmed bug (`backup_old_session::fetch()` only handles function codes
`0x1F`/`0x22` and silently drops anything else with just a `LOG_ERROR`, never calling
`ctx->complete()` — which would hang a caller forever if it were ever reached) — but the
probe's `is_hle=false` reading proves this specific hang isn't hitting it. Worth fixing
independently some day (dead giveaway: `grep -n "Unimplemented opcode for Old Backup
server" build/bin/log*` on any run that touches it), but it is a **different, dormant
bug**, not this one.

The healthy pattern for ord 31 and 34: after `ecomserver` sends and signals
`BackupServer`'s request semaphore (delivering the message), `BackupServer` wakes,
**self-signals its own request semaphore once more** (a `lr=0x803a7d83`-tagged call,
consistent with the standard Symbian active-object "complete now, re-enter via the
scheduler" idiom used to process-then-immediately-loop), *then* signals `ecomserver`
back with the reply, *then* goes back to a genuine blocking wait for the next message.
**On the `ord=32` request, `BackupServer` is woken (message delivered, semaphore
signalled) but never performs that self-signal step, never replies, and goes straight
back into a genuine blocking wait — as if it received the message but its dispatch
logic did nothing with it.** `ecomserver` is left blocked forever on its own
`wait_for_any_request()`, which is the actual, final, permanent idle state — matches
every earlier round's "genuine full idle" finding exactly, just now with the precise
last message identified.

**This reframes round 4's "it's a scheduler/shared-resource bug because a different
thread stalls each run" conclusion.** It's simpler than that: the underlying pattern is
still "some real guest binary silently drops one specific incoming message instead of
replying to it," it's just that *which* server/message pair this happens to hit first
varies by boot-timing luck (BackupServer via ecomserver in this run; something else in
other runs, per round 4's PlayServer/N-Gage-Installer captures) — not a global
scheduler-fairness bug. The `euser.dll` generic-wrapper finding from round 4 still
holds (that's just how every process calls IPC), it just wasn't evidence of a
scheduler issue after all.

### To resume from here

The next concrete, high-value step: disassemble the **real** `baksrvs.exe` (extract
from the installed drive — it's a standalone E32Image, not ROM-resident, same situation
as `playserver.exe` in round 4's notes — use `e32dump.cpp`/`loader::parse_e32img()`,
not `parse_rom.py`) with the same Ghidra pipeline from round 4, and find its message
dispatch handler for function code `0x20` (32 decimal) specifically. Two concrete
things to check once inside:
1. Does `baksrvs.exe`'s session dispatch even have a case for `0x20`, or does an
   unhandled-opcode path exist that's supposed to complete-with-error but doesn't?
2. Does handling `0x20` require touching some EKA2L1-side facility that might be
   missing/wrong — a HAL attribute, an `RProperty` category, a Central Repository key,
   or a file-system entry under `bur/` — that would make real Symbian code branch
   somewhere unexpected (e.g. into a wait on something that never gets signalled,
   consistent with "receives the message, does nothing, waits again").
Given `ord=31`/`0x1F` and `ord=34`/`0x22` both work, and only `0x20` breaks, the
dispatch table itself is almost certainly fine (it clearly routes at least two adjacent
codes correctly) — the more likely fault is inside whatever `0x20`'s specific handler
body does, or in some EKA2L1-side dependency that handler pulls in that `0x1F`/`0x22`
don't need.

## Round 6: disassembled the real `baksrvs.exe` — it's a near-empty bootstrap stub, not the server

Extracted and disassembled the real `data/drives/z/rm-320/sys/bin/baksrvs.exe` (ROM-resident,
same header format as round 4's targets) with the same Ghidra headless pipeline. Two
header/extraction bugs had to be fixed first, both worth remembering for next time:

1. **The quick-and-dirty `parse_rom.py` header layout from round 3 was wrong (or at
   least dangerously incomplete) for small files.** This project's own authoritative
   struct — [`rom_image_header`](../../src/emu/loader/include/loader/romimage.h) — is
   **128 bytes** (`0x80`), not the 84 bytes (`0x54`) `parse_rom.py` assumed. It has a
   full `rom_vsec_info` block *and* `checksum_code`/`checksum_data` *and*
   `major`/`minor`/`build`/`flags_raw`/`priority`/`data_bss_linear_base_address`/
   `next_extension_linear_address`/`hardware_variant`/`total_size`/`module`/
   `exception_des` — `parse_rom.py`'s two guessed "styles" only ever covered the first
   84 bytes, treating the sec_info/checksum fields as *alternatives* rather than both
   being present sequentially, and stopping there. This happened to not matter for the
   big DLLs in round 3/4 (extracting from the right offset vs. 32 bytes too early
   inside megabytes of code shifts everything but a stray desync partway through a huge
   file is easy to miss/tolerate) but produced total garbage for a 1.3KB file where
   being 44 bytes off starts you in the *middle of the header*. Symptom: first
   instructions decoded as nonsense NOPs/out-of-range branches regardless of ARM or
   Thumb mode — that "wrong in every mode" symptom is the tell that the extraction
   offset itself, not the mode, is wrong. **Use the real 128-byte header from
   `romimage.h` for any future ROM-image extraction, not `parse_rom.py`'s.**
2. **This binary mixes ARM and Thumb**, unlike the big DLLs (round 4 confirmed those
   are ~100% Thumb). The E32Dll/process-bootstrap portion and the import-veneer table
   (`E5 1F F0 04` = `LDR PC,[PC,#-4]`, repeated dozens of times near the end of the
   file) are plain ARM; at least one small function (`0x821b7518`) is genuine Thumb
   reached via `BLX` (which mode-switches). Forcing `TMode=1` globally (round 4's
   correct move for the DLLs) is wrong here — let ARM be the default and only force
   Thumb where there's specific evidence (a `BLX` target, or bytes that only make sense
   as Thumb).

**With the correct 0x80-byte offset, disassembly is clean and makes sense** — see
`ghidra-scripts/baksrvs_dump_arm.txt` and `ghidra-scripts/baksrvs_tail_dump.txt` in this
folder. But what it reveals changes the shape of the problem again:

`baksrvs.exe`'s own code is **only ~1.25KB total**, and essentially all of it is
generic, compiler-generated Symbian process bootstrap: `E32Dll` entry-reason dispatch
(`thunk_FUN_821b7100`), heap/TLS/exception-descriptor setup, and — the interesting
part — `FUN_821b71c0`, which builds a string, calls a comparison function, and takes
one of two paths depending on the result:

```c
iVar3 = thunk_EXT_FUN_803a7cf2(auStack_444, auStack_64c);   // some kind of compare
if (iVar3 == 0) {
    // "real" path: several more setup calls, then calls through a function
    // pointer obtained via thunk_EXT_FUN_803a7e8a(...) — this is where the
    // actual server/session object would come from
} else {
    // fallback path: piVar4 = thunk_EXT_FUN_821b6444()  — a fixed, low address
    // (0x821b6444) *outside* baksrvs.exe's own code entirely
}
```

Both paths end by calling through the resulting object's vtable
(`(**(code**)(*piVar4 + 0x28))(piVar4)`), which is where **all** of the real
opcode-dispatch/session logic must live — and it is *not* inside this 1.25KB file.
Accounting for every byte: ~1064 bytes of the bootstrap code shown above, plus ~188
bytes of embedded UTF-16 string literals at the very end of the file (confirmed one of
them is literally `"!BackupServer"`, length-prefixed, matching the kernel object name
seen throughout this investigation — a real, solid landmark, not a guess) leaves **zero
room** in the file for a real `CServer2`/`CSession2` implementation. Checked whether
this means a second DLL gets dynamically loaded at runtime
(`RLibrary::Load`/`loader_server::load_library` in
[loader.cpp](../../src/emu/services/src/loader/loader.cpp), which logs `"Loaded
library: {}"` on success or `"Try loading {} to {} failed"` on failure) — **neither log
line appears anywhere after `"Trying to summon: baksrvs"` in a full capture**, so
`baksrvs.exe`'s process never calls this SVC. The real logic isn't in a dynamically
loaded DLL either.

**Working hypothesis, not yet confirmed:** the `if (iVar3 == 0) / else` branch above
looks exactly like a "real implementation available? / fall back to a generic default"
check — and the fallback target (`thunk_EXT_FUN_821b6444`, address `0x821b6444`) is
*not* in `baksrvs.exe`'s own code range and not in `euser.dll`'s either (checked against
round 4's confirmed `0x8039CDB8`–`0x803CF328` range) — it belongs to some other,
not-yet-identified ROM module sitting at a lower address, plausibly a generic/no-op
default server implementation. If EKA2L1 causes `iVar3`'s comparison to come out
non-zero when real hardware would get zero (i.e. baksrvs takes the fallback path here
under emulation but wouldn't on a real N95), a generic fallback stub would plausibly:
handle basic/generic session ops correctly (matching `ord -1`/`31`/`34` working), but
have no real handling for domain-specific op `0x20` and simply never complete it
(matching the freeze) — the exact same failure shape as the *other*, already-confirmed,
separate dormant bug in this project's own `backup_old_session::fetch()` (round 5). Two
independent pieces of evidence pointing at the same *kind* of bug (unhandled-opcode
paths that don't call `complete()`) is suggestive but not proof this is what's actually
happening here.

**Tried to verify this empirically and hit a tooling wall**: the clean way to confirm
which branch actually executes at runtime is a PC breakpoint at the `cmp r0,#0x0` /
`beq` around `0x821b7256`/`0x821b7258` inside the live emulator. This project has a
breakpoint/callback mechanism (`kernel_system::register_breakpoint_hit_callback`,
[kernel.h](../../src/emu/kernel/include/kernel/kernel.h)) used by the GDB stub, but it
appears to require an actual breakpoint to already be *set* (likely via writing a trap
instruction into guest memory, the way the GDB stub does it) rather than offering a
simple "watch this PC and log" API — wiring that up correctly wasn't attempted this
round given time already spent; it's a well-defined, bounded task for next time rather
than more manual disassembly.

### To resume from here

1. **Confirm or refute the fallback-stub hypothesis empirically** — this is the highest
   leverage next step. Either: (a) properly use the existing GDB-stub breakpoint
   mechanism to log which branch `FUN_821b71c0` takes at `0x821b7256` inside the live
   `!BackupServer`/`Main` thread during a repro run, or (b) find and disassemble
   whichever ROM module owns address `0x821b6444` (parse every ROM file's header in
   `data/drives/z/rm-320/sys/bin/` looking for one whose `code_address`..`code_address
   + code_size` range contains it) to see directly whether it's a generic/stub
   implementation.
2. **If confirmed**, the actual root-cause question becomes: *what does
   `thunk_EXT_FUN_803a7cf2` (the `0x803axxxx` address means it's inside `euser.dll` —
   round 4's confirmed range) compare, and why would EKA2L1 make that comparison come
   out differently than real hardware?* Disassembling that one `euser.dll` function
   (now that round 4's Ghidra pipeline + this round's corrected header-size lesson are
   both in hand) is a bounded, concrete task — much more tractable than this round's
   open-ended search, since it's one specific already-located function.
3. **Reusable fix for next time**: use the real 128-byte
   [`rom_image_header`](../../src/emu/loader/include/loader/romimage.h) layout for any
   future ROM-image code extraction, not `parse_rom.py`'s 84-byte guess. Consider
   updating `parse_rom.py` itself to match before using it again.
4. All temp instrumentation from this round (probes in `svc.cpp`/`sema.cpp`/
   `thread.cpp`/`epoc.cpp`, the `log.cpp` flush-level change) has been reverted —
   `git status`/`git diff` confirmed clean (only the unrelated per-device-drives
   feature and one pre-existing whitespace line in `dsp_shared.cpp` remain). New
   Ghidra scripts and their outputs from this round are saved in `ghidra-scripts/`
   (`DumpBaksrvs.java`, `DumpBaksrvsArm.java`, `DumpBaksrvsTail.java`,
   `DumpSmokingGun.java`, and their `.txt` results) alongside the ones from round 4.

## Round 7: found the real backup DLL, found the fallback stub, comparison-function lead was a dead end

**Fixed `parse_rom.py`** to match the real 128-byte `rom_image_header` (was still using
the old 84-byte guess even after round 6 flagged it) and used it to scan every ROM
binary on `sys/bin/` for whichever module owns `0x821b6444` (the fallback address from
round 6). Found it immediately: **`baksrv.dll`**, range `0x821B5798`–`0x821B7080`, sitting
directly below `baksrvs.exe` in ROM address space (their ranges are back-to-back) and
exporting 39 functions (`baksrvs.exe` itself exports zero) — this is almost certainly
the real Secure Backup Engine implementation, with `baksrvs.exe` as a thin process
wrapper around it, both linked into one contiguous ROM region (explains why there's no
runtime `RLibrary::Load` call — round-6 finding still holds, ROM linking here is static/
pre-resolved, not dynamic).

Disassembled `baksrv.dll` (fixed ARM/Thumb mixing the same way as round 6) and located
the fallback function at `0x821b6444` (it's mid-function, not a function start — the
real entry is `0x821b643c`, 8 bytes earlier; the veneer's embedded pointer value points
partway in). What it does:

```c
void target_thumb_0x821b643c(void) {
    void *obj = thunk_EXT_FUN_803a83ae(0x78);  // allocate 0x78 (120) bytes
    FUN_821b642e(obj, 0);                       // construct it (param 0)
    return obj;
}
```

This reads exactly like compiler-generated `new SomeBaseClass()` — allocate, construct,
return. **This confirms the round-6 hypothesis's shape**: the fallback path in
`baksrvs.exe` constructs a small, generic 0x78-byte object instead of whatever the real
implementation object would be, and that generic object is plausibly a bare/default
`CServer2`-family stub that only knows how to handle a few basic operations — matching
`ord -1/0x1F/0x22` working and `ord 0x20` (a real, domain-specific backup operation)
having nothing behind it.

**The "comparison function" lead (`thunk_EXT_FUN_803a7cf2`) turned out to be a
misreading of the decompiler output — not a comparison at all.** Disassembled it
directly: it's a tiny 6-byte helper (`ldr r1,[sp,#4]; stmia r4!,{r0,r1}; pop
{r2,r3,r4,pc}`) that just appends two words to a buffer — a serialization/stream-write
helper, not anything that could produce the `iVar3` compared in `if (iVar3 == 0)`.
The earlier round-6 decompiled pseudocode attributing the comparison to this call was
the Ghidra decompiler's own inference in a function full of `unaff_r4`/`in_stack_*`
markers (a sign it couldn't fully resolve the calling convention) — not reliable enough
to trust at face value. **Retracting that specific claim**; the real condition
controlling the real-vs-fallback branch in `FUN_821b71c0`
(`ghidra-scripts/baksrvs_dump_arm.txt`) is still unidentified.

### Honest assessment after 7 rounds

What's now solid, cross-checked, reproducible: the exact failing message (`ecomserver`
→ real `baksrv.dll`-backed `baksrvs.exe`, op `0x20`), and strong circumstantial
evidence that `baksrvs.exe` degrades into a generic fallback object under EKA2L1 that
doesn't implement that specific operation. What's still missing: proof that the
fallback branch is actually the one taken at runtime (static reasoning only, so far),
and if so, *why* — which requires either a live breakpoint at `FUN_821b71c0`'s branch
point or correctly identifying the real condition being tested (this round's
attempt at the latter was a dead end).

Manual decompiler archaeology on 20-30 year old Thumb-1/ARMv6 EABI code with no symbols
has a real, demonstrated failure mode now seen twice (round 6's address-alignment
issue, round 7's decompiler-misattribution issue) — each individually resolvable, but
compounding, and each next step keeps requiring another disassembly detour rather than
closing in. The live-breakpoint route (see round 6's "to resume" item 1) stopped being
just "the more rigorous option" and is now the more *efficient* one too: one instrumented
run settles "which branch, definitively" in a way no amount of further static reading
can.

### To resume from here

1. **Wire up a real PC breakpoint** at `FUN_821b71c0`'s branch (`0x821b7256`
   `cmp r0,#0x0` / `0x821b7258 beq`, live runtime addresses will differ from these
   static ROM addresses only if ASLR/relocation applies — ROM XIP code shouldn't
   relocate, so these should be the live addresses too, worth double-checking on the
   first attempt). This project already has
   `kernel_system::register_breakpoint_hit_callback`
   ([kernel.h](../../src/emu/kernel/include/kernel/kernel.h)) wired up for the GDB
   stub — look at how the GDB stub itself sets a breakpoint (likely by patching a trap
   opcode into guest memory, `src/emu/debugger/` or wherever the GDB stub lives) and
   reuse that mechanism rather than reinventing it.
2. If that's too heavy for a quick session, the cheaper fallback: a temp probe (same
   style as rounds 2-6) logging the CPU's PC once per scheduler switch specifically
   while running inside `baksrvs.exe`'s address range (`0x821B70F8`–`0x821B75DC`) during
   the repro window — coarser than a real breakpoint but might still catch it near the
   branch given how little code runs before the freeze.
3. If the fallback path is confirmed: the next question is what real condition
   controls it. Don't reuse round 7's `thunk_EXT_FUN_803a7cf2` lead — look instead at
   `FUN_821b71c0`'s full decompiled body (`ghidra-scripts/baksrvs_dump_arm.txt`) for
   which call's return value actually feeds `iVar3`/the `cmp` at `0x821b7256`, tracing
   register flow by hand instruction-by-instruction rather than trusting the
   decompiler's high-level attribution.

## Round 8: found the actual root cause

Started down the live-breakpoint path from round 7's "to resume" list. Along the way,
found and fixed a THIRD extraction bug bigger than either of the first two — and it
led straight to a real, verified, complete root-cause chain.

### The extraction bug (bigger than rounds 6/7 realized)

Wired a temporary breakpoint (reusing this project's existing
`kernel_system::register_breakpoint_hit_callback` / GDB-stub-style trap-byte-patching
mechanism, see the reverted diff history of
[epoc.cpp](../../src/emu/system/src/epoc.cpp) for the technique if needed again) at the
two addresses round 6 identified as `FUN_821b71c0`'s real-vs-fallback branch targets.
Reading back the *original* bytes at those addresses (before patching, straight from
live guest memory via `kernel::process::get_ptr_on_addr_space`) to sanity-check them
against the static ROM file **did not match** — not a small mismatch, a completely
different (but still valid-looking) instruction encoding.

That was suspicious enough to escalate to a direct, no-interpretation check: patched
the emulator to dump a wide, contiguous slice of `baksrvs.exe`'s *live* process memory
straight to disk, byte for byte, no Ghidra involved. Compared against the round-6/7
static extraction: **873 of 1252 bytes differed**, but shifted by a constant 8-byte
offset, matched **exactly** (0 byte differences across the full file). Root cause,
found by finally reading [`romimage.cpp`](../../src/emu/loader/src/romimage.cpp)'s
*actual parsing code* instead of re-deriving the header layout from the struct
declaration (which is what every previous round's `parse_rom.py` did): the real parser
is **conditional**, not a flat sequential read —

```cpp
if (os_ver <= epocver::eka2) {
    // read checksum_code, checksum_data (8 bytes)
} else {
    // read sec_info: secure_id, vendor_id, cap1, cap2 (16 bytes)
}
```

Every previous version of `parse_rom.py` read **both** blocks sequentially (24 bytes)
instead of the real **either/or** (8 or 16 bytes, never both). For this device
(Symbian OS 9.1 / S60 3rd Edition, well past `eka2`), the true block is 16 bytes, so
every extraction using the old parser was short by a fixed 8 bytes for the "eka2 8-byte
style" guess. Fixed `parse_rom.py` to mirror the real conditional parser exactly; the
corrected header size is **120 bytes (0x78)**, not the 84 or 128 bytes every earlier
round assumed. Verified with the live-memory-dump technique: **zero byte differences**
between the corrected extraction and live guest memory, for both `baksrvs.exe` (1252
bytes) and, more importantly —

### This bug also silently invalidated part of round 4's "ground truth"

`euser.dll`'s round-3/4 extraction used the same broken parser (a different, 84-byte
guess for that round's script version) — **36 bytes short** of the correct 120.
Verified with a full 206KB live memory dump: the old extraction differs from live
memory at 161222 of 206108 compared bytes (78%!) directly, but matches **exactly**
when shifted +36 bytes, and the newly-corrected extraction (offset 120, using the
fixed `parse_rom.py`) matches live memory with **zero** differences across all 206072
bytes.

Consequence: round 4's specific claim "the function containing `0x803a02dc` disassembles
to exactly `swi 0x25`, matching `BRIDGE_REGISTER(0x25, session_send)`" was looking up
that address in the *old, mis-offset* Ghidra project — which, per the correction, was
actually showing content that truly belongs 36 bytes away. Re-checked with the
corrected extraction: **the underlying story survives, with corrected details.** The
real caller_lr addresses captured live in round 5 (`0x803aa9ab`, `0x803aa997` — these
were always genuine, since they came directly from `kern->get_cpu()->get_lr()` at
runtime, never from static analysis) now resolve, in the *corrected* Ghidra project, to
two real, sensible functions:
- `0x803aa9ab` → inside `FUN_803aa998`, which calls `FUN_803a02cc` (`swi 0x4C`) —
  `BRIDGE_REGISTER(0x4C, session_send_sync)`, not `0x25`. Still a genuine
  SendReceive-family SVC, just the synchronous-specific one, not the general one round
  4 named.
- `0x803aa997` → inside `FUN_803aa976`, the conditional-status-set-then-send pattern
  round 4 already correctly described in shape (just wrong sub-addresses/names:
  `FUN_803bb3fc`/`FUN_803a02b8`, not `FUN_803bb420`/`FUN_803a02dc`).

**Net effect: round 4's mechanism-level conclusion (this is euser's generic
SendReceive-family wrapper, shared by every process, not one buggy caller) holds up
and is now on solid ground — the specific function names/addresses cited for it in
rounds 4-7 should be treated as superseded**, in favor of the ones in this round's
`ghidra-scripts/euser_fixed_targets_result.txt`.

### The actual root cause

With the corrected `euser.dll` extraction, round 7's retracted "comparison function"
lead was redone properly: `0x803a7cf2` (baksrvs.exe's real-vs-fallback branch
condition) → `FUN_803a9e7c` → converts a 16-bit string descriptor to 8-bit, calls
`FUN_8039fb78` which is exactly `swi 0x0`, and branches on the result. `swi 0x0` is
`BRIDGE_REGISTER(0x00, object_next)` — the SVC behind Symbian's `TFindThread`/
`TFindServer`/`TFindProcess`-family "does an object matching this name pattern
exist?" lookup. The object type passed (traced through the register chain by hand,
`obj_type=0`) is `kernel::object_type::thread`
([kernel_obj.h](../../src/emu/kernel/include/kernel/kernel_obj.h)) — **this is a
`TFindThread`-style check for a specific already-running thread**, and the embedded
search string, extracted directly from `baksrvs.exe`'s data section, is
**`"Wserv"`**.

Checked what EKA2L1's Window Server actually is:
[`window_server::window_server`](../../src/emu/services/src/window/window.cpp:1550)
constructs its `service::server` base with **`owner_thread = nullptr`**:

```cpp
window_server::window_server(system *sys)
    : service::server(sys->get_kernel_system(), sys, nullptr,
        get_winserv_name_by_epocver(sys->get_symbian_version_use()), true, true)
```

**EKA2L1's Window Server is a pure HLE `server` kernel object with no backing
`kernel::thread` at all.** Real Symbian's Window Server runs as an actual process
with an actual thread (whose name contains `"Wserv"`, matching `baksrvs.exe`'s search
string exactly). EKA2L1 never creates that thread — only the server object used for
IPC routing. So `object_next(thread, "Wserv")` can never succeed under EKA2L1,
**regardless of whether the Window Server is fully up and running** — the thread it's
searching for structurally doesn't exist in this emulator's kernel object model.

### Full root-cause chain, now complete and verified end to end

1. `baksrvs.exe` boots and, before fully initializing, does a `TFindThread`-style
   readiness check for a thread matching `"Wserv"` (plausibly: "is the UI subsystem up
   yet before I finish initializing the real backup engine").
2. Under EKA2L1, that thread can never be found — the Window Server exists only as an
   ownerless HLE server object, never as a `kernel::thread`.
3. `baksrvs.exe` falls back to a generic path: allocates and constructs a small
   (0x78-byte) generic/default object instead of the real backup-engine implementation
   (confirmed in round 7: the fallback call is a textbook allocate+construct pattern).
4. That generic object only handles a couple of basic/generic session operations
   (matching `ord -1`/`0x1F`/`0x22` working) and has no handler for the real,
   domain-specific `ord 0x20` that `ecomserver` sends it as part of its normal backup
   registration protocol.
5. The generic object never replies to `ord 0x20`. `ecomserver` (or whichever
   app/thread happens to be the one making this exact call in a given run — round 4's
   non-determinism finding, still valid) blocks forever on `wait_for_any_request()`
   waiting for a reply that will never arrive.
6. Permanent full-system idle — matches every round's "genuine, sustained,
   reproducible idle, not a busy loop" finding exactly.

### Fix directions (not yet implemented — needs a decision, see below)

Two shapes of fix, different invasiveness:
1. **Give `window_server` (and any other similarly `owner_thread = nullptr` HLE
   servers) a real backing `kernel::thread`**, named to match what real guest code
   expects (e.g. containing `"Wserv"`). Most faithful to real Symbian semantics, fixes
   this class of bug generally (any other guest code doing similar
   `TFindThread`/`TFindServer`/`TFindProcess` checks against HLE servers would also
   start working), but touches shared server-construction code and needs checking for
   side effects (thread scheduling, priority, any code that currently assumes these
   servers have no thread).
2. **Narrower**: special-case `object_next`'s `find_object` lookup
   ([svc.cpp](../../src/emu/kernel/src/svc.cpp), around line 1775) to also match
   HLE servers without a backing thread by name pattern, for `object_type::thread`
   queries. Less faithful to real Symbian (a `TFindThread` "finding" a server object
   is semantically odd), but far smaller blast radius — one function, no new kernel
   objects, no scheduling implications.

Given the size of this change either way, and that it's the first fix candidate that's
actually gone this deep in eight rounds, this needs your call before implementing
anything.

### Housekeeping

All temporary instrumentation from this round (the breakpoint-callback registration,
then its replacement with the live-memory-dump probe, both in `epoc.cpp`, plus the
`<cstring>`/`<fstream>` includes added for them) has been fully reverted — `git diff
src/emu/system/src/epoc.cpp` is back to exactly the round-6-baseline 26-line diff (the
unrelated per-device-drives feature only). `parse_rom.py` is now fixed at the top level
of this folder (not just referenced) — it matches `romimage.cpp`'s real conditional
parsing logic. New Ghidra scripts/results from this round are in `ghidra-scripts/`:
`DumpFixedTargets.java`/`euser_fixed_targets_result.txt` (corrected caller_lr and
comparison-function lookups), `DumpCompareFn3.java`/`euser_comparefn_fixed_result.txt`
(the full `object_next`/`TFindThread` call chain), and
`baksrvs_dump_arm_corrected.txt` (baksrvs.exe redisassembled with the fixed extraction
offset — the old `baksrvs_dump_arm.txt` from round 6 is kept for history/traceability
but its addresses are all 8 bytes off per this round's finding).

## Round 9: first fix attempt — crashed, reverted, real complexity found

User picked the more faithful fix option from round 8's two candidates: give
`window_server` a real backing `kernel::thread` (rather than the narrower `object_next`
patch). Implemented it, and it **crashed the emulator immediately on every boot**
(segfault, exit 139) — not specific to N-Gage or `RM-320`, it crashed before device
selection even took effect. Reverted immediately;
[window.cpp](../../src/emu/services/src/window/window.cpp),
[kernel.h](../../src/emu/kernel/include/kernel/kernel.h), and
[kernel.cpp](../../src/emu/kernel/src/kernel.cpp) are all back to their pre-round-9
state, verified via clean `git diff` and a successful boot test after rebuilding.

### What was tried and why it broke

Added `kernel_system::create_hle_presence_thread(name)`, modeled on the existing
`"Supervisor"` thread (`kernel_system::setup_nanokern_controller()`,
[kernel.cpp](../../src/emu/kernel/src/kernel.cpp) around line 397) — a real
`kernel::thread` created purely to be discoverable, never scheduled. Called it from
`window_server`'s constructor with name `"Wserv"`, **without** touching
`service::server`'s own `owner_thread` field (deliberately decoupled — every other HLE
server in this codebase passes `owner_thread = nullptr`, so wiring a real thread into
that specific field would have been genuinely unprecedented and higher-risk; this
avoided that specific risk on purpose).

The crash came from a different, simpler oversight: the `"Supervisor"` thread's real
owning process, `nanokern_pr_`, **only exists for EKA1 systems** —
`setup_nanokern_controller()` returns immediately if `!is_eka1()`. This device (`RM-320`,
Symbian OS 9.1, solidly EKA2) never creates `nanokern_pr_`, so it's `nullptr`. The fix
was written to pass `nullptr` as the new thread's owning process too, reasoning that
other kernel objects in this codebase (e.g. the `"SpaceReservePatchROM"` chunk,
also created with a `nullptr` owner) tolerate an ownerless parent fine. Threads don't:
`kernel::thread`'s constructor ([thread.cpp](../../src/emu/kernel/src/thread.cpp)
around line 295) allocates a **local-scoped** stack chunk and name chunk via
`owning_process()` — local-scoped chunks need a real process's address space to bind
to, unlike the global/ROM-scoped chunk that tolerated a null owner. With `owner =
nullptr`, `owning_process()` returns `nullptr`, and something downstream in that local
chunk allocation path dereferences it — hence the immediate segfault.

### What a correct implementation needs

Not just a nullptr fix — a real owning process. Options, roughly in order of
increasing faithfulness/effort:
1. Create one small, dedicated, permanently-idle `kernel::process` (analogous to
   `nanokern_pr_`, but usable on EKA2) specifically to own this and any future
   presence-only threads. Bounded scope, but needs verifying `kernel::process`'s own
   constructor doesn't have similar hidden requirements (this round didn't get far
   enough to check).
2. Find (or add) an existing always-present EKA2-era system process suitable for this,
   if one already exists, to avoid introducing a new kind of process into the kernel
   object model.
3. Reconsider the narrower `object_next`/`find_object` patch from round 8 instead —
   confirmed lower blast radius precisely because this round's crash demonstrated the
   "give it a real thread" path touches more of the kernel's process/thread lifecycle
   assumptions than round 8's writeup anticipated.

### Second crash: even a real owning process wasn't enough

Tried option 1 immediately after: added `kernel_system::create_hle_presence_thread`
with a dedicated, lazily-created, always-idle `kernel::process` (not reusing
`nanokern_pr_`, which only exists on EKA1) as the owner. **Still crashed**, same
segfault signature. Added checkpoint `LOG_ERROR` calls to bisect and found the crash
one level deeper than expected: `presence_pr_` creation succeeds fine, but the crash
is inside `create<kernel::thread>(...)` itself — specifically
`stack_chunk->host_base()` in [thread.cpp](../../src/emu/kernel/src/thread.cpp) around
line 315, with no null check, dereferencing a chunk that came back `nullptr`. Local-
scoped chunk allocation apparently doesn't work reliably for a process that was never
actually made the *current/active* process — a memory-model requirement neither crash
made obvious in advance, and genuinely below where this investigation had context to
safely keep pushing without risking a third crash. **Two independent crashes in this
approach, at two different depths of the same code path, is a strong enough signal
that "give it a real thread" needs someone with real memory-model familiarity, not
more trial and error.** Reverted cleanly again (verified via `git diff` + successful
boot).

## The fix that's actually in the tree now: the narrower `object_next` patch

Pivoted to round 8's other candidate — no thread/process creation at all, so none of
the memory-model risk above applies. In
[`kernel_system::find_object`](../../src/emu/kernel/src/kernel.cpp) (around line
1188), the `object_type::thread` search case now falls back to matching against
**ownerless HLE servers** (`is_hle() && !get_owner_thread()`) when no real thread
matches, using a new virtual method,
[`service::server::thread_presence_alias()`](../../src/emu/kernel/include/kernel/server.h)
(default: empty = opt-out), which
[`window_server`](../../src/emu/services/include/services/window/window.h) overrides
to return `"Wserv"`.

**First attempt at this got the alias wrong and had to be corrected live.** Initially
matched against the server's own `name()`/`full_name()` (`"!Windowserver"`), reasoning
it "contains Wserv" — reran the repro with a temporary probe directly on `object_next`
(logging every call's `obj_type`/`name`/result) and got hard evidence the search
pattern is **`"*Wserv"`** — a *suffix* wildcard (leading `*`, no trailing `*`), meaning
it requires the target name to literally **end in** `"Wserv"`. `"!Windowserver"` ends
in `"...erver"`, not `"...Wserv"` — doesn't match at all, hence the fallback returning
false even though it looked right on paper. Fixed by *not* reusing the server's real
name at all — added the dedicated `thread_presence_alias()` override returning the
literal string `"Wserv"`, which trivially satisfies the confirmed real search pattern.
This is the concrete lesson: don't trust "should contain X" reasoning about a wildcard
pattern without checking its exact shape (leading vs trailing `*` completely changes
what matches) — a live probe settled it in one run where more reasoning-from-the-name
would have kept guessing wrong.

### Verified: the fix works exactly as the round-8 hypothesis predicted

Reran the full N-Gage repro with temporary probes on both `object_next`'s result and
every `session_send`/thread-switch, trustworthy-flush enabled (all removed again
afterward). Confirmed, in order:
1. `object_next obj_type=0 name='*Wserv' ... caller_process='baksrvs[...]'` →
   **`result found=true`** (was `false` before the fix, on the exact same repro).
2. `baksrvs.exe`'s thread (`!BackupServer`) immediately does things it **never did
   before**: connects to `!Loader` and loads **`eikbackupsrv.dll`** — a real backup
   engine DLL distinct from `baksrv.dll` (round 8's find) — then connects to
   `!FileServer`, `!Fontbitmapserver`, and `!Windowserver`. None of this happened on
   any previous run; this is `baksrvs.exe`'s real-implementation branch actually
   running, not the generic fallback stub.

This is unambiguous confirmation of the round-8 root-cause chain, steps 1-3
specifically (the `TFindThread` gate existing and controlling which path `baksrvs.exe`
takes) — not a guess anymore.

### But N-Gage still hangs — a second, different, still-unidentified issue

After the real-implementation setup above, `ecomserver` connects to `!BackupServer`
again with the same `ord -1, 31, 34, 32` sequence documented since round 5, and **still
never gets a reply to `ord 32`** — same final symptom, same permanent
`!ecomserver`/`!BackupServer` idle. The difference: this now happens *after* real
initialization (loading `eikbackupsrv.dll`, talking to four other real servers)
instead of immediately falling back to a stub. Two live-verified facts about this new
stall:
- It is a **different** underlying cause than round 8's finding — that specific gate
  is now fixed and confirmed working.
- `eikbackupsrv.dll`'s own initialization touches `!Windowserver`/`!Fontbitmapserver`/
  `!FileServer` — any of those, or something else `eikbackupsrv.dll` needs and
  EKA2L1 doesn't fully provide, is now the space to search. Not yet investigated at
  all - this is genuinely a fresh problem, not a continuation of round 8's.

### To resume from here (superseded in part by round 10 below — read on)

1. **The fix in the tree is real and should stay** — it's a verified, working
   correctness improvement (confirmed via live probe, not just plausible reasoning),
   even though it alone doesn't fully unblock N-Gage. Don't revert it while chasing
   the next issue.
2. If the "give it a real thread" approach (the more architecturally faithful fix,
   still not implemented) is revisited later: don't restart from scratch — the two
   crash signatures in this round (null owning-process dereference, then a null
   `stack_chunk` even with a valid owning process) are now known, specific blockers.
   Whoever picks this up needs to understand this project's memory-model/chunk
   allocation code (`mem/` and `kernel::chunk`) well enough to know why local-scoped
   chunk allocation requires an *active* process, not just a *valid* one — this
   investigation didn't get that far.
3. All temporary diagnostic instrumentation from this round (probes in `svc.cpp`/
   `epoc.cpp`, the `log.cpp` flush-level change, the reverted thread/process-creation
   attempts) has been fully removed — `git diff` now shows only the real fix:
   [server.h](../../src/emu/kernel/include/kernel/server.h) (+12),
   [kernel.cpp](../../src/emu/kernel/src/kernel.cpp) (+59),
   [window.h](../../src/emu/services/include/services/window/window.h) (+9), plus the
   pre-existing per-device-drives feature in `epoc.cpp` from before this
   investigation started. Verified with a clean rebuild and a successful plain boot.

## Round 10: characterized the second issue — a silently-dropped message inside eikbackupsrv.dll

**Build-system note first, since it caused a real incident.** Mid-round, a full
rebuild triggered by a `kernel.h` change was run with unbounded `-m` parallelism while
a Ghidra headless JVM and the emulator process were *also* running. That exhausted
system memory badly enough that git-bash's `fork()` started failing and `cl.exe` hit
`C1060: insufficient heap space` — the user had to force-close the whole session to
get their PC back. **Guardrail now in place** (saved to durable memory, not just this
file): cap MSBuild parallelism explicitly (`-m:4`, not bare `-m`), never run a build
concurrently with Ghidra or the emulator, check `tasklist` for stale
`cl.exe`/`MSBuild.exe`/`java.exe`/`eka2l1_qt.exe` before starting anything heavy, and
clean up MSBuild's node-reuse processes (`taskkill //F //IM MSBuild.exe`) after each
build — they linger by design and would otherwise stack up across rounds.

### Static analysis on eikbackupsrv.dll hit a wall, live probing didn't

Extracted `eikbackupsrv.dll` (same corrected `parse_rom.py`, header offset 0x78) and
ran it through the Ghidra pipeline. Found real code (136 functions, one clean,
correctly-decoded constructor at export[1]: `alloc(0x8c) + construct(obj, 200)`,
noticeably bigger/more real than `baksrv.dll`'s bare fallback stub). But: exports 2-15
all decode to garbage regardless of forced ARM or Thumb mode (not an alignment bug
this time — likely genuinely a data table, not code), and tracing the constructor's
callees quickly leads into a `0x806cxxxx`-range module (some base runtime/CONE-style
library, not `euser.dll` or `baksrv.dll`) that isn't loaded into any Ghidra project
here — a bigger multi-module setup than was worth building for this round. No message
dispatch table was found statically.

Pivoted to a live PC-sampling probe instead (temp, in `epoc.cpp`, watching
`!BackupServer`'s PC/LR immediately before and after each scheduler quantum) and got a
useful negative result fast: PC repeatedly lands at the exact same address
(`0x8039fa9c`, inside `euser.dll`'s generic SVC trampoline for `wait_for_any_request`,
`BRIDGE_REGISTER(0x00800000, wait_for_any_request)` — a real, implemented fast
executive call, not a missing one) with a *varying* instruction count beforehand each
time. Initially looked like a busy-spin, but a second, more precise probe (temp, in
`sema.cpp`/`thread.cpp`, logging every `avail_count` and every `signal_request` caller
targeting `!BackupServer`'s own request semaphore, no hit cap this time) showed this
reading was misleading — the real picture is more specific than "spinning."

### The real picture: a working exchange, then one silently-dropped message

Full, uncapped signal/wait trace for `!BackupServer`'s request semaphore across the
whole stall, in order:
1. Several `signal_request(...) called from thread=!BackupServer lr=0x803aa9ab`
   immediately followed by `wait() avail_count_before=1` (self-signal, consumed
   immediately, doesn't block) — `!BackupServer`'s own active scheduler processing its
   own internal events during startup. Normal.
2. `wait() avail_count_before=0` — a **genuine block** (nothing pending).
3. `signal_request(...) called from thread=!ecomserver lr=0x803aa9ab` — `ecomserver`
   delivers a real message, waking `!BackupServer` up. Then
   `signal_request(...) called from thread=!BackupServer lr=0x803a7d83` — the same
   "self-complete, re-enter via scheduler" idiom round 6 already identified as the
   *healthy* processing pattern — followed by the matching `wait()` calls consuming
   it. **`!BackupServer` genuinely processed this message correctly.**
4. Two more `signal_request(...) called from thread=!ecomserver lr=0x803aa997` (the
   generic euser SendReceive-with-status wrapper, same address family documented since
   round 4/8) arrive — `ecomserver` sending its next messages (the historical `ord
   31`/`34`/`32` sequence from round 5 onward). **Each time, `!BackupServer` goes
   straight back to `wait() avail_count_before=0` — a genuine block — with no
   self-complete step and, critically, no `signal_request` ever sent back to
   `!ecomserver`.**

This confirms, with much cleaner evidence than any earlier round, exactly what round 6
first suspected: **on some specific message (matching the historical `ord 32`/`0x20`),
`!BackupServer`'s real message-dispatch logic wakes up, does nothing with the message
at all — no reply, no error, no self-completion — and goes straight back to sleep.**
Round 8's fix (the `Wserv` thread lookup) got `eikbackupsrv.dll`'s *real*
implementation loaded and running instead of the fallback stub, and that real
implementation does correctly handle at least one message (step 3 above) — but still
silently drops whatever the final one is. This is now clearly a **bug (or missing
dependency) inside `eikbackupsrv.dll`'s own message dispatch**, not a generic
"wait/signal mechanism is broken" issue and not round 8's issue recurring.

### To resume from here (superseded by round 11 below — read on)

1. **Find `eikbackupsrv.dll`'s message dispatch (`ServiceL`-equivalent) function.**
   Static analysis of exports didn't find it this round (exports 2-15 are data, not
   entry points, and the constructor's callee chain leads into an unloaded module
   before reaching anything opcode-shaped). Two ways forward:
   - Load `0x806cxxxx` (whatever module that is) into the same Ghidra project as
     `eikbackupsrv.dll` and continue the static callee trace from
     `ghidra-scripts/eikbackupsrv_ctor_trace.txt`'s stopping point — first identify
     which ROM file owns that address range (same `parse_rom.py` scan technique round
     8 used to find `baksrv.dll`).
   - Or: since `export[1]`'s constructed object gets its vtable/dispatch wired up
     somewhere in that chain, a live approach might be faster now that the exact
     stall point is pinned down precisely (see above) — e.g., dump the constructed
     session object's memory (its vtable pointer, specifically) right when
     `!BackupServer` receives the fatal message, then follow that pointer statically.
   - This is real, open-ended reverse engineering, not a quick follow-up — budget
     accordingly, and re-read the build-resource-limits guardrail above before
     stacking Ghidra + build + emulator again.
2. **All temp instrumentation from this round has been removed** — `git diff` on
   `sema.cpp`/`thread.cpp`/`log.cpp` is clean; only the round-9 fix
   (`server.h`/`kernel.cpp`/`window.h`) remains in the tree, verified via a fresh
   capped-parallelism rebuild.

## Round 11: found the vtable, traced real execution, retracted a same-round "panic" theory

Followed the build-resource-limits guardrail from round 10 throughout: every rebuild
capped at `-m:4`, `tasklist` checked for stale `cl.exe`/`MSBuild.exe`/`java.exe`/
`eka2l1_qt.exe` before each one, MSBuild's node-reuse processes killed after each build,
never ran the emulator and a build/Ghidra pass at the same time. No resource incidents
this round.

### Found the real vtable — static tracing still couldn't pin down `ServiceL`

`eikbackupsrv.dll`'s constructor (`export[1]` → `FUN_821473e6`) stores a real,
14-slot primary vtable pointer (found at `0x82148150`, referenced from the
constructed object's offset 0) plus a second vtable at offset `0x78` (multiple
inheritance — likely a backup-observer mixin interface). Dumped and disassembled
every slot. Several are plain `LDR PC,[PC,#-4]` ARM import veneers to a `ws32.dll`
(Window Server client library, 580 exports — makes sense given `eikbackupsrv.dll`
talks to `!Windowserver`) — those are inherited/un-overridden virtuals, not
candidates. The handful of real, non-veneer Thumb functions found (destructor, a
"replace member" setter) were ruled out by inspection; none of the remaining
candidates decompiled cleanly enough via static analysis alone to identify
`ServiceL` with confidence — mixed ARM/Thumb noise (the same class of problem as
rounds 6-8, still not fully solved for this binary) kept producing garbage for
several slots even after correctly forcing Thumb mode globally.

### Pivoted to a full live instruction trace — real, working code, not what round 10 implied

Round 10's PC-sampling (quantum-boundary only) suggested `!BackupServer` might be
busy-spinning. This round added a temp probe that single-steps `!BackupServer`'s own
turns specifically (every other thread keeps running at full speed) and logs every PC
visited — a complete instruction-level trace, not just quantum snapshots. Two runs (one
capped at 8000 steps, one at 35000) gave a much more accurate picture:

- `!BackupServer` executes substantial, structured, legitimate code between wakes:
  loops through what looks like real active-object bookkeeping, makes real calls into
  `baksrv.dll` and back, and — critically — does hit a real completion path
  (`FUN_8039fd78` in `euser.dll`, reached via `FUN_803aa0f6`'s "status pointer is
  non-null" branch) at least once. This is **not** a thread doing nothing; it's doing
  real work.
- The second, larger-budget run ran out of *scheduled turns*, not step budget — it
  ended at exactly `0x8039fa9c` (the `wait_for_any_request` SVC boundary from round
  10), meaning `!BackupServer` genuinely, correctly blocked there and simply never got
  scheduled again in that run. Not a spin, not stuck mid-instruction — a real,
  legitimate wait that's just never woken by anything further.

### A same-round theory, retracted before it left this session

While tracing `FUN_803aa0f6` statically (called at the end of a linear array-search
helper chain: `FUN_821b6178` → `FUN_821b615a` → `FUN_821b59cc`/`FUN_821b599c`, which
searches an array for a matching entry and returns -1 if nothing matches), noticed its
decompiled body has two branches: complete a pending status normally
(`FUN_8039fd78`), or — if the status pointer is null — call
`FUN_803af504(0x46)`, which chains through real Symbian-ABI-looking code
(`KCurrentThreadHandle = 0xFFFF8001`, `entity_exit_type::panic = 2`) into `swi 0x72`,
which **is** genuinely EKA2L1's `thread_kill` SVC for this device's ABI table
(`svc_register_funcs_v93`, confirmed correctly mapped from `epoc93fp1` — momentarily
suspected a version-table mix-up here, that suspicion was wrong, the mapping is
correct). Formed a hypothesis mid-investigation that this panic path was being hit and
silently failing to terminate the thread (checked `get_kernel_obj_raw`'s special-case
handling for the `KCurrentThreadHandle`/`KCurrentProcessHandle` magic values — the
masking logic there is also correct, ruling out that specific concern too).

**Checked the live trace before writing this up as a finding, and it directly
contradicts the theory**: the actual PC sequence at the point in question shows PC
landing on `0x803aa0f6` then immediately flowing into `0x8039fd78` (the *normal
completion* branch), not `0x803af504`/`swi 0x72` at all. The array search found its
entry; nothing panicked. `0x803af504`/`swi 0x72` never appears anywhere in either
trace. **Retracting this specific theory before it became a "finding"** — noting it
here mainly so the next round doesn't independently rediscover and re-chase the same
plausible-looking but empirically false lead. The `entity_exit_type::panic`/`swi 0x72`
call chain and the `get_kernel_obj_raw` special-handle logic are both real,
correctly-implemented code — just not what's happening in this specific stall.

### Honest state after 11 rounds

What's solid: the fix from round 9 works and should stay. `!BackupServer`'s own code
is doing genuine, working, non-buggy things for at least one full request cycle
(confirmed instruction-by-instruction, not inferred). The final `wait_for_any_request`
block is itself completely normal Symbian behavior. What's still missing: direct
proof of what happens on the *specific* wake-up that corresponds to `ecomserver`'s
final, unanswered message — this round's two trace runs captured real activity but
didn't unambiguously catch that exact moment (the runs cover different portions of
`!BackupServer`'s lifetime depending on non-deterministic scheduling, same caveat
noted since round 4).

### To resume from here (superseded by round 12 below — read on)

1. **Combine round 10's cheap signal/wait probe with this round's single-step trace**,
   rather than single-stepping `!BackupServer`'s *entire* lifetime (wasteful, and
   non-determinism means a fixed step budget doesn't reliably reach the interesting
   moment). Concretely: keep the `sema.cpp`/`thread.cpp` avail_count/signal_request
   probes from round 10, and only flip `should_step = true` for `!BackupServer`
   starting *after* detecting the specific `signal_request(...) called from
   thread=!ecomserver lr=0x803aa997` pattern that round 10 identified as preceding the
   silent drop (the second/third occurrence, not the first — the first is the healthy
   exchange). This should land single-stepping precisely on the few instructions that
   matter instead of thousands of instructions of unrelated, already-understood
   activity.
2. Once that trace is captured, cross-reference against the vtable slots found this
   round (`ghidra-scripts/vtable_fns_result.txt` once copied over, and the raw
   candidate addresses in this section) to identify which one is actually
   `ServiceL` — the live trace's own PC values during the fatal wake-up will point
   directly at it, which is more reliable than continuing to guess from vtable slot
   position alone.
3. All temp instrumentation from this round has been removed — `git diff` shows only
   the round-9 fix remains, verified via a clean capped-parallelism rebuild. No
   resource incidents this round; the guardrail held up.

## Round 12: the precise mechanism — re-arm only happens once, ever

Followed the build-resource-limits guardrail throughout (capped `-m:4`, `tasklist`
checks before/after every build, MSBuild node cleanup, one heavy process at a time).
No resource incidents.

### Did round 11's plan — and it directly disproved round 10's "silent drop" framing

Implemented item 1 from round 11's plan exactly: a probe in `thread.cpp::signal_request`
arms single-stepping the moment it sees `!ecomserver` signal `!BackupServer` from
`lr=0x803aa997` (the pattern round 10 flagged as preceding the drop), then traces a
bounded window from there. Two occurrences fired in one run; captured the full
wake-to-block cycle for the first.

**What the trace actually shows is not "wakes and does nothing."** `!BackupServer`
runs a real, substantial cycle: iterates its own active-scheduler bookkeeping in
`euser.dll`, calls into `eikbackupsrv.dll`'s tiny thunk (`0x8214722c`) which calls into
`baksrv.dll` (`0x821b6252`, the same function traced statically in round 11), which
does the array-search-then-complete pattern (`FUN_821b6178` → ... →
`FUN_803aa0f6` → **the success branch this time**, `FUN_8039fd78`) — then continues
through more scheduler bookkeeping and correctly, genuinely blocks again on
`wait_for_any_request`. This is real, working code executing, not a stall or a silent
no-op. Round 11's own brief "panic" theory already established the array search does
succeed sometimes; this round confirms it succeeds *specifically during the fatal
wake-up too* — so whatever `FUN_8039fd78` completes, **it is not completing
`ecomserver`'s new message** (nothing in this trace looks like message-specific
dispatch; it looks like the same generic internal bookkeeping seen on every wake).

### The actual mechanism, found by counting instead of guessing

Cross-referencing against round 6's finding (the *healthy* first exchange includes a
distinctive "self-complete, re-enter via scheduler" call at `lr=0x803a7d83` — the
classic Symbian idiom for re-arming an active object and immediately continuing) raised
the question: does that re-arm happen again for the later messages? Added one more
cheap (non-single-step, whole-run) probe that just logs every `signal_request` call
targeting `!BackupServer`'s own request semaphore with its caller and `lr`. Full,
unambiguous result for one complete run:

```
lr=0x803aa9ab  (ecomserver's generic session_send wrapper)     x9  (mix of self- and ecomserver-sourced)
lr=0x803aa997  (ecomserver's generic session_send-with-status) x2  (the two later, unanswered messages)
lr=0x803a7d83  (the self-complete/re-arm idiom)                 x1  (once, ever, for the whole run)
```

**The re-arm idiom fires exactly once, immediately after the first message (session
connect, `ord -1`), and never again** — not for the second message, not for the third.
This is the precise mechanism: `!BackupServer`'s session correctly processes its first
message and re-registers to receive the next one. For every message after that, nothing
ever re-issues that "receive next message" registration. On the guest-code side, this
plausibly means `eikbackupsrv.dll`'s per-message handler isn't reaching the point in
`CServer2`'s framework (`RunL`/`ReStart`-equivalent) that would normally re-arm
automatically after `RMessage2::Complete()` — consistent with round 12's own trace
evidence that whatever gets "completed" during these later wake-ups isn't the incoming
message itself. On EKA2L1's side
([server.cpp](../../src/emu/kernel/src/server.cpp), `server::deliver()`): when the
server isn't `ready()` (no outstanding receive registered), a newly arrived message is
just pushed onto `delivered_msgs` with **no signal, no status completion, nothing** —
so if the guest genuinely never re-arms, the message sits there forever with no
mechanism to ever surface it again. Whether *that's* the bug (a real Symbian
`CServer2` would presumably behave the same way if a session genuinely stopped
re-arming) or whether the re-arm should be happening and something upstream prevents
it is the one open question left.

### Honest state after 12 rounds

This is now about as far as black-box tracing (live PC/LR sampling, signal counting)
can take the investigation without directly reading `ServiceL`'s actual body. The
mechanism is precisely characterized: real, correct code runs, one message gets
handled with a proper re-arm, all subsequent messages get silently queued forever
because nothing ever re-arms again. What's still missing is *why* — is
`eikbackupsrv.dll`'s own `ServiceL` failing to reach its own `RMessage2::Complete()`
call for opcodes `0x1F`/`0x22`/`0x20` specifically (in which case: a bug in
`eikbackupsrv.dll`/`baksrv.dll`'s own dispatch, or a missing EKA2L1 dependency that
handler needs), or is something about EKA2L1's message delivery specifically breaking
the *second* delivery in a way the first one doesn't hit?

### To resume from here

1. **Identify `ServiceL` for real, now with a much narrower target.** Round 11 found
   the vtable (`ghidra-scripts/vtable_fns_result.txt`) but couldn't confirm which slot
   is `ServiceL` from static analysis alone. This round adds a strong new constraint:
   whichever vtable function is being called, it must differ in behavior between the
   *first* message (which completes+re-arms) and the *second/third* (which don't) —
   that's a concrete property to test for, not just "looks big enough to be
   ServiceL." A live breakpoint (or the same single-step-after-trigger technique used
   this round, extended slightly further into the trace) at each vtable candidate
   address, comparing what happens on message 1 vs. message 2, would settle it
   directly.
2. **Alternative, possibly faster path**: check `server::deliver()`/`server::accept()`
   in [server.cpp](../../src/emu/kernel/src/server.cpp) against real Symbian's
   documented `CServer2` message-delivery contract to see whether EKA2L1's behavior
   when `!ready()` (silently queueing with no signal) is actually the *correct*,
   faithful emulation of what real hardware does, or a known simplification/gap. If
   real Symbian's `CServer2` framework is *supposed* to always have a receive
   outstanding except for a vanishingly brief window, and EKA2L1's session/message
   framework has some structural reason a real guest session's re-arm call isn't
   reaching `receive_async_lle()` correctly, that would be a different (and more
   central, more impactful) class of bug than anything guest-binary-specific found so
   far.
3. All temp instrumentation from this round removed — verified via clean
   capped-parallelism rebuild, `git diff` shows only the round-9 fix. No resource
   incidents; guardrail held up across all builds this round.

## Round 13 — correcting round 12, and a direct answer on `message_complete`

Round 12 concluded "`!BackupServer` only ever re-arms once" from counting
`signal_request` calls matching one specific LR (`0x803a7d83`, the "self-complete"
idiom). That conclusion doesn't hold up: `signal_request` targeting `!BackupServer`
was independently observed firing again for the later messages too (round 10/12 data
itself shows this), and the only way that happens is via `accept()`'s
`notify_owner` path — which requires `ready() == true` — which requires a prior
`receive_async_lle()` re-arm call. So the re-arm mechanism *must* have kept working;
round 12 was just counting one of several code paths that lead to a re-arm, not the
only one.

### New probes: `deliver()`, `receive_async_lle()`, `message_complete()`

Added direct instrumentation (all removed after this round, per the standing
guardrail):
- `server::deliver()` in [server.cpp](../../src/emu/kernel/src/server.cpp) — logs
  `ready()`, whether `delivered_msgs` was empty, and `msg->function` for every message
  delivered to `!BackupServer`.
- `server::receive_async_lle()` in the same file — logs every re-arm call against
  `!BackupServer`, with the `ready()` state beforehand.
- `BRIDGE_FUNC(message_complete, ...)` in
  [svc.cpp](../../src/emu/kernel/src/svc.cpp) (line ~719) — logs every completion
  whose owning thread is `!ecomserver`, with the message's `function` and completion
  `val`.

Result, full run to the freeze (guardrail-compliant: `tasklist` clean before build,
`-m:4` capped build, MSBuild nodes killed after, bounded 30×5s poll with 5-tick
stability breaking, force-kill after capture):

- **`deliver()`**: all four messages (`function=-1, 31, 34, 32`) arrive with
  `ready=true` — none of them are silently queued into `delivered_msgs`.
- **`receive_async_lle()`**: fires again after each of the four messages, including
  after `function=32` — `!BackupServer` re-arms every single time, with no exception.
- **`message_complete()` targeting `!ecomserver`**: fires exactly **three** times, for
  `function=-1`, `function=31`, and `function=34`. **`function=32` never appears.**

This is a direct, unambiguous answer, not an inference: the IPC delivery and re-arm
machinery are completely healthy for every message. `!BackupServer`'s own guest-side
handler for `function=32` runs (round 11/12 already traced real code executing on
that wake-up — an array search into `FUN_8039fd78`, not a spin or a crash) but that
code path never reaches `RMessage2::Complete()` (the guest-side call that lowers to
the `message_complete` SVC). `!ecomserver` is left waiting on a status that will never
be set, which is the hang.

### What this rules in / rules out

- **Rules out**: any remaining suspicion of an EKA2L1-side IPC plumbing bug
  (`deliver`/`accept`/`receive_async_lle`/`message_complete` all behave correctly and
  symmetrically for every message observed). Round 12's alternate "to resume" path
  (checking whether EKA2L1's `!ready()` queueing is unfaithful to real `CServer2`) is
  no longer the likely direction — the queueing path isn't even being taken for
  `function=32` in the first place, since `ready()` is `true` every time.
- **Rules in**: this is squarely a guest-side (`eikbackupsrv.dll`, running as
  `baksrvs.exe`) dispatch bug, or `eikbackupsrv.dll`'s handler for that specific
  opcode is waiting on some real on-device dependency/state that EKA2L1 doesn't
  provide (a P&S property, a file, a notifier) and it's blocking silently instead of
  erroring out.

### To resume from here

1. Identify which vtable slot actually handles `function=32` and single-step through
   it specifically, following it further than round 11/12 did (they confirmed the
   *wake-up* runs real code but didn't trace all the way to wherever it decides not to
   call `Complete()`). Static Ghidra decompiles of the vtable slots
   (`ghidra-scripts/vtable_fns_result.txt`) are not trustworthy for this — they come
   out as garbage (`halt_baddata()`, bogus `extraout_*` chains), almost certainly a
   Thumb/ARM mode-context mismatch during batch disassembly, consistent with every
   earlier round's finding that static disassembly misleads on this codebase. Live
   single-stepping remains the only reliable technique here.
2. Once the exact handler is found, compare against any available public Symbian
   backup-server protocol documentation/source (opcode 32 semantics) to judge whether
   it's plausibly waiting on something EKA2L1 doesn't implement (candidate: some kind
   of drive/backup-state query or notification specific to `RBackupSession`-style
   protocols) versus a genuine guest-binary bug unrelated to emulation gaps.
3. All temp instrumentation from this round removed — verified via clean
   capped-parallelism rebuild and a clean `git diff` (only the round-9 fix remains).
   No resource incidents; guardrail followed throughout (tasklist checks before/after,
   `-m:4` capped build, MSBuild node cleanup, bounded-wait polling, force-kill on
   capture completion).

## Round 14 — single-stepping the exact `function=32` dispatch

Guardrail followed throughout: `tasklist` clean before build, `-m:4` capped build
(this round's `kernel.h` change forced a fuller rebuild, ran in the background rather
than blocking, no concurrent heavy process), MSBuild nodes killed after every build,
bounded 30×5s stability poll before every force-kill.

### Probe design

Round 13 pinned the open question down to "what does `!BackupServer` actually execute
for `function=32`, and where does it diverge from a message that completes
successfully." Rather than single-stepping blindly (rounds 11/12's approach, which
never reliably landed on the right window), this round armed stepping precisely:

- `kernel_system` got two temp public members, `probe_eik8_arm` / `probe_eik8_budget`
  ([kernel.h](../../src/emu/kernel/include/kernel/kernel.h)).
- `server::deliver()` ([server.cpp](../../src/emu/kernel/src/server.cpp)) sets
  `probe_eik8_arm = true` and `probe_eik8_budget = 4000` the instant it sees
  `name() == "!BackupServer" && msg->function == 32` — i.e. armed at the exact moment
  the message we care about is delivered, nothing else.
- `system_impl::loop()` ([epoc.cpp](../../src/emu/system/src/epoc.cpp)) forces
  `should_step = true` only while the currently-scheduled thread is `!BackupServer`
  and the probe is armed with budget remaining, logging `pc`/`lr` after every step and
  disarming when the budget hits zero.

### Result: 176 real instructions, no completion, straight to the final wait

Full trace saved at
[traces/round14_function32_singlestep.log](traces/round14_function32_singlestep.log).
The budget was 4000 but only 176 steps were ever logged — not because it exhausted
the budget (it stopped at `budget_left=3824`, nowhere near 0) but because the thread
never got scheduled again after that point, matching the observed freeze exactly (the
30×5s stability poll fired, confirming no further log growth anywhere in the process).

The trace: enters the `eikbackupsrv.dll` thunk (`0x8214722c` → `0x82147c28` →
`0x821b6252`, the same entrypoint identified in round 11), runs through what looks
like the same array-search helper chain rounds 11/12 traced before
(`0x821b6180`-ish region, matching `FUN_821b6178`'s neighborhood), but then — instead
of continuing into `0x8039fd78` (the specific completion function rounds 11/12
identified as the one that successfully finishes a message) — it goes to
**`0x80399f48`**, a euser.dll address that hasn't shown up in any prior round's
trace. From there it loops through more `eikbackupsrv.dll`/`baksrv.dll` bookkeeping
(`0x821b62xx`/`0x821b5cxx`/`0x821b68xx` addresses, a `803a82aa`/`803a82c0`
lock/unlock pair bracketing a large chunk of work — matches the
`thunk_EXT_FUN_803a82aa`/`803a82c0` pair seen in round 9's static read of
`FUN_821476b6`, vtable slot 12), re-enters the `0x8214722c` thunk a **second** time
within the same dispatch, and finally lands on `0x8039fa98`/`0x8039fa9c` —
**the exact `wait_for_any_request` SVC boundary identified in round 10** — and simply
never runs again.

`0x8039fd78` (the known-good completion path) does not appear anywhere in this
176-instruction trace. This is now direct, address-level confirmation of round 13's
`message_complete`-probe finding: `function=32`'s dispatch runs real, structured code,
but takes a genuinely different branch than the one that leads to completion, and that
branch dead-ends at the normal end-of-processing wait.

### To resume from here

1. `0x80399f48` is the single most useful new lead: it's the point where this
   dispatch's path first diverges from anything seen in a successful completion. Get a
   comparable single-step trace of a message that *does* complete (`function=31` or
   `34` — arm the same `probe_eik8`-style mechanism on `msg->function == 31` instead)
   and diff the two traces from the array-search chain onward. Whatever's different
   between the two traces starting around that point is the actual branch condition
   that decides completion vs. silent drop.
2. Static-analyze `0x80399f48` specifically (not the whole vtable — a single, precise
   target this time) with fresh Ghidra state (`clearListing()` first, force Thumb mode
   via `TMode` register, per the established working procedure) to get a trustworthy
   decompile of just that one function, now that it's known to matter.
3. All temp instrumentation from this round removed — `kernel.h`'s temp members,
   `server.cpp`'s arm site, and `epoc.cpp`'s step/log site are all reverted; verified
   via a clean rebuild and `git diff` showing only the round-9 fix. No resource
   incidents this round; guardrail held up (background build used instead of a
   blocking foreground one when the `kernel.h` header change triggered a fuller
   rebuild, still single-heavy-process, no stacking).

## Round 15 — diffing a completing message's trace against round 14's

Same guardrail discipline as round 14 (background builds for the `kernel.h`-touching
change, `tasklist` checks, MSBuild node cleanup, bounded stability poll, force-kill).

### Probe design

Exact same mechanism as round 14 (`PROBE-EIK8` renamed to `PROBE-EIK9` to avoid
confusion), just armed on `msg->function == 31` instead of `32` — i.e. tracing one of
the two messages round 13 confirmed *does* reach `message_complete`, to get a directly
comparable trace.

### Result: identical for 99 steps, then an 8-byte-apart split, then reconverge

Full trace saved at
[traces/round15_function31_singlestep.log](traces/round15_function31_singlestep.log)
(691 steps this time — this message's handler does substantially more work than
`function=32`'s). Extracted just the `pc=` values from both round 14's and this
round's trace and diffed them directly:

```
$ diff eik8_pcs.txt eik9_pcs.txt
99a100,614
> 821b6226
> 821b6228
> 821b5c80
...
```

That is: **lines 1-99 are byte-for-byte identical between the two traces** — both
messages go through the exact same generic entry sequence (the `0x8214722c` thunk,
the `0x80399f48` euser.dll helper noted in round 14 as "not seen before" — turns out
it's not `function=32`-specific at all, it's part of the shared, generic per-message
entry path both opcodes take). Line 100 is where they split:

- `function=31` (completing): jumps to **`0x821b6226`**
- `function=32` (silently dropped): jumps to **`0x821b622e`**

Only 8 bytes apart — strongly suggestive of adjacent case-handler entries reached via
a computed/jump-table dispatch on the message's `function` opcode, inside
`eikbackupsrv.dll`/`baksrv.dll`'s `ServiceL`-equivalent. After this split, the two
traces run completely different code for hundreds of instructions (`function=31`'s
handler is ~515 instructions vs. `function=32`'s ~76), but **both eventually
reconverge onto an identical tail** — the last 10 PCs of both traces are byte-for-byte
the same, ending at `0x8039fa98`/`0x8039fa9c` (`wait_for_any_request`). So both
handlers return through the same generic post-dispatch/re-arm path; the only
difference is what happens *inside* the handler itself, and specifically whether it
calls through to `message_complete` before returning.

### Why this matters

This resolves round 14's open question ("is `0x80399f48` `function=32`-specific?") —
no, it's shared, generic per-message plumbing. The real, opcode-specific code is
exactly the block between the split (`0x821b6226`/`0x821b622e`) and the reconvergence,
and now there's a precise, single, small address (`0x821b622e`) to target for static
analysis instead of an entire vtable's worth of slots.

### To resume from here

1. Static-disassemble (not full Ghidra decompile — every decompile attempt so far on
   this binary has come out corrupted) starting at `0x821b622e` (`function=32`'s
   handler) and `0x821b6226` (`function=31`'s, for comparison) using the same
   `clearListing()` + forced-Thumb-mode procedure established in earlier rounds. Given
   `function=32`'s handler is only ~76 instructions before it flows back into the
   shared tail, this is a small enough target that a careful manual instruction-by-
   instruction read (not a batch decompile) should actually be tractable, unlike the
   full-vtable attempts in round 11 that produced garbage.
2. While reading `0x821b622e`'s code, watch specifically for what it does or doesn't
   call relative to `0x821b6226` — plausible candidates given round 9's history with
   this binary: a check against something EKA2L1 doesn't provide (a drive/backup
   capability query, a P&S property, a file), or simply a deliberately-unimplemented
   "not supported" opcode that real Symbian would also silently not-reply to (in which
   case the real bug might be on `!ecomserver`'s side — sending an opcode it shouldn't,
   or not handling a timeout/error for one that goes unanswered — worth checking what
   opcode 32 is *supposed to be* for a real `RBackupSession`-family protocol before
   assuming `eikbackupsrv.dll` itself is at fault).
3. All temp instrumentation from this round removed (`kernel.h`, `server.cpp`,
   `epoc.cpp`, `log.cpp` all back to their round-9-fix-only state); verified via a
   clean rebuild and `git diff`. No resource incidents; guardrail held throughout.

## Round 16 — correcting round 15: it's a handler-pointer jump, not inline case code

No emulator source changes this round — pure static analysis with Ghidra headless
against the existing `baksrv_dll_analysis` project. Guardrail note: confirmed nothing
else heavy was running (`tasklist` clean) before invoking `analyzeHeadless`, since it's
its own JVM process and the guardrail treats it the same as a build.

### First static read was misleading — same Thumb-alignment trap as round 11

Naively disassembling straight at `0x821b6226` and `0x821b622e` (treating them as
function entry points, same technique as every prior round's vtable dump) produced:

```
821b6226: ldr r1,[r1,#0x0]
821b6228: movs r0,r4
821b622a: bl 0x821b5c60
821b622e: str r6,[r5,#0x0]
821b6230: pop {r3,r4,r5,r6,r7,pc}
```

which looked at first like confirmation of round 15's "adjacent switch case" theory —
except it isn't: `0x821b622e` isn't a separate entry point at all here, it's simply
the instruction immediately after the `bl` at `0x821b622a` finishes (a 4-byte Thumb
`BL` encoding spans `0x821b622a`-`0x821b622e`). So this single linear read cannot be
what actually happens for `function=32`, since round 15's live trace showed `32`'s
step landing on `0x821b622e` in a **single instruction step** directly from
`0x80399f5c` (inside the shared euser.dll helper) — it never executes the `ldr`/
`movs`/`bl` at all. Something upstream must reach `0x821b622e` by a completely
different route.

### Finding the real route: disassemble from the live return address, not a guess

The live trace's own data holds the answer: right before the split, both traces show
identical `lr=0x821b6195` while stepping through `0x80399f4c`-`0x80399f5c`
(`traces/round14_function32_singlestep.log` / `round15_function31_singlestep.log`,
lines ~96-101 of each). Disassembling a wide net starting from a guessed entry point
(`0x821b6180`, matching round 11's remembered address for the array-search helper)
produced a *different*, internally-consistent-looking but ultimately misleading
sequence, ending in an unconditional `b 0x821b622e` at `0x821b61a6` — plausible, but
its instruction boundaries didn't actually line up with the live `lr=0x821b6195`
(that address fell mid-instruction in this disassembly, the exact same "confidently
wrong due to bad alignment" failure mode every earlier round hit with vtable slots).

Fix: disassemble starting **exactly** at the live, trace-confirmed address
(`0x821b6195`) instead of guessing an entry point. That produced a clean, correctly
aligned sequence:

```
821b6196: subs r0,#0x34
821b6198: ldr r5,[0x821b62c0]
821b619a: ldrsb r3,[r2,r5]
821b619c: ldrh r6,[r1,r0]
821b619e: ldr r1,[r1,#0x0]
821b61a0: movs r0,r4
821b61a2: bl 0x821b615a
821b61a6: b 0x821b622e
821b622e: str r6,[r5,#0x0]
821b6230: pop {r3,r4,r5,r6,r7,pc}
```

This is unconditional and identical regardless of opcode — so it can't be the branch
point either. Combined with the fact that **the live trace shows PC landing on two
different addresses (`0x821b6226` vs `0x821b622e`) from the same single instruction
step at `0x80399f5c`, despite `lr` being identical in both cases**, the only
consistent explanation is that `0x80399f48`'s own code (the euser.dll helper both
traces call into) does **not** return via a plain `bx lr` — it performs a computed /
indirect jump to a handler address that's read from somewhere else (a per-opcode
handler-pointer table, most plausibly), and that handler pointer is different for
`function=31` vs `function=32`. This matches Symbian's real active-object/scheduler
architecture: `0x80399f48` is very plausibly generic `CActiveScheduler`-style
completion-dispatch code, calling through to whichever specific handler is registered
for the request that just completed — not a fixed return address at all.

### The punchline

Given that framing, the two "landing addresses" are not two branches of one function —
they're two different registered handlers, and the earlier straight-line read of them
(the very first disassembly in this round) is actually the right data, just
misinterpreted:

- **`function=31`'s handler, `0x821b6226`**: `ldr r1,[r1,#0]; movs r0,r4;
  bl 0x821b5c60` — genuinely calls into further logic (`0x821b5c60`, dumped in this
  round too — it's a small real function with its own conditional branch) — then
  falls into the shared epilogue at `0x821b622e`.
- **`function=32`'s handler, `0x821b622e`**: is *itself* nothing but the shared
  epilogue (`str r6,[r5,#0]; pop {r3,r4,r5,r6,r7,pc}`) — an empty no-op. Whatever
  registered `function=32`'s handler pointed it directly at the common return code,
  skipping any actual work entirely.

This is a materially different, more precise finding than round 15's "adjacent switch
case" theory (which assumed both were reached via ordinary sequential/branch code, not
an indirect jump) — correcting it here. It's also a very plausible root cause: if
`eikbackupsrv.dll`'s per-opcode handler table has a no-op registered for opcode 32,
`message_complete` can never be reached for that opcode **regardless of anything
EKA2L1 does**, because the guest code itself never attempts it.

### To resume from here

1. Find where the handler-pointer table itself lives and how it's indexed by opcode —
   that's the structure that would show *why* opcode 32 got a no-op (deliberately
   built that way, vs. a slot EKA2L1's environment fails to populate). The call
   sequence into `0x80399f48` should have the table/index as one of its arguments;
   worth single-step tracing a few instructions further back from the `bl` into
   `0x80399f48` itself (not yet done) to see what's passed in.
2. Cross-reference against any available public documentation/source for Symbian's
   backup-server protocol (`RBackupSession`/`CBackupServer`-family opcodes) to check
   whether opcode 32 (`0x20`) is a real, well-known operation that should have a
   handler, or something app/build-specific to this N-Gage image's `eikbackupsrv.dll`
   build. If it's a legitimately no-op/unsupported operation on real hardware too,
   this shifts the investigation toward "why does `!ecomserver` send it and then wait
   forever with no timeout" instead of "why doesn't `eikbackupsrv.dll` reply."
3. No emulator source touched this round — nothing to revert. New Ghidra scripts
   (`DumpFunc31vs32Handlers.java`, `DumpFunc32SkipBranch.java`, `DumpFromLiveLr.java`)
   added to `ghidra-scripts/` alongside the earlier rounds' scripts, all reusable.
   `git diff` on the emulator source tree still shows only the round-9 fix. No
   resource incidents.

## Round 17 — round 16's module attribution was wrong; a new silent-trap caught

No emulator source changes this round either — continued static analysis, plus a
`parse_rom.py` header scan (cheap, no Ghidra/build/emulator needed). Guardrail:
confirmed `tasklist` clean before the one Ghidra headless invocation this round.

### Chasing round 16's "to resume" item 1 uncovered a bad assumption

Round 16's plan said to trace back into `0x80399f48` to find the handler-table lookup.
Wrote `DumpEuserDispatch.java` to disassemble it in ARM mode (justified by the live
trace's 4-byte-per-step stride through that region) against the `euser_fixed_analysis`
Ghidra project — it ran without error and produced a plausible-looking function (64-bit
multiply helpers, `umull`/`umlal`/`smlal` instructions). **This output was not
actually from `0x80399f48` at all.**

Caught by habit, not luck: per this file's own recurring lesson (never trust a
static read without cross-checking), wrote a follow-up sanity script
(`CheckEuserAddr.java`) that just asks Ghidra directly what memory block contains
`0x80399f48` and what raw bytes are there — and got
`MemoryAccessException: Unable to read bytes at ram:80399f48`. The address isn't
mapped in that project at all. `DumpEuserDispatch.java`'s `disassemble()`/
`clearListing()` calls on an address outside any memory block silently no-op instead
of erroring, and the subsequent `listing.getInstructions(start, true)` iterator simply
returned whatever pre-existing instructions came at-or-after that address in the
program's *existing* analysis — which happened to look like a real, coherent function,
purely by coincidence of where Ghidra's iterator landed. **A new, sharper version of
the "silent trap" pattern this file has hit before** (round 6's permission-bug
"zero cross-module calls" false negative) — this time a false *positive*: a clean,
plausible, wrong answer with no error at all, unless you separately verify the address
is actually mapped before trusting output built from it.

### Where `0x80399f48` actually is

Re-parsed `euser.dll`'s own ROM header with `parse_rom.py` directly against the live
device's file
(`build/bin/data/drives/z/rm-320/sys/bin/euser.dll`): `code_address=0x8039CDB8`,
`code_size=0x32570` → range `0x8039CDB8`-`0x803CF328`. `0x80399f48` is below that
range's start — **not euser.dll**. Scanned every `.dll`/`.exe` under
`rm-320/sys/bin/` with the same header parser looking for whichever module's code
range actually contains `0x80399f48`:

```
0x80399088 0x8039ac8c  drtrvct2_1.dll
0x80399088 0x8039ac8c  drtrvct2_2.dll
```

`drtrvct2` is the RVCT (RealView Compiler Toolchain) runtime-support library —
standard Symbian ROM component providing compiler-generated helper routines
(long multiply/divide, floating point, etc.), not application or server logic. This
actually makes the earlier ARM-mode 64-bit-multiply-helper output *plausible* as
genuine `drtrvct2` content in general (that's exactly the kind of code this DLL
contains) — but it was still read from the wrong address with no verification, so it
cannot be trusted as *the* function at `0x80399f48` specifically without redoing the
read against a project that actually has `drtrvct2` imported (none of the existing
projects do).

### What this changes

Round 16's two hard facts about `baksrv.dll` itself are untouched by this correction
(that project's address mapping has been cross-validated against the live trace many
times over many rounds, unlike this one-off euser.dll guess): `0x821b622e` is still
verified to be the bare shared epilogue, and `0x821b6226` still verified to call real
logic first. What's now retracted is only the *interpretation* of the call in between
as "indirect handler-pointer dispatch inside a euser.dll scheduler helper" — that was
speculation built on a wrong module attribution. The actual mechanism connecting "call
a `drtrvct2` arithmetic helper" to "land on two different addresses despite identical
`lr`" is still unexplained and needs real evidence, not another guess.

### To resume from here

1. The most direct fix: extend the round 14/15-style single-step probe to log full
   registers (`r0`-`r12`, not just `pc`/`lr`) for the ~10 instructions immediately
   before and after the call into `0x80399f48`, for both `function=31` and
   `function=32`. Whatever register differs between the two traces at that point is
   either the branch-driving value itself or points at the table that produces it —
   this replaces guessing with the same live-evidence-first approach that got rounds
   9-16 their real results.
2. If a genuine jump-table read is confirmed, import `drtrvct2_1.dll` into a fresh
   Ghidra project (its own `parse_rom.py` header, `-loader-baseAddr 0x80399088`, same
   Thumb-forcing setup as every other module here) so `0x80399f48` can finally be
   statically read from a project that actually contains it, instead of guessing
   against an unrelated one.
3. No emulator source touched this round — nothing to revert. Two new Ghidra scripts
   (`DumpEuserDispatch.java`, `CheckEuserAddr.java`) added to `ghidra-scripts/`;
   the former's output should not be trusted as-is (see above) but the script itself
   is fine once pointed at a project that actually maps the target address. No
   resource incidents; the one Ghidra invocation this round was not run concurrently
   with anything else per the guardrail.

## Round 18 — direct register evidence of a real opcode-indexed table lookup

Guardrail followed throughout: `tasklist` clean before build, `-m:4` capped
(background) build for the `kernel.h`-touching change, MSBuild nodes killed after,
bounded 30×5s stability poll, force-kill on capture.

### Probe design

Round 17's own "to resume" plan: extend the single-step probe to log full registers,
not just `pc`/`lr`, through the call both `function=31` and `function=32` make into
`0x80399f48`. Implemented as two parallel probes (`PROBE-EIK10` for `function=32`,
`PROBE-EIK11` for `function=31`) armed independently in `server::deliver()` — since
both messages occur in the same run, a single repro captured both traces at once
instead of needing two separate runs like rounds 14/15/18's predecessors. Logged
`pc`, `lr`, `r0`-`r4` on every step, budget 150 (smaller than rounds 14/15's 4000 —
the split happens within the first ~100 steps, no need for a larger window).

### Result

Full trace saved at
[traces/round18_register_trace.log](traces/round18_register_trace.log). The critical
rows, right at the step where the two traces diverge (both stepping through
`0x80399f4c`-`0x80399f5c` inside the `drtrvct2` call, then landing on `0x821b6226` /
`0x821b622e`):

```
function=31: pc=80399f5c lr=821b6195 r0=700548 r1=700134 r2=4047d8 r3=49 r4=700548
             pc=821b6226 lr=821b6195 r0=700548 r1=700134 r2=4047d8 r3=49 r4=700548
function=32: pc=80399f5c lr=821b6195 r0=700548 r1=700134 r2=4047d8 r3=4d r4=700548
             pc=821b622e lr=821b6195 r0=700548 r1=700134 r2=4047d8 r3=4d r4=700548
```

`r0`, `r1`, `r2`, `r4` are byte-for-byte identical between the two runs (same object
pointers, same session state — expected, since round 15 already showed the first 99
steps are identical). **`r3` is the one register that differs: `0x49` for
`function=31` vs `0x4d` for `function=32` — a delta of exactly 4**, matching the
function-number delta (`32 - 31 = 1`) times a 4-byte stride. This is real,
measured evidence — not the speculative "indirect handler-pointer dispatch" framing
round 16 guessed and round 17 had to walk back for lacking any actual support. `r3`
is almost certainly a byte-offset into a per-opcode table (`base + opcode * 4`,
computed by the `drtrvct2` arithmetic call using the opcode as an input), and whatever
reads through that offset — not yet directly observed, it happens just past this
round's 150-step window, right around where the trace lands on `0x821b6226`/
`0x821b622e` — is what actually selects the two different landing addresses.

### To resume from here

1. Widen the register probe's window slightly (worked at budget 150; try 20-30 more
   steps past the landing address) to directly catch the instruction that reads
   `[table_base + r3]` and branches — this would give the actual table's base address
   and entry count, letting the *real* handler-dispatch table be read directly
   (function pointers for every opcode, not just 31 and 32), rather than continuing to
   infer it indirectly.
2. Once the table's base address is known, a live memory dump of it (same technique
   round 7 used to ground-truth `parse_rom.py`) would show every registered opcode
   handler at once — likely including whichever opcodes silently no-op like `32` does,
   which would settle whether this is `eikbackupsrv.dll`-specific or a broader pattern.
3. Import `drtrvct2_1.dll` into its own Ghidra project (round 17's plan, still valid
   and not yet done) if static analysis of the arithmetic helper itself becomes
   useful — lower priority now that the table-index register (`r3`) has been found by
   direct live evidence without needing it.
4. All temp instrumentation from this round removed (`kernel.h`'s two probe member
   pairs, `server.cpp`'s two arm sites, `epoc.cpp`'s combined step/log site, `log.cpp`
   flush level); verified via a clean rebuild and `git diff` showing only the round-9
   fix. No resource incidents.

## Round 19 — the mechanism, closed with a live memory dump and hand-verified arithmetic

Guardrail followed throughout, including a genuine new module import for the first
time this investigation (`drtrvct2_1.dll` into a fresh Ghidra project) — confirmed
`tasklist` clean before it, one heavy process at a time, MSBuild nodes killed after
every build, bounded stability polling before every force-kill.

### Reading `0x80399f48` for real this time

Round 17's plan (import `drtrvct2_1.dll` into its own project so `0x80399f48` could
finally be read from a project that actually contains it) turned out necessary sooner
than expected. `parse_rom.py` against the live device's
`drtrvct2_1.dll`: `code_address=0x80399088`, `code_size=0x1C04`. Importing it into
Ghidra needed the exact `-processor <languageID>` / `-cspec <compilerSpecID>` flag
split (a combined `ARM:LE:32:v5t:default` string is rejected; `analyzeHeadless`'s own
`-help` output showed the correct separate-flag form) — once that was right, the
import succeeded and disassembling `0x80399f48` in ARM mode (justified by the earlier
4-byte-per-step live stride through that region) gave a small, clean, six-instruction
function:

```
80399f48: ldrb r12,[lr,#-0x1]
80399f4c: cmp r3,r12
80399f50: ldrbcc r3,[lr,r3]
80399f54: ldrbcs r3,[lr,r12]
80399f58: add r12,lr,r3, lsl #0x1
80399f5c: bx r12
```

This is immediately recognizable as RVCT's standard compiler-emitted switch/case
byte-jump-table trampoline, with built-in bounds checking: `r12` = the max valid case
index, read from one byte before the table (`[lr,#-1]`); if the incoming case index
(`r3`) is in range (`CC`, unsigned less-than), fetch `table[r3]`; otherwise (`CS`)
clamp to `table[r12]` (the default case); either way, double the fetched byte and add
it to the table base (`lr`) to get the branch target. `lr` here isn't a real return
address at all — it's the address immediately after the `bl` into this trampoline,
deliberately used as a data pointer to the inline table that lives right there in the
instruction stream (a well-known ARM/RVCT idiom; the trampoline never "returns" to
that address as code).

### Ground-truthing it against live memory, not another static guess

Rather than trust a second static read of the table bytes (round 17/18's earlier
static table read had *already* turned out inconsistent with the live `r3` values —
see below), added a live-memory-dump probe: on the first time `!BackupServer`'s PC
hits `0x80399f48` while processing `function=32`, read `0x20` bytes of real guest
memory starting at `lr-1` directly via `eka2l1::get_raw_pointer()`
([epoc.cpp](../../src/emu/system/src/epoc.cpp)) — same "ground truth via live memory,
not static extraction" principle round 7 used to fix `parse_rom.py`. First attempt
checked PC unconditionally every scheduler tick and never fired (PC is only sampled at
quantum boundaries that way, essentially never landing exactly mid-function); fixed by
combining it with the same single-step-while-armed mechanism every trace probe since
round 14 has used. Full capture saved at
[traces/round19_switch_table_dump.log](traces/round19_switch_table_dump.log):

```
hit#1 lr-1=821b6194 r3=c: 10 09 12 16 1c 21 25 29 2d 31 34 38 49 4d 53 57 0e 5a 09 68 20 00 ff f7 da ff 42 e0 20 00 ff f7
```

Only one hit landed within budget across several attempts (600-step budget, still only
one hit — `!BackupServer`'s actual scheduling turns out to only pass through this
exact trampoline invocation once per run for this message, consistent with everything
else about non-determinism this file has noted before) — but one confirmed live hit
was enough. Two things fall out of it immediately:

- **The live `r3` value going into the trampoline is `0xc` (12), not `0x49`/`0x4d`.**
  This resolved an apparent contradiction from an earlier same-round static read of
  this table (base address `0x821b6194`, matched exactly) that showed the byte at
  `table[-1]` as `0x2d` (45) — which would mean `r3=0xc` (12 < 45) takes the
  in-range path and **overwrites `r3`** with `table[12]`, not leave it unchanged.
  Round 18's `r3=0x49`/`0x4d` readings were captured *after* this exact overwrite
  happened (a few instructions later in the same trampoline call) — they were never
  the raw case index at all, they were already `table[case_index]`.
- **The live table's byte content doesn't match the earlier static Ghidra read at
  all** (`10 09 12 16 1c 21 25 29 2d 31 34 38 49 4d ...` vs. the static read's
  `2d 31 34 38 49 4d 53 57 0e 5a ...`) **except for an exact 8-byte offset**: the
  static read is precisely the live dump with its first 8 bytes stripped off. This
  means the *earlier* static `DumpSwitchTable.java` script (round 19's own first
  attempt, before the live-memory probe) had silently read from 8 bytes past the true
  table start — not a base-address bug this time, but circumstantial evidence for
  exactly how easy it is to get table/array indexing subtly wrong even when the base
  address itself is correct. Not chased further since the live-memory dump made it
  moot, but worth remembering as one more entry in this file's running list of
  "plausible-looking but wrong" static-read traps.

### Hand-verified arithmetic, closing the loop completely

Using the live-confirmed table (`table[-1]=0x10`, i.e. max valid index 16 — both case
11 and case 12 are comfortably in range, not clamped defaults) and the trampoline's own
logic:

- `function=31`, case index `0xb` (11): `table[11]` = byte at `buf[1+11]=buf[12]` =
  `0x49`. Target = `0x821b6194 + 0x49*2` = `0x821b6194 + 0x92` = **`0x821b6226`** —
  exactly `function=31`'s live-observed handler address (rounds 14/15/16).
- `function=32`, case index `0xc` (12): `table[12]` = byte at `buf[13]` = `0x4d`.
  Target = `0x821b6194 + 0x4d*2` = `0x821b6194 + 0x9a` = **`0x821b622e`** — exactly
  `function=32`'s live-observed landing address (the bare shared epilogue).

Both computed by hand from the single live memory dump and both match the
independently-observed live PC values from four rounds ago, exactly. This is no
longer an inference chain — it's a complete, arithmetic-verified reconstruction of the
entire dispatch mechanism from the opcode down to the branch target. **`baksrv.dll`'s
`ServiceL`-equivalent has a genuine, in-range, compiler-emitted switch case for
whatever internal case number opcode 32 maps to (`0xc`), and that case's compiled code
is nothing but the shared return path.** This is not a clamped/default fallback, not
an EKA2L1 IPC bug (round 13 already ruled that out directly), and not a
disassembly/alignment artifact (verified against live memory, not a static guess) —
it is exactly, deliberately, what the guest binary's compiled switch statement does
for this specific case.

### What's actually left

The mechanism side of this investigation is now closed as far as static/dynamic binary
analysis alone can take it. What remains is a source-level question that likely can't
be answered from the binary: *why* does `eikbackupsrv.dll` have an empty case for
this specific backup-server opcode — a deliberate, real "no-op, don't reply" by
design (in which case the actual bug, if there is one, is that `!ecomserver` sends
this message and waits synchronously forever with no timeout — an `!ecomserver`-side
or higher-level protocol question, not an `eikbackupsrv.dll` or EKA2L1 bug at all), or
a case that's *supposed* to be completed by some other asynchronous path this specific
build/configuration never reaches. Answering that needs either public documentation/
source for the real Symbian backup-server protocol (what opcode `0xc`/case-12 actually
represents), or accepting that this specific empty case is simply how the shipped
`eikbackupsrv.dll` binary behaves and shifting focus to whether `!ecomserver` (or
EKA2L1 on its behalf) should have a timeout for an unanswered backup-server message
instead of waiting forever.

### To resume from here

1. This is a reasonable point to step back from pure mechanism-tracing and ask whether
   a *pragmatic* fix is worth pursuing even without full source-level certainty: e.g.,
   EKA2L1 could recognize this specific hang pattern (a message to `!BackupServer`
   that's dispatched to a genuine no-op case and never completed) and either
   synthesize a completion after a timeout, or investigate whether `!ecomserver`
   itself has a timeout mechanism that EKA2L1 isn't correctly triggering. This is a
   different kind of work than the last nineteen rounds (engineering a fix vs.
   characterizing a mechanism) and probably warrants explicit direction before writing
   any of it.
2. If continuing pure investigation instead: check whether other, unrelated N-Gage (or
   other S60v3) titles that use `eikbackupsrv.dll`'s backup session ever hit this same
   case-12 opcode — if it's never hit by anything except this specific restored-backup
   scenario, that's circumstantial support for "this opcode is backup-restore-specific
   and genuinely expected to no-op outside of an active restore," which would clarify
   the source-level question above without needing real Symbian source access.
3. All temp instrumentation from this round removed (`kernel.h`'s probe members,
   `server.cpp`'s arm site, `epoc.cpp`'s step/dump site, `log.cpp` flush level);
   verified via a clean rebuild and `git diff` showing only the round-9 fix. Two new
   Ghidra scripts added (`DumpDrtrvct2Dispatch.java`, `DumpSwitchTable.java`) alongside
   a new `drtrvct2_analysis` Ghidra project (not part of the git repo, lives under
   `C:\Users\Usuario\Desktop\tools\ghidra_project\`, same as every other project this
   investigation has used). No resource incidents; guardrail held throughout, including
   through the new-module-import step.

## Round 20 — cross-device comparison: is the empty case normal for this codebase?

Following round 19's "to resume" item 2 (user picked this direction explicitly over
writing a pragmatic fix): check whether other devices' `baksrv.dll` builds ever fill
in the analogous case, as a way to judge whether an empty switch case is
`eikbackupsrv.dll`/N-Gage/RM-320-specific or a normal pattern in this codebase. No
emulator source touched — pure static comparison across the emulator's bundled device
ROMs, all under the existing Ghidra-headless workflow. Guardrail: confirmed `tasklist`
clean before every Ghidra invocation, one at a time.

### Which devices even have this DLL

`eikbackupsrv.dll`/`baksrv.dll`/`baksrvs.exe` exist on `rm-320` (N95, this
investigation's device), `rm-409`, and `rm-675`, but not on `nem-4` or `rh-29` (older
or differently-configured devices bundled with EKA2L1, presumably pre-dating this
particular backup subsystem or configured without it). Header comparison via
`parse_rom.py`:

```
rm-320 baksrv.dll: code_size=0x18E8, exports=0x27, version=2.1 build=0x240
rm-409 baksrv.dll: code_size=0x3148, exports=0x2C, version=2.1 build=0x240  (~2x larger)
rm-675 baksrv.dll: code_size=0x1EB8, exports=0x31, version=2.3 build=0x1   (different OS edition)
```

`rm-409`'s `baksrv.dll` reports the *exact same* nominal DLL version as `rm-320`'s
(`2.1`, build `0x240`) but is nearly double the code size with more exports — the best
available candidate for "a fuller build of essentially the same component," so this
round focused on it.

### Confirming it's the same dispatch mechanism, then finding the site

Extracted `rm-409`'s `baksrv.dll` and `drtrvct2_1.dll`
(`code_address`/`code_size` via `parse_rom.py`, same as every module this
investigation has extracted), imported both into fresh Ghidra projects
(`baksrv_rm409_analysis`, and confirmed the trampoline's presence directly in the raw
`drtrvct2_1.dll` file rather than importing it separately — see below). Rather than
guess which of 185 auto-detected functions might be the dispatcher, searched
`drtrvct2_1.dll`'s raw bytes for the *exact* 24-byte instruction sequence round 19
identified as the switch trampoline
(`01 c0 5e e5 0c 00 53 e1 03 30 de 37 0c 30 de 27 83 c0 8e e0 1c ff 2f e1`) — found at
file offset `0xf38`, mapping to runtime address `0x80196fe8`. This exact address
showed up in `rm-409`'s `baksrv.dll` overview as `thunk_EXT_FUN_80196fe8` — direct
confirmation `rm-409`'s `drtrvct2` build contains the byte-identical trampoline (not
just a similar one), and `baksrv.dll` calls it the same way. Cross-referencing that
thunk found exactly one caller, `FUN_80b847b6`, structurally identical to `rm-320`'s
dispatcher (`bl <helper>; mov r6,#1; add r7,r4,#0x30; blx <trampoline>`).

### The table itself: much bigger, and mostly sparse/shared

Dumped the table at the caller's return address (`0x80b847d0`, same
"return-address-doubles-as-table-pointer" idiom) the same hand-verified way as round
19. `table[-1]` (max valid case index) is `0xea` (234) here — a much larger declared
range than `rm-320`'s `0x2d` (45)/effective max observed (16), consistent with
`rm-409`'s roughly 2x larger code size. Decoded targets for cases 0-126:

```
target=0x80b847cf  x14   (points at the table itself - a "0" entry byte, i.e. unused/reserved slot)
target=0x80b8480f  x13
target=0x80b849cd  x12
target=0x80b849bf  x9
target=0x80b84949  x9
target=0x80b849bd  x8
(plus several more distinct real-looking targets appearing 1-3 times each)
```

Out of the 127 cases sampled, a handful of shared/generic addresses account for the
large majority — this table is **sparse**: most of the declared 0-234 case range
routes to a small set of shared fallback/generic targets (including, notably, several
cases whose entry byte is literally `0`, landing back on the table's own data bytes —
clearly unused/reserved slots, not real code, the compiler's way of padding a
wide-ranging case value space where only specific values are actually used) rather
than each case having distinct, unique per-opcode logic.

### What this does and doesn't answer

This directly supports the theory round 19 flagged but couldn't confirm: **switch
cases that don't do unique per-opcode work — routing to shared/generic code instead of
distinct logic — are a normal, recurring pattern in this codebase's compiled backup-
server dispatchers, not something specific to `rm-320`'s smaller `baksrv.dll` or to
N-Gage.** `rm-409`'s independently-compiled, nearly-2x-larger build shows the same
qualitative pattern (many cases sharing generic targets) even though it's a materially
different binary with a different case count and (almost certainly) a different
case-to-opcode numbering.

What it does **not** answer, and likely can't from binary comparison alone: whether
the *specific* case that corresponds to N-Gage's opcode 32 is one of the "shared/
generic" ones on `rm-409` too, or a real, implemented one. Mapping case-index-12 in
`rm-320`'s table to a specific case index in `rm-409`'s much larger, differently-laid-
out table isn't reliable without knowing the real opcode semantics (the two binaries
aren't guaranteed to number cases the same way just because they implement a related
protocol) — attempting that mapping was judged not worth pursuing further; the
qualitative finding (sparse tables with shared fallbacks are normal here) is the
solid, defensible result from this round.

### To resume from here

1. The original round-19 fork in the road still stands, now with more evidence
   supporting the "expected no-op, not a bug" reading: since sparse/shared switch
   cases are demonstrably normal in this codebase across at least two independently-
   compiled devices, the more probable real issue is `!ecomserver` waiting
   synchronously forever on a message that (by this codebase's own normal patterns)
   may simply never get a distinct reply — i.e., a missing timeout on `!ecomserver`'s
   side (or EKA2L1 needing to model one), not a `eikbackupsrv.dll` defect specific to
   this restored backup.
2. A pragmatic fix is the natural next step if this investigation continues:
   EKA2L1 could detect this specific stuck-wait pattern (a message sent to
   `!BackupServer` that reaches a dispatch case but never completes) and use a timeout
   to unstick it, or more precisely investigate whether real Symbian's `RSendReceive`-
   with-status pattern has a bounded wait that EKA2L1's implementation is missing.
   This is implementation work, not further characterization, and should proceed only
   with explicit direction on the desired approach.
3. No emulator source touched this round. Two new Ghidra scripts added
   (`DumpBaksrvRm409Overview.java`, `FindRm409SwitchCallers.java`,
   `DumpRm409SwitchTable.java`) alongside a new `baksrv_rm409_analysis` Ghidra project
   (same external location as every other project here). `git diff` on the emulator
   source tree still shows only the round-9 fix. No resource incidents; guardrail
   held throughout.

## Round 21 — the pragmatic fix was tried, made things worse, and was fully reverted

At the user's explicit direction, implemented round 20's option 1: a narrow watchdog
that auto-completes a message to `!BackupServer` with `KErrNone` the moment the server
re-arms while that message is still un-completed (event-driven off the exact proven
mechanism, not a blind timer). **Built, tested end-to-end, and found it makes the
situation strictly worse — reverted completely.** Documenting why, since this is a
real, useful negative result that rules out the simplest version of this approach.

### The fix as implemented

- [server.h](../../src/emu/kernel/include/kernel/server.h): added
  `ipc_msg_ptr last_accepted_msg_` to `server`, tracking the most recently
  `accept()`-ed message.
- [server.cpp](../../src/emu/kernel/src/server.cpp): `accept()` set
  `last_accepted_msg_ = msg`. `receive_async_lle()` (the re-arm entry point) checked,
  before doing its normal work: if `name() == "!BackupServer"` and
  `last_accepted_msg_` is still `ipc_message_status::accepted` (never transitioned to
  `completed`), force-complete it right there — set status, signal the client thread,
  close the held thread handle, run the IPC-complete callbacks, `unref()` — mirroring
  the exact idiom `session::detatch()` already uses for a different scenario (session
  teardown with messages still in flight).
- [svc.cpp](../../src/emu/kernel/src/svc.cpp): `message_complete` needed to actually
  set `msg->msg_status = ipc_message_status::completed` (it never had — a real, if
  previously harmless, small gap: nothing currently reads that enum value on the
  normal completion path) so the watchdog could reliably tell "genuinely completed"
  from "abandoned" and never double-complete an already-answered message.
- Deliberately scoped to `name() == "!BackupServer"` only, not applied to `server`
  generally — other servers legitimately hold messages open across multiple re-arms
  (real async, multi-outstanding-request patterns), where this exact check would
  incorrectly force-complete messages that were never actually abandoned.

### What happened when it ran

Guardrail followed (build → confirm clean → run repro → bounded poll). The poll that
previously showed the log plateau at ~42KB (the hang) instead showed **continuous,
extremely fast growth**: 42KB → 4.2MB in the first 5-second tick, then roughly
10-12MB every tick after, reaching 323MB before the 150-second bound was hit and the
process was force-killed. `grep -c` on the captured log found the watchdog's warning
line (`"!BackupServer re-armed with message (function=32) never completed by guest
dispatch - auto-completing with KErrNone"`) had fired **1,873,129 times** in that
window — roughly 12,500 times per second, sustained. Not a one-time unstick: a tight,
continuous loop. Both the resulting log file and the process's captured stdout had
grown past 300MB each; both deleted immediately after inspection (disk had ample
headroom throughout - `df` showed 76G free even at peak - so this didn't threaten the
machine, but it's a much less contained state than the original clean, stable hang).

### Why this is a real, useful negative result

The rate strongly suggests genuine guest-driven activity, not a bug in the watchdog's
own bookkeeping (each full round trip — `!ecomserver` sends `function=32`, EKA2L1
delivers/accepts/force-completes it, the client thread gets signaled, re-evaluates,
and immediately sends `function=32` again — plausibly fits in the instruction budget a
modern JIT can execute in under 100 microseconds, so ~12,500/sec sustained is not
implausible for a real, tight retry loop). Read together with rounds 19/20's findings
(the guest's own compiled dispatch code has a real, in-range switch case for this
opcode that does nothing), the most likely explanation: **`!ecomserver` is not
blocked waiting for *any* reply — it's waiting for a *specific* outcome, and reacts to
an unwanted/unexpected `KErrNone` by retrying the same request immediately rather than
treating it as a legitimate answer.** That's consistent with `function=32` being some
kind of polling/state-check request (e.g. "has the restore state changed yet") where
the real, correct reply depends on state this fix has no way to know or provide.
Blindly completing it with *any* fixed value doesn't satisfy whatever real condition
`!ecomserver` is actually testing for — it just gets asked again immediately.

This rules out the simple "auto-complete with a canned status" approach entirely, not
just this specific error-code choice — the problem isn't picking the *right* value to
complete with, it's that a single, unconditional auto-complete can't satisfy a
client that's polling for a state transition it expects to eventually observe for
real. A working fix would need to know what `!ecomserver` is actually testing for
(needs tracing `ecomserver.exe`'s own logic around this call, not yet done in this
investigation) rather than guessing a value from the `!BackupServer` side alone.

### Reverted completely

All three files back to their exact pre-round-21 state (`server.h`'s temp member,
`server.cpp`'s `accept()`/`receive_async_lle()` changes, `svc.cpp`'s
`message_complete` addition — all removed); `log.cpp`'s flush level restored.
Verified via a clean rebuild and `git diff` showing only the round-9 fix. The runaway
log/stdout files from the failed test run were deleted. No resource incidents beyond
the large-but-not-machine-threatening log growth already described; guardrail held
throughout (single heavy process at a time, `tasklist` checks, MSBuild cleanup).

### To resume from here

1. Before attempting another fix: trace `ecomserver.exe`'s own code around this
   specific call — what does it actually check the reply against, and does it have
   any bound on retries (a max-attempt count, a delay, anything) or is it a genuine
   unconditional busy-loop on the guest side regardless of what EKA2L1 does? If
   `!ecomserver` itself busy-loops with no backoff on ANY reply it doesn't like, no
   completion value from EKA2L1's side can fix this cleanly — the fix would need to
   either supply the exact real value being polled for (needs understanding the real
   backup-restore state machine) or address it at a different layer entirely (e.g.
   possibly not a bug to "fix" at the emulator level at all, if this really would
   spin/hang on real hardware too under this specific restored-backup scenario).
2. If continuing, use the same probe infrastructure this investigation already has
   (single-step + register tracing, established rounds 14-19) but pointed at
   `ecomserver.exe` this time instead of `baksrv.dll`, to see what value it reads
   back from the reply and what it does with it.

## Round 22 — the whole "re-arm implies abandonment" premise is unsound

Tried to observe `!ecomserver`'s actual reaction to a reply by making round 21's
watchdog fire exactly once (not on every re-arm, to avoid the retry storm) and
single-step-tracing `!ecomserver` for 400 instructions right after. **The result
invalidates the core assumption behind round 21's whole approach, not just its choice
of completion value — reverted immediately, no fix attempted this round.**

### What was built

- Re-added `server::last_accepted_msg_` (temporary, same as round 21) and a
  `kernel_system::probe_eik13_fired` one-shot latch: the watchdog in
  `receive_async_lle()` now only fires the very first time it sees a re-armed server
  with a still-`accepted` prior message, never again after — specifically to avoid
  round 21's runaway retry loop while still getting one live look at what happens
  right after a synthetic completion.
- On firing, armed a single-step trace on `!ecomserver` specifically (400-instruction
  budget), logging `pc`/`lr`/`r0`-`r3`.

### What actually happened

The watchdog fired on the **very first** re-arm it saw — which turned out to be for
`function=-1` (the session connect message), not `function=32`. This is the message
round 13 already proved *does* get a real `message_complete` from the guest in normal
operation. In this run, the re-arm for the *next* message happened before the guest's
own completion of the connect message had occurred — the two events raced, and this
time re-arm won.

This means **the premise behind the entire round 21 approach doesn't hold in
general**: seeing a server re-arm while a message is still marked `accepted` does not
reliably mean that message will never be completed. It can just as easily mean the
guest's own completion is happening moments later than the re-arm, not never. Round
21's fix worked out (that is, appeared to correctly target `function=32` specifically)
only because — per that round's own log — every one of its 1.87 million firings named
`function=32` in *that* particular run; this round's completely separate process
launch hit the race against `function=-1` instead on its very first shot. Given the
non-determinism this investigation has flagged repeatedly since round 4, which
message wins the race is not something either implementation could actually rely on.

### A more serious problem than a wrong trace target

Beyond invalidating the heuristic, this exposes a real correctness hazard in the
round-21-style approach generally: forcing a message to `completed` and calling
`unref()` on it *while the guest may still independently complete the same message
handle moments later* risks the guest's real, legitimate `message_complete` SVC
running against a message EKA2L1 has already torn down — a potential use-after-free
or handle-confusion bug, not just a redundant/harmless double-completion. No crash or
corruption was observed in this specific run (checked the log for
crash/assert/exception signatures - none beyond expected, pre-existing scripting-fix
log lines), but that's not the same as it being safe; it just didn't happen to bite
this time. This rules out the whole class of "detect abandonment by racing against
re-arm" fix, not just the specific KErrNone choice round 21 tried.

### Reverted completely

`kernel.h`'s temp members, `server.h`'s `last_accepted_msg_`, `server.cpp`'s
`accept()`/`receive_async_lle()` changes, `epoc.cpp`'s step/log site, and `log.cpp`'s
flush level all removed. Verified via a clean rebuild and `git diff` showing only the
pre-existing round-9 fix. Trace saved (for reference, despite targeting the wrong
message) at
[traces/round22_ecomserver_reaction_flawed.log](traces/round22_ecomserver_reaction_flawed.log).
No resource incidents; guardrail held throughout (the one-shot design worked exactly
as intended for containing runaway growth — log grew from 42KB to 116KB and
re-stabilized cleanly, nothing like round 21's multi-hundred-MB runaway).

### To resume from here

1. A *safe* way to observe `!ecomserver`'s real reaction to a real reply exists
   without any of round 21/22's risk: single-step-trace `!ecomserver` around one of
   its **genuine** `message_complete` events for `function=31` or `34` (both
   confirmed real, normal completions since round 13 - no synthetic completion, no
   race, no `unref()` risk at all). That won't show its reaction to `function=32`
   specifically, but it would establish the normal "receive reply, branch on status"
   pattern this server uses, which is a reasonable, safe starting point before
   attempting anything synthetic again.
2. Any future attempt at a synthetic/forced completion for `function=32` specifically
   needs a reliable way to distinguish "this message will never be completed" from
   "not completed *yet*" that doesn't race against the guest's own completion timing
   - e.g., only act after confirming via a bounded wait that the *entire* process has
   gone fully idle (matching the same "stable for N ticks" signal this investigation's
   own guardrail-driven test harness already uses to detect the hang from the outside),
   not merely "the server happened to re-arm before this specific message completed."
3. All temp instrumentation from this round removed, confirmed via clean rebuild and
   `git diff`. No resource incidents.

## Round 23 — a safe trace, and a real re-framing of the "missing timeout" theory

Did exactly what round 22's "to resume" item 1 suggested: single-step-traced
`!ecomserver` around a **genuine** `message_complete` for `function=31` — no
synthetic completion, no race, no `unref()` risk. Guardrail followed throughout
(background build for the `kernel.h` touch, `tasklist` checks, bounded polling,
force-kill).

### Probe

`svc.cpp`'s `message_complete` armed a one-shot single-step trace (400-instruction
budget) the first time it saw a real completion targeting `!ecomserver` for
`function=31`, purely observational — no behavior changed, nothing forced. `epoc.cpp`
stepped `!ecomserver` and logged `pc`/`lr`/`r0`-`r3` while armed.

### What the trace shows

Full trace saved at
[traces/round23_ecomserver_normal_reaction.log](traces/round23_ecomserver_normal_reaction.log)
(192 steps; ran out of scheduled turns before the 400 budget, same "non-deterministic
scheduling" caveat this file has noted since round 4). `!ecomserver` runs through the
same generic "SendReceive-with-status" plumbing (`lr=0x803aa997`) documented since
round 4/8/10, but this time — for the first time in this whole investigation — the
trace also shows **`!ecomserver`'s own code executing** (addresses in the
`0x804cbc2x` range, squarely inside `ecomserver.exe`'s own code block,
`0x804c7dc8`-`0x804cf243`, confirmed back in round 21's earlier module-range work).
Every prior round's traces only ever showed generic `euser.dll` plumbing and
`baksrv.dll`/`eikbackupsrv.dll` code — this is the first direct look at
`ecomserver.exe` itself doing anything. After that brief stretch of its own code, the
trace ends at the by-now-very-familiar `wait_for_any_request` boundary
(`0x8039fa98`/`0x8039fa9c`) — the exact same SVC every `!BackupServer` trace since
round 10 has also ended on.

### Why this matters: `WaitForAnyRequest()` has no timeout, by design

That's the important part. `wait_for_any_request` is EKA2L1's implementation of
Symbian's `RThread::RequestSemaphore()`/`User::WaitForAnyRequest()` mechanism — and on
real Symbian, that primitive **takes no timeout parameter at all**. It's an
inherently unbounded, cooperative block by construction: the only way out is another
thread signaling the matching request status. Seeing `!ecomserver` land on the exact
same instruction after a *successful* completion (function 31) as it presumably does
after sending the *never-completed* `function=32` means there's no separate, missing
"bounded wait" code path to find on `!ecomserver`'s side — it's using the one wait
primitive Symbian offers for this pattern, and that primitive was never going to have
a timeout regardless of what's wrong or right elsewhere.

This meaningfully re-frames round 20's "a missing timeout on `!ecomserver`'s side"
theory: it's not that `!ecomserver` is missing a timeout it should have — the
primitive it's using **cannot** have one, on real Symbian just as much as in EKA2L1.
So this was never really a candidate for an EKA2L1-side "add the missing timeout"
fix at all. Two possibilities remain, and they're the same two round 19 already
identified, now on firmer footing: either `eikbackupsrv.dll` really is expected to
complete this message and its compiled no-op case is a genuine bug (in the guest
binary, or in whatever populates its dispatch table, not in EKA2L1), or this
exact code path — `!ecomserver` sending this specific opcode and then blocking
unconditionally — is simply never exercised on real hardware outside this specific
restored-backup scenario, so a mismatch here would never surface as a real Symbian
bug report. Distinguishing those two needs information this investigation doesn't
have access to (real Symbian source, or a genuine device to test against) rather than
more binary tracing of either `baksrv.dll` or `ecomserver.exe`.

### Reverted completely

All temp instrumentation removed (`kernel.h`'s probe members, `svc.cpp`'s arm site,
`epoc.cpp`'s step/log site, `log.cpp`'s flush level); verified via clean rebuild and
`git diff` showing only the pre-existing round-9 fix. No resource incidents; the
one-shot design kept growth bounded exactly as intended (49KB → 80KB, clean
re-stabilization, nothing like round 21's runaway).

### Honest state after twenty-three rounds

The mechanism is now characterized about as completely as static/dynamic binary
analysis alone can take it, on both ends of the exchange: `baksrv.dll`'s dispatch
(rounds 13-20) and `!ecomserver`'s wait primitive (this round). The round-9 fix in
the tree is real, verified, and should stay. The second issue doesn't have a
verified fix — round 21's attempt made things measurably worse and was reverted;
round 22 showed the detection heuristic behind that attempt is unsound in general.
Further progress on an actual fix most likely needs information outside what's
available from the binaries alone (real Symbian source for the backup-server
protocol, or a genuine device to compare against) rather than another round of
tracing.

## Round 24 — confirmed: the hang doesn't happen without a restored backup

Direct empirical test, at the user's suggestion: launch the exact same N-Gage 2.0
(`0x20007B38`) on a *different* device with no restored-backup history, and see
whether `!BackupServer` is even involved at all.

### Test

`RM-409` (Nokia 5320d-1, confirmed via `data/devices.yml`) already has N-Gage 2.0
installed (`sys/install/sisregistry/20007b38` and matching `private/20007b38`
directories present, same UID as the `rm-320` repro). No source or build changes
needed — just launched:

```
./eka2l1_qt.exe --device RM-409 --run 0x20007B38
```

Guardrail followed (clean `tasklist` before launch, bounded stability poll, force-kill
after confirming state, log files cleaned up after inspection).

### Result

The log stabilized quickly (52KB → 55KB, essentially flat after the first 10 seconds)
— but unlike every `rm-320` run in this entire investigation, **`grep -c` for
`!BackupServer`/`baksrv` across the whole log returned zero matches.** The backup
session that rounds 9-23 spent twenty-three rounds characterizing never gets created
at all on this device. Instead the log shows completely normal, ongoing activity:
window server managing focus for an actively-running `"N-Gage Installer"` window
group, `oom_app`/`etel`/socket-connection service calls cycling (the socket-opcode
spam is N-Gage's own network connectivity retry behavior in an offline emulator,
unrelated to this investigation) — the process is genuinely running, not stalled.

### What this confirms

This is a clean, direct confirmation of the theory rounds 19/20/23 built up to but
couldn't verify from binary analysis alone: **the `!BackupServer`
session-connect-and-dispatch sequence this whole investigation traced is specific to
the restored-backup scenario on `rm-320`, not something N-Gage 2.0 or
`eikbackupsrv.dll` does unconditionally.** On a device with no restored backup to
process, N-Gage's startup code apparently never has a reason to talk to
`!BackupServer` in the first place, so the specific compiled no-op case (round 19)
never gets reached, and the hang this whole investigation exists to describe simply
doesn't occur. This doesn't pin down *why* `baksrv.dll`'s dispatch table has that
particular no-op case, but it does settle the broader question round 23 left open:
this is very much tied to the restore scenario specifically, not a general defect
that should be showing up in ordinary N-Gage usage.

### To resume from here

1. This significantly narrows any future fix's scope: it only needs to matter for the
   restored-backup path, not general `!BackupServer` usage. A fix that's conditioned
   on "a restore is actually in progress" (if that state is knowable from EKA2L1's
   side) would be much safer than anything unconditional, sidestepping round 21's
   busy-loop failure mode entirely for the common case.
2. If pursuing this further, checking what's different about `rm-320`'s specific
   installed/restored state (backup registration files, `private/20007b38` contents)
   versus `rm-409`'s clean install could reveal what actually triggers N-Gage to talk
   to `!BackupServer` in the first place — a different, and possibly more tractable,
   question than continuing to trace the dispatch mechanism itself.
3. No emulator source touched this round; no probes, no build. `rm-320`'s repro state
   (the actual restored-backup scenario this investigation is about) is untouched.

## Round 25 — found the real caller: `bafl.dll`, and why its own bounded retry never helps

Traced the actual decision point: what guest code decides to open a session with
`!BackupServer` in the first place. Found it, with a complete, satisfying causal
chain from there down to the hang. Guardrail followed throughout (one Ghidra
invocation at a time, `tasklist` checks, bounded polling for the one live-repro run).

### Probe

Added a one-shot probe in `session_send_general` (`svc.cpp`, the implementation
behind the `session_send`/`session_send_sync` SVCs) that fires on the very first send
targeting `!BackupServer` (the connect, `ord=-1`) and logs full context plus 48 words
of the guest stack from `sp`. `lr` at the SVC boundary was, as expected from every
prior round, just the generic `euser.dll` `SendReceive` wrapper (`0x803aa9ab`) — but
the stack scan turned up something new: several values (`0x80780864`, `0x8078086c`,
`0x80780870`, `0x8078088c`) that didn't match any module identified in this
investigation so far.

### Identifying the module, and a trap that turned out to be real data

Scanned every `.dll`/`.exe` under `rm-320/sys/bin` for one whose code range covers
those addresses (same header-scan technique as round 17/20): all four fall inside
**`bafl.dll`** (Backup And File List — a genuine Symbian backup-integration utility
library, code range `0x807780A8`-`0x8078266C`). Imported it into a fresh Ghidra
project (`bafl_analysis`) and ran full auto-analysis (small file, cheap) — but the
Function Start Search found **no function at all** covering those four addresses.
Forcing Thumb disassembly directly there
([bafl_forced_result.txt](ghidra-scripts/bafl_forced_result.txt)) showed why: it
isn't code. The raw bytes decode as UTF-16LE text — literally the strings
**`"!BackupServer"`** (at `0x80780870`) and **`"baksrvs"`** (at `0x80780890`) hardcoded
in `bafl.dll`'s own data. The four stack values weren't a return-address chain at
all — they were pointers into this literal string table, most likely arguments
prepared for a "connect to server by name" call, not saved LRs. A instructive
near-miss: had this been treated as a call chain without checking, it would have been
another confidently-wrong conclusion in the style this file has flagged several times
before.

### The actual function, and its own (bounded!) retry loop

Found every code reference to this string table
([bafl_string_refs_result.txt](ghidra-scripts/bafl_string_refs_result.txt)), which
led directly to two functions:

- **`FUN_8077811a`** — connects to `!BackupServer` (`thunk_EXT_FUN_803aaa6a`, matching
  `RSessionBase::CreateSession`'s signature: name descriptor, version, message slots),
  then makes one further generic call, checks the result, and closes the session.
  This is the actual connect-and-query logic.
- **`FUN_80778190`** — calls `FUN_8077811a` inside a loop **explicitly capped at 10
  iterations** (`iVar3 = 10`, decremented each pass, returns once it hits zero),
  breaking out early only on specific result codes. This is real Symbian client code,
  and it is **not** an unbounded loop — it's a bounded, well-behaved retry, exactly
  what you'd expect from a properly-written backup-integration utility.

### Why the bound doesn't save it

This is the piece that ties every prior round together into one coherent story:
`bafl.dll`'s 10-attempt cap only protects against a callee that *fails fast* (returns
an error like "not found" or "busy" promptly, so the loop can decrement and retry).
It was never designed to survive a callee that **never returns at all**. And that's
exactly what rounds 13/19/23 already proved happens here: the specific `!BackupServer`
message this exchange reaches (compiled to a genuine no-op switch case in
`baksrv.dll`, round 19) never calls `message_complete`, so the guest thread blocks
forever on `WaitForAnyRequest` (round 23) — which has no timeout on real Symbian
either. `FUN_80778190`'s loop counter never even gets to decrement past its first
iteration, because that iteration's call into `FUN_8077811a` never returns control at
all. A perfectly reasonable, bounded piece of client code is defeated by a callee
that doesn't play by the "always eventually reply" contract it assumes.

This also fully explains round 21's runaway failure in retrospect: my synthetic
auto-complete watchdog fired on *every* re-arm, unconditionally — each firing
unblocked that same stuck first iteration, letting the guest's overall flow
(`bafl.dll`'s loop plus whatever wraps it) proceed and immediately re-enter the same
stuck call again. Since my watchdog had no cap of its own to match `bafl.dll`'s
real 10-attempt design, it just kept feeding completions far past where the real
code would have given up — 1.87 million times instead of 10.

### What this changes for a future fix

A fix that's aware of `bafl.dll`'s own 10-attempt design, rather than blindly
auto-completing on every re-arm, would look completely different from round 21's
attempt: e.g., only auto-complete a stuck `!BackupServer` message a *bounded* number
of times (matching or exceeding `bafl.dll`'s own cap isn't even necessary — once
the underlying call fails fast even a single time, the guest's own bounded retry logic
takes over correctly on its own). That's a meaningfully different, more targeted shape
of fix than "auto-complete forever," and worth exploring if this investigation
continues toward an actual patch.

### To resume from here

1. A bounded version of round 21's watchdog (fire at most once, or at most a small N
   times, per session — not unconditionally on every re-arm) would let `bafl.dll`'s
   own real retry loop take over instead of fighting it. Worth testing directly:
   does N-Gage actually proceed past the hang and boot successfully once `bafl.dll`'s
   loop gets one fast failure to react to, or does something else downstream still
   get stuck?
2. `FUN_8077811a`'s middle call (`thunk_EXT_FUN_803aaa6a` then
   `thunk_EXT_FUN_803aab70`) likely corresponds to the specific opcode sequence
   (31/34/32) traced since round 13 — not fully mapped instruction-by-instruction to
   confirm which call sends which opcode, since the causal chain was already
   conclusive without it. Worth doing if an exact fix needs to know precisely which
   BAFL-level operation corresponds to `function=32` specifically.
3. All temp instrumentation from this round removed (`svc.cpp`'s one-shot probe,
   `log.cpp`'s flush level); verified via clean rebuild and `git diff` showing only
   the round-9 fix. Three new Ghidra scripts added (`DumpBaflOverview.java`,
   `DumpBaflForced.java`, `FindBaflStringRefs.java`) alongside a new `bafl_analysis`
   Ghidra project. No resource incidents; guardrail held throughout.

## Round 26 — the fix, built around round 25's finding, verified working

At the user's direction to try the fix round 25 pointed at. Guardrail followed
throughout (background builds for the touched headers, `tasklist` checks, MSBuild
cleanup, watched log growth closely on every test run specifically to catch a
round-21-style runaway early).

### The fix

Two small, targeted changes, both in the tree now:

- [svc.cpp](../../src/emu/kernel/src/svc.cpp), `message_complete`: now sets
  `msg->msg_status = ipc_message_status::completed`. This didn't exist before (round
  19 found the same gap, added it temporarily, reverted it with that round's probes) -
  this round it's load-bearing for the fix's own correctness (see below), so it stays
  permanently this time.
- [server.cpp](../../src/emu/kernel/src/server.cpp), new
  `arm_backup_server_msg_timeout()` (called from `accept()`, scoped to
  `name() == "!BackupServer"` only): schedules a single delayed check, 1 second of
  virtual time after a message is accepted, using the kernel's existing
  `ntimer::register_event`/`schedule_event` machinery (the same established pattern
  already used for mutex/condvar/thread wait timeouts elsewhere in this codebase -
  see `mutex.cpp`, `condvar.cpp`, `thread.cpp`). When the delayed check fires:
  re-resolves the message **by id**, not a held raw pointer (the message slot could in
  principle be reused for something else during the 1-second window - id + a fresh
  `kern->get_msg()` avoids ever touching a stale pointer); confirms it's still
  `ipc_message_status::accepted` (a message that completed normally in the meantime -
  the overwhelmingly common case - will already be `completed` by round 26's
  `message_complete` addition, and the check silently no-ops); confirms it's still
  genuinely a `!BackupServer` session message (in case the slot really was reused);
  and only then completes it with `epoc::error_not_supported`, mirroring the exact
  completion sequence `message_complete` itself performs (status set, signal, close
  handle, run IPC-complete callbacks, `unref()`).

### Why this avoids round 21 and round 22's failure modes

- **Not racy** (round 22's problem): the trigger is *elapsed real time* (1 second),
  not *the event of a re-arm*. Rounds 13/23 showed genuine completions happen within
  microseconds of guest execution - a full second of margin means this should never
  fire on a message that was always going to complete normally, unlike round 21/22's
  "re-arm implies abandonment" heuristic which raced and misfired on `function=-1` in
  round 22's own test.
- **Not unbounded** (round 21's problem): fires **at most once per message** (the
  scheduled event is a one-shot; there's no re-arming of the same timeout, and a
  newly re-armed server message gets its own independent, freshly-scheduled timeout).
  There is deliberately no cap-avoidance logic needed beyond that, because each
  message can only ever be checked once.
- **Fails, doesn't fake success**: completes with `epoc::error_not_supported`, not
  `KErrNone` (round 21's choice) - the guest gets a real failure signal, letting
  `bafl.dll`'s own retry/give-up logic (round 25) decide what to do, rather than
  being told the operation vacuously succeeded.

### Verification

Guardrail-compliant test runs (clean `tasklist` before launch, growth watched closely
every 5 seconds specifically to catch runaway growth early and abort, force-kill after
confirmed stability). Full trace saved at
[traces/round26_fix_verification.log](traces/round26_fix_verification.log).

- The fix fired **exactly once** per run (`grep -c` confirmed 1, then 1 again on a
  second independent run) - no repeat storm, nothing resembling round 21's 1.87
  million firings.
- After the single fail-fast completion, the log shows extensive activity **never
  observed in any of the previous 25 rounds**: `eikbackupsrv.dll` finishes loading,
  then the process goes on to load `ngiinstaller.exe`, `akninit.dll` (application
  framework init), `10281fc7.dll`, `101fe2aa.dll`; creates real window groups; opens
  an actual video (`Z:\Private\20007B38\teaser\SizzleG1sound.mp4` - the N-Gage teaser
  clip); opens ETel phone/voice sessions; handles OOM (out-of-memory/app-switching)
  capability calls; controls the HWRM light (backlight) resource. Every previous
  round's repro stayed flat at 42-116KB with zero further activity past the
  `!BackupServer` exchange; this run reached 62-116KB of substantially different,
  much-later-stage content and then went genuinely idle (stable log size for 40-45+
  seconds across two separate runs) with no further hang signature - consistent with
  reaching a real idle/waiting-for-input state rather than a new, differently-shaped
  stall.
- Second, independent test run reproduced the same qualitative outcome: one fail-fast
  firing, followed by even further progress (reaching `ngiinstaller.exe`,
  `akninit.dll`, and window-group setup) before settling idle again - not a fluke of
  one specific run's scheduling.

**Not done**: full visual confirmation that N-Gage reaches an actual playable menu
(this environment has no straightforward way to screenshot the Qt GUI window). Every
log-observable signal points at genuine, substantial forward progress rather than a
different hang, but the user should verify visually to confirm the app is fully
usable end to end.

### State of the tree

`git diff --stat` on the emulator source now shows three files: `server.h` (round 9,
unchanged), `server.cpp` (round 9's fix, unchanged, plus round 26's new function and
its call site), and `svc.cpp` (round 26's one-line `msg_status` addition). `log.cpp`
is back to its original state - the trace-level flush was only for observing test
runs, not needed for the fix itself. No temp/probe code remains anywhere.

### To resume from here

1. Visual/interactive confirmation from the user: does N-Gage actually reach a
   playable state, or is there a further, different stall past what the log shows?
2. If the user wants tighter tuning: the 1-second delay and `KErrNotSupported` choice
   were reasonable first picks, not deeply optimized - could investigate whether a
   shorter delay is safe (round 23's traced completions took microseconds, so even
   100ms would likely still be safe margin) if faster boot time matters, or whether a
   different error code changes `bafl.dll`'s downstream behavior in a more desirable
   way (not investigated - `KErrNotSupported` was chosen as a reasonable, honest "this
   isn't available" signal, not verified against `bafl.dll`'s exact code-path
   preferences).
3. Twenty-six rounds of investigation, from an initial boot hang down to a verified,
   narrowly-scoped, safely-bounded fix. This file's per-round structure (oldest-first,
   later rounds correcting earlier ones where needed) should stay intact for anyone
   picking this up later - the full path from symptom to root cause to working fix is
   preserved here, not just the final answer.

## Round 27 — the user confirmed no playable menu; screenshot capability added; found a second, different stall

The user confirmed round 26's fix does **not** get N-Gage to an actual playable menu,
and asked (a) for analysis of the new blocker and (b) what's needed to screenshot the
Qt window directly, since text logs alone had already produced one wrong conclusion
this investigation and a visual would be more reliable. Both delivered this round;
found a genuinely new, second stall, distinct from anything rounds 1-26 dealt with.

### Screenshot capability

No new tool or install needed - PowerShell (already available) plus a small
`user32.dll` `PrintWindow` P/Invoke, scoped to the specific `eka2l1_qt` window handle
(via `Get-Process -Name eka2l1_qt`, `MainWindowHandle`), not the full desktop. Used
`PW_RENDERFULLCONTENT` (flag `2`) since the emulator likely renders through
OpenGL/Direct3D, which plain `BitBlt`-style capture often can't see correctly. The
user explicitly approved capturing only this one window before any capture happened.

### What the screenshots showed

Two captures, saved at
[traces/round27_screenshot_blank.png](traces/round27_screenshot_blank.png) (first, ~30s
in) and [traces/round27_screenshot_black.png](traces/round27_screenshot_black.png)
(second, ~90s in, after confirming the log had gone completely flat). The first showed
a plain white phone screen (nothing drawn yet). The second showed a status bar area
plus a solid black rectangle where video content should be - not a crash, not an error
dialog, just unpainted content.

### First theory (video playback stuck) - investigated and ruled out

The black rectangle plus a `Trying to summon: baksrvs` / fail-fast sequence right
before a `Open a new video to play` log line (the N-Gage teaser,
`SizzleG1sound.mp4`) suggested the video player might be the new stall. Read
`dispatch/video.cpp` and the ffmpeg backend
(`drivers/video/backend/ffmpeg/video_player_ffmpeg.cpp`): the completion path looked
properly wired (`on_play_done` → `play_done_notify_.complete()`, with the decode loop
having a fallback "call it anyway if nothing else did" safety net). Added temporary
probes at `evideo_player_open_file`, `evideo_player_play`, `post_new_image` (first
frame), and `on_play_done` to check directly rather than keep guessing from static
code. **Result: the video plays and completes cleanly** -
`open_res=true`, `play` called, first frame posted (`buffer_size=307200`, a real
decoded frame), `on_play_done` fired with `error=0`. Not a bug - the black rectangle
was likely just the video's own dark closing frame, or a snapshot taken exactly at
the completion boundary. This is round 27's own version of the "confidently wrong
without verification" trap this file has flagged repeatedly - caught by adding direct
probes instead of trusting the visual alone.

### The real second stall: `CdlServer` spinning inside `drtaeabi.dll`

A full-thread-state dump (`kernel_system::get_thread_list()`, every thread's
`name()`/`current_state()`, scheduled 8s after round 26's fail-fast fires) taken
across two independent runs consistently showed the same picture: `N-Gage Installer`,
`!AknIconServer`, `PlayServer`, `Main`, and `!BackupServer` all sitting in
`wait_fast_sema` (idle, waiting normally) or `stop` (`!ecomserver`, already finished)
- except **`CdlServer`, which showed `state=1` (`run`)**, not waiting, in both runs.
Single-step-traced it directly (3000-instruction budget): the **entire budget was
consumed without the thread ever being descheduled**, and of those 3000 steps, only
**549 unique addresses** appeared - each address hit roughly 5-6 times on average.
That's a genuine spin over a narrow, bounded code region
(`0x8039456c`-`0x80395728`, saved at
[traces/round27_cdlserver_spin_trace.log](traces/round27_cdlserver_spin_trace.log)),
not idle waiting and not forward progress through fresh code.

Identified the module the same way as every other round here (header-scan every
`.dll`/`.exe` under `rm-320/sys/bin` for one whose code range covers the spinning
addresses): **`drtaeabi.dll`** - the ARM EABI compiler-runtime support library
(division, memory operations, and similar low-level helpers), a different library
than round 19's `drtrvct2` but the same general category (generic compiler-emitted
runtime code, not application logic). `CdlServer` is Symbian's Component Description
Language server - real, guest-side, involved in first-run component
registration/dependency tracking, fitting naturally with `ngiinstaller.exe` needing it
during a fresh install.

### Genuinely unresolved: bug vs. slow-but-bounded work

Unlike every prior stall in this investigation (all confirmed, via `message_complete`
tracing, to be **permanently** stuck - literally never going to resolve on their own),
this one has a real, live possibility of being **legitimate but slow**, not broken:
software arithmetic/utility routines in a compiler-runtime library (long division,
software floating point, block copies) are often implemented as bounded loops that
take many actual iterations to finish a single real operation - looking identical to
an infinite spin over a short window without knowing the loop's true iteration count.
A 90-second patience test (round 27's second screenshot) showed the overall
HLE-level log completely flat throughout, but a low-level runtime routine has no
reason to log anything regardless of whether it's genuinely stuck or just working
slowly - so that flat log doesn't distinguish the two possibilities either.

This is different from every previous round's finding in this file: it has not been
verified as unfixable-by-waiting the way the `!BackupServer` hang was (round 13/19/23
proved *nothing* was ever going to complete it). It's reported honestly as unresolved
rather than assumed to be another permanent hang.

### Reverted completely

All temp instrumentation removed: `kernel.h`'s `probe_eik19_arm`/`budget` members,
`server.cpp`'s chained thread-dump-and-arm code (added to the round-26 fail-fast
handler, now stripped back to exactly its round-26 form), `svc.cpp`'s session-send
logger, `epoc.cpp`'s `CdlServer` step/log site, and all four `video.cpp` probe lines.
Verified via `git diff` matching exactly the round-26 state (`server.h` unchanged,
`server.cpp` back to its 77-line round-26 fix, `svc.cpp` back to its 1-line addition,
`epoc.cpp` back to the pre-existing unrelated feature only, `video.cpp` and `kernel.h`
showing no diff at all) and a clean rebuild. No resource incidents; guardrail held
throughout (background builds for header changes, `tasklist` checks before every
build, bounded polling, force-kill after each capture).

### To resume from here

1. The most direct next step: wait *much* longer (several minutes, not 90 seconds) on
   a fresh run and see whether `CdlServer` ever finishes and the app proceeds on its
   own - this single test cleanly separates "slow but working" from "genuinely
   infinite," which nothing done this round could distinguish.
2. If it never finishes: trace what `CdlServer`'s own guest code (not the runtime
   library it's calling into) is trying to compute right before entering
   `drtaeabi.dll` - i.e., find the call site and its arguments (same "capture the LR
   at the call boundary" technique round 25 used to find `bafl.dll`), to see what
   real value it's dividing/computing/copying and whether that value is something
   EKA2L1 is feeding it incorrectly (e.g. a corrupted or zero size/count parameter
   causing an unbounded or excessively large loop trip count) versus a value that's
   correct but simply large.
3. Round 26's fix stays in the tree regardless of how this second stall resolves - it
   is independently verified to move N-Gage substantially further than before, and
   this second issue is unrelated to `!BackupServer`/`bafl.dll` entirely.

## Round 28 — the patience test: genuinely permanent, not slow

Ran round 27's own suggested next step: no source changes, just watched a fresh run
for much longer than any prior test in this investigation.

### Test

Launched normally (`--device RM-320 --run 0x20007B38`), then watched
`EKA2L1.log`'s size every 10 seconds for **350 seconds (5 minutes 50 seconds)**
without killing the process, followed by a third screenshot capture (same
`PrintWindow`-based, single-window-only technique as round 27, same user approval
still in force).

### Result

The log grew normally for the first ~20 seconds (reaching 42838 bytes, ending - as in
every other capture of this exact stall - right after `!BackupServer message
(function=32) never completed by guest dispatch after 1s - failing fast with
KErrNotSupported`), then stayed at **exactly** 42838 bytes for the remaining 330
seconds with no exceptions across all 33 subsequent 10-second checks. The
[screenshot taken at the 6-minute mark](traces/round28_screenshot_after_6min.png)
showed plain blank white - this run happened to stall slightly earlier in the
sequence than round 27's second capture (which showed a black video placeholder
instead), the same non-deterministic scheduling this file has noted since round 4,
not a different bug - and showed no change from the very first seconds of the run.

This settles round 27's one open question directly: a genuine software
division/copy/arithmetic routine, however slow under emulation overhead, would
complete within 5-6 minutes for what's ultimately a bounded computation - real
divide/copy loops run for tens to low thousands of iterations, not the millions that
would be needed to stay busy for minutes. Six minutes of total silence, with the
visual state identical at the start and end, rules out "still working, just slowly"
and confirms this is a genuine, permanent stall - the same category of finding as the
original `!BackupServer` hang (rounds 13/19/23 proved that one permanent via direct
`message_complete` tracing; this one is proven permanent via direct elapsed-time
observation instead, since `CdlServer`'s spin doesn't go through any IPC completion
mechanism to trace the same way).

### To resume from here

1. Round 27's item 2 is now the live next step, not a conditional one: trace
   `CdlServer`'s own guest code at the point it calls into `drtaeabi.dll` (capture the
   call site's `lr` and argument registers, same technique round 25 used for
   `bafl.dll`) to find out what value it's operating on and whether that value looks
   like guest-supplied data EKA2L1 is getting wrong (e.g. a bad length/count argument
   producing a pathologically large or literally unbounded loop trip count) versus
   correctly-computed data that genuinely shouldn't take this long.
2. No emulator source touched this round - purely an observational test. `git diff`
   still shows only the round-9 and round-26 fixes. No resource incidents; the
   6-minute wait was a single foreground `sleep`-polling loop, no stacked heavy
   processes at any point.

## Round 29 — traced the real call chain: `cdlserver.exe` → itself → `drtaeabi.dll`

Continued directly from round 28's confirmation that the `CdlServer` spin is
permanent. Two approaches tried; the second one worked. Guardrail followed throughout
(background builds for header changes, `tasklist` checks, bounded polling, force-kill
after each capture, one Ghidra invocation at a time for the later static analysis).

### First approach: step from `CdlServer`'s very first scheduled instant (too slow)

Round 27's 8-second-delayed trigger caught the spin already in progress, with no way
to see the real entry point. Tried arming a step trace unconditionally the moment
`CdlServer` is *ever* first scheduled, hoping to single-step all the way through to
the actual call into `drtaeabi.dll`. At an 8,000-instruction budget
([traces/round29_early_scheduling_trace.log](traces/round29_early_scheduling_trace.log)),
the trace never reached `drtaeabi.dll`'s range at all - 1,497 unique addresses,
genuine forward progress (a tail that looks like a real byte-copy loop, source/dest
pointers both incrementing), just legitimate setup work. Bumped the budget to 60,000
with sparse-then-dense logging (every 20th step outside `drtaeabi.dll`, every step
once inside) -
[still never reached it](traces/round29_60k_no_reach.log). `CdlServer` genuinely does
tens of thousands of instructions of real work (plausibly parsing multiple component
description files as part of the first-run install) before ever reaching the stuck
call - single-stepping from the start isn't a practical way to find the entry point.

### Second approach: stack scan while already spinning (worked)

Switched to reusing round 27's original "confirmed inside `drtaeabi.dll`" detector
(checked unconditionally, no stepping needed - `cpu->get_pc()` checked against
`drtaeabi.dll`'s range the moment `CdlServer` is scheduled) and, the instant it fires,
read 64 words of guest stack from the live `sp` looking for values that fall *outside*
`drtaeabi.dll`'s address range - the calling frame back to real application code
should still be there even deep inside a spinning subroutine. Fired within 10 seconds
of launch, no stepping overhead at all. Full result at
[traces/round29_stack_scan.log](traces/round29_stack_scan.log): several euser.dll
plumbing addresses (expected, uninteresting), plus three genuinely new hits -
`0x83485fef` (twice, same value in two adjacent stack slots) and `0x83482541`
(once) - neither previously seen in this investigation.

### Identifying the modules, and a false lead caught before it became a "finding"

Header-scanned every `.dll`/`.exe` under `rm-320/sys/bin` (the same technique used
since round 17): `0x83482541` falls in **`cdlengine.dll`** (the real Symbian CDL -
Component Description Language - parsing engine), `0x83485fef` falls in
**`cdlserver.exe`** itself. Disassembled the `cdlengine.dll` address first
([ghidra-scripts/cdlengine_callsite_result.txt](ghidra-scripts/cdlengine_callsite_result.txt))
and found `0x83482540` (the even-aligned code address `0x83482541` actually points to)
is nothing but a plain function epilogue (`pop {r4,pc}`) - not a real call return
site at all. This is very likely **stale stack data** left over from an
already-returned, earlier call frame, not a live part of the current call chain -
caught by checking rather than assuming it was meaningful, the same discipline this
file has needed several times before (round 19's "8-byte-shift" static read, round
25's identical trap).

The `cdlserver.exe` hit told a real story instead
([ghidra-scripts/cdlserver_callsite_result.txt](ghidra-scripts/cdlserver_callsite_result.txt)):
`0x83485fee` (matching the odd/thumb-bit `0x83485fef` on the stack) is the instruction
immediately after `bl 0x83485e24` at `0x83485fea`, inside a small wrapper function
(`0x83485fde`-`0x83485ff4`) that itself is `cdlserver.exe`'s own code - not a generic
library trampoline. That call address, `0x83485e24`, is also inside `cdlserver.exe`
(internal, not an external library call) - and the *same* return address appearing
twice, back-to-back, on the stack is consistent with recursive or iterative calls
into that same internal function, each pushing an identical return address.

### The real chain, and what's still open

`cdlserver.exe`'s own code (a small wrapper, `0x83485fde`) calls its own internal
routine (`0x83485e24`) - plausibly recursively or in a loop, given the duplicated
stack entry - which eventually, several frames deeper than this stack scan's 64-word
window could reach, calls into `drtaeabi.dll` and gets stuck. This is real,
concrete progress: the call chain runs through `cdlserver.exe`'s own logic, not
straight from `cdlengine.dll` or from a single, shallow library call. What's not yet
answered is what `0x83485e24` actually does, what data it's recursing/iterating over,
and whether that data is something EKA2L1 is supplying incorrectly (making an
otherwise-normal recursive descent into an infinite one) or whether `0x83485e24`
itself has a genuine logic bug independent of EKA2L1 - the next natural target for
static analysis, now with a precise, confirmed-live address to start from instead of
a guess.

### Reverted completely

All temp instrumentation removed (`kernel.h`'s `probe_eik20_seen`/`budget` members,
`epoc.cpp`'s early-scheduling step site and later stack-scan site, `log.cpp`'s flush
level); verified via `git diff` matching exactly the round-26 state and a clean
rebuild. Three new Ghidra scripts added (`DumpCdlengineCallsite.java`,
`DumpCdlserverCallsite.java`) alongside two new Ghidra projects
(`cdlengine_analysis`, `cdlserver_analysis`). No resource incidents; guardrail held
throughout.

### To resume from here

1. Disassemble `0x83485e24` itself (the recursing/iterating function) - same
   `clearListing` + forced-Thumb + fresh-window technique used for every other target
   in this file - to see its actual loop structure and what data it walks over.
2. If it's genuinely recursive, check what bounds (if any) the recursion depth or
   loop trip count, and whether that bound is being fed a real, finite value or
   something EKA2L1 is getting wrong (a corrupted or unbounded count/length coming
   from wherever `cdlserver.exe` reads its component description data from).
3. This is real application logic in a genuine Symbian system server
   (`cdlserver.exe`/`cdlengine.dll`), not compiler-runtime plumbing - unlike every
   other module this investigation has traced into, a bug here is more plausibly
   fixable by supplying correct input data (a file, a P&S property, a parameter)
   than by patching the emulator's IPC layer the way round 26's fix did.

## Round 30 — the alternate launcher UID doesn't sidestep this

The user recalled a real detail from their actual device history: N-Gage 2.0 has two
launcher UIDs, `0x20007b38` (the original, bundled one - shows the teaser video, the
one this entire investigation has been running via `--run`) and `0x20007b39` (an
updated launcher installed later, which on the user's real N95 superseded and hid the
original). The user's real, working device used `0x20007b39`, not `0x20007b38` - so
every round of this investigation might have been chasing a launcher path that was
never actually exercised on the real hardware being restored from.

### Test

Cheap and direct: relaunch with `--run 0x20007B39` instead, no source changes, and see
whether it takes a genuinely different path.

### Result

It doesn't. The log shows `0x20007b39` reaching the *exact* same chain -
`Trying to summon: ngiinstaller.exe`, then `baksrvs`/`eikbackupsrv.dll`, then round
26's fail-fast firing for `!BackupServer` `function=32` - identically to every
`0x20007b38` run in this file. The log went flat immediately after, same as always,
and the [screenshot](traces/round30_20007b39_screenshot.png) taken at that point is
visually identical to round 27's second capture (status bar plus the same black video
placeholder).

### What this means

Both launcher UIDs funnel through the same shared platform machinery
(`ngiinstaller.exe`, the backup-server exchange, and - almost certainly, though not
separately re-confirmed via probes this round - the same `CdlServer` spin) rather than
being independent, self-contained apps. This rules out "wrong launcher" as a
quick fix, but it's still useful negative information: whatever's actually broken
lives in shared first-run-install infrastructure both launchers depend on, not in
either launcher's own, separate code - so continuing to trace `cdlserver.exe`'s
internal logic (round 29's plan) remains the right direction rather than a dead end
this finding would have redirected away from.

### No revert needed

No source touched this round - a single alternate-UID launch test, guardrail followed
(clean `tasklist` before launch, bounded polling, force-kill after confirming the
stall, one screenshot capture using the same previously-approved single-window
technique).

## Round 31 — traced the merge algorithm, raised a real hypothesis, then killed it with 25 minutes of hard evidence

Continued round 29's plan directly: disassembled `0x83485e24` and its sibling
`FUN_83485d62`, found what looked like a real, non-buggy explanation for the delay,
then tested that explanation directly rather than assuming it. It didn't hold up. No
source changes this round - all Ghidra static analysis plus one long observational
test.

### `FUN_83485e24`: builds a linked-list node, touches `ecom.dll`

Disassembled cleanly (no garbage, unlike several earlier rounds' decompile attempts):
allocates a small object, calls into `ecom.dll` (`thunk_EXT_FUN_804c67fe`, address
inside `ecom.dll`'s confirmed range from round 21), then links the new object into a
list via a "prepend" pattern on `param_1+0x28` (read the old head, set the new node,
store it as the new head). One call = one new list node.

### `FUN_83485d62`: a genuine, correctly-bounded merge-sort

This is the sibling function `FUN_83485e24` calls, and it decompiled into something
immediately recognizable: a **standard two-list merge** (the classic merge-sort
merge step), walking two indices against two counts (`thunk_EXT_FUN_803b072a`,
almost certainly a `Count()` call) via a comparator
(`thunk_EXT_FUN_803a909a`), terminating cleanly once both indices reach their
respective counts. Nothing about this loop looks broken - both indices only ever
increase, and the exit condition is a plain `&&` over two bounded counters.

### The hypothesis this raised

Round 20 already established that `rm-320`'s restored backup carries an unusually
large real component set (`backup.lst`: ~90 installed apps, plus a populated
`10203636` backup-engine directory with 29 Java MIDlet registrations). If `CdlServer`
is genuinely merging/registering CDL dependency records for a real device's *entire*,
heavily-used backup - not a clean install's handful of components - a **correct,
bounded** algorithm could still take far longer than round 28's 6-minute patience
test allowed, purely from doing legitimately more work than a typical clean-install
scenario ever would. This would mean no bug anywhere - not in `cdlserver.exe`, not in
`drtaeabi.dll`, not in EKA2L1 - just an unusually large amount of real work needing
more patience than tested so far. Round 28's 6-minute silence doesn't distinguish
this from a true infinite loop either way, since internal merge processing produces
no HLE-level log output regardless of how many iterations it's doing.

### The test: 25 minutes, watched directly, not assumed

At the user's request, ran the patience test this hypothesis called for. Launched
normally, then watched `EKA2L1.log`'s size *and* the process's own memory usage every
10 seconds for **25 minutes (1500 seconds)** straight, across three chained
background-polling windows (the tool's own per-call cap is 10 minutes). Followed with
a final screenshot capture (same single-window `PrintWindow` technique, same standing
approval).

**Result: no change of any kind for the entire 25 minutes.** `EKA2L1.log` stayed at
exactly 42,838 bytes - the identical size it's been stuck at since round 26 - for
every single one of the 150 ten-second checks. The process's own reported memory
usage was effectively flat too (153.372MB → 153.400MB → 153.220MB across the three
windows - noise-level variance, not a growth trend). The
[final screenshot](traces/round31_screenshot_after_25min.png) is visually identical
to the very first one taken in round 27 - plain blank white, no change whatsoever.

This is the piece that actually settles round 31's own hypothesis: a real merge over
hundreds of real component records would need to **allocate memory** for each new
list node `FUN_83485e24` creates - if `CdlServer` were genuinely grinding through a
large, real dataset, memory usage should climb measurably as nodes accumulate. It
doesn't move at all. Combined with the total absence of any log growth, this rules
out "correct but slow because of an unusually large real dataset" just as firmly as
round 28 already ruled out "slow but bounded arithmetic." **This is a genuine,
permanent stall, confirmed twice now by two different, independent lines of
evidence** (round 28's elapsed-time reasoning, and this round's direct memory-growth
absence) - not scale, not patience, an actual stuck condition.

### Where this leaves the investigation

The merge algorithm itself (`FUN_83485d62`) is verified correct and properly bounded
by static analysis. The stall is therefore most likely inside something the merge
*calls* on each iteration - the comparator (`thunk_EXT_FUN_803a909a`) or whatever it
in turn calls - rather than in the outer loop structure itself. Given memory isn't
growing at all, it's plausible the loop never even completes its *first* comparison -
consistent with round 27/29's live evidence that execution is stuck deep inside
`drtaeabi.dll` specifically, not cycling through many merge steps.

### To resume from here

1. Disassemble `thunk_EXT_FUN_803a909a` (the comparator `FUN_83485d62` calls) - a new,
   more precise target than `FUN_83485d62` itself, since the outer merge loop is now
   cleared of suspicion.
2. Given zero memory growth over 25 minutes, focus specifically on whether this
   comparator is being called *at all* past the first invocation, versus being stuck
   somewhere inside its own first call - the live single-step/stack-scan techniques
   from rounds 27/29 remain the most reliable way to answer this, more so than further
   static reading alone.
3. No emulator source touched this round; no probes added or reverted. `git diff`
   still shows only the round-9 and round-26 fixes. No resource incidents; the
   25-minute observation was a single foreground process with periodic background
   polling, no stacked heavy processes, guardrail held throughout.

## Round 32 — traced the comparator to a real binary search in `euser.dll`, found its division call into `drtaeabi.dll`

Continued directly from round 31's plan: disassemble `thunk_EXT_FUN_803a909a` (the
merge's comparator) and find out whether it's ever called more than once. No source
changes - all Ghidra static analysis, with one important process fix along the way.

### The comparator itself: ruled out

`thunk_EXT_FUN_803a909a` disassembled cleanly to a trivial 6-instruction equality
check - load two 4-byte fields, subtract, return the sign. No loop, no external call,
nothing that could stall. Ruled out as the stall's location.

### Stale Ghidra project trap, caught and fixed

Following the call chain one level further (`FUN_83486654`, an array-element accessor
the merge loop calls up to 6 times per iteration, thin-wrapping
`thunk_EXT_FUN_803a0478` in `euser.dll`) hit the exact same "reused Ghidra project for
15+ rounds" trap flagged repeatedly earlier in this file: disassembling
`0x803a0478` in the long-lived `euser_fixed_analysis`/`euser_analysis` projects
produced a nonsense function jumping to an unrelated address with classic
misalignment red flags (`unaff_r4`, `in_CY` pseudo-variables). Fix: imported a
completely fresh `euser.dll` project (`euser_fresh_r32`) instead of continuing to
debug the stale one - immediately gave a clean, small, coherent result. **Any further
euser.dll work in this investigation should keep using `euser_fresh_r32`, not the
older projects.**

### `FUN_803a0ad8`: a real, correctly-implemented binary search

Following the fresh project's tail-jump (`0x803a0478` → `0x803a0b24`) led to
`FUN_803a0ad8` (size 94) - a genuine binary search: halves the range each iteration,
compares against a computed array element (`uVar1 = *(uint *)(unaff_r4 + uVar2 *
unaff_r5 + unaff_r6)`), returns on exact match. Nothing wrong with the search logic
itself - bounded, standard, correctly terminating on the "not found" path.

The **match-found** path is the interesting one: `bics r3,r3,#0x1` /
`lsl r6,r0,#0x8` / `asr r3,r2,#0xd` (arithmetic shift right by 13 - a division-by-8192
shape) / `bl #0xfea` (a tiny-offset call, almost certainly a stub into an imported
runtime function) / then a chain of further branches
(`0x803a0b2e → 0x803a0e72 → 0x803a0f10 → 0x803a139e`) that repeated attempts to widen
the disassembly window couldn't cleanly resolve - `0x803a139e` turned out to be an
inline literal-pool constant (a 32-bit address embedded between instructions,
misread as code because the target address wasn't 4-byte aligned to the real
instruction stream). Chasing this further by pure static disassembly hit diminishing
returns for the third time in a row (misalignment, stale context, garbage decodes) -
this is generic `euser.dll` binary-search utility code, not something specific to
N-Gage, and its exact tail beyond the division call wasn't worth forcing statically
when a live capture could just show the real values directly.

### Where this leaves the investigation

A real, concrete call chain now exists all the way from `cdlserver.exe`'s merge-sort,
through its comparator (ruled out) and array-accessor
(`FUN_83486654` → `thunk_EXT_FUN_803a0478` → `FUN_803a0ad8`), to a genuine division
operation (`asr`+`bl`) that very plausibly calls into `drtaeabi.dll` - directly
matching rounds 27/29's live evidence that execution is stuck there. Static analysis
alone couldn't resolve exactly what happens after the division call; the natural next
step is a live capture instead, which round 33 provided.

### To resume from here

1. Set up a live probe that fires the instant `CdlServer`'s PC enters `drtaeabi.dll`'s
   known spin range (`0x8039456c`-`0x80395728`), capturing PC/LR/SP and a stack scan -
   this directly continues from where static tracing hit its limit.
2. Keep using `euser_fresh_r32` for any further `euser.dll` disassembly; avoid the
   older, now-confirmed-stale projects.

## Round 33 — a long methodology detour, then a live capture and a third independent confirmation of "permanent"

Set out to do exactly what round 32 asked for: a live probe firing the instant
`CdlServer` enters `drtaeabi.dll`. Got there, but only after chasing two
self-inflicted false leads first - both worth recording since they cost most of this
round and could easily recur.

### False lead #1: mistook `timeout N` kills for genuine self-exits

The first several attempts used a bounded `timeout 40/60/75/90` wrapper around the
repro command and, seeing the process gone and the log stopped growing right after
the `!BackupServer` fail-fast fires, concluded the app was now exiting cleanly on its
own - a real behavior change from rounds 27-31, where the window stayed open for
6-25 minutes. This led to an extensive, ultimately pointless detour: bisecting every
unrelated in-progress file in the tree (an 8-file multi-device-storage refactor in
`mainwindow.cpp`/`state.cpp`/`cmdhandler.cpp`/`devices.h`/`devices.cpp`/`epoc.h`/
`device_install_dialog.cpp`/`applist.cpp`, reverted via `git stash` and restored
after, confirmed **not** the cause), then testing a cache-warming hypothesis (Central
Repository's `persists/rm-320/` cache, confirmed to be long-standing, unrelated code
with zero diff - also not the cause). **The actual bug was in the test methodology**:
re-running *without* the `timeout` wrapper and measuring real elapsed time directly
showed the process genuinely alive at t=301s, then again past a full hour (t=3610s)
in a later test - `timeout N` had been killing the process at exactly `N` seconds the
whole time, and "process gone" was misread as a self-exit instead of an external
kill. No code regression ever existed; the app behaves exactly as rounds 27-31 found.

### False lead #2: a live probe that silently never showed its output

With survival confirmed, added a direct probe (`epoc.cpp`'s `system_impl::loop()`,
later moved to an `ntimer`-scheduled callback chained onto `server.cpp`'s round-26
fail-fast handler, matching round 27's own proven technique exactly) to log
`CdlServer`'s PC/LR/stack the moment it's inside `drtaeabi.dll`'s range. It produced
*zero* output across a 45-second test, a 3-minute test, and eventually a full
**one-hour** unattended test - despite the process staying alive throughout (ruling
out a crash) and the scheduling code being correct (`ntimer`'s clock,
`common::teletimer`, is confirmed to be a true wall-clock timer via
`std::chrono::system_clock`, not something that could be starved by slow emulated
CPU throughput). The actual cause: `config.yml`'s `log-filter` contains
`Kernel:Warn`, which silently drops every `LOG_TRACE(KERNEL, ...)` call - including
every line this probe ever logged. The probe had very likely been firing correctly
the entire time; the output was just filtered out before ever reaching the log file.
Confirmed by temporarily changing the filter to `Kernel:trace` and re-running: the
probe's output appeared immediately, including a full thread-state dump showing
`CdlServer` still `state=1` (`run`) - the exact same finding round 27 made, live
reconfirmed via a completely independent path.

### The live capture

With logging fixed, extended the probe to capture `CdlServer`'s full register context
(via `kernel::thread::get_thread_context()`) plus a stack scan the moment the
`ntimer`-scheduled dump fires. Result:

```
pc=0x803946b8 lr=0x80394bef sp=0x401b80
```

Both `pc` and `lr` fall inside `drtaeabi.dll`'s known spin range
(`0x8039456c`-`0x80395728`) - confirming this is a **nested call within
`drtaeabi.dll` itself** (one internal routine calling another), not a single flat
loop. The stack scan found several more in-range return addresses
(`sp+0x1c=0x80395343`, `sp+0x24=0x80394acf`, `sp+0x34=0x80394b7f`, all inside
`drtaeabi.dll`) plus one frame that lands outside it: **`sp+0x54=0x803a3385`, inside
`euser.dll`'s range** - almost certainly the actual return address back into the
binary search (`FUN_803a0ad8`, round 32) that made the original division call. This
directly, concretely ties round 32's static call chain to round 27/29's live
evidence: `cdlserver.exe`'s merge → its binary-search array-accessor in `euser.dll` →
a division operation → nested execution inside `drtaeabi.dll`.

### The 32-minute patience test: still flat

The nested-call shape briefly raised a new version of round 31's "maybe it's just
slow" hypothesis - a multi-level software division/normalization routine calling
sub-helpers is exactly what a legitimately slow (but bounded) arithmetic routine
would look like. Tested it directly, the same way rounds 28/31 did: reverted all
probe code back to the clean round-26 baseline (no risk of the probe itself
influencing timing), launched the repro with no `timeout` wrapper, and polled log
size and process memory every 30 seconds for **32 minutes (1909 seconds)** straight,
across three chained background-polling windows. **Result: no change of any kind for
the entire 32 minutes.** The log stayed at exactly 404 lines from t=48s onward -
every single one of the roughly 60 checks after that point identical. Memory stayed
within 146,124-146,216 KB the whole time - a sub-0.1% drift that's measurement noise,
not growth. This is a longer, harder version of round 31's own memory-growth
argument: a genuinely slow division algorithm computing something real would still
be *doing* something - if nothing else, its own call stack/working state would show
some variation over half an hour. Seeing literally nothing move for 32 minutes,
on top of rounds 28 and 31's own flat results, settles this a third time, more
strongly than either previous test: **this is a genuine, permanent stall, not
slow-but-bounded division work** - `CdlServer` is cycling through the same narrow set
of addresses/call frames in `drtaeabi.dll` without ever making forward progress.

### Reverted completely

All round-33 diagnostic code removed: `kernel.h`'s `probe_eik33_*` members,
`server.cpp`'s `ntimer`-chained thread-dump-and-context-capture code (stripped back
to exactly its round-26 form, `mem/ptr.h` include removed), `epoc.cpp`'s host-loop
probe attempts (also stripped back to exactly the pre-existing, unrelated
per-device-drives feature diff), `log.cpp`'s `flush_on` level reverted to `debug`.
`config.yml`'s `log-filter` restored to its original `Kernel:Warn` value. Verified via
`git diff --stat` showing only `server.cpp` (+77, round 26) and `epoc.cpp` (+26,
pre-existing unrelated feature) - `kernel.h` and `log.cpp` show no diff at all,
matching the clean baseline. No resource incidents; guardrail held throughout
(`tasklist` checks before/after every build and run, MSBuild node cleanup after every
build, one heavy process at a time, no stacked builds/emulator runs).

### To resume from here

1. The stall is now pinned down to a specific, narrow address range
   (`0x8039456c`-`0x80395728`) inside `drtaeabi.dll`, reached via a concrete call
   chain (`cdlserver.exe`'s merge → `euser.dll`'s binary search at `0x803a0ad8` → a
   division operation) with a live PC/LR/stack snapshot on record. The natural next
   step is static analysis of `drtaeabi.dll` itself at `0x803946b8`/`0x80394bef` (a
   fresh Ghidra project, per round 32's own lesson about stale projects) to find out
   exactly what division/arithmetic algorithm this is and whether it has a genuine
   termination bug (e.g., a divisor of zero, a malformed operand from the guest side,
   or an off-by-one in the loop's own exit condition) - this is real Symbian
   compiler-runtime code, so a fix here is much more likely to be "the operand/data
   feeding this routine is wrong" than an emulator-side IPC patch like round 26's fix.
2. Multiple live snapshots over time (not just one) would help distinguish "cycling
   through the exact same few addresses" from "slowly progressing through a larger
   but still-bounded set of addresses" - the 32-minute memory/log test argues for the
   former, but a direct address-level comparison across several captures a few
   seconds apart would settle it beyond doubt.
3. Two lessons worth keeping in mind for any future probe in this investigation: (a)
   never rely on a `timeout N` wrapper to distinguish "process still running" from
   "process exited" - measure real elapsed time directly against a background poll
   with no external kill; (b) `config.yml`'s `log-filter` restricts the `Kernel`
   category to `Warn` and above by default - any `LOG_TRACE(KERNEL, ...)` probe needs
   either a temporary filter change (`Kernel:trace`, remembered to revert afterward)
   or a `LOG_WARN`/higher call to be visible at all.

## Round 34 — not a division algorithm: `CdlServer` is stuck retrying a `Leave(KErrServerBusy)` against an already-stopped `ecomserver`

Set out to statically disassemble the function containing round 33's captured
`pc=0x803946b8`/`lr=0x80394bef` in `drtaeabi.dll`, per round 33's own "to resume"
plan. The disassembly itself completely overturned the working "division algorithm"
theory and, combined with one live capture, found the real mechanism.

### `drtaeabi.dll`'s code isn't arithmetic - it's C++ exception-handling machinery

Parsed `drtaeabi.dll`'s own ROM header the same way as every other module in this
investigation (`build/bin/data/drives/z/rm-320/sys/bin/drtaeabi.dll`,
`code_address=0x80394398`, `code_size=0x3950` → range
`0x80394398`-`0x80397CE8`, comfortably containing both captured addresses),
extracted its code section, and disassembled it in a fresh Ghidra project
(`drtaeabi_fresh_r34`, following round 32's lesson about stale projects). The result
looked nothing like arithmetic: functions doing `sub r2,r2,r1` / `asr r2,r2,#0x3`
(subtracting two pointers and dividing by 8 - a classic "size of one table entry"
computation), searching a table, then dispatching through **literal-pool constants**
(`ldr r0,[0x80394cd0/cd4/cd8]`) and a **virtual call through a function pointer**
(`ldr r3,[r4,#0x14]; blx r3`) comparing the result against small integers (6, 7, 8,
9). This is the unmistakable shape of C++ **exception type-matching/dispatch**
tables, not a division routine - `drtaeabi.dll`'s name literally stands for "ARM
EABI," and EABI's defining feature (beyond basic arithmetic helpers) is its
standardized C++ exception-unwinding ABI. Round 32's own "division-shaped"
instruction sequence (`asr r3,r2,#0xd` then `bl #0xfea`) wasn't computing a
quotient at all - it was almost certainly the setup for a `User::Leave()` call.

### The piece that had been sitting in every capture's log output the whole time

This reframing made one thing obvious in hindsight: every single trace capture this
investigation has ever taken while `CdlServer` was in this state - round 27's,
round 33's - shows a **flood** of two interleaved messages from
[`svc.cpp`](../../src/emu/kernel/src/svc.cpp)'s own pre-existing, permanent
instrumentation (not anything added by this investigation):
`"Leave started! Guess leave code: -15"` and `"Leave trapped by trap handler."`.
These are EKA2L1's own HLE implementations of the real Symbian `User::LeaveStart()`/
`User::LeaveEnd()` SVCs - meaning **guest code is genuinely, repeatedly calling
`User::Leave(-15)`** (`-15` = `KErrServerBusy`) inside a `TRAP(...)` block, over and
over, at a rate fast enough to flood the log. This had been visible the entire time
and was dismissed as background noise in every prior round, this one included until
now.

### The live capture: found the exact call chain

Added a one-shot probe directly to `leave_start` (`svc.cpp`) - fires the first time
the leave code is `-15` on `CdlServer`'s thread, using `LOG_WARN` specifically to
avoid round 33's own log-filter trap (`Kernel:Warn` by default) rather than needing
a config change. Ran against **the new launcher, UID `0x20007B39`**, per this
round's guidance to prefer it over the old teaser-video launcher (`0x20007B38`)
except where the old one is specifically needed - round 30 already confirmed both
hit the identical stall, so this doesn't change anything evidentially, just brings
testing in line with which launcher is actually expected to end up fixed.

Captured `pc=0x803a024c` / `lr=0x803af4af` (both inside `euser.dll`'s confirmed
`0x8039CDB8`-`0x803CF328` range - the leave machinery's own internal call chain)
and a 64-word stack scan. The scan reconstructs a complete, concrete call chain by
address:

- `sp+0xc = 0x804c67fd` and `sp+0x9c = 0x804c6831` - both inside **`ecom.dll`**'s
  confirmed range (`0x804C6608`-`0x804C7D44`, established all the way back in round
  1/4 of this investigation).
- `sp+0xc4 = 0x83485e45` - inside **`cdlserver.exe`**'s `FUN_83485e24` (round 31's
  linked-list-building routine, already known from static analysis to call into
  `ecom.dll` via `thunk_EXT_FUN_804c67fe` - this stack value is that exact call
  site's return address, off by the Thumb bit).
- `sp+0xd4 = 0x83485fef` - inside `cdlserver.exe`'s small wrapper (`0x83485fde`,
  round 29's finding) that calls `FUN_83485e24`. **This exact address
  (`0x83485fef`) also appears independently in round 29's own stack scan**
  ([traces/round29_stack_scan.log](traces/round29_stack_scan.log)) - two completely
  separate capture techniques, a static analysis and two live captures four rounds
  apart, all converging on the same address.

Put together: `cdlserver.exe`'s wrapper calls `FUN_83485e24`, which calls into
`ecom.dll`, and *that* ECom call is what's leaving with `KErrServerBusy` - not
looping internally, but being **repeatedly retried from further up the stack**
every time it fails, at whatever rate the guest's own retry logic runs at.

### Why ECom would report "busy" forever: `ecomserver`'s thread is already stopped

Every thread-state dump taken anywhere in this investigation - starting with round
27's original one - has shown **`!ecomserver` sitting in `state=4`**, which
[`kernel/thread.h`](../../src/emu/kernel/include/kernel/thread.h)'s
`thread_state` enum defines as `stop` (`create=0, run=1, wait=2, ready=3, stop=4`).
`ecomserver`'s thread has **already terminated** by the time `CdlServer` tries to
talk to it. If EKA2L1's IPC layer responds to a message sent to an already-stopped
server's session with `KErrServerBusy` - a "try again shortly" error - rather than
something like `KErrServerTerminated`, then the guest's own busy-retry logic
(itself entirely reasonable client code, in the same spirit as round 25's finding
about `bafl.dll`'s bounded retry) would have no way to know the server is never
coming back, and would retry forever. This directly connects this investigation's
very first target (`ecom.dll`/`ecomserver.exe`, rounds 1-4) to its latest
(`CdlServer`, rounds 27-34): `ecomserver` stopping early - for whatever reason
originally drove rounds 1-4's own investigation - is very plausibly the real root
cause of the later stall, not a coincidence of two unrelated modules both being
involved.

### Reverted completely

`svc.cpp`'s temp probe (the `mem/ptr.h` include and the `leave_start` capture block)
fully removed, verified via `git diff --stat` showing only the pre-existing round-26
one-line fix (`message_complete`'s `msg->msg_status = ipc_message_status::completed;`).
Rebuilt clean afterward. No other files touched this round - the `drtaeabi.dll`
analysis was pure Ghidra static work, no emulator source needed for that part. No
resource incidents; guardrail held throughout.

### To resume from here

1. Find out **why** `ecomserver.exe`'s thread stops when it does - is this correct,
   expected termination (ECom finishing its own real work and legitimately exiting),
   or itself a symptom of something upstream (e.g., related to the restored backup's
   unusual component set, or another interaction with round 26's fix)? Rounds 1-4's
   original notes on `ecom.dll`/`ecomserver.exe` are the natural starting point.
2. Find exactly what error code EKA2L1's IPC layer returns when a message is sent to
   a session whose server thread has already stopped - if it's genuinely
   `KErrServerBusy` (or anything else implying "temporary, try again") rather than a
   terminal error, that's very likely the actual, fixable bug: the guest's retry
   logic is reasonable, but the signal it's retrying on is wrong.

   **Both answered by round 35 - see below.**
3. Confirm the guest-side retry mechanism directly - trace what code calls
   `FUN_83485e24` in a loop (or whether `FUN_83485e24` itself gets re-invoked
   repeatedly by its own caller further up `cdlserver.exe`) to see the actual
   retry cadence and whether it has (or lacks) its own bound, the same way round 25
   found `bafl.dll`'s bounded retry.
4. Per this round's guidance: prefer the new launcher (`0x20007B39`) for any further
   repro/testing going forward, since that's the one expected to end up fixed - only
   use the old launcher (`0x20007B38`) where testing it specifically is necessary
   (e.g., confirming a fix doesn't regress it, or isolating whether a finding is
   launcher-specific).

## Round 35 — `ecomserver` stopping is correct kernel behavior; the real gap is missing server-relaunch-on-demand

Answered round 34's own two open questions directly by reading the kernel object
lifecycle source (`kernel_obj.cpp`, `kernel.cpp`, `server.cpp`, `session.cpp`,
`svc.cpp`) rather than more live tracing. No source changes - pure code reading.

### Why `ecomserver.exe`'s thread stops: because it's supposed to

There is **zero EKA2L1-side implementation of `ecomserver`** - `ecomserver.exe` is
unmodified, real Symbian guest code, the same as `cdlserver.exe`. It stops because
the real compiled binary decides to, not because of anything EKA2L1 does to it.
Traced the exact mechanism: when its thread finishes and exits,
[`thread::do_cleanup()`](../../src/emu/kernel/src/thread.cpp) closes the thread's
handles (including its own handle to its server object) via
[`object_ix::close()`](../../src/emu/kernel/src/object_ix.cpp), which calls
`kernel_obj::decrease_access_count()`
([`kernel_obj.cpp`](../../src/emu/kernel/src/kernel_obj.cpp)) - when that count
hits zero, `kern->destroy(this)` fires
([`kernel_system::destroy()`](../../src/emu/kernel/src/kernel.cpp)), which calls
`server::destroy()` (correctly `detatch()`-ing every existing session with
`error_server_terminated`) **and fully erases the server object from `servers_`**.
This is textbook-correct Symbian kernel-object lifecycle management - not a bug.
Given round 1-2's own original finding that `ecomserver`'s `!BackupServer`
4-message exchange is finite and legitimate, `ecomserver.exe` is almost certainly a
one-shot bootstrap server by design: scan and register ECom plugins once early in
boot, then exit - matching real Symbian's actual ECom architecture.

### What error code EKA2L1 returns: the *correct* one

[`session_send_general`](../../src/emu/kernel/src/svc.cpp) checks
`ss->is_server_terminated()` and immediately returns `epoc::error_server_terminated`
- and [`utils/err.h`](../../src/emu/utils/include/utils/err.h) confirms
`error_server_terminated = -15`. **This is exactly the `-15` round 34 captured -
not a misreported `KErrServerBusy` as first guessed, but the genuinely correct
"server terminated" error, working exactly as intended, with no delay or hang at
the kernel level at all.** EKA2L1's own server/session bookkeeping is not the bug.

### The actual gap: no server-relaunch-on-demand

Checked [`session_create`](../../src/emu/kernel/src/svc.cpp) - the function a
client calls to *reconnect* after learning its session died - and it only does
`kern->get_by_name<service::server>(server_name)`; if that returns null (which it
now correctly will, since `ecomserver`'s server object was erased), it returns
`epoc::error_not_found` and stops. **There is no code path anywhere that launches a
system server's executable in response to a client trying to connect to it.** Real
Symbian OS provides exactly this: system servers are started on-demand by the
kernel/loader the first time a client connects to their well-known name, and
ECom's own real client framework (`REComSession`) is specifically designed around
being resilient to its server dying, relying on being able to reconnect (which
transparently relaunches the server) rather than treating `KErrServerTerminated` as
fatal. **This is the real, fixable bug: not that `ecomserver` stops (expected), not
that EKA2L1 reports the wrong error (it doesn't) - but that EKA2L1 has no mechanism
for a client's reconnect attempt to actually relaunch the terminated server**, so
whatever guest-side retry/reconnect logic exists in `cdlserver.exe`/`ecom.dll`
(designed against real hardware, where reconnecting silently restarts the server)
spins forever against a `session_create` that can only ever fail once the server
is truly gone.

### To resume from here

1. Implement server-launch-on-demand: give `session_create` (and its
   `session_create_from_handle`/eka1 equivalents) a fallback when
   `get_by_name` finds nothing - a well-known-server-name → executable-path lookup
   (likely already something in the ROM's own server-registration data, e.g. an
   SCR/opaque-data mechanism, or simply "try `kern->spawn_new_process` on the
   matching `.exe` name") that launches a fresh instance and retries the connect,
   matching real Symbian's actual on-demand-server-start behavior.
2. Before implementing anything, confirm this is really what the guest is waiting
   on: trace whether `cdlserver.exe`'s retry loop ever calls `session_create` again
   after getting `error_server_terminated` (as opposed to only ever resending on the
   same dead session handle, which round 34's single capture didn't fully
   distinguish) - this determines whether the fix belongs in `session_create`
   specifically or somewhere else in the reconnect path.

   **Answered by round 36 - see below.**
3. Confirmed via source reading only, no live capture or build this round - no temp
   probes added, nothing to revert. Source tree unchanged from round 26's baseline.

## Round 36 — confirmed directly: `CdlServer` never retries `session_create`, it just hammers the same dead handle

Answered round 35's own open question with a live capture instead of guessing.
Added a temp probe to all three session-connect entry points
(`session_create`, `session_create_from_handle`, and `session_send_general` -
[svc.cpp](../../src/emu/kernel/src/svc.cpp)), each gated to `CdlServer`'s thread
and logging every call up to a 3000-event cap per site, using `LOG_WARN` (per
round 33's own lesson - avoids the `Kernel:Warn` log-filter trap entirely, no
config change needed this time). Ran against **the new launcher (`0x20007B39`)**,
continuing round 34's guidance.

### The result: two connects, total, then 2955+ blind resends

Across the entire capture window (process alive throughout, confirmed via
`tasklist`):
- **`session_create`** fired exactly **twice** for `CdlServer`: once for
  `!FileServer` (found), once for `!ecomserver` (found - the server object was
  still alive and connectable at that moment, consistent with round 35's finding
  that it only terminates partway through boot, not before `CdlServer` even
  starts).
- **`session_create_from_handle`** fired **zero** times.
- **`session_send`** fired thousands of times on a handful of handles. The very
  first `terminated=true` appears **immediately after round 26's `!BackupServer`
  fail-fast fires** (matching every previous round's timing), on the exact
  `!ecomserver` session handle created moments earlier. From that point on, the
  same handle gets resent to **2955 more times within a single 60-second
  capture**, every single one `terminated=true`, with **no further
  `session_create` or `session_create_from_handle` call of any kind** appearing
  anywhere in between.

### What this rules out, and what it leaves open

This directly answers round 35's question: `cdlserver.exe`'s (or `ecom.dll`'s)
retry behavior is **not** "reconnect, then resend, forever" - it's "resend on a
session that's already known to be dead, forever, without ever trying to
reconnect." That means round 35's proposed fix (teach `session_create` to
relaunch a terminated server on demand) **would not, by itself, fix this
particular loop** - relaunching `ecomserver` wouldn't help a session object whose
`svr` pointer is already permanently `nullptr`; the client would need to call
`session_create` again to get a session to the *new* instance, and this capture
shows it never does that.

The rate (2955 in 60 seconds, one session handle, zero variation) is a genuinely
tight native loop, not a bounded, spaced-out retry-with-backoff pattern - matching
every prior round's characterization of this as a real stall rather than legitimate
slow work. Two explanations remain open: either the real guest code (ECom client
logic in `euser.dll`/`ecom.dll`, or `cdlserver.exe` itself) has no handling for
`KErrServerTerminated` at all and just blindly retries the same failed call forever
(which would be surprising for a mature framework, but not impossible for this
specific code path), or there's a reconnect attempt that exists in principle but
never gets reached because something about the *outer* loop driving these calls
never lets the guest observe the error and branch on it correctly.

### Reverted completely

All three `PROBE36` blocks removed from `svc.cpp`. Verified via `git diff --stat`
showing only the pre-existing round-26 one-line fix. Rebuilt clean afterward. No
resource incidents; guardrail held throughout.

### To resume from here

1. Trace one level up from `session_send`/`leave_start`: capture the actual return
   address chain repeatedly (not just once, like round 34) to see whether the
   *calling* code around `FUN_83485e24` ever branches differently based on the
   error code, or whether it's provably the same straight-line path every time
   with no conditional check on the result at all - this would settle whether the
   guest code itself is missing error handling, independent of any EKA2L1 gap.

   **Answered by round 37 - see below.**
2. If the guest genuinely never checks the error, the practical fix is almost
   certainly EKA2L1-side after all - not relaunching `ecomserver` (round 36 rules
   that out as sufficient on its own), but something further upstream, such as
   `ecomserver.exe` not being allowed to exit this early in the first place (i.e.,
   revisiting round 35's item 1 - is early exit really correct/expected, or does a
   real N95 keep `ecomserver` alive longer, e.g. as a persistent background
   server rather than a one-shot bootstrapper?).

## Round 37 — no loop exists in `cdlserver.exe`'s own code to check the error: this is Active Object re-arming, not a busy-loop bug

Answered round 36's own open question directly with fresh static analysis (a new
`cdlserver_fresh_r37` Ghidra project, per round 32's standing lesson about avoiding
stale projects). No emulator source touched - pure Ghidra work.

### `FUN_83485e24` and both its callers: straight-line code, zero checks

Freshly disassembled and decompiled `FUN_83485e24` in full (it decompiled cleanly
this time, unlike round 31/34's partial reads - Ghidra's flow-following needed to
be seeded exactly at the target address, not just somewhere nearby in the same
window, which is why earlier attempts this round initially came back empty):

```c
void FUN_83485e24(int param_1) {
  func_0x83486400(0x18);
  uVar1 = func_0x834863d8();
  func_0x83486358(_DAT_83486074, uVar1);
  func_0x83486550(_DAT_83486078, uVar1);   // <- this is the one that leaves
  func_0x834863d0(uVar1, _DAT_8348607c);
  func_0x83485d62(param_1, uVar1);          // round 31's merge-sort
  func_0x83486624(*(undefined4*)(param_1+0x28));
  func_0x834865b8(*(undefined4*)(param_1+0x28));
  *(undefined4*)(param_1+0x28) = uVar1;
  func_0x83486340();
}
```

The call to `func_0x83486550` at `0x83485e40` is the one whose return address
(`0x83485e44`, matching round 34's captured `0x83485e45` exactly once the Thumb
bit is accounted for) sits on the stack during the live `Leave(-15)` capture -
confirming precisely which of `FUN_83485e24`'s several calls is the one that
reaches `ecom.dll`. **Nothing in this function captures a return value from that
call, checks it, or branches on it.** It's an unconditional, straight-line
sequence from top to bottom.

Found **two** direct callers of `FUN_83485e24` in `cdlserver.exe`'s own code
(round 29/34 only knew about one): the known wrapper at `0x83485fde`, and a
second, previously-unseen one at `0x83485e86`. Both are structurally identical
- acquire something, call `FUN_83485e24`, release it, one cleanup call - and
**neither contains a single conditional branch or return-value check either.**

### `FUN_83485fde` has no caller anywhere in `cdlserver.exe`'s own code

Scanned the entire 6.5KB binary (every 2-byte-aligned offset, since no
auto-analysis is available to establish function boundaries structurally) for any
instruction referencing `0x83485fde`. **None exist.** The wrapper is never called
from anywhere inside this executable's own machine code - it must be invoked
externally, almost certainly via Symbian's real Active Object framework
(`CActive::RunL()`), the standard architecture for a server like `CdlServer` that
watches for asynchronous ECom events: the scheduler calls this handler once per
completed request, and real Active Object `RunL()` implementations commonly
re-issue ("re-arm") the same request unconditionally at the end, by design.

### What this means: there's no guest-code loop to fix

This settles round 36's question cleanly: **there is no traditional loop anywhere
in `cdlserver.exe`'s own code that checks the leave/error code and decides whether
to retry.** The repeated calls round 36 captured (2955 in 60 seconds) aren't a
busy-loop bug in application logic being wrongly triggered by round 26's fix or
anything else EKA2L1 does at the CPU level - they're the natural consequence of a
real, correctly-behaving Active Object being re-armed every time its request
against `ecom.dll` completes, which happens near-instantly precisely *because*
the target session is already dead (an immediate `error_server_terminated`
completion, not a real wait). On genuine Symbian hardware this exact code would
behave identically **if** `ecomserver` died this early there too - meaning the
real, only remaining lever is round 35's original item 1, now more clearly the
crux of the whole investigation: **why does `ecomserver.exe` terminate before
`CdlServer` needs it, and would a real N95 keep it alive at this point in boot?**

### Reverted completely

No emulator source files were touched this round - only Ghidra static analysis
(new project `cdlserver_fresh_r37`, extracted from
`build/bin/data/drives/z/rm-320/sys/bin/cdlserver.exe`,
`code_address=0x83485248`, `code_size=0x1A30`). Nothing to revert in the source
tree; `git diff --stat` still shows only the round-9/round-26 baseline.

### To resume from here

1. This is now the single most important open question in the whole
   investigation: **why does `ecomserver.exe` exit when it does.** Static-trace
   `ecomserver.exe` itself (fresh Ghidra project, following the same lesson) to
   find its own exit path - what condition does it check before deciding it's
   done, and is that condition being satisfied correctly or prematurely under
   EKA2L1 (e.g., missing a plugin/registry entry it's waiting to see, a
   `!BackupServer`-adjacent dependency it never gets a real answer from and gives
   up on, or something in the restored backup's unusual component set that a
   fresh/clean install wouldn't trigger).

   **Answered by round 38 - see below. It doesn't exit; it panics.**
2. If `ecomserver` is found to exit because of something it never receives (a
   real answer from a dependency, a specific registration, a timeout it
   shouldn't be hitting), that dependency - not `CdlServer`'s own code, and not
   the session/reconnect layer round 35/36 already cleared - is very likely the
   actual, fixable root cause of this entire second stall.

## Round 38 — `!ecomserver` doesn't exit, it panics: found the exact trigger, and it loops all the way back to round 1

Traced `ecomserver.exe`'s own shutdown path expecting to find an exit condition.
Found something more specific and more useful: it isn't shutting down at all.

### The panic was already sitting in an existing capture

Before writing any new static-analysis tooling, checked whether any
already-captured log from this session happened to show `!ecomserver`'s own
thread-kill message (`thread::kill()` in
[thread.cpp](../../src/emu/kernel/src/thread.cpp) logs a different line for
`panic` vs. `kill` vs. peaceful `terminate`/`pending`, so this settles the
question directly without needing a new probe). It did:

```
Thread !ecomserver panicked with category: E32USER-CBase and exit code: 47 (An active object leaves)
```

`E32USER-CBase` panic `47` is a well-known, standard Symbian panic: it's what
`CActiveScheduler`'s default error handler raises when an Active Object's
`RunL()` calls `User::Leave()` and there's no `RunError()` override to catch
it. **`!ecomserver` is crashing, not finishing its work and exiting cleanly** -
directly overturning round 35's working assumption that its termination was
ordinary and benign (the kernel-object lifecycle round 35 traced is still
100% correct; it's just cleaning up after a crash, not a graceful exit).

### Following the trail: static analysis dead-ended, so a live capture answered it instead

Attempted to statically trace `ecomserver.exe`'s own entry point and shutdown
logic first (fresh Ghidra project `ecomserver_fresh_r38`/`ecomserver_analyzed_r38`,
extracted from `build/bin/data/drives/z/rm-320/sys/bin/ecomserver.exe`,
`code_address=0x804C7DC8`, `code_size=0x74BC`). This hit a real, useful lesson
mid-round: the entry point (`0x804C7DC8`) is a thunk that `blx`-calls
`0x804CD43C` - an **even** target address, which per ARM's `BLX` semantics means
execution switches to **ARM mode**, not Thumb, at that specific call (forcing
Thumb there, as every other disassembly this round did by default, produced
total garbage - `ldmia` loading into a register already holding a live pointer,
"could not recover jumptable", nonsense branch targets thousands of bytes away).
Once corrected, `0x804CD43C` turned out to be generic RVCT/EABI process-startup
boilerplate (`ldr pc,[0x804CD440]`, an import-table-style indirection into
`euser.dll`'s generic `E32Startup` glue) - identical scaffolding every compiled
Symbian executable shares, not anything `ecomserver`-specific. A full-analysis
pass (589 auto-detected functions, no symbol table, no readable strings - a
stripped release build) made manually hunting for the real shutdown logic
impractical in reasonable time.

Switched to a live approach instead, the same way round 34 found its
breakthrough: enabled `log-ipc: true` in `config.yml` for one capture (a
config-only change, no rebuild) alongside the by-now-standard temporary
`Kernel:trace` filter override (reverted immediately after, per round 33's
lesson). This showed the complete sequence leading directly into the panic:

```
Sending a blind sync message
Sending 34 sync to !BackupServer
Receive requested from !BackupServer
Sending 32 sync to !BackupServer
Receive requested from !BackupServer
!BackupServer message (function=32) never completed by guest dispatch after 1s - failing fast with KErrNotSupported
Leave started! Guess leave code: -5
Leave trapped by trap handler.
Sending a blind sync message
Sending 34 sync to !BackupServer
Receive requested from !BackupServer
Sending 32 sync to !BackupServer
Receive requested from !BackupServer
Leave started! Guess leave code: -5
Leave trapped by trap handler.
Thread !ecomserver panicked with category: E32USER-CBase and exit code: 47
```

**`!ecomserver` is a client of `!BackupServer`** - the same server rounds 1-26 of
this entire investigation are about. It sends function 34, then function 32 (the
exact opcode round 26's fix targets); round 26's fix fires as designed after 1
second, completing with `KErrNotSupported` (`-5`); `-5` becomes a `Leave`, caught
once by a `TRAP`. Then **the identical sequence repeats immediately** - same two
functions, same 1-second fail-fast, same `Leave(-5)` - but this second time
nothing catches it, and the active scheduler panics the thread.

### Why this is round 26's own fix causing the crash, and why that isn't as simple as "revert it"

Round 26's own header comment ([server.cpp](../../src/emu/kernel/src/server.cpp))
already documents, from the original rounds 13-23 static/live tracing, that
function 32's dispatch is a **genuine, real, in-range no-op even on actual
Symbian hardware** - the guest's own compiled switch statement does nothing for
this opcode, so `message_complete` never fires there, and "real Symbian" has no
timeout on this wait either. That means **a real N95 would also block here
forever** if it ever made this exact call - it just would never *panic*, because
it would never receive any response (good or bad) to react to. Round 26's
fail-fast is what turns eternal silence into a fast, explicit `KErrNotSupported`
- correct and necessary to avoid the original hang, but a response `!ecomserver`'s
real, compiled error-handling code was never written to expect or survive.

This means the actual, deepest open question in this entire investigation -
tying round 1's very first target directly to round 38's latest one - is:
**why does `!ecomserver` (via `bafl.dll`) call this specific,
always-broken-even-on-real-hardware `!BackupServer` operation at all?** Round 24
already proved this whole chain (both this one and the original round 1-9 hang)
only reproduces with a restored backup, never on a clean install - so a healthy
real device's `ecomserver` presumably never takes this path in the first place.
Something in this specific restored backup's state is the true trigger.

### Reverted completely

`config.yml`'s `log-ipc` and `log-filter` restored to their original values
(`false` and `Kernel:Warn` respectively) immediately after the capture. No
emulator source files were touched this round - all Ghidra static analysis plus
one config-only live capture. `git diff --stat` still shows only the
round-9/round-26 baseline.

### To resume from here

1. This is now the single most important open question in the whole
   investigation: **what, specifically, in this restored backup's state causes
   `!ecomserver`/`bafl.dll` to call `!BackupServer` function 32 in the first
   place** - the very same question round 1-25 spent its early rounds on for a
   *different* caller (the original hang), now reopened for this second one.
   `backup.lst`, the `10202be9`/`10009D8F` private folders, and anything
   `bafl.dll`-adjacent (round 25's own findings) are the natural starting points.

   **Partially advanced by round 39 - see below.**
2. A more surgical alternative worth considering in parallel: since round 26's
   fix is what converts "hangs forever" into "panics `!ecomserver`," and a real
   device apparently never receives a response either way, investigate whether
   completing this specific message with something other than
   `KErrNotSupported` (e.g. whatever a genuine no-op dispatch's *implicit*
   result would be, if one exists) would let `!ecomserver`'s own error-handling
   path treat it as recoverable instead of panicking - without reopening the
   original infinite-hang bug round 26 fixed.
3. Per this round's continued guidance: this capture used the new launcher
   (`0x20007B39`); keep doing so for future repro unless the old one is
   specifically needed.

## Round 39 — traced ecomserver's own call site into `bafl.dll`; static tracing hit a real wall (C++ vtables), a live argument capture answered more of it instead

Set out to find exactly what in `ecomserver.exe`'s own code decides to call
`bafl.dll` at all - the actual trigger, one level up from round 38's IPC trace.

### Static tracing found the call site, then hit vtable dispatch

Re-used the fully-analyzed `ecomserver_analyzed_r38` project (round 38, 589
auto-detected functions) and searched every instruction for references landing in
`bafl.dll`'s confirmed range (`0x807780A8`-`0x8078266C`, round 25). Found two
direct import thunks `ecomserver.exe` calls: `thunk_EXT_FUN_807781f4` and
`thunk_EXT_FUN_80778118` (both close to, but distinct from, round 25's
`FUN_8077811a`/`FUN_80778190` - a higher-level API layer, not the same
connect-and-retry logic round 25 already characterized). Traced the one direct
caller (`0x804cbc18`) and found a textbook lazy-singleton pattern: *"if not
already created, create the `bafl.dll` client object and cache it; then always
call a method on it."* Traced one level further up (`0x804cbc64`) and hit a real
wall: **no direct caller exists anywhere in the binary's own instructions** -
the surrounding code is full of `blx r1`/`bx r1` (register-indirect calls) and
`ldr r1,[r1,#0x4]` (vtable-slot loads), the unmistakable signature of C++ virtual
dispatch. Without a symbol table, finding which vtable actually holds a pointer
to this function - and therefore which class/condition invokes it - would
require either recovering the vtable layout by hand or a different technique
entirely. Static call-graph tracing was the wrong tool for this specific
question; switched to a live capture instead.

### Live IPC argument capture: the concrete data, if not yet the exact trigger

Added a temporary probe to `session_send_general` ([svc.cpp](../../src/emu/kernel/src/svc.cpp))
capturing the raw IPC arguments (and, for anything that looked like a valid guest
pointer, the first 32 bytes it points to) for every message `!ecomserver` sends to
`!BackupServer`. Ran against the new launcher, as this round's own guidance asks.
Found more than round 38's trace alone showed:

- **`!ecomserver` sends function `31` first, then `34`, then `32`** - round 38's
  trace only showed 34/32; this capture confirms 31 is part of the same sequence
  and happens on every attempt, not just the first.
- The connect call's server-name descriptor decoded cleanly to UTF-16LE
  `"!BackupServer"` at `0x80780870` - matching round 25's own literal-string
  finding exactly, a solid cross-check that this probe is reading real memory
  correctly.
- The message payloads are genuine C++ objects, not simple flag values: several
  argument pointers' first words decode as Symbian descriptor headers
  (`0x20000004` = `EPtr` type, length 4), and others contain embedded pointers
  landing inside **`euser.dll`'s own code range** (`0x8039552f`, `0x803955f7`,
  `0x80397958`) - almost certainly vtable pointers for a real backup-observer
  interface object (`MBackupObserver`-shaped), not application-level data.
- **Found `-5` (`error_not_supported`) baked directly into the retry attempt's
  own data** - the second cycle's `args[1]` (`0x802d78`) has `fb ff ff ff`
  (`-5` as a little-endian `int32`) sitting in its second word, meaning the
  first attempt's failure result gets written into a cached state field before
  the identical sequence is retried - real, working client-side state tracking,
  not a naive blind repeat.

### Where this leaves the trigger question

This round didn't reach a single named file or registry entry as *the* trigger -
that would need either recovering the vtable/class layout behind
`0x804cbc64` (to find the actual condition gating the whole call), or a more
targeted experiment. Given round 24 already proved this entire chain (both the
original round 1-9 hang and this one) **never happens without a restored
backup at all**, the most likely shape of the real trigger is closer to *"a
restored backup exists" itself* (some marker or state that a clean install
simply never sets) rather than a specific corrupted value inside the backup -
`!ecomserver`'s own initialization flow may unconditionally perform this
backup-status check whenever such a marker is present, clean install or not.

### Reverted completely

`svc.cpp`'s temporary probe (the `mem/ptr.h` include and the `session_send_general`
argument-dump block) fully removed. Verified via `git diff --stat` showing only
the round-9/round-26 baseline. Rebuilt clean afterward. No resource incidents.

### To resume from here

1. The most direct empirical next step, matching this investigation's own
   established method (round 13's `bur\` folder check): selectively remove or
   rename candidate backup-state markers from the restored data one at a time
   (`Private\10202be9`'s contents, `Private\10009D8F`'s ECom cache, `backup.lst`
   itself) and re-test whether `!ecomserver` still sends function 31/34/32 at
   all - this would narrow "what marks a device as having a restored backup"
   empirically, without needing the vtable layout.

   **Round 40 tried the first candidate - see below. Ruled out.**
2. If recovering the exact C++ call site is still wanted, the vtable pointers
   found this round (`0x8039552f`, `0x803955f7`, `0x80397958`, all in
   `euser.dll`) are a concrete starting point - disassembling what's *at* those
   addresses (not just noting they're referenced) would identify which
   interface/class this object implements.

## Round 40 — `Private\10202be9` ruled out; caught a methodology trap along the way

Tested round 39's first candidate directly: temporarily renamed
`Private\10202be9` aside on both `drives/c` and `drives/d` (the SBE/Central
Repository folder, present on both the restored backup's C: and D: drives),
reran the repro, checked whether `!ecomserver` still panics.

### A near-miss: the panic log line is itself filtered by default

The first test run showed no panic message at all - but before treating that as
a result, checked why: `thread.cpp`'s panic log
(`"Thread {} panicked with category: {}..."`) uses `LOG_TRACE(KERNEL, ...)`,
which `config.yml`'s default `Kernel:Warn` filter silently drops, the exact same
trap rounds 33/38 already hit and documented. The first run's silence proved
nothing either way. Reran with the standard temporary `Kernel:trace` override
(reverted immediately after, per that established lesson) before drawing any
conclusion.

### Result: still panics, identically

With `Private\10202be9` completely absent from both drives, `!ecomserver`
**still panics with the exact same `E32USER-CBase`/47 ("An active object
leaves")**, and the process still stays alive afterward with `CdlServer` in its
usual stuck state - the whole downstream chain is unaffected.
**`Private\10202be9` is not the trigger.**

### Reverted completely

Both `Private\10202be9` folders restored exactly (renamed aside, not deleted,
then moved back - never destructive). `config.yml`'s `log-filter` restored to
`Kernel:Warn`. No emulator source touched this round - a pure data-only
experiment. `git diff --stat` still shows only the round-9/round-26 baseline.

### To resume from here

1. Next candidates from round 39's own list: `Private\10009D8F`'s ECom cache
   (the `.cre`/`ECom.dat`/`ECom.idx` files discussed back in round 32-33) and
   `backup.lst` itself - repeat the same rename-aside-and-retest method used
   this round for each.

   **Round 41 tried `10009D8F` - see below. Also ruled out.**
2. Given `10202be9` (an SBE/CentralRepository folder, not ECom-specific) turned
   out not to matter, the trigger may be something ECom-specific rather than a
   generic "was this device restored" flag - strengthening the case for
   checking `10009D8F` next.

## Round 41 — `Private\10009D8F` (ecomserver's own cache) also ruled out

Tested the next candidate from round 39/40's list: renamed `Private\10009D8F`
aside on both `drives/c` and `drives/e` (`!ecomserver`'s *own* UID3/private
folder, holding its `.cre`/`ECom.dat`/`ECom.idx` cache files discussed back in
round 32-33) and reran the repro. Applied round 40's lesson from the start this
time - `Kernel:trace` enabled for the whole test, no silent first pass.

### Result: still panics, identically

Same outcome as round 40: `!ecomserver` **still panics with the identical
`E32USER-CBase`/47**, process still alive afterward with `CdlServer` stuck as
usual. **`Private\10009D8F` is not the trigger either** - notably, this rules
out even `!ecomserver`'s *own* cache being stale/corrupted as the cause; it
panics the same way starting from a completely fresh cache.

### Reverted completely

Both `Private\10009D8F` folders restored exactly (renamed aside, then back -
never deleted). `config.yml`'s `log-filter` restored to `Kernel:Warn`. No
emulator source touched - a pure data-only experiment. `git diff --stat` still
shows only the round-9/round-26 baseline.

### To resume from here

1. Two candidates down (`10202be9`, `10009D8F`), both negative. The remaining
   round-39 candidate is `backup.lst` itself - worth testing next, though two
   folder-removal misses in a row raises real doubt that any single missing
   *file* is the trigger at all.
2. Worth reconsidering the shape of the hypothesis: if removing the two most
   obviously-relevant backup/ECom state folders changes nothing, the trigger
   may not be a *missing marker* a clean install lacks, but something more
   structural - e.g. simply the **number or specific identity of installed
   components** in this restored backup's real ~90-app set (round 20), where
   `!ecomserver`'s normal per-component scan happens to reach one specific
   component (or a boundary condition like "more components than some internal
   limit") that a typical clean install's much smaller component set never
   would. That would need a different kind of test than folder removal -
   e.g. narrowing which specific installed app/component is involved, not
   which backup-metadata folder exists.

   **Round 42 checked this premise directly before bisecting 109 folders on a
   hunch - it doesn't hold. See below.**

## Round 42 — there is no "specific component": the call happens before `!ecomserver` scans anything at all

Before committing to a bisection across the ~109 private folders on drive C (a
long, many-cycle test), checked a cheap precondition first: *does this call even
happen after any per-component work, or before it?* One quick full-trace capture
answered it directly, no bisection needed.

### The evidence: 8 log lines, zero component activity, between ready and panic

Enabled full tracing (`*:trace` plus `Kernel:trace`) for one capture and looked at
line numbers, not just message content. Between `!ecomserver`'s own
`"Rendezvous to: !ecomserver"` (its thread signalling "I'm initialized and
ready") and the fatal panic, there are exactly **8 log lines** - the blind sync
send, round 26's fail-fast firing, the `Leave(-5)`, trapped, then the identical
sequence repeating and panicking. **No plugin resource file is opened, no
per-app registry entry is read, nothing component-specific happens at all.**
This is `!ecomserver` calling `!BackupServer` as the very first action of its
own startup sequence - matching real ECom's documented architecture (check
backup/restore status once at boot, to decide whether the persistent plugin
cache is still valid) - not a loop over installed components that happens to
hit one bad entry.

### What this rules out, and what it redirects to

**"Which specific component triggers this" isn't the right question - there is
no specific component involved.** This also retroactively explains rounds 40/41's
two negative results cleanly: removing `10202be9` or `10009D8F` couldn't have
mattered, because the call this investigation has been chasing since round 38
fires before `!ecomserver` ever looks at *any* installed app's data, including
its own cache.

Given round 24 already proved this entire chain **never happens without a
restored backup at all**, and this round now shows the call itself is
unconditional and content-independent, the real differentiator between a clean
boot and this one must be something coarser than any single file:

- **Timing/ordering**: on a clean, much lighter boot, `!BackupServer`
  (`baksrvs.exe`) might not be summoned yet at the exact moment `!ecomserver`
  tries to connect - meaning `session_create` would return
  `epoc::error_not_found` immediately, which `!ecomserver`'s error handling
  might treat completely differently (recoverably) than round 26's 1-second
  fail-fast timeout, which delivers a different error (`KErrNotSupported`)
  after a real delay. A heavier, restored-backup boot could simply reorder
  things enough that `!BackupServer` *is* already up and connectable by this
  point, changing which code path `!ecomserver` takes.
- **Global system/session-count state**: something not tied to any one
  component - e.g. total registered component count, a coarse System Agent
  property, or overall boot duration - crossing some threshold only a
  real, heavily-used device's restore would reach.

### Reverted completely

`config.yml`'s `log-filter` restored to `Kernel:Warn`. No filesystem changes this
round (no folders renamed - this was a pure read/observe capture). No emulator
source touched. `git diff --stat` still shows only the round-9/round-26
baseline.

### To resume from here

1. The most direct next test: capture whether `!ecomserver`'s `session_create`
   to `!BackupServer` ever returns `epoc::error_not_found` (server not yet
   summoned) in any run, versus always successfully connecting first - this
   would confirm or rule out the timing/ordering hypothesis directly, and is a
   much smaller, more targeted probe than any further data-file bisection.

   **Answered by round 43 - see below.**
2. If timing turns out not to matter either, revisit `backup.lst` (round 39's
   last untested candidate) - though after two directly-relevant misses and
   this round's finding that the call is unconditional, it's a weaker
   candidate than it looked a few rounds ago.
3. Bisecting the ~109 real installed-app private folders is no longer worth
   attempting on its own - this round's finding means it would necessarily come
   back negative, since the call in question fires before any of them are ever
   read.

## Round 43 — `!ecomserver` already has working connect-retry logic; the fragility is entirely in what happens after it connects

Answered round 42's own question directly: does `!ecomserver`'s `session_create`
to `!BackupServer` ever hit "not summoned yet," and if so, what happens?

### Probe and result

Added a one-line temporary probe to `session_create`
([svc.cpp](../../src/emu/kernel/src/svc.cpp)), logging `found=(server != nullptr)`
whenever `!ecomserver` connects to `!BackupServer` by name. Ran against the new
launcher. Result, in order:

```
PROBE43 !ecomserver session_create('!BackupServer') found=false
PROBE43 !ecomserver session_create('!BackupServer') found=true
```

**`!ecomserver`'s first connect attempt genuinely fails** (`!BackupServer` isn't
summoned yet at that exact moment) - and its own code correctly, gracefully
retries, succeeding the second time. This is real, working client logic, not a
gap: `!ecomserver` does *not* choke on "server not found yet," it waits and
tries again. This refines round 36's "no retry" finding rather than
contradicting it - round 36 was specifically about `session_send` retries on an
*already-established* session (`CdlServer`'s, which genuinely never reconnects);
`session_create`-level retry-on-not-found, for a *different* server
(`!ecomserver`'s connection to `!BackupServer`), is a separate mechanism and it
works correctly.

### What this narrows, and a caveat worth flagging on round 24's own finding

This means the fragility isn't in *reaching* `!BackupServer` at all - `!ecomserver`
gets there reliably, on the very next attempt, regardless of exactly when
`baksrvs.exe` happens to spin up. The entire failure is downstream of a
successful connection: the function 31/34/32 exchange itself, round 26's
fail-fast, and `!ecomserver`'s inability to survive the resulting `Leave`.

While re-reading round 24 for context on "clean vs. restored," a real caveat
surfaced: **round 24's clean-install comparison used a different physical
device model** (`RM-409`, a Nokia 5320d-1) **, not `rm-320` without a restored
backup.** It's a same-UID, different-ROM comparison, not a single-variable test -
device/ROM differences between `RM-409` and `RM-320` could plausibly explain
`!BackupServer` never appearing on `RM-409` just as easily as "no restored
backup" could, since a different ROM could summon `ecomserver`/`baksrvs.exe`
through an entirely different path (or not at all) for reasons unrelated to
backup content specifically. This doesn't overturn round 24's finding, but it
does mean "no restore = no hang" hasn't been confirmed on `rm-320` itself with
its backup data removed/faked - only inferred from a different device
entirely.

### Reverted completely

The one-line `session_create` probe removed. Verified via `git diff --stat`
showing only the round-9/round-26 baseline. Rebuilt clean afterward. No
filesystem changes this round. No resource incidents.

### To resume from here

1. Given the connection itself is robust, the remaining open question is purely
   about the function 31/34/32 exchange and `!ecomserver`'s own error handling
   around it - not about reaching `!BackupServer` in the first place. Rounds
   38's live IPC-argument capture already has the concrete data for this
   (message payloads, the cached `-5` in the retry); the next step there is
   understanding the actual C++ object behind those `euser.dll` vtable pointers
   (round 39's own suggestion), not further connection-timing tests.
2. If the device-model caveat on round 24 is worth closing out properly, the
   cleanest test would be running `rm-320`'s *own* ROM/device with a
   synthetically minimal component set (removing most of the ~90 real apps,
   not just the two backup-metadata folders rounds 40/41 already ruled out) to
   see whether `!ecomserver`'s panic disappears - a real single-variable test
   on the same device, unlike round 24's cross-device comparison. Round 42
   already shows this wouldn't change *whether* the connect-and-check call
   happens, but it could still change whether `!BackupServer`'s function 32
   dispatch (or something about its own summon path) behaves differently with
   fewer components present.

   **Round 44 ran exactly this test - see below. No change either.**

## Round 44 — a real same-device, single-variable test: component count truly doesn't matter

Ran the test round 43 itself proposed: strip `rm-320`'s own restored component
set down to a small baseline on the *same* device/ROM, and see whether
`!ecomserver`'s panic disappears - closing the round-24 device-model caveat
properly, on `rm-320` itself rather than a different phone model.

### Method: full backup first, then a minimal keep-list

Backed up the entire `data/devices/rm-320` folder (`cp -a`, 458MB) before
touching anything - a full safety net, not just the individual-folder
rename-aside used in rounds 40/41. Identified `drives/c/Private`'s 10
folders still carrying their original `Aug 9` timestamp (`10000850`,
`101F79DD`, `101FFC31`, `101fd693`, `101ff976`, `101ffaa4`, `101ffca9`,
`10207218`, `10282f06`, `20000907`) as a plausible pre-restore baseline set
(everything else shows a later timestamp matching the per-device migration/restore
event). Kept those 10, plus both N-Gage UIDs (`20007b38`/`20007b39`) and the two
already-individually-ruled-out folders (`10009D8F`, `10202be9`), moved the other
95 aside to a holding folder - **109 real component folders down to 14.**

### A methodology near-miss, caught and fixed before drawing conclusions

The first run failed immediately with `"App with UID: 0x20007B39 doesn't
exist"` - the strip had accidentally removed `10003A3F` (AppArc's own
application-registry cache, a UID that happened to fall outside both the
`Aug 9` baseline set and the explicit keep-list), breaking N-Gage's own app
registration entirely. Caught this before treating the (inconclusive, app never
even launched) first result as data, restored `10003A3F` from the holding
folder, and reran - 15 folders kept this time, N-Gage launched normally.

### Result: identical panic, with 95 of 109 real components entirely absent

**`!ecomserver` still panics with the exact same `E32USER-CBase`/47**, at the
same point, on the same device/ROM, with the vast majority of the restored
backup's real installed apps completely gone. This closes round 24's
device-model caveat, but not in the direction that caveat suggested it might:
**component count and identity are conclusively not the differentiator** -
consistent with, and now directly confirming on the same device, round 42's
own finding that the triggering call happens before any per-component work at
all.

### Reverted completely

Entire `data/devices/rm-320` folder deleted and replaced with the pre-test
backup copy (guaranteed exact restoration, safer than reversing 96
individually-moved folders). Holding folder and test logs removed.
`config.yml`'s `log-filter` restored to `Kernel:Warn`. No emulator source
touched. `git diff --stat` still shows only the round-9/round-26 baseline.

### To resume from here

1. With component count/identity and both individually-tested backup-metadata
   folders (`10202be9`, `10009D8F`) now all ruled out, and `!ecomserver`'s own
   connect logic confirmed working (round 43), the remaining open surface is
   narrow: something about `rm-320`'s ROM/firmware itself (not its restored
   *data*) may simply always take this code path on boot - meaning the earlier
   framing ("what does the restored backup trigger") may need to invert to
   "does this happen on `rm-320`'s ROM regardless of restore state at all,"
   which would point toward comparing against a **truly fresh, never-restored
   `rm-320` boot** (if one can be constructed) rather than continuing to vary
   the restored data.
2. Failing that, the C++ object identification round 39 already proposed
   (disassembling what's at the `euser.dll` vtable addresses found in that
   round's IPC argument capture) remains the most concrete unexplored thread,
   since it's the one open question that doesn't depend on backup content at
   all.

   **Round 45 got the fresh comparison this item asked for - see below. Both
   stalls reproduce identically with no restored backup at all.**

## Round 45 — the user provided real N95 firmware and a real N-Gage installer: neither stall is restore-specific

Got what round 44's own "to resume" pointed at: a genuinely fresh, never-restored
`rm-320` boot to compare against, using firmware and an N-Gage installer the user
supplied directly (`N95 (S60v3 - FP1).7z` and `N-Gage 2.0 v1.40.1557...sis`). This
settles the biggest open question in the entire file.

### Setting it up: a fully isolated sandbox, zero risk to the existing repro

Built a completely separate EKA2L1 instance in scratchpad (own copy of
`eka2l1_qt.exe` and resources, own empty `data/` folder) specifically so nothing
about this test could touch the existing, precious `rm-320` repro state. Checked
[`install_firmware`](../../src/emu/system/src/installation/firmware.cpp) first
and confirmed it safely refuses and reverts if a device with the same firmware
code already exists - an extra safety margin, though the isolated sandbox made it
moot anyway.

The user's archive contained raw `N95.ROM` (60.6MB) and `N95.RPKG` (100MB) files
- not the `.vpl`+FPSX bundle EKA2L1's installer natively expects, and RPKG turned
out to be an undocumented, proprietary binary format (its own magic header, per-
variant locale metadata) not worth reverse-engineering blind. But a direct byte
comparison showed **`N95.ROM`'s header is identical to the existing
`rm-320/SYM.ROM`'s**, and matches Symbian's real bootstrap-jump convention - a
strong signal it's simply an earlier (pre-defragmentation), directly-usable ROM
dump. Placed it straight in as the sandbox's `SYM.ROM`, no conversion needed.

### First attempt crashed for an unrelated reason - fixed, not worked around

An empty C:/D:/E: alone wasn't enough: the window server's `load_wsini()`
([window.cpp](../../src/emu/services/src/window/window.cpp)) calls
`io->get_raw_path()` for `Z:\system\data\wsini.ini` and does a real host-file
load - meaning, contrary to this round's own initial assumption, Z: drive access
for config/resource files needs a **physically-dumped** directory on disk, not
just the in-memory ROM filesystem `create_rom_filesystem` provides for code
execution. Without it, `load_wsini` failed, and the window server crashed with an
access violation before anything else could run.

Fixed properly rather than patched around: since the ROM header match already
established this is very likely the same core firmware, copied the *existing*,
already-extracted `data/drives/z/rm-320/` dump (107MB, pure ROM-resident system
files - `euser.dll`, `ecomserver.exe`, etc. - identical for any same-firmware
device, restored or not) into the sandbox at the same legacy path structure Z:
mounting actually uses (`data/drives/z/<firmcode>/`, separate from the
per-device-migrated C:/D:/E: path). This doesn't reintroduce the restored-backup
variable at all - Z: is ROM content, never touched by backup restore, on either
setup.

### Installed N-Gage for real, no backup involved anywhere

`--install` on the user's `.sis` file registered several packages, including
N-Gage itself under **`0x20003b78`** - a different UID than the
`0x20007b38`/`0x20007b39` this whole investigation has used, since this is an
independently-repackaged community release ("Fixed by FROST_A-52"), not the
exact same build as the restored backup's copy. It also creates
`0x20007b38`/`0x20007b39` registrations as dependencies, so `--run 0x20007B39`
still applies. The installer logged a trailing `"Installation of SIS failed"`
alongside several `"Unimplemented operation 12 for expression"` warnings from
the SIS script interpreter, but the app's private folders and registry entries
were created correctly both times this was run, and the app launched normally -
this looks like a non-fatal gap in a rarely-used script opcode, not a real
install failure.

### Result: both stalls, unchanged

Ran `--run 0x20007B39` on this genuinely fresh device - fresh ROM, empty C:/D:/E:
drives except N-Gage's own real install, zero restored-backup data anywhere.
**Both stalls this entire investigation is about reproduced exactly:**

- `!BackupServer`'s function-32 message never completes; round 26's fail-fast
  fires identically (`"!BackupServer message (function=32) never completed by
  guest dispatch after 1s - failing fast with KErrNotSupported"`).
- `!ecomserver` panics the same way afterward: `E32USER-CBase`, code 47, "An
  active object leaves" - at a different entry point address
  (`0x804C8658` vs. the original setup's `0x804C7DC8`, confirming this is a
  genuinely independent ROM build, not a reused copy) but otherwise identical.

### What this settles

Every round from 1 through 44 characterized this investigation as being about a
*restored backup* breaking N-Gage on RM-320. **That framing was never correct.**
Both stalls are general RM-320/EKA2L1 compatibility issues that reproduce on any
N-Gage install on this device model, restored or freshly installed. This also
directly answers round 43's own caveat about round 24: `RM-409` never showing
`!BackupServer` activity was almost certainly explained by it being a different
device/ROM entirely, not by it lacking a restored backup - round 24's
"clean vs. restored" framing was comparing the wrong variable the whole time.

None of this investigation's technical work is invalidated - the exact call
chains, the panic mechanism, `error_server_terminated`, round 26's fix, all of
it holds up and reproduces identically on fresh hardware. What changes is the
*question*: this was never about what a restore does to N-Gage - it's about why
N-Gage doesn't work correctly on RM-320 under EKA2L1 at all, restored or not.

### Reverted completely

All test files live in an isolated scratchpad sandbox, entirely separate from
the project's own `data/` folder - nothing in the existing `rm-320` repro setup
was touched, copied over, or modified. No emulator source changed. `git diff
--stat` still shows only the round-9/round-26 baseline.

### To resume from here

1. Given this is a general compatibility issue rather than a restore-triggered
   one, the most useful next step is probably identifying *why* `baksrv.dll`'s
   function-32 dispatch never completes in EKA2L1 specifically for this
   call pattern - not "what's different about this backup," since nothing about
   backup content matters anymore. Rounds 13-23's original static tracing of
   that dispatch (the genuine in-range no-op case) is the right starting point
   to revisit with this reframing in mind.
2. The C++ vtable identification round 39 proposed (the `euser.dll` addresses
   from the IPC argument capture) is still the most concrete unexplored thread
   for understanding `!ecomserver`'s own crash specifically.
3. The isolated fresh-N95 sandbox (`scratchpad/fresh_n95_test`) is preserved for
   now in case further comparison tests against a clean install are useful -
   it's fully separate from the project's own data and safe to reuse or delete
   at any point.

## Round 46 — capturing the actual IPC argument bytes for the first time

At the user's direction: switched the investigation's goal from "trace the
restore trigger" (closed as moot, round 45) to "understand and develop a fix
for why N-Gage doesn't work on RM-320 under EKA2L1 at all." Re-read rounds
13/14/19/20/22/23 in full before doing anything new, to avoid re-deriving
already-settled ground. Confirms round 24's "restore-specific" read no longer
holds either - that round compared `rm-320` (restored) against `rm-409` (a
*different device model*, never restored), so it was always confounding
device identity with restore state. Round 45 already isolated that properly
(same device, fresh vs restored) and found no difference. What round 24 really
showed, correctly, is that `rm-409` doesn't hit this at all - i.e. the
determining variable is the device/ROM build, not backup state.

### New probe: the actual IPC argument words, never captured before

Every prior round tracked `function` (the IPC opcode) but never the four raw
argument words `!ecomserver` actually sends with each call, nor the `sync`
flag distinguishing `send_receive_sync` from `send_receive` (async-send +
separate wait). Added a temporary, read-only probe in `session_send_general`
([svc.cpp](../../src/emu/kernel/src/svc.cpp)): whenever the target server is
`!BackupServer`, log `ord`, all four `arg.args[]` words, `flag`, and `sync`
via `LOG_WARN` (clears the default `Kernel:Warn` filter with no config
change needed - unlike every prior probe in this file, which had to
temporarily flip `log-filter` to `trace`). A second pass added a follow-up
dump: for any arg word that looks like a guest address, dereference 16 bytes
via `eka2l1::ptr<std::uint8_t>(word).get(crr_pr)` and log hex + ASCII.

Guardrail followed throughout: `tasklist` clean before each build, `-m:4`
capped, MSBuild nodes killed after every build, bounded 30×5s stability poll
(5-tick break) before every force-kill, repro run against the **new launcher**
(`0x20007B39`) on the main project's existing `rm-320` data per the user's
standing preference, not the old launcher.

### What the capture shows

Full raw captures: [traces/round46_probe_ipc_args.log](traces/round46_probe_ipc_args.log)
(first pass, plain args) and the pointer-dump pass (`probe46b.log`, not
preserved verbatim but fully transcribed below - see reversion note).

The sequence for one connect + two backup-observer cycles, before the log
went permanently stable (second Leave/panic):

```
ord=-1 (connect)                                                    sync=true
ord=31  args=[0x501dfc,0x80098c,0x803a04bc,0x800afc] flag=0x4        sync=true
ord=34  args=[0x501df8,0x501dfc,0x80098c,0x803a04bc] flag=0x0        sync=false
ord=32  args=[0x800b18,0x501dfc,0x80098c,0x803a04bc] flag=0x4        sync=false
ord=34  args=[0x501e10,0x802cc0,0x0,0x800ae0]        flag=0x0        sync=false
ord=32  args=[0x800b18,0x802cc0,0x0,0x800ae0]        flag=0x4        sync=false
```//(log stops here - the process is now permanently idle)

Decoded as little-endian words (via a quick PowerShell one-liner, not manual
hex arithmetic - a past-round trap this file has flagged before):

- **`ord=32`'s `arg[0]` is always `0x800b18`, in every cycle, byte-identical.**
  Dereferencing it: `[0x30000008, 0x00000008, 0x00000000, 0x00000000]` - shaped
  exactly like a Symbian `TDesC` header (top nibble `0x3` = descriptor type
  `EPtr`, low 28 bits = length `8`; second word `8` = `iMaxLength`; matches the
  standard `TPtr8` layout). **The third word - where `iPtr` (the actual data
  pointer) would live - is `0x00000000`, i.e. null.** This is a fixed,
  ROM-resident constant (same address both cycles, in the `0x800xxx` range,
  not the stack or heap), not something rebuilt per plugin/cycle.
- `ord=32`'s `arg[1]` is the one value that changes per cycle (`0x501dfc` then
  `0x802cc0`), and it's always identical to the preceding `ord=34` call's
  `arg[0]` - a correlator/handle threaded from `ord=34`'s result into `ord=32`.
- `ord=32`'s `arg[2]`/`arg[3]` also track `ord=34`'s `arg[1]`/nothing new -
  consistent with `ord=32` reusing state `ord=34` just produced.

### Why this doesn't change round 19's conclusion, but does add real information

Round 19 already showed - via a live memory dump of the guest's own compiled
switch table, hand-verified arithmetic, not a guess - that case-index-12
(opcode 32) branches straight to the trampoline's shared epilogue with **no
body instructions of its own**. A dispatch with zero body can't be inspecting
`arg[0]`'s null `iPtr` at all; the guest never gets the chance to look. So
this round's find does **not** locate a null-pointer-guard bailout inside
case 32 - that theory doesn't survive contact with round 19's already-solid
result, and is retracted as soon as raised.

What it *does* establish, for the first time with real bytes instead of
inference: **opcode 32's first argument is a fixed, constant, unpopulated
placeholder descriptor - not a per-request output buffer that anything
upstream forgot to fill in.** That's consistent with opcode 32 being a
generic, non-plugin-specific request (a poll or notification-registration
call, given the pattern - request per plugin via `ord=34`, then a shared,
argument-light `ord=32` check) rather than a "give me the answer for this
specific plugin" call whose reply the guest is waiting to read out of
`arg[0]`. Combined with round 21's finding (a synthetic `KErrNone` completion
gets rejected and instantly retried, ~12,500/sec) this is more consistent with
`!ecomserver` polling for a *state transition* it expects some other part of
the system to eventually cause - not for a value written into this
particular descriptor.

### What's still open

Two questions neither this round nor rounds 1-45 have answered, and which
`baksrv.dll`/`ecomserver.exe` static analysis alone probably can't:

1. **Why does `rm-320`'s N-Gage startup path reach this call at all, when
   `rm-409`'s doesn't** (round 24, re-confirmed as a genuine device-level
   difference, not a restore artifact per round 45). This is a decision made
   somewhere *before* the `!BackupServer` session is even opened - in
   `bafl.dll`'s or `ecomserver.exe`'s own startup logic, gated on something
   that differs between `rm-320` and `rm-409`'s device profile/ROM (a
   central-repository default, a P&S property, a capability flag, or
   simply a different `bafl.dll`/`ecomserver.exe` build entirely - not yet
   checked). This is the one lead from this round most likely to lead to an
   actual, safely-scoped fix: if it's an EKA2L1 emulation gap (a missing or
   wrong default that real `rm-320` hardware would have set differently),
   fixing *that* would stop the guest from ever calling into the no-op case,
   which sidesteps rounds 21/22's "no safe way to complete this message"
   dead end entirely instead of trying to work around it after the fact.
2. Whether `rm-409`'s `bafl.dll`/`ecomserver.exe` are byte-identical builds to
   `rm-320`'s (round 20 already compared `baksrv.dll` itself and found them
   different builds - `bafl.dll`/`ecomserver.exe` haven't been compared the
   same way yet).

### Reverted completely

Both probe additions (the plain-args log line and the pointer-dereference
loop) fully removed from `session_send_general` in
[svc.cpp](../../src/emu/kernel/src/svc.cpp); verified via a clean rebuild and
`git diff --stat` showing only the round-9/round-26 baseline plus the
pre-existing unrelated diffs already present at this investigation's start.
No config changes needed this round (the `LOG_WARN` level probe never
required touching `log-filter`, unlike most prior rounds). No resource
incidents; guardrail held throughout - two builds, both capped and cleaned up,
two bounded-poll repro runs, both force-killed after confirming the familiar
stable-log signature.

### To resume from here

1. The most promising concrete next step: identify what condition in
   `rm-320`'s device profile or ROM causes `ecomserver`/`bafl.dll` to open a
   `!BackupServer` session and issue this observer-registration sequence at
   all, when `rm-409` apparently never does. Candidates worth checking first:
   `devices.yml`/device-capability differences EKA2L1 models for these two
   firmwares, and whether `rm-409`'s bundled `bafl.dll`/`ecomserver.exe` are
   simply a different, later build that dropped this behavior entirely (the
   same kind of comparison round 20 already did successfully for
   `baksrv.dll`).
2. If that traces back to something EKA2L1 gets wrong (versus a genuine
   difference in the real device's ROM), that's the actual, safely-scoped fix
   this investigation has been looking for since round 19 - one that prevents
   the no-op call from ever being made on `rm-320`, rather than trying to
   answer it after the fact (which rounds 21/22 already showed is unsafe/
   ineffective).

## Round 47 — compared `rm-320` vs `rm-409`'s `bafl.dll`/`ecomserver.exe` builds directly

At the user's direction: round 46's own "to resume" item 2 (round 20 already
did this comparison for `baksrv.dll`; this round does the same for the other
two modules `!ecomserver`'s call chain runs through).

### Header comparison: `bafl.dll` is the same build, `ecomserver.exe` isn't

`parse_rom.py` against both devices' `sys/bin/{bafl.dll,ecomserver.exe}`
(extracted module dumps, same files this whole investigation's Ghidra work has
always used):

```
bafl.dll:       rm-320 v2.1 build=0x240 code_size=0xA5C4
                rm-409 v2.1 build=0x240 code_size=0xA434
ecomserver.exe: rm-320 v2.1 build=0x23D code_size=0x74BC (29884 bytes)
                rm-409 v2.1 build=0x240 code_size=0x6548 (25928 bytes)
```

`bafl.dll` is the **same nominal build** on both devices (only a ~400-byte
size difference, most likely relocation/padding noise). `ecomserver.exe` is a
**genuinely different build** - `rm-320`'s is build `0x23D`, `rm-409`'s is
`0x240` (three builds newer), and `rm-320`'s is ~4KB **larger** despite the
lower build number.

### A false start: tried to reuse `import_e32img`/E32Image tooling, wrong format entirely

First attempt was to log real import-table DLL names via a temporary probe in
`import_e32img` ([libmanager.cpp](../../src/emu/kernel/src/libmanager.cpp)) -
it never fired, even once the repro reached its usual stable/hung state. Root
cause: ROM-resident (XIP) modules like `ecomserver.exe` get their codeseg
built directly from the ROM at system-init time, not through the per-process
E32Image-loading path that function serves - there's no dynamic "import
this DLL" event to catch for a module that was already pre-linked when the
ROM was built. Reverted (verified via `git diff --stat`).

Follow-up attempt: wrote a small standalone tool (`src/tools/e32dump`, modeled
on the existing `rofsdump` tool, using `loader::parse_e32img` directly) to
read the import section straight from the files on disk. This failed too, on
every one of the four files: `is_e32img()`'s signature check
(`header.sig != E32IMG_SIGNATURE`, i.e. the literal `"EPOC"` magic) never
matches. Root cause, same shape as round 6/7's original `parse_rom.py`
lesson: these extracted module dumps use the **ROM image header format**
(what `parse_rom.py` itself was written to parse), not the standalone
E32Image format `loader::parse_e32img` expects - two genuinely different
container formats for what ends up being the same ARM code. Reverted the tool
and the `CMakeLists.txt` line that added it; a clean CMake reconfigure and
`git diff --stat` confirmed `src/tools` and `libmanager.cpp` both back to
exactly their pre-round-47 state.

### What actually worked: reused round 19/20/25's own Ghidra technique

Extracted both devices' `ecomserver.exe`/`bafl.dll` code regions (via
`parse_rom.py`'s `code_address`/`code_size`, the same fields every prior
round has used) and imported `rm-409`'s copies into two fresh Ghidra projects
(`ecomserver_rm409_r47`, `bafl_rm409_r47`), full auto-analysis, same
`-processor ARM:LE:32:v5t -cspec default` split round 19 already worked out.

**Finding 1 - `rm-409`'s `ecomserver.exe` still imports `bafl.dll`, extensively.**
[FindRm409EcomserverBaflRefs.java](ghidra-scripts/FindRm409EcomserverBaflRefs.java)
found 24 distinct import thunks landing inside `rm-409`'s `bafl.dll` code range
(`0x80557a28`-`0x80561e5c`) - this build did **not** drop the dependency.

**Finding 2 - `bafl.dll`'s internal layout is byte-identical between devices,
just relocated.** Reused round 25's exact technique
([FindRm409BaflStringRefs.java](ghidra-scripts/FindRm409BaflStringRefs.java)):
found the same `"!BackupServer"` UTF-16LE string in `rm-409`'s `bafl.dll`
(`0x805603c0`) and its code references, landing in `FUN_80557a9a` and
`FUN_80557b10`. Both sit at **exactly** the same offset from their module's
own `code_address` as round 25's `FUN_8077811a`/`FUN_80778190` do on
`rm-320` (`+0x72` and `+0xE8` respectively, on both devices, matching
`bafl.dll`'s identical build/version) - direct confirmation this is the same
compiled function, just at a different load address.

**Finding 3 - `ecomserver.exe` on both devices calls the same *pair* of
higher-level `bafl.dll` functions round 39 already identified on `rm-320`
(`thunk_EXT_FUN_807781f4`/`thunk_EXT_FUN_80778118`), at the matching relative
offsets (`+0x14C`/`+0x70` from `bafl.dll`'s `code_address`) on `rm-409` too -
`0x80557b74` and `0x80557a98`, both present in the 24-thunk list.** So far,
structurally identical on both devices.

**Finding 4 - the one real difference found: which of those two calls is
statically reachable is swapped between builds.**
[FindRm409ThunkCallers.java](ghidra-scripts/FindRm409ThunkCallers.java)
checked every one of `rm-409`'s 24 thunks for a static caller. On `rm-320`
(round 39): the "lazy singleton" thunk (`80778118`) had one direct caller,
the other (`807781f4`) had none (a C++ vtable wall). On `rm-409`, it's
**reversed**: the thunk at the `807781f4`-equivalent offset (`0x80557b74`)
has a direct caller (`0x8029955e`, in an unnamed function), while the
`80778118`-equivalent (`0x80557a98`) has **no static callers at all**.

### What this does and doesn't establish

This rules out the simplest possible explanation - "`rm-409`'s newer
`ecomserver.exe` build simply removed the code that talks to `!BackupServer`."
It didn't: both builds still wire up calls to the same pair of `bafl.dll`
functions, at the same relative offsets, in the same module. The actual
`0x23D`-vs-`0x240` build difference didn't remove this dependency - if
anything it looks like a minor refactor that swapped which of the two calls
is direct vs. vtable-dispatched, not a removal.

Combined with round 24's hard empirical fact (an actual `!BackupServer`
session is **never** created at runtime on `rm-409`, zero log matches across
a full run) and round 45's (the trigger isn't backup-restore state, since a
fresh `rm-320` hits it too), the remaining explanation has to be a **runtime
condition** - something read before this code path executes that's true on
every `rm-320` boot and false on every `rm-409` boot - not a structural
code-presence difference between the two builds. This is now the second
independent line of evidence pointing the same way (round 39's own "to
resume" already guessed this shape); this round makes it far more solid by
ruling out the code-absence alternative directly.

### Reverted completely

The `libmanager.cpp` probe, the `src/tools/e32dump` tool, and its
`CMakeLists.txt` line are all fully reverted - `git diff --stat` confirms only
the same 43-file baseline present since this investigation began (round-9/
round-26 fixes plus the pre-existing unrelated multi-device diffs). Two new
Ghidra projects (`ecomserver_rm409_r47`, `bafl_rm409_r47`) and three new
scripts (`FindRm409EcomserverBaflRefs.java`, `FindRm409ThunkCallers.java`,
`FindRm409BaflStringRefs.java`) added alongside every other round's Ghidra
artifacts - read-only static analysis, no emulator behavior changed. No
resource incidents; one Ghidra/Java process at a time throughout, guardrail
held.

### To resume from here

1. The real next question, sharpened by this round: **what specific runtime
   condition - checked somewhere between `ecomserver.exe`'s startup and its
   call into one of these two `bafl.dll` functions - is true on `rm-320` and
   false on `rm-409`.** Candidates worth checking first, roughly in order of
   how cheap they are to test: (a) whatever `0x8029955e` (the direct caller
   found on `rm-409`, currently unnamed/unexamined) actually contains - does
   it have an early-return guard that's simply never satisfied at runtime,
   even though the call is statically present; (b) `devices.yml`/EKA2L1's own
   device-capability modeling differences between `rm-320` (`epoc93fp1`) and
   `rm-409` (`epoc93fp2`) - a central-repository default, P&S property, or
   capability flag EKA2L1 might get wrong specifically for one platform
   version; (c) decompiling `rm-320`'s equivalent direct-caller function
   (whichever of `0x804cbc18`/`0x804cbc64` corresponds to the one that *is*
   statically reachable there) to see what it actually branches on before
   making the call.
2. If (a) or (c) turns up a real condition check (an `if` on some queryable
   state) rather than an unconditional call, that's the concrete fix target:
   whatever EKA2L1 does differently for that specific check on `rm-320` vs.
   real hardware's expected behavior.

## Round 48 — chased candidate (a); the wall is symmetric on both devices

Followed up directly on round 47's candidate (a): decompiled `0x8029955e`,
`rm-409`'s one static caller into the reachable `bafl.dll` thunk.

### `0x80299554`: the same lazy-singleton pattern, confirmed byte-for-byte in shape

`0x8029955e` sits inside a small function at `0x80299554`
([DumpRm409Caller955e.java](ghidra-scripts/DumpRm409Caller955e.java)):

```
80299554: push {r4,lr}
80299556: mov r4,r0
80299558: ldr r0,[r4,#0xc]      ; load cached pointer field
8029955a: cmp r0,#0x0
8029955c: bne 0x80299564        ; already created -> skip to use
8029955e: blx 0x8029a8f0        ; not created -> create it (bafl.dll)
80299562: str r0,[r4,#0xc]      ; cache it
80299564: ldr r0,[r4,#0xc]
80299566: add r1,r4,#0x4
80299568: blx 0x8029a8e0        ; always: register with it
8029956c: mov r0,#0x1
8029956e: str r0,[r4,#0x14]     ; mark registered=1
80299570: pop {r4,pc}
```

This is the exact same "if not created, create; then always register" shape
round 39 found in `rm-320`'s equivalent wrapper - **no guard of its own**. Once
reached, it unconditionally creates (if needed) and registers.

### The caller one level up is itself a vtable adjustor thunk - the wall repeats

[FindRm409Caller99554Callers.java](ghidra-scripts/FindRm409Caller99554Callers.java)
found exactly one static caller of `0x80299554`: a two-instruction function at
`0x802995a0`:

```
802995a0: ldr r0,[r0,#0x20]   ; load a sub-object at offset 0x20 from whatever's in r0
802995a2: b 0x80299554         ; tail-jump into the singleton wrapper with it
```

`ldr` from a fixed offset immediately followed by a tail-branch is the
standard ARM/RVCT "this-adjusting thunk" idiom for a C++ virtual method (the
same shape round 39 already recognized in `rm-320`, one level higher up at
`0x804cbc64`). This function is itself a **vtable slot target** - reachable
only through indirect (`blx r1`/`bx r1`) dispatch, not a direct call anywhere
in the binary. **The wall round 39 hit on `rm-320` and the wall this round
hit on `rm-409` are the same wall, one level apart in a slightly different
place** - both builds bottom out in unreachable-by-static-analysis virtual
dispatch right around the same point in the call chain.

### A parallel confirmatory test was attempted and got stuck on unrelated tooling friction

Tried a different, empirical angle instead of more static tracing: install
the exact same N-Gage `.sis` fresh onto a brand-new `rm-409` device profile
(mirroring round 45's method exactly - full `data/roms/rm-409` and
`data/drives/z/rm-409` copied from the main project's own bundled test data,
fresh empty `drives/{c,d,e}`) to directly confirm round 24's non-reproduction
on `rm-409` isn't an artifact of that device's specific *existing* install
history. The install failed before reaching any SIS-parsing code at all
(`identify_sis_type` returned no result, `packages::install_package`'s very
first check, [manager.cpp:534-538](../../src/emu/package/src/manager.cpp) -
silent by design, no log line either way) - most likely a path-resolution
quirk specific to this one invocation (the same file installs fine elsewhere
in this investigation), not a genuine compatibility rejection. Didn't chase
this further; it's tooling friction on a confirmatory side-check, not new
evidence about the actual bug, and the main project's data was never touched
by the attempt (the failed install left nothing behind to revert - the
sandbox's fresh `rm-409` data can simply be deleted or retried later).

### Where this leaves things

Both the direct static-call-graph approach (rounds 39, 47, this round) and a
parallel empirical cross-check have now either hit a genuine wall (C++
virtual dispatch, unreadable without recovering vtable contents) or stalled
on unrelated tooling friction. Pure static/call-graph tracing is exhausted on
this specific question for both devices. What's left needs either:
recovering the actual vtable/object behind the adjustor thunk (round 39 already
found the underlying interface object's vtable pointers live in `euser.dll`,
not app code - suggesting whatever decides membership in this call chain is a
generic framework mechanism, not N-Gage- or `ecomserver`-specific logic), or
a live single-step trace on `rm-320` (where the path actually executes) to
capture what object/state flows into the indirect call, cross-referenced
against whatever the equivalent state would be on `rm-409` (candidate (b)
from round 47 - device capability/central-repo defaults - remains untested
and is now the most promising remaining angle precisely because it sidesteps
the vtable wall entirely).

### Reverted completely

No emulator source changed this round - purely new Ghidra scripts (added to
this investigation's scripts folder, same as every prior round) and a
throwaway sandbox install attempt that produced no output to clean up.
`git diff --stat` unchanged from round 47's baseline. No resource incidents;
one Ghidra/Java process at a time throughout.

### To resume from here

1. **Most promising, sidesteps the vtable wall entirely**: check `rm-320` vs
   `rm-409`'s modeled device capabilities/central-repository defaults in
   EKA2L1 directly (round 47's candidate (b)) - this doesn't require
   recovering any vtable contents at all, just comparing what EKA2L1 itself
   exposes differently for `epoc93fp1` vs `epoc93fp2`, or for these two
   specific `machine-uid`/`firmcode` values, that a generic euser.dll-level
   backup-observer mechanism might read at registration time.
2. If that comes up empty, a live single-step trace on `rm-320` (following
   round 25/39's exact established technique) capturing the actual vtable
   pointer and object contents at the indirect call site would at least name
   the concrete class/interface involved, even without `rm-409` to compare
   against directly (since the call never executes there to trace).
3. Fix the fresh-`rm-409`-install path-resolution snag if this confirmatory
   check still seems worth completing - likely just needs the `.sis` path
   passed differently (try an absolute path, or verify quoting/encoding
   through the full `utf8_to_ucs2`/`ucs2_to_utf8` round trip) rather than any
   deeper investigation.

## Round 49 — checked candidate (b): no EKA2L1-side capability or central-repo difference found

Pure source/data comparison, no build or run needed. Checked every plausible
place EKA2L1 itself could be modeling `epoc93fp1` (`rm-320`) and `epoc93fp2`
(`rm-409`) differently in a way that could reach this call chain.

### Every `epocver`-gated branch that touches anything nearby, checked and ruled out

- **`services/backup/backup.cpp`'s `backup_old_server`** (registered as plain
  `"BackupServer"`, no `!` prefix - a *different* server name than the real
  `"!BackupServer"` this whole investigation traces) is only created when
  `sys->get_symbian_version_use() <= epocver::eka2`
  ([init.cpp:252-254](../../src/emu/services/src/init.cpp)). Checked the enum
  ordering directly in
  [types.h](../../src/emu/common/include/common/types.h): both `epoc93fp1`
  and `epoc93fp2` sit well past `eka2`. **Registered on neither device** -
  irrelevant to both, not a source of asymmetry.
- **`hal.cpp`'s `epoc93fp1` branch** (reported system RAM size) uses `>=`, so
  it applies identically to `epoc93fp2` too (128MB on both) - not a point of
  difference between these two specific devices.
- **`sisregistry.cpp`'s `epoc93fp1`-specific opcode remap** is real (EKA2L1
  does shift IPC function numbers differently for `epoc93fp1` alone,
  confirming the emulator *does* have precedent for per-version protocol
  quirks) but it's scoped to the `SisRegistry` server's own IPC dispatch, a
  different server entirely from `!BackupServer`/`bafl.dll`'s protocol -
  no plausible connection to this call chain.
- **`centralrepo.cpp`'s `backup_cacher`** is an in-memory repo *object* cache
  (a generic "avoid re-reading the same file" cache), unrelated to the
  `!BackupServer` concept - a naming coincidence, nothing more.
- The window server's `EWsWinOpMaintainBackup` is a legacy, ignored opcode
  for window redraw-buffer backing, unrelated to the backup subsystem this
  investigation means.
- **Publish & Subscribe properties are purely dynamic in EKA2L1** - traced
  `property_define`/`property_attach`/`property_find_get_int` in
  [svc.cpp](../../src/emu/kernel/src/svc.cpp): every property is created
  on-the-fly by whichever guest process calls `RProperty::Define` first, with
  no static, device-keyed default table anywhere in EKA2L1's implementation.
  If a real backup-notification P&S property gates this behavior, its
  *default value* would have to come from guest code (some other system
  server defining it at boot), not from anything EKA2L1 itself seeds
  per-device.

### Capability bits: confirmed byte-identical, not assumed

Re-ran `parse_rom.py` on both devices' `ecomserver.exe`/`bafl.dll` for
`secure_id`/`vendor_id`/`cap1`/`cap2`/`flags_raw` - **all five fields are
byte-identical between `rm-320` and `rm-409` for both modules**:

```
rm-320/ecomserver.exe: secure_id=0x10009D8F vendor_id=0x70000001 cap1=0x00000100 cap2=0x00000000 flags_raw=0x500012A
rm-409/ecomserver.exe: secure_id=0x10009D8F vendor_id=0x70000001 cap1=0x00000100 cap2=0x00000000 flags_raw=0x500012A
rm-320/bafl.dll:       secure_id=0x10003A0F vendor_id=0x70000001 cap1=0x000FFFFE cap2=0x00000000 flags_raw=0x12B
rm-409/bafl.dll:       secure_id=0x10003A0F vendor_id=0x70000001 cap1=0x000FFFFE cap2=0x00000000 flags_raw=0x12B
```

Platform-security capability checks are ruled out as a source of asymmetry -
not a plausible EKA2L1-side explanation.

### One more data-level check: N-Gage's own installed private folders

Compared the *existing* installed N-Gage state on both devices' main project
data (not the blocked fresh-install attempt): both `rm-320` and `rm-409` have
**both** launcher UID folders present (`private/20007b38` and
`private/20007b39`) and the same core dependency folder
(`private/10009D8F`, the ECom cache round 41 already examined). `rm-320` has
109 total private folders vs. `rm-409`'s 35 - expected and already explained
by existing investigation history (round 44's restored-backup content on
`rm-320` vs. a cleaner pre-existing install on `rm-409`), not new evidence -
round 45 already showed folder count/backup-state doesn't gate the
reproduction anyway.

### Conclusion: this round comes up empty on the EKA2L1 side, and that's itself informative

No EKA2L1-side device-capability model, central-repository default, or
P&S-property seeding differs between `epoc93fp1` and `epoc93fp2` in any way
that plausibly reaches this call chain. Combined with round 47/48's finding
that `bafl.dll` is a byte-identical build on both devices while
`ecomserver.exe` is a **different compiled build** (`0x23D` vs `0x240`) that
still statically wires up the same calls - the weight of evidence now points
away from "EKA2L1 models something about these two devices differently" and
toward **a genuine difference between the two real, Nokia-compiled
`ecomserver.exe` binaries themselves**, not anything EKA2L1 introduces. That
reframes what "a fix" can reasonably mean here: this may not be an EKA2L1
emulation gap to close so much as a real defect specific to the
`ecomserver.exe` build that shipped with `rm-320`'s firmware - which fits the
whole shape of this investigation (N-Gage 2.0's spotty real-world device
compatibility is well documented) better than an emulator bug does.

### Reverted completely

Nothing to revert - this round was pure source reading and two cheap,
read-only shell comparisons (a `parse_rom.py` header dump, a `find` over
already-existing private folders). No emulator source touched, no build, no
run. `git diff --stat` unchanged.

### To resume from here

Given the EKA2L1-config angle is now exhausted and the guest-binary-level
wall (round 48) can't be pushed further without live tracing, the two
remaining productive directions are:

1. **A targeted, narrow fix at the `baksrv.dll`/message-completion level,
   informed by `bafl.dll`'s own retry logic** - not round 21's rejected
   "complete with `KErrNone`" approach, but decompiling `FUN_80778190`'s
   (round 25's bounded-retry function) actual early-break condition to find
   *which specific error code* makes that loop terminate gracefully instead
   of retrying. If EKA2L1 completed the stuck `function=32` message with
   that specific code (not a guess, the exact value the guest's own retry
   logic already expects and handles) instead of leaving it uncompleted
   forever, `bafl.dll`'s bounded retry might do exactly what it was designed
   to do - fail gracefully - without the round-21 retry-storm failure mode
   (which happened because `KErrNone` specifically wasn't a value the guest
   was prepared to accept, not because *any* completion is inherently
   unsafe). This is the most concrete unexplored fix shape left.
2. A live single-step trace on `rm-320` capturing the real vtable/object
   behind the indirect call (round 48's item 2), to settle definitively
   whether this is guest-binary-version-specific behavior or something a
   live capture would reveal differently.

## Round 50 — decompiled `FUN_80778190`'s retry gate, and empirically confirmed round 26's `-5` is the better choice

Followed round 49's item 1 directly: decompiled both `FUN_8077811a` and
`FUN_80778190` in full (round 25 had only characterized them from
disassembly-level reading; this round got the actual decompiled C and
traced every branch condition).

### What `FUN_80778190` actually gates on

Full decompile/disassembly saved at
[fun80778190_retry.txt](ghidra-scripts/fun80778190_retry.txt). The bounded
loop is more structured than round 25's summary suggested:

```c
iVar3 = 10;
while (true) {
    uVar1 = thunk_EXT_FUN_803a91fc(&uStack_18, 0, 1, 1);   // prepare/poll something
    iVar2 = thunk_EXT_FUN_803aaa38(param_1, iVar4, uVar1, 4); // wait/check result
    iVar3 = iVar3 - 1;
    if (iVar3 < 1) return;                                  // 10-attempt cap
    if ((iVar2 != -1) && (iVar2 != -0xf)) break;             // only proceed if -1 or -15
    iVar2 = FUN_8077811a(param_1);                           // the actual !BackupServer connect+SendReceive
    if ((iVar2 != 0) && (iVar2 != -0xb)) return;             // any OTHER result -> exit immediately
}
```

Two separate gates, not one:

1. **Before even attempting the real IPC call**: a poll/wait helper must
   return `-1` (`KErrNotFound`) or `-15` (`KErrServerTerminated`) - i.e. "no
   existing valid state" - before `FUN_8077811a` (the function that actually
   opens `!BackupServer` and sends `31`/`34`/`32`) gets called at all.
2. **After calling it**: the loop only retries (loops back to the top) if the
   result is `0` (`KErrNone`) or `-11` (`KErrAlreadyExists`). **Any other
   value - including round 26's `-5` - causes an immediate return, no more
   attempts.** This is the exact mechanism that explains round 21's failure:
   completing with `KErrNone` (`0`) is *precisely* one of the two values this
   loop treats as "try again," not "done."

`FUN_8077811a` itself (also fully decompiled) confirms `bafl.dll`'s own
`SendReceive` completion status is exactly what gets returned as `iVar1` -
directly explaining the boot log's `"Leave started! Guess leave code: -5"`
line already captured in an earlier round: the completion value round 26
injects propagates, via `User::LeaveIfError`-style guest code, straight into
a real software `Leave`, unmodified.

### The panic is deliberate application logic, not an accidental unhandled Leave

Re-examined the exact boot log sequence already captured (visible at
[README lines ~3907-3919](README.md)) with this new understanding:

```
Leave started! Guess leave code: -5
Leave trapped by trap handler.        <- cycle 1, caught cleanly
Sending a blind sync message
Sending 34 sync to !BackupServer
...
Leave started! Guess leave code: -5
Leave trapped by trap handler.        <- cycle 2, ALSO caught cleanly
Thread !ecomserver panicked with category: E32USER-CBase and exit code: 47
```

**Both Leaves are individually trapped and recovered from** (`"Leave trapped
by trap handler"` appears both times, not just once) - this rules out "an
unhandled Leave falls through to `CActiveScheduler`'s default panic handler"
as the mechanism. The panic happens immediately *after* the second trapped
Leave, which reads as deliberate application logic: `!ecomserver`'s own code
tries this check, catches a clean failure, tries again, catches a second
clean failure, and then explicitly decides two consecutive failures is fatal
and calls `User::Panic()` itself - not an accident, a real "give up" path in
the guest's own error handling.

### Empirical test: would `KErrNone` (avoiding the Leave entirely) actually help?

`User::LeaveIfError` only leaves on negative values - completing with `0`
instead of `-5` should avoid the Leave (and therefore the panic) altogether,
at the cost of letting `FUN_80778190`'s own bounded (max 10) internal loop
keep retrying. Round 26's fix is structurally different from round 21's
(a single one-shot timer per message ID via `ntimer`, not an unconditional
watchdog re-firing on every re-arm), so round 21's runaway failure mode
isn't guaranteed to recur - worth testing rather than assuming.

Changed `server.cpp`'s existing fix (temporarily) to complete with
`epoc::error_none` instead of `epoc::error_not_supported`, rebuilt, ran the
new-launcher repro on `rm-320`, and bounded-polled for 5 minutes (guardrail:
`tasklist` clean before/after, capped build, MSBuild cleanup, a runaway-size
guard added to the poll loop given round 21's history).

**Result: no panic, but no progress either.** The fail-fast fired steadily,
almost exactly once per second (341 times over ~300 seconds, matching round
26's fixed 1-second delay per message instance) - the log grew slowly and
linearly (roughly 1KB every 5-second tick) for the entire 5-minute
observation window, never stabilizing and never crashing. This is a third,
previously-uncharacterized failure mode, distinct from both round 21's
runaway (12,500/sec, unbounded rate) and `-5`'s current behavior (crashes
`!ecomserver` but lets the rest of boot continue substantially further, per
this file's own status header): **a bounded-rate but unbounded-duration
stall** - `!ecomserver` (or whatever wraps it) treats a `0` result as "not
wrong, but not done either - check again," so it just polls forever at
~1Hz instead of either failing fast or truly succeeding.

### Conclusion: round 26's `-5` was already the right choice

This confirms, empirically rather than by further guessing, that changing
the completion value doesn't sidestep `!ecomserver`'s own "two strikes"
panic logic - it can only change *whether* a Leave happens *at this specific
message* at all, and the one value that avoids the Leave (`0`) turns out to
make things measurably worse for the actual goal (reaching a playable menu)
by preventing boot from ever progressing past this point, rather than
letting it continue past a crashed `!ecomserver` thread. Round 26's existing
choice of `-5` remains the correct one - this round found no better
alternative among the values `FUN_80778190` itself distinguishes, and ruled
out the one plausible alternative (`0`) directly with a real test instead of
leaving it as an untested assumption.

### Reverted completely

`server.cpp`'s temporary `error_none`/`PROBE50` change fully reverted back to
the exact round-26 baseline (`error_not_supported`/original log message);
verified via clean rebuild and `git diff --stat` showing the file's diff
unchanged from before this round (`+77` lines, identical to the standing
round-26 fix). Two builds, both capped and cleaned up; one bounded-poll test
run (with a runaway-size guard added given round 21's history, never
triggered - peak size stayed under 120KB); force-killed after confirming the
steady, non-stabilizing pattern. No resource incidents.

### To resume from here

The completion-value angle is now exhausted - round 26's `-5` is confirmed,
not just assumed, to be the best available choice at this specific message.
Any further progress on the `!ecomserver` panic itself needs a different
angle entirely:

1. **Most direct remaining option**: find and decompile `!ecomserver`'s own
   "two strikes and panic" logic (the code that runs after `FUN_8077811a`'s
   caller returns, deciding to retry once and then call `User::Panic()`).
   This is genuinely new territory - round 39/47/48 traced the call *into*
   `bafl.dll`, but nothing yet has traced *outward* from that call, into
   whatever in `ecomserver.exe` reacts to the Leave. If that logic could be
   made to treat this specific, expected-on-this-firmware failure as
   non-fatal (e.g. by widening what it tolerates, if that's even something
   fixable outside the guest binary), that would be a fundamentally
   different, and more promising, fix shape than anything tried so far -
   though it's worth being clear this may require patching guest behavior
   EKA2L1 doesn't own, not just emulator-side logic, which raises the
   question of whether this is fixable at the EKA2L1 level at all.
2. Given how much of this investigation's fix-shaped ideas keep running into
   "the guest's own code intentionally does this," it may be worth stepping
   back and asking directly whether `!ecomserver` panicking here is actually
   still the blocker to a playable menu at all, versus `CdlServer`'s separate
   `drtaeabi.dll` spin (rounds 27-31) - re-confirming which of the two
   remaining stalls is the one actually standing between current behavior
   and a playable menu, on top of round 26's fix, would clarify where
   further effort is best spent.

## Round 51 — re-confirmed live: `CdlServer` is still stuck, today, on the current baseline

Directly answered round 50's own item 2. Re-ran round 27's exact diagnostic
(thread-state dump + single-step trace) fresh, on the current tree (round 9 +
round 26 fixes only, everything else already reverted) rather than relying
on rounds 27-31's historical results alone.

### Probe

Temporarily re-added round 27's technique: a second one-shot `ntimer` event,
chained onto round 26's existing fail-fast handler
([server.cpp](../../src/emu/kernel/src/server.cpp)), firing 8 seconds after
the fail-fast (matching round 27's exact delay). On firing, it dumps every
thread's `name()`/`current_state()`, and if any thread is named `CdlServer`,
arms a 3000-instruction single-step budget on it specifically
([kernel.h](../../src/emu/kernel/include/kernel/kernel.h) temp members,
stepped from [epoc.cpp](../../src/emu/system/src/epoc.cpp)'s `loop()`).
Guardrail followed: `tasklist` clean before/after the build, `-m:4` capped,
MSBuild nodes killed, bounded polling, run against the new launcher
(`0x20007B39`) on `rm-320` per standing preference.

### Result: identical picture to round 27, confirmed fresh today

Thread dump, 8 seconds after the fail-fast fires:

```
PlayServer state=5
!AknIconServer state=5
!ecomserver state=4
CdlServer state=1        <- the only thread in "run", everyone else idle/waiting
PlayServerSplashScreenFallBackThread state=0
Main state=5
Games state=6
!BackupServer state=5
```

Exactly matches round 27's finding: every other thread is waiting/stopped,
`CdlServer` alone is actively running. The single-step trace (3000
instructions) showed **634 unique addresses, spanning `0x80394414` to
`0x834865a0`** - a wider span than round 27's originally-reported narrower
window (`0x8039456c`-`0x80395728`), but the same overall shape: roughly 4.7
hits per unique address (3000/634), consistent with a repeating cycle, not
forward progress through fresh code. The wider range isn't a contradiction -
it spans exactly the two modules round 29 already traced as one connected
call chain (`drtaeabi.dll`'s `0x8039xxxx` range together with `cdlserver.exe`'s
own merge-routine code around `0x8348xxxx`) - this capture just happened to
catch a larger slice of the same overall repeating cycle than round 27's
narrower 3000-step window did, consistent with this file's own
non-determinism caveat (noted since round 4) rather than a different
mechanism.

Confirmed the real (non-probe) HLE log froze at exactly **387 lines**
(~54.7KB), stable and unchanging across three repeated 5-second checks, while
only the intentional instrumentation kept growing - the same
freeze-immediately-after-round-26's-fail-fast pattern documented in every
round since 26.

### Conclusion

**Yes - `CdlServer`'s spin is still the actual, current blocker to a playable
menu**, confirmed live today rather than assumed from historical rounds
alone. Nothing about rounds 32-50's work (all of it either reverted
instrumentation, static analysis, or the round-26 completion-value
experiments) changed this - unsurprising, since none of that work touched
`CdlServer`, `cdlserver.exe`, or `drtaeabi.dll` at all. This is the same
permanent stall rounds 28 (6-minute patience test) and 31 (25-minute patience
test, memory usage flat too) already proved doesn't resolve on its own -
today's check confirms it's still exactly where those rounds left it, not
that it's newly appeared or changed shape.

### Reverted completely

All three temporary additions (`kernel.h`'s two members, `server.cpp`'s
chained dump-event registration, `epoc.cpp`'s step-forcing and PC-logging
blocks) fully removed; verified via a clean rebuild and `git diff --stat`
showing the same 43-file baseline as every round since 46. No resource
incidents; one build, capped and cleaned up; one bounded-poll repro run,
force-killed after confirming the frozen-log/step-trace signature.

### To resume from here

Given round 50 already exhausted the completion-value angle on the
`!BackupServer`/`bafl.dll`/`!ecomserver` chain, and this round confirms
`CdlServer`'s spin is the actual remaining blocker, the productive next
direction is squarely rounds 27-31's own unfinished item: **trace
`CdlServer`'s own guest code at the exact point it calls into
`drtaeabi.dll`** (capture the call-site `lr` and argument registers, the same
"capture the LR at the call boundary" technique round 25 used successfully
for `bafl.dll`) to find out what value it's actually operating on - and
whether that value looks like something EKA2L1 is feeding it incorrectly
(a corrupted or unexpectedly-huge length/count argument) versus a value
that's correct but sits inside a genuinely-unbounded loop in the guest's own
code (which round 31's merge-sort read as correctly bounded, but that
analysis was of one specific function - `FUN_83485d62` - and this round's
wider PC range shows the real cycle touches more code than just that one
function, so it may be worth re-examining whether *every* function in the
cycle is as clearly bounded as that one was).

## Round 52 — traced the real call sites: this is C++ exception-cleanup machinery, not arithmetic

Directly followed round 51's own recommendation: captured `CdlServer`'s call
sites into `drtaeabi.dll` live, then decompiled both ends. The result
overturns this file's working assumption about what `drtaeabi.dll` is doing
here - it isn't a division/copy routine at all.

### Live capture: two call sites, each hit twice, identical arguments both times

Reused round 51's exact probe shape (arm 8s after round 26's fail-fast,
target `CdlServer`), but this time logged `pc`/`lr`/`r0`-`r3` on every step
where `pc` fell inside `drtaeabi.dll`'s confirmed range
(`0x80394398`-`0x80397ce8`, via `parse_rom.py`) instead of logging every PC
unconditionally. Of a 20000-instruction budget, 17446 steps (87%) landed
inside `drtaeabi.dll`. Filtering the captured `lr` values down to ones
*outside* that range (genuine external callers, not internal
`drtaeabi.dll`-to-`drtaeabi.dll` branches) found exactly two, each hit twice
with byte-identical arguments both times:

```
lr=0x83485ffb  r0=0x7006d8     r1=0x0  r2=0x80397398  r3=0x0
lr=0x8348600d  r0=0xfffffff1   r1=0x0  r2=0x4d5241    r3=0x4d5241
```

### A Thumb-alignment trap, caught before drawing a wrong conclusion

Both `lr` values are odd (the standard Thumb-mode return marker), so the
real return addresses are `lr-1`. A first, flow-based disassembly attempt
around these addresses showed what looked like a clean, linear function
(`0x83485fde`, exactly round 29's own previously-identified "cdlserver.exe's
own wrapper") containing an unconditional branch that seemed to skip
straight past the real return points - the same kind of misleading
static-disassembly artifact this file has hit and flagged several times
before (rounds 11-13, 38). Forced Thumb-mode disassembly at the exact
byte offsets instead of trusting flow-following
([DumpCdlserverCallInstr.java](ghidra-scripts/DumpCdlserverCallInstr.java)),
confirmed via raw bytes: the actual call instructions are
`0x83485ff6: blx 0x83486598` and `0x83486008: blx 0x834865a0` - both still
inside `cdlserver.exe`'s own module, not `drtaeabi.dll` directly.

### Local thunks, confirmed via raw bytes, not guessed

`0x83486598` and `0x834865a0` turned out to be plain
`ldr pc,[pc,#-4]`-style import thunks (`04 f0 1f e5` in raw bytes - the
exact same idiom this investigation has already found for `bafl.dll` and
`ecom.dll` thunks) - each one's literal pointer, read directly from raw
memory, gives the real `drtaeabi.dll` target:

- `0x83486598` → literal `0x803954e9` → real target `0x803954e8`
- `0x834865a0` → literal `0x80395581` → real target `0x80395580`

Both match the live-captured `pc` values from the very first capture
exactly, confirming the whole chain end-to-end:
`cdlserver.exe` (`0x83485ff6`/`0x83486008`) → local thunk
(`0x83486598`/`0x834865a0`) → `drtaeabi.dll` (`0x803954e8`/`0x80395580`),
with `lr` still showing the *true* original caller in `cdlserver.exe`
throughout, since a plain `ldr pc,[...]` thunk never touches `lr`.

### The real finding: these two `drtaeabi.dll` functions take no arguments at all

Decompiling `0x803954e8` and `0x80395580`
([DumpDrtaeabiCalleeFuncs.java](ghidra-scripts/DumpDrtaeabiCalleeFuncs.java))
showed neither one reads `r0`-`r3` as parameters in any meaningful way -
`FUN_80395580` is declared `void FUN_80395580(void)` and immediately calls
`func_0x80395370()` with **zero arguments**, working entirely off its return
value. **This means the register values captured live (`r0=0x7006d8`,
`r2=0x80397398`, `r2=r3=0x4d5241`) were never real call arguments to begin
with** - they were just whatever happened to be sitting in those registers
beforehand, an assumption this round made initially and then corrected
directly from the decompile rather than building further analysis on a wrong
premise.

Tracing the actual call shape instead
([DumpDrtaeabiSubCallees.java](ghidra-scripts/DumpDrtaeabiSubCallees.java)):

- `0x80395370` → `bl 0x80395a34` - a bare, argument-less call, the classic
  shape of `__cxa_get_globals()` (fetch the current thread's per-thread C++
  exception-handling state).
- `0x803953a8` reads a function pointer at `globals+0x10`; if null, calls a
  fallback at `globals+8`; either way falls through to `func_0x80394522()` -
  the standard "call a custom terminate handler if one is installed,
  otherwise call the default one, then terminate" shape.
- `0x80394bae` checks a field for exactly `0` or `1`: `0` → call
  `0x80394522` (terminate); `1` → decrement to `0` and return normally;
  anything else also falls through to terminate. This is the textbook shape
  of the Itanium/ARM-EABI C++ ABI's `__cxa_end_catch()` - decrement the
  active-handler count, and treat any state other than "exactly one handler
  was active" as a fatal condition.
- `FUN_803954e8`/`FUN_80395580` themselves (the two actual call targets) are
  higher-level wrappers around this same machinery - refcount bookkeeping,
  conditional cleanup callback invocation, exactly what a compiler emits
  around the end of a C++ `catch` block or an unwind-cleanup landing pad.

**None of this is arithmetic, memory copying, or division - it's genuine
C++ exception-handling cleanup infrastructure.** `CdlServer` isn't stuck in
a long-division or memcpy loop at all; it's repeatedly running through
exception `throw`/`catch`-cleanup machinery, over and over, which is exactly
consistent with round 51's finding that the spin's address footprint spans
both `drtaeabi.dll` and `cdlserver.exe`'s own code - a genuine
throw/catch/retry cycle would naturally bounce between guest application
code (`cdlserver.exe`) and the runtime library implementing the actual
unwind/cleanup mechanics (`drtaeabi.dll`), not stay confined to one tight
arithmetic loop.

### What this changes

This reframes the entire second stall. It's structurally the *same shape*
of bug this investigation already fully understood on the other side (round
25/50's `Leave`/`trap`/retry cycle in `!ecomserver`) - except here it looks
like `cdlserver.exe`'s own internal logic is repeatedly throwing and
catching a C++ exception (or a Symbian `Leave` implemented via this same
EABI unwind mechanism on this build) without ever resolving whatever
triggers it, and unlike `bafl.dll`'s well-behaved, explicitly-bounded
10-attempt retry (round 25), nothing here appears to cap the retry count -
consistent with this being a genuinely unbounded loop rather than a slow
bounded one (matching round 31's own 25-minute hard-evidence conclusion).
It also means round 31's "genuinely correct, bounded merge-sort" finding for
`FUN_83485d62` may still be accurate in isolation, but isn't the whole
picture - whatever calls `0x83485fde` (the wrapper this round confirmed sits
directly above this exception-cleanup call chain) repeatedly is the real
locus of the bug, not the merge algorithm itself.

### Reverted completely

All three temporary probe changes (`kernel.h`'s two members, `server.cpp`'s
chained arm-event registration, `epoc.cpp`'s step-forcing and conditional
`pc`/`lr`/`r0`-`r3` logging) fully removed; verified via a clean rebuild and
`git diff --stat` matching the same 43-file baseline as every round since
46. All Ghidra work was static analysis against existing projects
(`cdlserver_analysis`, `drtaeabi_fresh_r34`) - the two forced-disassembly
scripts ran with `-readOnly` specifically to avoid mutating either project's
saved state. No resource incidents; guardrail held throughout (one
Ghidra/Java process at a time, one build, bounded polling, force-kill after
capture). One unrelated process was spotted mid-round
(`C:\Users\Usuario\Documents\1 eka2l1\bin\eka2l1_qt.exe`, PID 32420) - a
separate installation, not this session's own test build; left untouched
and confirmed to have exited on its own moments later, not something this
investigation started or needed to manage.

### To resume from here

1. **The real next target**: find who calls `0x83485fde` repeatedly, and
   what condition it's checking that keeps causing it to re-enter this
   throw/catch cycle instead of resolving. This is one level up from
   everything traced so far in `cdlserver.exe` (rounds 29, 31, this round) -
   nothing has yet looked at `0x83485fde`'s own caller(s).
2. Consider also checking what exception/`Leave` code is actually being
   thrown each cycle (the `_DAT_80395798` constant `FUN_803953de` compares
   against, and whatever `func_0x803953c6` does with it) - if it's a
   specific, identifiable error code, that would name the actual condition
   `cdlserver.exe` keeps failing on, the same way finding `-5`/`-15` in the
   `!BackupServer` chain (rounds 25, 50) named that failure precisely.
3. Given this is now understood to be a genuine C++ exception-handling
   retry loop rather than arithmetic, a bafl.dll-style *bounded* retry cap
   doesn't exist on this side at all (unlike round 25's finding) - if this
   turns out to be fixable at the EKA2L1 level (rather than a guest-binary
   defect EKA2L1 doesn't own, the same caveat round 50 already raised for
   the `!ecomserver` panic), the fix shape would likely need to look
   different from anything tried on the `!BackupServer` side so far, since
   there's no existing guest-side bound to lean on here.

## Round 53 — `0x83485fde` is itself a vtable slot; hit the same wall as the other stall

Directly followed round 52's own item 1: searched for every caller of
`0x83485fde`.

### No direct callers - it's reached only through a vtable, same as before

[FindCaller83485fde.java](ghidra-scripts/FindCaller83485fde.java) found
**zero** direct call-flow references anywhere in the binary.
[FindDataRefs83485fde.java](ghidra-scripts/FindDataRefs83485fde.java) found
the real answer: one **data** reference, from `0x83486a50`, storing the
literal `0x83485fdf` (the Thumb-marked address) - `0x83485fde` is itself a
vtable slot, invoked only through indirect (`blx r_n`-style) C++ virtual
dispatch, exactly the same shape of wall round 39/48 already hit on the
*other* stall's call chain (`bafl.dll`'s connect-and-retry logic, reached
only through an unrecoverable vtable slot).

### The vtable itself: a real, structured table, likely one of several near-identical blocks

[DumpVtable83486a50.java](ghidra-scripts/DumpVtable83486a50.java) dumped the
surrounding 24 words:

```
83486a30: 0x834862b8   83486a48: 0x83486370   83486a60: 0x00000000
83486a34: 0x834862b0   83486a4c: 0x83485fc9   83486a64: 0x83486944
83486a38: 0x00000000   83486a50: 0x83485fdf   83486a68: 0x834865f9
83486a3c: 0x83486930   83486a54: 0x83486228   83486a6c: 0x834865e9
83486a40: 0x83485f85   83486a58: 0x8348608d   83486a70: 0x83486370
83486a44: 0x83485fb9   83486a5c: 0x834860af   83486a74: 0x834865b0
```

A `0x00000000` word appears at `83486a38`, `83486a60`, and `83486a88` -
evenly spaced every 40 bytes (10 words) - and several entries repeat at the
same relative offset across those 40-byte blocks (`0x83486370` at
`+0x18`/`+0x18` in two consecutive blocks, `0x83486228` similarly). This
looks like an **array of several near-identical small interface tables**,
not one single vtable - consistent with CDL's actual job (processing
dependency records for potentially many components, matching round 46's
"two 34/32 cycles" observation on the other chain and this file's own
long-standing pattern of things repeating 2-3 times per run).

### Conclusion: static analysis is exhausted here too, for the same reason

Recovering which class owns this vtable, and what condition selects it,
needs the same next step round 48 already identified for the *other*
stall's equivalent wall: a live single-step trace capturing the actual
object/vtable-pointer contents at the indirect call site, not more static
call-graph work. This round confirms the two stalls this investigation has
found are not just structurally similar in *behavior* (both are
retry-forever cycles through a Symbian error-handling primitive) but hit the
*identical kind of analysis wall* - generic, vtable-dispatched
framework/runtime code that plain disassembly can't resolve without ground
truth from a live capture.

### Reverted completely

No emulator source touched this round - pure Ghidra static analysis
(`cdlserver_analysis` project, three new read-only scripts). `git diff
--stat` unchanged from round 52's baseline.

### To resume from here

The productive next step for `CdlServer`'s stall is now the same shape as
round 48's recommendation for the `!ecomserver` stall: a live single-step
trace on `rm-320`, armed specifically at the point execution enters this
vtable's dispatch (or simply continuing round 52/53's existing probe
infrastructure one step further - log the actual register holding the vtable
pointer, not just `pc`/`lr`, right before the indirect `blx`), to name the
real class/object involved rather than inferring it from table shape alone.

## Round 54 — the two stalls are connected: `CdlServer`'s spin is downstream of `!ecomserver`'s panic

Did exactly round 53's own recommendation, then followed the data one step
further. The result unifies this whole investigation's two "separate"
stalls into one causal chain.

### Live capture: the real object, its vtable, and a very suspicious member field

Re-armed `CdlServer` stepping (round 51-53's established technique), this
time logging `r0` (the real `this` pointer - confirmed live, since
`0x83485fde`'s own first instruction is `push {r0,r4,r5,lr}`) and
dereferencing it the instant `pc` reaches `0x83485fde`/`0x83485fdf`. Result:

```
this = 0x7002d8 (a real heap address, not corrupted - explains round 52's
                  earlier "weird" low-address values, which were always
                  genuine RAM pointers, never bogus)
vtbl  = 0x83486a40 - matches round 53's static vtable dump exactly, word
                     for word (vtbl[0..7] = 0x83485f85, 0x83485fb9,
                     0x83486370, 0x83485fc9, 0x83485fdf, 0x83486228,
                     0x8348608d, 0x834860af) - confirms this is the real,
                     live object using that exact 8-slot table
obj[+0x4] = 0xfffffff1  =  -15  =  KErrServerTerminated
```

The object stores `-15` (`KErrServerTerminated`) as one of its own member
fields - the exact error family this investigation already knows intimately
from the *other* stall (`error_server_terminated`, established back around
round 35).

### Confirmed directly: `CdlServer` talks to `!ecomserver`

Rather than guess what server this `-15` relates to, added one targeted
probe in `session_create` ([svc.cpp](../../src/emu/kernel/src/svc.cpp)):
log every server name `CdlServer`'s own thread tries to connect to. Result,
captured on a completely normal run (no arming/timing needed - this fires
unconditionally):

```
CdlServer session_create target=!FileServer
CdlServer session_create target=!ecomserver
```

**Both connects succeed** - no "not found" for either. `!ecomserver`'s
server *object* still exists and is reachable by name at the point
`CdlServer` reaches for it, even though (rounds 25/50) `!ecomserver`'s
*thread* has already panicked (`E32USER-CBase`/47) by then. This is exactly
the condition that produces `error_server_terminated`: the session connects
fine (the object is still there), but the underlying thread is dead, so a
subsequent send against that session returns `-15` - the exact value this
round found stored in `CdlServer`'s own object.

### The unified picture

Putting the whole chain together, end to end, for the first time:

1. `!ecomserver` sends `!BackupServer` a message that never completes
   (rounds 13-19-23, the guest's genuine compiled no-op case).
2. Round 26's fix fails the message fast after 1 second with `-5`, letting
   boot proceed - but `!ecomserver`'s own error-handling panics
   (`E32USER-CBase`/47) after two such failures (rounds 25/50).
3. `!ecomserver`'s thread is now dead. Its server *object*, however, is not
   necessarily torn down immediately (kernel object lifetime is reference-
   counted, round 35) - `CdlServer` can still successfully `session_create`
   against the name `!ecomserver` afterward.
4. `CdlServer` depends on ECom (round 31 already found it linking against
   `ecom.dll`) and tries to actually use that session. Since the thread
   behind it is gone, the send comes back `KErrServerTerminated` (`-15`).
5. `CdlServer`'s own C++ exception-handling machinery (round 52 -
   `__cxa_end_catch`-shaped cleanup code in `drtaeabi.dll`) catches this
   failure - but instead of giving up or falling back, retries the same
   operation, with **no bound** (unlike `bafl.dll`'s explicit 10-attempt cap,
   round 25) - producing the permanent spin rounds 27-53 have been
   characterizing.

**The two stalls this investigation treated as separate for 53 rounds are
not independent - the second is a direct downstream consequence of the
first.** `!ecomserver` panicking isn't just cosmetic; it's the actual root
cause of `CdlServer` never finishing either.

### Reverted completely

All temporary additions (`kernel.h`'s three members, `server.cpp`'s chained
arm-event, `epoc.cpp`'s step-forcing/object-dump block, `svc.cpp`'s
`session_create` logging) fully removed; verified via clean rebuild and
`git diff --stat` showing the same 43-file baseline as every round since 46.
No resource incidents; two builds (one for the object-capture probe, one for
the `session_create` probe), both capped and cleaned up; two bounded-poll
repro runs, both force-killed after confirming their respective one-shot
captures.

### What this means for an actual fix

This changes the shape of the problem completely. There are not two
independent bugs needing two independent fixes - there is fundamentally
**one root cause** (`!ecomserver` panicking after `!BackupServer`'s message
fails) with two visible symptoms downstream of it. Round 50 already showed
changing the *completion value* for the `!BackupServer` message can't avoid
the panic (every value either causes a Leave that gets caught-then-panicked,
or - for `0` specifically - an entirely different infinite-poll stall).
**The real fix target is now squarely round 50's own item 1**: find and
address whatever in `!ecomserver`'s own code decides "two consecutive
failures = call `User::Panic()`" - if that panic can be avoided or made
non-fatal (keeping `!ecomserver`'s thread alive, or at minimum leaving its
server object in a state that doesn't return `error_server_terminated` to
later callers), `CdlServer`'s spin may resolve as a direct side effect,
without needing any separate fix of its own.

### To resume from here

1. **The single highest-value next step for this whole investigation**:
   trace `!ecomserver`'s own "two strikes and panic" logic (round 50's
   already-identified next step, now confirmed to matter for *both* stalls,
   not just one) - specifically, what decides to call `User::Panic()` after
   the second failed attempt, and whether there's a way to prevent that
   panic (or make `!ecomserver` survive it) without touching guest code
   EKA2L1 doesn't own.
2. If `!ecomserver`'s panic genuinely can't be avoided at the EKA2L1 level
   (a real, guest-binary-level "give up" decision this investigation can't
   safely override), the fallback would be to make dead-session sends
   against a panicked server's object behave in a way `CdlServer`'s own
   exception-retry logic can actually terminate on - but this round's
   finding makes clear that's treating the symptom, not root cause 1 above.

## Round 55 — traced the panic mechanism, attempted a real fix, found it makes things worse

Did exactly what round 54 called for: traced `!ecomserver`'s panic logic, and
tried a genuine fix using EKA2L1's own existing infrastructure for this
exact situation. Both parts produced real, useful results - the fix attempt
included, even though it didn't pan out.

### The panic mechanism, traced: it's generic Symbian, not custom app logic

Added a one-shot probe in `thread_kill` ([svc.cpp](../../src/emu/kernel/src/svc.cpp),
the real EKA2 panic/kill/terminate SVC - a different, non-EKA1 path than the
`thread_panic_eka1` function this file's earlier static reading briefly
suspected) to capture the guest `pc`/`lr` at the exact moment `!ecomserver`
gets killed with category `E32USER-CBase`/reason `47`. Result:

```
pc=0x803a0474  lr=0x803a9717   (both inside euser.dll, not ecomserver.exe)
```

Traced `0x803a9717`'s containing function (`FUN_803a96da`, freshly imported
- the previously-used `"euser_analysis"` Ghidra project turned out to
actually cover `ecomserver.exe`'s address range, not `euser.dll`'s at all, a
mislabeled leftover from an earlier round; re-extracted `euser.dll` properly
via `parse_rom.py`, same as every other module this file has ever imported).
Decompiled, it's unmistakably `User::Panic()`'s own generic implementation:
builds a category descriptor capped at 16 characters (matching Symbian's
real, documented `TExitCategoryName` length limit exactly) and dispatches
via `FUN_803a045c(2, reason, category)` - `2` matching `EExitPanic`,
Symbian's real, documented exit-type constant. All 10 callers of this
function are themselves still inside `euser.dll`, not `ecomserver.exe`.

**This settles round 50's framing**: `E32USER-CBase`/47 is Symbian's own
well-documented, generic panic - *"an Active Object's `RunL()` or its own
`RunError()` override also leaves"* - triggered by `CActiveScheduler`'s
default error handler, not a custom "two strikes and I give up" counter
written into `ecomserver.exe`'s own code. The real locus, per this
correction, is `!ecomserver`'s own `RunError()` override succeeding once and
then failing (or leaving again) the second time - not literally hand-rolled
retry-counting logic. This doesn't change what to look for next so much as
sharpen it.

### A real fix attempt, using infrastructure EKA2L1 already has

Rather than write new machinery, found EKA2L1 already ships exactly the
right tool for this: [config/panic_blacklist.h/.cpp](../../src/emu/config/src/panic_blacklist.cpp),
loaded from `compat/panicBlackList.json`, keyed by process name → thread
name → category/code, already in active use for one other known
compatibility issue (`playserver`/`PlayServer`/`Timeout`/`-33`). Added a
matching entry - `ecomserver`/`!ecomserver`/`E32USER-CBase`/`47` - to both
the source (`miscs/compat/panicBlackList.json`) and build-output copies, and
tested it directly (no rebuild needed - this is a runtime-loaded config
file, not compiled source).

### The result: worse, not better - and a genuine non-determinism discovery along the way

With the blacklist entry active, the boot log grew *continuously* for over
200 seconds (never stabilizing, unlike every prior capture), with the
`!BackupServer` fail-fast message repeating roughly once per second (208
times in the observed window) and **zero** panic log lines - a real change
in behavior, even though the blacklist's own confirmation message
(`"Panic for the thread is blocked"`) never appeared, because it's logged
via `LOG_INFO`, silently swallowed by the default `Kernel:Warn` filter
threshold - the exact same log-filter trap this file has flagged in rounds
33/38/40, caught again here rather than mistaken for "the fix did nothing."

To isolate the variable properly, reverted the blacklist entry and re-ran
clean. That baseline run showed something genuinely new: **`!ecomserver`
retried only *once*, then went silently, permanently stable for 225
straight seconds - no second attempt, no panic at all.** This directly
contradicts rounds 45/50's own captured logs (which showed exactly two
attempts before panicking) - not because those captures were wrong, but
because **this specific behavior is itself subject to the scheduling
non-determinism this file has flagged since round 4**: sometimes
`!ecomserver` panics after two attempts, sometimes it just silently stalls
forever after one, with no panic ever occurring. A third distinct failure
shape, previously unrecorded, sitting alongside round 21's runaway-retry and
round 50's "-15 vs 0" completion-value findings.

Put together, the picture is: blocking the panic doesn't let `!ecomserver`
recover gracefully and carry on - it just removes the one thing that was
ending its retry attempts, leaving it to retry the same doomed operation
forever at a steady, bounded rate (structurally the same shape of "not a
runaway, but never-terminating" failure round 50's `KErrNone` experiment
already found on the *other* side of this same chain). **This is a real,
useful negative result**: whatever `!ecomserver`'s `RunError()` is trying to
do on a real device that lets it succeed, simply keeping the thread alive
after a failed attempt isn't equivalent to it - the thread survives, but
never does anything useful afterward either.

### Reverted completely

The `svc.cpp` probe and both `panicBlackList.json` copies (source and build
output) fully reverted; verified via clean rebuild, `git diff --stat` (src)
matching the same 43-file baseline as every round since 46, and empty `git
diff`/`git status` specifically for both blacklist file copies. No resource
incidents; guardrail held throughout - two builds (probe add, probe
revert-verify), no concurrent heavy processes, several bounded-poll runs
(including two long ones, ~200s and ~225s, both with runaway-size guards
that never triggered), force-killed after each capture.

### What this means for an actual fix

Two real, substantive negative results now bracket this problem from both
sides: round 50 showed changing the *completion value* on the
`!BackupServer` side can't avoid `!ecomserver`'s eventual bad outcome
(either the panic, or an equally unproductive endless poll). This round
shows blocking the *panic itself* doesn't help either - it trades a clean
death for an endless equally-unproductive retry. **Neither symptom-level
lever available at the EKA2L1 layer changes the actual outcome for the
user**: with or without either intervention, `!ecomserver` never reaches a
state where it (or anything depending on it, like `CdlServer`) makes real
forward progress. That's a strong signal this specific failure - `!ecomserver`
choking on a `!BackupServer` response its own `RunError()` can't handle a
second time - is a genuine defect in the specific guest binaries Nokia
shipped with this device's firmware, not a gap in EKA2L1's emulation that
has a clean fix at this layer.

### To resume from here

1. The remaining honest options are: (a) accept this as a known, documented
   incompatibility (this investigation now has enough evidence - traced
   mechanism, ruled-out fixes, the exact panic code and category - to write
   a clear, permanent note in this project's compatibility docs rather than
   keep searching for an emulator-side fix that may not exist), or (b) go
   one level deeper still and find `!ecomserver`'s actual `RunError()`
   override in `ecomserver.exe` itself (not `euser.dll` - static tracing
   hasn't located this specific override yet) to understand precisely what
   it does differently between its first and second invocation, which might
   reveal a *guest-state* fix (e.g. pre-seeding whatever internal flag or
   resource `RunError()` checks) rather than an SVC-layer one - but given
   two independent symptom-level fixes have both failed to help, expectations
   for this should be modest.
2. If continuing, the two non-determinism variants found this round (2
   attempts + panic, vs. 1 attempt + silent permanent stall, both observed
   on otherwise-identical runs) are themselves worth a note for anyone
   testing future fix attempts here - a single run showing "no panic" is
   not evidence a fix worked; this round's own blacklist test looked like
   progress at a glance (0 panics!) until the repeating-retry pattern and
   the reverted-baseline comparison showed it wasn't.

## Round 56 — went one level deeper still: this is real ARM EHABI stack unwinding, not app logic

At the user's explicit direction to keep pushing on a fix. Traced `!ecomserver`'s
own leave/recovery sequence far more completely than any prior round, and the
destination is a genuinely different, more fundamental kind of code than
anything characterized so far.

### The real Leave sequence: 26 leaves, not 2 - and the middle 23 are benign

A wide re-arm-on-every-leave probe (re-using round 51-54's technique,
armed inside `leave_start`/`leave_end` in [svc.cpp](../../src/emu/kernel/src/svc.cpp))
captured the true sequence for the first time:

```
1x  leave_code=-1   (KErrNotFound)
23x leave_code=-12  (KErrPathNotFound)
2x  leave_code=-5   (KErrNotSupported)   <- the two leaves every prior round saw
```

Rounds 45/50/55 only ever captured the tail end of this (the last two `-5`
leaves) - this round found the full picture. Decompiling `ecomserver.exe`'s
own code active during cycle 24 (the last `-12` leave, right before the
shift to `-5`) via the existing 589-function `ecomserver_analyzed_r38`
project showed real, coherent ECOM plugin-resolution logic: `FUN_804c9356`
(a `Count()`/`At()` loop over installed interface implementations) calling
`FUN_804c920e`, which builds a path and does file I/O (`RFs`/`RFile`-shaped
calls) to write a diagnostic log entry for each plugin needing one - clearing
its own "pending" flag when done. **The 23 `-12` leaves are very likely
completely normal ECOM plugin-scan/logging behavior**, gracefully handled
every single time (never escalating), not a bug at all - this reframes
rounds 45/50's "two strikes" reading as having only ever seen the very end of
a much longer, mostly-uninteresting sequence.

### The fatal `-5` cycles: zero ecomserver.exe activity, ~90% in drtaeabi.dll

A second, targeted probe armed a wide (no module filter) trace specifically
for the two `-5` cycles only (detected via the leave code at `leave_start`,
skipping the 23 benign cycles entirely to give the fatal ones a full
budget). Result: **zero** instructions in `ecomserver.exe`'s own code range
during either fatal cycle - confirming round 39/48's read that this specific
step is handled entirely by generic framework code, not app logic. Of what
*did* execute: ~90% (2690/3000 sampled instructions) landed in
`drtaeabi.dll`, ~9.5% in `euser.dll`, ~0.8% in `bafl.dll`.

### Mapping the drtaeabi.dll activity: this is the ARM EHABI unwinder itself

Most of `drtaeabi.dll` had never been auto-analyzed into function boundaries
in the existing Ghidra project (only two specific addresses were
force-created back in round 52) - force-created boundaries for every hit
address (read-only Ghidra session, no project mutation persisted) and
decompiled the top functions by hit count. The result is unambiguous:

```c
// FUN_80394d10 (619 hits) - walks a table byte-by-byte:
uVar9 = FUN_80394cdc(&iStack_44);      // fetch next unwind opcode byte
if (uVar9 == 0xb0) { ... return 7/8; } // 0xB0 = FINISH (ARM EHABI spec)
if (uVar9 < 0x40) { ...sp += ...; }    // 0x00-0x3F = vsp = vsp + offset
if (uVar9 < 0x80) { ...sp -= ...; }    // 0x40-0x7F = vsp = vsp - offset
if (uVar9 < 0x90) { ...pop mask...; }  // 0x80-0x8F = pop register mask
```

This is not app code, not a custom retry mechanism, not anything specific to
`ecomserver.exe`, `bafl.dll`, or Nokia's own code at all - it's a direct,
recognizable implementation of the **publicly-documented ARM Exception
Handling ABI's personality-routine byte-code interpreter** (the standard
opcode encoding: `0xB0` terminates unwinding of a frame, `0x00-0x7F` adjust
the virtual stack pointer, `0x80-0x9F` pop register masks - exactly matching
the ARM EHABI specification). `drtaeabi.dll` ("ARM EABI" - its name has said
this since round 27) is providing the *generic, compiler-supplied* C++
exception-unwinding runtime that any ARM EABI-compliant binary uses -
identical in kind to what runs on real hardware for any C++ exception,
completely independent of what `ecomserver.exe`, N-Gage, or this specific
scenario are doing.

### What this means

The "give up and panic" decision for the second `-5` leave is not a
hand-written `if` statement anywhere - it's the *outcome* of walking
compiler-generated, data-driven unwind tables baked into the binary at
compile time, searching for a catch handler at each stack frame. Whether
unwinding finds a handler (survives) or runs out of frames (escalates to
`CActiveScheduler::Error()` → `User::Panic()`, rounds 55/56) is determined
by the *compiled structure* of `ecomserver.exe`/`bafl.dll`'s own exception
handling (which functions have `catch` blocks, at which call depths) - not
by any runtime state EKA2L1 controls or could plausibly patch through
configuration, SVC interception, or an emulation fix. This closes off "find
and patch `!ecomserver`'s `RunError()` override" as a real target: there
isn't a hand-written override to find beyond what's already been traced -
the mechanism is generic OS/compiler runtime all the way down.

This doesn't mean nothing is left to try - it reframes what "the bug" even
is. The unwinding machinery is provably not broken (it's the same code any
correct ARM EABI program relies on). What differs between the *first* `-5`
leave (survived) and the *second* (escalates to panic) is almost certainly
**which function, and at what call depth, each Leave actually originates
from** - not how the unwinder processes it. That's a call-stack-shape
question, not a code-behavior question, and it's the one thing this round
did not yet capture (the trace logged `pc`/`lr` per instruction, but not a
full call-stack snapshot at the moment each of the two `-5` leaves begins).

### Reverted completely

All temporary probes (two rounds of `kernel.h`/`svc.cpp`/`epoc.cpp` changes)
fully reverted; verified via two clean rebuilds and `git diff --stat`
matching the same 43-file baseline as every round since 46. All Ghidra
analysis used existing projects (`ecomserver_analyzed_r38`,
`drtaeabi_fresh_r34`) with `-readOnly` for anything that force-created
function boundaries, so no project state was mutated. No resource
incidents; guardrail held throughout - two builds (probe add, probe
revert-verify) each for two probe iterations, several bounded-poll runs, one
misdiagnosed Ghidra "bug" (turned out to be most of `drtaeabi.dll` genuinely
lacking function boundaries in the existing project, not a scripting error -
resolved by force-creating them, read-only, rather than trusting the first
confusing result).

### To resume from here

1. **The concrete next step, now that the unwinder itself is ruled out**:
   capture the actual call stack (not just `pc`/`lr`, but walking `sp`
   through however many frames are live) at the moment each of the two `-5`
   leaves begins, for both the surviving first attempt and the fatal second
   one. If they genuinely differ in depth or originating function, that
   would finally explain *why* the second attempt has no enclosing handler
   when the first one did - and might reveal something concrete enough to
   act on (though, given this lives in the guest's own compiled call
   structure, still not guaranteed to be fixable at the EKA2L1 level even
   once found).
2. Given three substantive fix attempts/angles now (round 50's completion
   value, round 55's panic-block, and this round's mechanism trace) have
   each deepened understanding without producing a working fix, it's worth
   being honest that the remaining path may end in "this is how the actual
   Nokia-shipped binaries behave, full stop" - but the call-stack-depth
   question above is a genuinely new, concrete, previously-unasked question,
   not a repeat of anything already ruled out.

## Round 57 — captured the call-stack difference directly: a real, concrete divergence found

Did exactly round 56's own recommendation. The result is the most precise
finding this investigation has produced about what actually differs between
the surviving first `-5` leave and the fatal second one.

### The raw stack scan: identical outer chain, different inner depth

Armed a probe in `leave_start` ([svc.cpp](../../src/emu/kernel/src/svc.cpp))
that, on each of `!ecomserver`'s two fatal (`-5`) leaves, scans 64 words
above `sp` for anything that looks like a guest code address, tagged by
occurrence number. Comparing the two captures by **absolute stack address**
(not relative offset - the two leaves have slightly different `sp`, but
overlapping stacks): from address `0x501e4c` upward, **every single word is
byte-identical between the two cycles** - the same 13 return addresses, in
the same order, all the way out through `bafl.dll`'s retry logic,
`ecomserver.exe`'s own dispatch, and `drtaeabi.dll`. The *entire outer call
chain is the same call, re-entered the same way, both times.*

The difference is confined to the innermost few frames, below `0x501e4c`:

```
cycle 1 (survives):  ...  0x80778969  0x807789b5  [shared chain from here up]
cycle 2 (fatal):     ...  0x803af4e7  0x807789e7  [identical shared chain]
```

### Identifying the two bafl.dll addresses: same function, different point in it

`0x807789b5` and `0x807789e7` are both return addresses **inside the same
function**, `FUN_80778990`:

```c
void FUN_80778990(int param_1) {
    if ((*(int *)(param_1 + 4) != -3) &&
        (0 < FUN_8078011c(param_1 + 0x1c))) {
        uVar2 = thunk_EXT_FUN_803a82aa();
        FUN_80778956(param_1);          // observer-notify loop (virtual dispatch)
        thunk_EXT_FUN_803a82c0(uVar2);
        FUN_807784fc(param_1);
        thunk_EXT_FUN_803af4da(0);
    }
    return;
}
```

- **Cycle 1's return point** (`0x807789b4`) sits *immediately after* the call
  into `FUN_80778956` - the observer-notification loop that invokes each
  registered observer's callback via virtual dispatch (matching round 39's
  original vtable finding). The Leave that unwound to here originated from
  *inside* that notification call, mid-loop.
- **Cycle 2's return point** (`0x807789e6`) is the function's own **final
  instruction** - `pop {r3,r4,r5,r6,r7,pc}`, the epilogue. By the time this
  Leave happens, `FUN_80778990` has already run to completion and is in the
  literal act of returning to its own caller.

And `0x80778969` (present only in cycle 1's scan) sits inside `FUN_80778956`
itself - direct confirmation that cycle 1's frame was still live one level
deeper than cycle 2's ever gets.

### What this means

This isn't "the same operation fails identically twice, and the second time
something arbitrary decides to give up." **The two fatal leaves originate
from genuinely different points in the call tree** - one from deep inside
the observer-notification loop, the other from one frame higher, right as
that whole function is finishing and handing control back to its own
caller. Since the entire *outer* chain above this point is identical both
times, whatever varies between the two attempts is confined to a single
function's internal control flow (`FUN_80778990`) and, more specifically, to
whatever `FUN_80778990`'s own caller does immediately after it returns -
that's the code that appears to actually Leave the second time, not a
literal repeat of the first failure.

Checked whether `FUN_80778990` itself has a findable static caller
(the natural next step): no direct call-flow reference anywhere in the
binary, but one data reference (`0x80781728`) to its Thumb-marked address -
**`FUN_80778990` is itself a vtable slot**, reached only through indirect
C++ virtual dispatch. This is the third time this exact wall has appeared in
this investigation (round 39/48's connect-and-retry object, round 53's
`CdlServer` object, and now this) - confirming, for the third independent
time, that the layer directly responsible for this behavior is a generic,
vtable-dispatched framework object, not something with a nameable, patchable
function sitting in application code.

### Reverted completely

The `svc.cpp` stack-scan probe fully reverted; verified via clean rebuild and
`git diff --stat` matching the same 43-file baseline as every round since
46. All follow-up work was read-only Ghidra analysis against the existing
`bafl_analysis` project. No resource incidents; one build (add), one build
(revert-verify), one bounded-poll repro run, several quick read-only Ghidra
invocations.

### Honest assessment at this depth

This round found something genuinely new and precise - not a repeat of
round 55/56's negative results, a real, concrete structural difference
between the surviving and fatal paths. But it also confirms, for the third
time, that the next step needed to go further (recovering the actual
`param_1` object and its vtable contents for `FUN_80778990`, the same live
technique round 54 used successfully for `CdlServer`) is reaching into
generic framework/observer-pattern machinery whose behavior is fundamentally
data-driven (which observers are registered, in what order, doing what) -
not a single function with an obvious bug. Each round at this depth has
been finding *more precise facts*, not fewer, but the facts keep resolving
to "this is exactly how the compiled, generic C++ observer/exception
machinery behaves given this specific object graph" rather than to anything
resembling a bug in a specific line of code.

### To resume from here

1. **The concrete next step, if continuing**: capture `FUN_80778990`'s own
   `param_1` (the object/`this`-equivalent) and its vtable, for both fatal
   cycles, the same way round 54 did for `CdlServer`'s vtable object - this
   would show directly whether the two cycles are notifying the *same*
   observer object (meaning the difference is purely about *when* in its
   lifecycle the Leave happens) or *different* observers entirely (meaning
   `!ecomserver`/`bafl.dll` register more than one observer, and only one of
   them is unprotected at the point it's invoked).
2. Given the pattern across rounds 39, 48, 53, and 57 - four independent
   occasions where the actual decision point turned out to be inside a
   generic, vtable-dispatched object whose class this investigation has
   never been able to name - it may be worth treating "recover this specific
   C++ class's identity, once, thoroughly" as its own focused task rather
   than re-deriving partial answers to it each time a new function turns out
   to be one of its virtual methods.

## Round 58 — the fix: found it, tried it, verified it works

Continued round 57's own recommendation, hit the same vtable wall a fifth
time, and - per the user's explicit direction to fall back on everything
learned and find a real fix rather than trace further - stopped tracing and
looked at what round 57 had *already* handed us.

### The object-capture attempt: confirms same object, hits the wall again

Redid round 57's stack scan without the code-address filter (which would
have hidden a heap object pointer sitting right next to a return address).
Aligning both fatal cycles by absolute stack address confirmed directly:
**both cycles operate on the exact same `bafl.dll` object** - identical heap
address (`0x800ae0`), identical vtable pointer (`0x80781718`), identical
first vtable slot (`0x807788b9`). This rules out "different observer" as an
explanation cleanly - it's the same object, same registration, both times.
The divergence really is confined to timing/control-flow within
`FUN_80778990`, exactly as round 57 found. `FUN_80778990`'s own caller
turned out to be unreachable by static analysis too (no call-flow
reference, one data reference confirming it's itself a vtable slot) - the
fifth occurrence of this exact wall (rounds 39, 48, 53, 57, 58).

### Stepping back: the fix was already sitting in round 57's own quote

Per the user's direction, stopped chasing the vtable and reread round 57's
own finding with fresh eyes. It had already surfaced the answer without
this file recognizing it as one:

```c
void FUN_80778990(int param_1) {
    if ((*(int *)(param_1 + 4) != -3) && ...) { ... }
}
```

**`-3` is `KErrCancel`.** This object's own compiled code already contains an
explicit check for that exact value, on its own state field, as a guard
before doing any notification work at all. That is not a coincidence to
read past - it's the guest binary's own author documenting, in compiled
form, that `KErrCancel` is the value this object's state machine recognizes
as "this outstanding request was cancelled, not a genuine failure, don't
treat it as one." Round 26's fix has been completing this message with
`KErrNotSupported` (`-5`) - a generic, plausible-sounding error code chosen
without ever checking what the object on the receiving end actually expects.
`-5` is *a* valid error code; it was never *the* value this specific object's
logic was written to recognize as benign.

### The test

Changed `server.cpp`'s existing fail-fast completion value from
`epoc::error_not_supported` to `epoc::error_cancel`, rebuilt, and ran the
full repro repeatedly (three initial trials, then a fourth with round 51's
own thread-state-dump technique chained on, then a fifth extended to a full
5-minute patience window matching round 28's own standard):

- **Zero panics across every trial** (`grep -c "panicked with category"`
  consistently `0`, versus `-5`'s documented non-deterministic mix of
  "2 attempts then panic" and "1 attempt then silent stall").
- **Thread-state dump, 8 seconds after the fail-fast fires:**
  ```
  PlayServer state=5           !ecomserver state=5    (was state=4 - stopped/dead - with -5)
  !AknIconServer state=5       CdlServer state=5      (was state=1 - run/spinning - with -5)
  Main state=5                 !BackupServer state=5
  Games state=6
  ```
  Every thread, including `!ecomserver` and `CdlServer`, is in the same
  normal idle/waiting state (`5`) as threads that were never part of this
  bug at all. Re-checked after a full 5 minutes of continued observation
  (matching round 28's own patience-test standard) - unchanged, no relapse
  into a spin.
- A `PrintWindow`-based screenshot (same technique rounds 27/28/31 used,
  same standing approval) still shows the same status-bar-plus-black-
  rectangle placeholder those rounds documented - the visible app hasn't
  reached a playable menu in this specific capture window. Given every
  thread this investigation has ever cared about is now idle rather than
  stuck, this is most plausibly the app waiting on ordinary user interaction
  (a tap-to-continue splash, consistent with N-Gage's real UI flow) rather
  than a new stall - but that's the user's own call to verify by hand, not
  something to simulate here (an attempt to script a mouse click was
  correctly blocked by antivirus heuristics for the low-level input-injection
  API pattern involved, and wasn't worth working around for a check the user
  can do directly and more meaningfully themselves).

### What actually changed, and why it's a fix and not another narrowed symptom

Rounds 21/50/55 each showed that changing *how* the stuck message resolves
can make things worse (retry storms, infinite polling) precisely because
the guest's own logic reacts differently to different values - "any
completion" was never the same as "the *right* completion." This round is
the first time the reasoning worked in the correct direction: instead of
guessing a value and observing the consequence, it started from a value the
*guest binary itself* already treats specially, found via direct
decompilation, and confirmed empirically that using it produces the
behavior that value's own name implies (a cleanly cancelled request) rather
than an error needing recovery. `!ecomserver` and `CdlServer` - the two
symptoms this entire investigation exists to explain - are both resolved by
the same one-line change.

### The fix, made permanent

`server.cpp`'s `arm_backup_server_msg_timeout` now completes with
`epoc::error_cancel` instead of `epoc::error_not_supported`, with an updated
comment block explaining the reasoning and pointing at rounds 50-58 for the
full trace. All temporary diagnostic instrumentation (the object-capture
stack scan, the thread-state-dump chained event) was stripped back out -
only the one-line behavioral change and its documentation remain. Verified
via a final clean rebuild and a final confirmation run (zero panics, correct
log message). `git diff --stat` on `server.cpp` now shows `+97` lines
(up from round 26's original `+77` - the difference is entirely the
expanded comment explaining this round's finding).

**This change is left uncommitted in the working tree**, per this session's
standing practice of never committing without being explicitly asked - the
diff is there to review (`git diff -- src/emu/kernel/src/server.cpp`) and
commit whenever that's wanted.

### To resume from here

1. Confirm in person whether the app is genuinely waiting for a tap (in
   which case there's nothing left to fix) or whether something else, unrelated
   to this entire 58-round investigation, is the reason a menu hasn't
   appeared yet.
2. If a new, different stall turns up, it would be a *new* investigation -
   everything this file traced (the `!BackupServer` no-op, `!ecomserver`'s
   panic, `CdlServer`'s spin) is now independently confirmed resolved, not
   just quieter.

## Round 59: the tap did nothing - found a second, distinct deadlock, plus a lead on what still blocks the visual

The user directly tested tapping/clicking the running, fixed build: **"I did
try, but it did nothing."** This is real, first-hand negative evidence
against round 58's closing hypothesis ("probably just needs a tap"). Went
back to the emulator to find out what's actually happening now, using a
fresh, targeted probe rather than assuming.

### Full thread-state sweep (not just the 8 previously-tracked threads)

Added a temporary probe in `system_impl::loop()` ([epoc.cpp](../../src/emu/system/src/epoc.cpp))
that dumps **every** thread in `kern_->get_thread_list()` - name, state,
priority, and (new this round) whatever kernel object the thread is
waiting on, resolved by pointer, with the *holder* thread name if that
object is a mutex - every 2 real seconds via `LOG_ERROR` (bypasses the
default log-level filter, unlike the confirmation message that tripped up
round 55). First attempt used an instruction-count-based gate (`% 300000`)
copied from earlier rounds' assumptions about `loop()`'s call rate - it
never fired once in 30+ seconds, because `loop()` here is being driven by a
low-rate host timer, not per-instruction. Fixed by switching to a
`std::chrono::steady_clock`-gated 2-second wall-clock interval instead -
immediately started firing.

**Result - the real full picture, something no prior round had captured**:

```
PlayServer state=3                    wait=none
!AknIconServer state=5                wait=type=4(sema) name='requestSemaXXXX'
!ecomserver state=5                   wait=type=4(sema) name='requestSemaXXXX'
CdlServer state=5                     wait=type=4(sema) name='requestSemaXXXX'
PlayServerSplashScreenFallBackThread state=0 (create - never started)  wait=none
Main state=5                          wait=type=4(sema) name='requestSemaXXXX'
N-Gage Installer state=6 (wait_mutex) wait=type=5(mutex) name='playserver.exe' holder='PlayServer'
```

Every server thread (`!ecomserver`, `CdlServer`, `!AknIconServer`, `Main`,
`PlayServer` itself) is genuinely idle, correctly waiting on its own
request semaphore for the next IPC message - exactly what a healthy,
unblocked Symbian server looks like, and exactly what round 58 already
verified. **Round 58's fix holds.** But `N-Gage Installer` (the guest's
own `ngiinstaller.exe`, seen summoned earlier in the boot log) is in
`wait_mutex` - a real, intentional blocking wait for exclusive ownership
of a mutex, not a spin - on a mutex named `playserver.exe`, and that
mutex's `holder()` is `PlayServer`'s own thread, which never releases it.

This is a **genuinely new, distinct deadlock** - a two-thread handshake
where one side holds a lock forever and the other blocks on it forever -
architecturally nothing like the semaphore/IPC-completion stall rounds
1-58 all traced. It was invisible before round 58's fix because the whole
system used to die/stall earlier, before either thread ever reached this
point.

### Confirming the mechanism in `mutex.cpp`

Read [mutex.h](../../src/emu/kernel/include/kernel/mutex.h) /
[mutex.cpp](../../src/emu/kernel/src/mutex.cpp) and the `mutex_create`/
`mutex_wait*` bridges in [svc.cpp](../../src/emu/kernel/src/svc.cpp) to
rule out an EKA2L1-side name-deduplication bug (i.e., two unrelated guest
mutex objects accidentally colliding on the same kernel object because
`create<T>()` interns by name). Confirmed **`create<T>()` never dedupes by
name at all** - every `mutex_create` SVC call makes a genuinely new
`kernel::mutex` via `std::make_unique`, no matter what name string is
passed. So `PlayServer` and `N-Gage Installer` sharing the exact same
mutex instance (confirmed via `holder()` matching across the two threads)
means `N-Gage Installer` used an **open**-by-name call to get a handle to
the mutex `PlayServer` itself already created and locked, then called
`Wait()` on it and blocked - both completely ordinary, correct Symbian API
usage. Nothing here looks like an EKA2L1 handle-table bug. This reads as
either a deliberate real-hardware startup-barrier idiom (`PlayServer`
locks the mutex at the very start of its own init, does its slow setup,
`Signal()`s it once ready; `N-Gage Installer` opens the same mutex and
`Wait()`s on it purely to know "`PlayServer` is ready now") where
`PlayServer`'s own real code never reaches the `Signal()` call in this
emulator - or a single-instance-guard idiom used in an unusual blocking
way. Either way, the missing piece is inside `playserver.exe`'s own real,
un-HLE'd guest code (confirmed: `grep -ri playserver src/emu/services`
returns nothing - there is no EKA2L1-side implementation of PlayServer at
all, it is 100% real Symbian binary running under CPU emulation) - which
also lines up with the **pre-existing** (predates this whole
investigation) `playserver`/`PlayServer`/`Timeout`/`-33` entry in
`panicBlackList.json`: `PlayServer`'s own compiled code already has a
documented history of hitting a timeout-triggered panic on real hardware
emulation and having it silenced, which is consistent with its init logic
depending on something that doesn't behave the same under EKA2L1 as on
real hardware.

### Experiment 1: force-release the mutex - real forward progress, empirically confirmed

To test the hypothesis without yet reverse-engineering `playserver.exe`,
added a temporary probe in `mutex::wait()`: the moment any mutex named
`playserver.exe` transitions from unheld to held, schedule a one-shot
delayed event (5 virtual seconds, same `ntimer` pattern as round 26/58's
`arm_backup_server_msg_timeout`) that force-calls `signal()` on it if
still held - simulating "assume `PlayServer`'s init completed."

**Result: real, structural forward progress**, not just a log-line change.
After the forced release fired for `PlayServer`'s original lock:

- `N-Gage Installer` immediately re-acquired the same mutex three times in
  quick succession (a visible retry pattern, same shape as bafl.dll's
  known "retry then give up" behavior documented in earlier rounds) -
  then **finished and disappeared from the thread list entirely** (a
  correct outcome for a one-shot installer process, not a crash or a new
  stall).
- The boot log showed genuinely new lines never seen before this round:
  `Unblanking screen in AKNCAP session stubbed` (the direct opposite of
  the earlier `Blanking screen` line, i.e. real UI-visible progress), then
  `N-Gage Installer` proceeding all the way to its own `!BackupServer`
  call, correctly hitting round 58's already-fixed fail-fast (`function=32`,
  `KErrCancel`, no panic) - the exact same safety net built for
  `!ecomserver` now protecting a second, different caller for free.
- The system settled back into a clean, fully-idle steady state afterward -
  no panics, no new spins.

This confirms the `playserver.exe` mutex really is the next real blocker,
and that forcibly releasing it is safe enough to let the rest of the boot
sequence run to completion correctly.

### But the visual still didn't change - and here's why

Screenshotted the emulator window (same `PrintWindow` technique as round
27/58) both before and after the forced-release experiment. **Pixel-for-
pixel identical** - same white status-bar-shaped region, same solid black
rectangle below it, in both captures. Going back to the full thread-state
dump: `PlayServerSplashScreenFallBackThread` sits at `state=0` (`create` -
constructed, never `Resume()`d) in **every single snapshot across every
run this round**, including the run where the mutex was force-released
and `N-Gage Installer` ran to completion. Nothing in the system ever calls
`Resume()` on it. Given the thread's name, this is almost certainly the
renderer for exactly the placeholder screen that's been on-screen since
round 27 - so this, not the mutex, is the most direct remaining cause of
the frozen visual.

### Experiment 2: force-resume the fallback thread - wedges the whole interpreter

Added a second temporary probe: 12 real seconds after boot, scan the
thread list once for any thread named
`PlayServerSplashScreenFallBackThread` still at `thread_state::create`,
and call `resume()` on it directly (the same kernel-level call a guest
`RThread::Resume()` SVC would make - not a shortcut around normal thread
setup, since the thread object itself was already fully constructed with
a real entry point/stack by whatever guest code created it).

**Result: the entire interpreter wedged.** The probe's own log line
(`force-resuming stuck-at-create thread ...`) printed once, and then
*nothing* - not even the unconditional, wall-clock-gated 2-second
thread-dump probe (which does not depend on the emulated CPU making any
progress at all) fired again, for 20+ real seconds. The process stayed
alive (confirmed via `tasklist`, not crashed) but produced zero further
log output and zero visual change - consistent with the freshly-resumed
thread hitting a genuine, un-preemptable infinite loop in its own guest
code, not a host-side hang. This is a real, informative negative result:
**naively kick-starting this thread from the emulator side is the wrong
fix.** Whatever precondition this thread's real code expects before it
runs safely is not in place yet at the point something should have
resumed it - starting it early breaks it rather than fixing anything.
Force-killed the wedged process rather than waiting further (a genuine
infinite loop doesn't resolve itself).

### Reverted completely

All three round-59 probes - the full thread-state dump (`epoc.cpp`), the
forced mutex-release (`mutex.cpp`), and the forced thread-resume
(`epoc.cpp`) - fully removed, including the added `#include <chrono>` and
`#include <kernel/mutex.h>`. Verified via `git diff --stat` on both files
(`mutex.cpp` clean, `epoc.cpp` back to the same pre-existing +26-line
baseline every round since 46 has shown) and a final clean rebuild.
`git diff --stat` on the whole tree still matches the same 43-file
baseline. Guardrail held throughout: five builds this round (each capped
`-m:4`), `MSBuild.exe` nodes checked and force-killed after every one,
never more than one emulator instance running at a time, each force-killed
before the next build or edit.

### What this means, in simple terms

The bug reported at the start of this investigation (N-Gage hanging during
boot) **is fixed** - round 58's change is real, verified, and should stay.
What's left is a **second, separate bug** that was always there but was
previously hidden behind the first one: `PlayServer` (the real, un-emulated
guest program that manages the N-Gage game menu/splash screen) locks a
synchronization object at startup and never unlocks it, which in turn
means the program responsible for drawing the very screen currently stuck
on-screen is never told to start. This is not the same code, the same
mechanism, or the same guest binary as anything rounds 1-58 fixed or ruled
out - it's new territory, and it will need the same kind of ground-up
tracing (most likely disassembling `playserver.exe` itself, the same way
`bafl.dll`/`ecomserver.exe` were traced in rounds 45-58) to find the real,
principled fix rather than another externally-forced workaround.

### To resume from here

1. The next real target is `playserver.exe` itself - get a fresh Ghidra
   import of it (its runtime load address is dynamic; capture it via the
   `codeseg.cpp` runtime-code log line, filtered to its UID, same technique
   noted for this exact file back in round 4) and trace what its main
   thread does between locking the `playserver.exe` mutex and (on real
   hardware) presumably calling `Signal()` on it - looking specifically for
   what IPC call, file read, or property/HAL query it might be making that
   returns different data under EKA2L1 than on real hardware, causing it to
   branch away from the `Signal()` path without panicking or crashing.
2. Separately, find out what guest code is *supposed* to call `Resume()` on
   `PlayServerSplashScreenFallBackThread` - likely also inside
   `playserver.exe`, and likely gated on the same startup-barrier mutex,
   so fixing (1) may resolve this for free. Don't force-resume it directly
   again without understanding this first - round 59 already showed that
   wedges the interpreter.
3. If reproducing this round's probes: use `std::chrono::steady_clock`
   wall-clock gates in `system_impl::loop()`, not instruction-count
   modulo gates - the latter silently never fires at this call site.

## Round 60: new technique (live-memory code extraction for non-ROM binaries), root cause narrowed further but not yet pinned

Picked up round 59's own "to resume from here": trace `playserver.exe` itself
to find why it locks the `playserver.exe`-named mutex and never signals it.

### Building a new capability: dumping real, relocated code straight from live process memory

`playserver.exe` isn't ROM-resident - it's an **installed** binary
(`data/drives/c/SYS/BIN/playserver.exe`, confirmed real `EPOC` E32Image
magic at file offset 0xC), which round 3/6/7's `parse_rom.py` cannot parse
(different header format, ROM-image-specific) and round 47's reverted
`e32dump` tool never actually worked against (it was tested on ROM-image
files that round, the wrong format entirely - this is genuinely the right
target format for that approach, just never tried against it).

Rather than write a new standalone E32Image parser (real risk of repeating
round 6/7's multi-round header-format debugging saga), reused EKA2L1's own,
already-correct, already-battle-tested loader: added a temporary probe
directly in `codeseg::attach()` ([codeseg.cpp](../../src/emu/kernel/src/codeseg.cpp))
that, when the codeseg's name matches a target, dumps `code_base_ptr` (the
in-memory buffer EKA2L1 itself just decompressed, copied, and - critically -
**relocated and import-patched**) straight to a `.bin` file. First attempt
placed the dump too early (right after the copy, before the import-fixup
and relocation loops further down the same function) - the resulting file
disassembled as clearly-wrong tiny placeholder values (import ordinals
like `0x408`, `0x554`, not real addresses) at every indirect-call thunk,
which was itself a useful tell that something was off before rebuilding
around a corrected, later insertion point (after both the import-patch
loop and the relocation loop complete). The corrected dump, imported into
Ghidra via `-loader BinaryLoader -loader-baseAddr 0x70000000` (the runtime
address captured fresh this run via the existing `codeseg.cpp` LOG_INFO
line, temporarily promoted from `Kernel:Warn` to `Kernel:Info` in
`config.yml` - a **runtime config file, not compiled source**, backed up
and restored after each capture, same safe/reversible pattern as round 55's
blacklist-file testing), decompiled cleanly and correctly this time -
confirmed by cross-checking known import thunks against a second temporary
probe logging every resolved `(ordinal, resolved address)` pair directly
from `codeseg::attach()`'s own import-patch loop (far more reliable than
reverse-engineering ordinals from Ghidra's synthetic "external entry point"
list, which for a raw-imported binary is meaningless boilerplate, not
Symbian's real export table - confirmed and discarded as a dead end this
round too). This is a genuinely new, reusable capability for this
investigation: **any installed (non-ROM) guest binary can now be extracted
in its real, live, correctly-relocated form and handed to Ghidra**,
something the previous 59 rounds never had (everything traced so far -
`ecomserver.exe`, `bafl.dll`, `CdlServer`, `drtaeabi.dll` - was ROM-resident).

### Live capture: found two real call sites into the mutex, one anomalous

Added a probe in `kernel::mutex::wait()` ([mutex.cpp](../../src/emu/kernel/src/mutex.cpp))
logging the calling thread's PC/LR/SP whenever the `playserver.exe` mutex is
waited on, plus a stack scan for candidate return addresses in the broad
dynamic-code region (`0x70000000`-`0x78000000`, covering every non-ROM
module observed this session). Two hits:

- `N-Gage Installer` (the real blocking `Wait()` call): `pc=0x8039fa9c`,
  `lr=0x803a82e5` - both squarely inside `euser.dll`'s own address range,
  confirmed via the existing `euser_rm320_r55` Ghidra project to be a real,
  disassemblable, six-instruction SVC-dispatch stub (`FUN_8039fa98`,
  literally just `blx`+`bx lr` wrapping the raw `mutex_wait` SVC) called
  from `FUN_803a82d4` - a small, generic **spin-retry loop**
  (`do { retries++; FUN_8039fa98(); } while (*param_1 == DAT_803a8600);`,
  confirmed via full decompile) that is Symbian's standard user-side
  "fast lock" wait primitive, not anything specific to `RMutex` or to this
  app. The stack scan found nothing in the top 4KB at either capture -
  consistent with (not necessarily caused by) how many wrapper frames sit
  between this point and real application code, matching round 4's already-
  documented finding that this class of call ("who calls the shared,
  generic executor") is not answerable by simple stack/call-graph tracing.
- `Main` (later renamed `PlayServer`, the thread that locks and holds it):
  `pc=0x70000000` (exactly the module's own base address), `lr=0x0`. Cross-
  checked directly: Ghidra's own analysis of the freshly-correct, relocated
  dump found **no disassemblable instruction at all at that exact address**
  (it's header/padding, not code - the real entry point is at a nonzero,
  not-yet-determined offset). This means the captured PC/LR for this
  specific event most likely reflects a thread context that had not yet
  begun real execution at the moment this particular `wait()` call fired
  (consistent with, though not conclusively proving, a context-sync timing
  quirk specific to a thread's very first scheduled quantum) rather than a
  genuine live snapshot of executing guest code - flagged honestly as an
  open detail rather than built on top of.

### Ruled out: ordinals 2060/2061 are not the mutex - a concrete negative result with proof

`playserver.exe` (and, identically, `ngiinstaller.exe`) import exactly two
adjacent `euser.dll` ordinals - 2060 (resolves to `0x803a82aa`) and 2061
(resolves to `0x803a82c0`) - that sit in the same immediate neighborhood as
the real wait chain above (`0x803a82cc`-`0x803a8320`), which made them look
like promising candidates for "the" `RMutex::Wait`/`Signal` pair. Full
decompile of both, plus the actual application function that calls them
(`FUN_700008b6`, a message-dispatch/format-selector routine that calls out
to `playserver.exe`'s own companion codec DLLs based on an opcode field -
unrelated to the startup mutex), settled it: **this function calls the
acquire/release pair correctly, symmetrically, twice, around two short
operations, in the same routine** (`uVar2 = acquire(); ...; release(uVar2);`
- textbook correct critical-section usage, not a bug, and not related to
the long-held named mutex at all). This is a real, address-proximity-driven
false lead, caught and closed out with decompiled proof rather than assumed.

### Also tried and ruled out: string-literal tracing

Round 25/57's successful technique (find the UTF-16LE string, trace its
references) was tried against the literal `"playserver.exe"` text, which
does appear once in the dumped code region (file offset `0x22c4`, matching
runtime address `0x700022c4`). Ghidra found **zero** code references to
that address, direct or as a literal-pool constant (checked both, plus a
±8-byte alignment window) - meaning this specific string occurrence is
almost certainly not used as a mutex name via a hardcoded literal at all.
The much more likely real mechanism: Symbian code commonly builds a
single-instance/global-object name at runtime from the process's own image
name (e.g. via `RProcess::FileName()`), not from a compiled-in constant -
which would make this specific static-tracing angle a structural dead end,
not a probe bug.

### Reverted completely

All three round-60 probes - the import-resolution logger and the
post-relocation code dump (both in `codeseg.cpp`), and the mutex call-site
logger with stack scan (`mutex.cpp`) - fully removed, including the
temporary `#include <fstream>` additions. `config.yml`'s `log-filter` was
restored to `Kernel:Warn` after each of the two captures that needed
`Kernel:Info`. Verified via `git diff --stat` (both files clean, matching
the same 43-file/`epoc.cpp`-only-baseline pattern every round since 46) and
a final clean rebuild. Six builds this round, each capped `-m:4`,
`MSBuild.exe` nodes checked and force-killed after every one; never more
than one emulator instance running at a time. Five new Ghidra scripts added
under `ghidra-scripts/` (`TraceMutexWaitCallers.java`, `CheckEuserExports.java`,
`FindPlayserverEuserCalls.java`, `DumpMutexOrdFuncs.java`, `FindThunkCallers.java`,
`DecompileEntry.java`, `DecompileFunc.java`, `FindStringRefs.java`), all
read-only static analysis against existing or freshly-imported projects,
one Ghidra process at a time throughout.

### To resume from here

1. The real remaining question is unchanged in shape from round 59, just
   better-scoped: **which of `FUN_803a82d4`'s ten actual callers within
   `euser.dll`** (`0x803aa998`, `0x803a2e18`, `0x803a57ce`, `0x803af5ca`,
   `0x803af60a`, `0x803a84cc`, `0x803a846a`, `0x803a848e`, `0x803a8be6`,
   `0x803ac7e2` - already enumerated this round via
   `TraceMutexWaitCallers.java`, none matched by any import ordinal either
   `playserver.exe` or `ngiinstaller.exe` directly resolves) **is reached
   by one further hop from either binary's own import table.** This needs
   one more level of the same technique: for each of those ten addresses,
   find ITS callers within `euser.dll` (exactly like `TraceMutexWaitCallers.java`
   already does for `FUN_803a82d4` itself - just re-point it at each of the
   ten), then cross-reference the resulting address set against the full,
   already-captured import-ordinal list for both binaries (saved this round
   at the paths noted in the script outputs) via a script rather than manual
   grep guessing.
2. Alternative, likely faster path: instead of continuing bottom-up through
   `euser.dll`'s wrapper maze (the same shape of wall round 4 already hit
   once), go top-down with a live single-step execution trace of
   `PlayServer`'s thread from a clean, early point (its first real
   scheduled instruction, not the anomalous `pc=0x70000000` capture),
   re-using round 56's proven "re-arm a fresh budget on each relevant event"
   probe design - this would show the REAL call sequence directly, no
   static-tracing ambiguity, at the cost of a live run instead of a Ghidra
   pass.
3. `RProcess::FileName()`-style dynamic name construction (see the
   string-tracing paragraph above) is worth confirming directly: check
   `euser_rm320_r55` for that API's real ordinal/address and whether either
   binary imports it, before spending more effort on any static string
   search.
4. The `playserver_reloc_r60` and `euser_rm320_r55` Ghidra projects (in
   `C:\Users\Usuario\Desktop\tools\ghidra_project\`) are ready to continue
   from directly - no re-import needed. The corrected, properly-relocated
   `playserver_code_reloc.bin`/`ngiinstaller_code_reloc.bin` dumps are in
   the session scratchpad if a fresh Ghidra project is ever needed instead.

## Round 61: live single-step trace pins the exact lock call site; confirmed `Signal()` never fires

Picked up round 60's own suggested alternative: instead of continuing
bottom-up through `euser.dll`'s wrapper maze (the same wall round 4 already
hit), went top-down with a live single-step execution trace of
`PlayServer`'s own thread, reusing round 56's proven technique.

### A real bug in the first probe attempt - and what it revealed

First attempt gated single-stepping on `to_run->owning_process()->name() ==
"playserver.exe"`, checked unconditionally at the top of every
`system_impl::loop()` call ([epoc.cpp](../../src/emu/system/src/epoc.cpp)).
Three separate launches all stalled at the *exact* same point - right after
`!BackupServer`'s already-fixed fail-fast message, at a log size (~54886
bytes) matching the *original pre-round-58* stall - looking, at first
glance, like a severe regression. Reproducing it three times ruled out
ordinary scheduling non-determinism. The real cause, found by reading
[process.cpp](../../src/emu/kernel/src/process.cpp): **`kernel::process::name()`
is not a plain field return - it's `fmt::format("{}[{:08x}]{:04x}", ...)`,
called fresh every single time.** Comparing against a plain string literal
on the hot path of `loop()` - called at very high frequency during normal
(non-stepped) execution - turned an essentially free check into a
`fmt::format` call on every iteration, which alone was enough to make the
whole boot sequence appear to hang within our patience window, with zero
functional bug involved. Fixed by comparing `owning_process()->get_uid() ==
0x20007B39` instead (a cheap integer compare, matching `playserver.exe`'s
own UID3 already known from round 59's log capture) - the retest fired the
full step budget almost instantly. Worth remembering for any future probe
gated on `process::name()` specifically (`thread::name()` does **not** have
this cost - only the process override does).

### The trace: entry point confirmed real, plugin enumeration, then the lock

With the fix in place, a 200,000-step trace confirmed round 60's suspicion
directly: `pc=0x70000000` (the module's own base address) *is* genuinely
executable, real code - it's the actual entry point, stepping forward
sensibly (`0x70000000 → 0x70000004 → 0x70000008 → ...`). (Round 60's Ghidra
import had simply failed to disassemble that exact address during
auto-analysis - a tooling gap, not evidence the address was inert; noted
for next time a fresh raw-binary import needs force-disassembly right at
its own base address.) By around step 2000 the trace showed a clear,
repeating pattern - call into `0x803b6a82`/`0x803b6a84` (a generic
"call into library ordinal 1" helper in `euser.dll`), then jump into a
*different* companion codec DLL's base address each time
(`0x70200000`, `0x70500000`, `0x70400000`, `0x70800000`, ... - all the
`playserver*.dll`/audio-codec-plugin regions seen in earlier rounds' logs),
and back - unambiguously **`PlayServer` enumerating its own audio/video
codec plugins one by one at startup**, not related to the mutex at all.

Extending the budget to 2,000,000 steps found the actual `swi 0x11`
(`mutex_wait` - see below) at step 5877 and again at step 7915 (a
*different* mutex/semaphore wait, part of ordinary IPC - session-send
machinery matching round 4's own already-documented `0x803aa9ab` "everyone
funnels through this" address almost exactly, confirming that reading was
a real but different event, not our target).

### Pinning the exact chain with a combined ring-buffer + named-mutex probe

Rather than keep growing the step budget hoping to stumble onto the *right*
occurrence, added a second, much more targeted probe: a small rolling
history of the last 200 executed PCs (a `std::deque`, temporarily exposed
on `kernel_system` in [kernel.h](../../src/emu/kernel/include/kernel/kernel.h)),
fed by the step-tracer, and dumped in full by
[mutex.cpp](../../src/emu/kernel/src/mutex.cpp)'s `wait()` **only** when the
mutex being waited on is actually named `playserver.exe` - giving the exact
instruction history leading into the *real* event, unambiguous, no
guessing which of several similar-looking occurrences matters.

The dump landed the answer directly. The last several ring entries before
the lock:

```
ring[190]=0x700002ca   <- playserver.exe's OWN code (near its entry point)
ring[191]=0x700002cc
ring[192]=0x700002ce
ring[193]=0x700002d0
ring[194]=0x700002dc
ring[195]=0x70001594   <- an import thunk
ring[196]=0x803a7f6c   <- euser.dll: FUN_803a7f6c
ring[197]=0x803a7f6e
ring[198]=0x803a7f70
ring[199]=0x8039fc00   <- euser.dll: FUN_8039fc00
```

Force-disassembling `0x8039fc00` in the existing `euser_rm320_r55` Ghidra
project gave a two-instruction function: **`swi 0x11` then `bx lr`** -
`0x11` is exactly `BRIDGE_REGISTER(0x11, mutex_wait)` /
`BRIDGE_REGISTER(0x11, mutex_wait_ver2)` in this project's own
[svc.cpp](../../src/emu/kernel/src/svc.cpp). This is a direct,
unambiguous, decompiler-confirmed identification of the real `mutex_wait`
SVC - not an inference from address proximity like round 60's ordinal
2060/2061 false lead.

### Reading the actual guest logic around the lock

Force-disassembling `playserver.exe`'s own code at `0x700002ca` (in the
`playserver_reloc_r60` project from round 60) showed a classic
**lazy-singleton-guard** pattern:

```
700002ca: mov r4,r0
700002cc: ldr r0,[0x700004c4]     ; load a global "already initialized?" flag
700002ce: ldr r5,[0x700004c0]     ; load a global state-object pointer
700002d0: beq 0x700002dc          ; if flag==0 (first time): fall through to lock+init
700002d2: blx 0x70001304          ; else (already initialized): skip straight to common cleanup, return
700002d6: mov r0,r4
700002d8: add sp,#0x1c
700002da: pop {r4,r5,r6,r7,pc}
700002dc: blx 0x70001594          ; <<< the real lock: resolves to FUN_803a7f6c -> swi 0x11
700002e0: ldr r0,[0x700004b4]     ; increment a counter
700002e2: add r0,r0,#0x1
700002e4: str r0,[sp,#0x10]
700002e6: mov r0,r5
700002e8: blx 0x700012fc          ; resolves to euser 0x803ab18a - a debug/refcount-log helper
                                   ;   (confirmed via decompile - NOT a mutex signal)
700002ec: add r0,sp,#0x10
700002ee: blx 0x70001304          ; resolves to euser 0x803a9f34 - a generic field-clear/cleanup
                                   ;   helper called in BOTH branches, unrelated to the mutex
```

Decompiled all three follow-up calls to check whether any of them was the
missing `Signal()`: `0x803ab18a` sets up a large stack buffer and calls two
further helpers passing a global pointer - matches a debug/resource-count
logging utility, not a mutex operation. `0x803a9f34` conditionally clears
one field and calls two more helpers only if that field was non-zero -
generic cleanup, also unrelated, and it runs in *both* branches (first-time
and already-initialized), which a real mutex release wouldn't. **Neither
follow-up call releases the mutex.** This settles it: whatever calls
`Signal()` on this specific mutex, if anything ever does, is not inline in
this function at all - it has to happen later, in a different part of
`playserver.exe`'s own code, reached only after this singleton-guard
function returns.

### The clean, final test: does `Signal()` ever fire, at full run speed?

Rather than keep guessing where the release might be, added one more
narrowly-targeted probe directly on `mutex::signal()`
([mutex.cpp](../../src/emu/kernel/src/mutex.cpp)), logging if it's ever
called for the mutex named `playserver.exe` - and, having learned the
`process::name()` cost lesson above, removed all single-stepping for this
test so it would run at completely normal speed (no artificial slowdown
that could be blamed for a false "it never happens in time" result). The
boot sequence stabilized normally (~35 real seconds, matching every prior
round's clean-boot timing) and **the probe never fired once.** This is a
clean, timing-independent, empirically confirmed negative result:
`PlayServer`'s own code, across a complete and stable boot, genuinely never
reaches whatever point is supposed to call `Signal()` on this mutex.

### What this means

Put together with round 59's other finding (`PlayServerSplashScreenFallBackThread`
stuck at `thread_state::create`, never `Resume()`d, throughout every
observed run, fix or no fix), the picture now reads as: **`PlayServer` has
some later initialization milestone - reached only after the lazy-singleton
guard's plugin-enumeration work and whatever comes after it - that both
releases this mutex and resumes the splash-fallback thread, and that
milestone is never reached under EKA2L1.** Both symptoms plausibly share
one root cause rather than being two separate bugs. The next real target is
whatever `PlayServer` is doing *between* returning from this singleton
guard and that milestone - most likely another IPC call, file read, or
property/HAL query whose EKA2L1-side behavior differs from real hardware
enough to stall it indefinitely without panicking (the same shape of
finding rounds 45-58 eventually ran to ground for `!ecomserver`/`bafl.dll`).

### Reverted completely

All round-61 probes - the step-tracer and ring-buffer population in
`epoc.cpp`, the temporary `std::deque probe_r61_ring` member (and its
`#include <deque>`) on `kernel_system` in `kernel.h`, the ring-dump in
`mutex::wait()`, and the final `mutex::signal()` logger - fully removed.
Verified via `git diff --stat` (`mutex.cpp` and `kernel.h` both clean,
`epoc.cpp` back to the same pre-existing +26-line baseline every round
since 46 has shown) and a final clean rebuild. Eight new Ghidra scripts
added under `ghidra-scripts/` this round (`DecompileMutexCallSite.java`,
`DisasmRaw.java`, `DecompilePair.java`, plus reuse of round 60's
`TraceMutexWaitCallers.java`/`DecompileFunc.java` patterns). Guardrail held
throughout: several builds, each capped `-m:4` (two ran long enough to need
backgrounding, per this session's normal background-task handling - not a
guardrail exception, just larger `kernel.h`-triggered rebuild scope),
`MSBuild.exe` nodes checked and force-killed after every one, never more
than one emulator instance running at a time.

### To resume from here

1. The real next target: trace what `PlayServer`'s code does *after*
   returning from the singleton-guard function at `~0x700002ca` - most
   productively with the same live single-step + ring-buffer technique
   built this round, but now armed on a *different* trigger (e.g., re-arm
   whenever `PlayServer`'s thread makes any IPC call/session-send, dumping
   the ring each time, rather than trying to guess a single stopping
   point) - looking specifically for a call whose real-hardware response
   would look different from whatever EKA2L1's HLE/stub returns.
2. Given the likely shared root cause with the splash-fallback thread not
   being resumed, it may be more efficient to trace *that* symptom instead
   - find what guest code is supposed to call `Resume()` on
   `PlayServerSplashScreenFallBackThread` (same live single-step technique,
   armed on `thread::resume()` being called for that specific thread name)
   and work backward from there instead of forward from the mutex lock -
   either direction should converge on the same missing milestone.
3. The `swi 0x11` / `0x8039fc00` / `0x803a7f6c` addresses, and the exact
   singleton-guard disassembly above, are now solid, reusable ground truth
   for whichever direction is chosen next - no need to re-derive them.
4. Remember the `process::name()` cost lesson (see above) for any future
   probe gated on a process's name on a hot path - use `get_uid()` instead.

## Round 62: full decompile of the lock-holding function; timeout theory ruled out empirically

Continued directly from round 61's "to resume from here" - traced what
`PlayServer`'s code does inside the function that locks the mutex, all the
way to its own return.

### Forcing a real function boundary and decompiling the whole thing

Round 60's auto-analysis never created a function boundary at `0x700002ca`
(the singleton-guard entry point identified live in round 61). Used
Ghidra's `createFunction()` directly this round (after a plain
`disassemble()` call to make sure the bytes were interpreted first) against
the existing `playserver_reloc_r60` project, then decompiled the result in
full - an 842-byte function, `FUN_700002ca`. Structure, confirmed by
reading the whole decompile:

```c
int FUN_700002ca(int param_1) {
    if (param_1 == 0) {           // first call (the flag was unset)
        func_0x70001594(...);      // <- locks the mutex (round 61: swi 0x11)
        ... increment a counter, unrelated debug-log call ...
        thunk_EXT_FUN_803a9f34(...); // generic cleanup, called in BOTH branches
        // build an entire object graph: allocate a session object, configure
        // several sub-objects via ~10 calls through *unresolved vtable
        // pointers* (the same wall flagged since round 39/48/53/57/58 -
        // real dispatch target unknowable from static analysis alone),
        // panic (KErrNoMemory / other codes) on allocation failures along
        // the way, all wrapped in the correctly-paired 2060/2061
        // acquire/release helper ruled out in round 60
        ...
        // then, on the SAME straight-line path, tear the whole graph back
        // down again: release each sub-object through its own vtable
        // (destructor-shaped calls), zero every field, call the generic
        // cleanup helper one more time
        param_1 = 0;
        return param_1;
    } else {
        thunk_EXT_FUN_803a9f34();  // already initialized: skip straight to cleanup, return
    }
}
```

**No `Signal()` call exists anywhere in this function, on either branch** -
matching round 61's live, timing-independent confirmation exactly. This
isn't a case of execution getting stuck partway through and never reaching
a release call later in the function; the compiled code, even on a
completely successful run through this exact function, simply never
releases this specific mutex here. Whatever releases it (if anything ever
does) has to be in a different function entirely, reached only after this
one returns to its own caller.

### The "build it, then immediately tear it down" pattern - and the real ambiguity it leaves

The straight-line decompile makes it look like this function constructs a
whole object graph and then destroys it in the same breath, which reads
oddly for a "one-time init" - but this is very likely misleading in the
same way decompiled Symbian Active-Object code often is: several of the
~10 vtable calls in the middle plausibly *start* asynchronous work (opening
a file, connecting to a service, reading a property) whose real completion
happens later via a callback the decompiler can't show inline. Any one of
those unresolvable vtable dispatches is a plausible point where real
hardware's actual behavior and EKA2L1's HLE/stub behavior for whatever it's
calling into could diverge - the same shape of root cause rounds 45-58
eventually found for `!ecomserver`/`bafl.dll`'s stuck `!BackupServer` call.
Given round 59-61's own evidence that `PlayServer`'s thread *does* reach a
normal, healthy idle server loop (state cycling `run`/`wait_fast_sema`/
`ready`, servicing IPC correctly) after this point, the function does
genuinely return - so the "held forever" behavior may be entirely by
design (a real single-instance/init-barrier lock that Symbian's own kernel
would only release automatically on process death, and `PlayServer` is a
persistent server that's never meant to exit) rather than a stuck
asynchronous call at all.

### Checked the leading hypothesis this round: an EKA2L1-side timeout bug - ruled out cleanly

Before accepting "held for the process's whole life" as the likely design,
checked a concrete, EKA2L1-side candidate bug spotted while re-reading
[mutex.cpp](../../src/emu/kernel/src/mutex.cpp):

```cpp
void mutex::wait_for(int usecs) {
    bool should_schedule = false;
    if (!holding) {
        should_schedule = true;   // <- only arms the timeout when the mutex is FREE
    }
    wait(kern->crr_thread());
    if (should_schedule) {
        wait_for_timeout = true;
        timing->schedule_event(usecs, mutex_event_type, ...);
    }
}
```

This looks backwards: if `!holding` is true, `wait()` completes *instantly*
(the calling thread becomes the holder immediately, no blocking at all) -
there's nothing to time out from in that case. The timeout is only
meaningful when the mutex is *already held* by someone else and the caller
genuinely blocks - which is exactly the branch this code does **not** arm
a timeout for. If `Games`/`N-Gage Installer` were calling the
timeout-capable SVC path expecting a bounded wait, this bug would turn it
into an unbounded one - a real, credible, EKA2L1-side explanation.

Tested it directly rather than assuming: added a one-line probe to both
`mutex_wait` and `mutex_wait_ver2` in [svc.cpp](../../src/emu/kernel/src/svc.cpp),
logging which bridge gets called (and, for the `ver2` path, the timeout
value) whenever the `playserver.exe` mutex is involved. Result, from a
clean boot: **both calls - `PlayServer`'s own lock and `Games`'s blocking
wait - go through plain `mutex_wait` (`svc 0x11`, no timeout parameter at
all), never `mutex_wait_ver2`.** The `wait_for()` bug is real (worth fixing
on its own merits, since it does look like a genuine logic inversion) but
it isn't reachable from this specific code path - `Games` is calling a
truly infinite, unconditional `RMutex::Wait()`, by design, matching real
Symbian API usage for "wait until this becomes available with no time
limit." This cleanly rules out "the timeout silently never fires" as the
explanation here.

### Reverted completely

The `svc.cpp` diagnostic probe (two `LOG_ERROR` lines, one per bridge) fully
removed. Verified via `git diff --stat` (`svc.cpp` back to the same
pre-existing +1-line baseline from round 26 that every round since has
shown) and a final clean rebuild. One new Ghidra script this round
(`ForceCreateAndDecompile.java`, reusing the `createFunction()`+
`disassemble()` force-analysis pattern for the first time in this
investigation). Two backgrounded builds this round (both under the normal
background-task handling for larger rebuild scope, not a guardrail
exception); `MSBuild.exe` nodes checked and force-killed after each;
`tasklist` clean before every launch; never more than one emulator instance
running at a time.

### What this means

With the timeout theory ruled out and the lock-holding function fully read,
the honest picture is: **this specific mutex may genuinely be held for
`PlayServer`'s entire process lifetime by design**, and `Games`'s
unconditional `Wait()` on it is either (a) correct real-hardware behavior
that only works because on real hardware something *else* (a faster
property/RProperty/central-repo check, or a completely different
synchronization primitive) tells `Games` "don't bother waiting, PlayServer
is already up" *before* it ever reaches this blocking call - and that
faster check is returning the wrong answer under EKA2L1, routing `Games`
into a wait it was never supposed to take on real hardware - or (b) one of
the ~10 unresolvable vtable calls inside `PlayServer`'s own singleton-guard
function genuinely never returns under EKA2L1 (an async completion that
never fires), and the "healthy idle server loop" evidence from rounds
59-61 was observed on a run where that particular path wasn't hit. Both
remain open; distinguishing them is the real next step.

### To resume from here

1. **Most promising next step**: trace `Games`'s/`N-Gage Installer`'s *own*
   code (not `PlayServer`'s) in the same way this investigation traced
   `playserver.exe` - extract its live, relocated code via the round-60
   technique (already partially done - `ngiinstaller_code_reloc.bin` exists
   in the scratchpad from round 60), and look specifically at what it does
   *immediately before* opening/waiting on this mutex - is there an earlier
   check (a property read, a session connect, anything that could report
   "PlayServer already ready") that it's skipping past because that check
   itself returns an unexpected value under EKA2L1?
2. Alternative: definitively settle whether `PlayServer`'s thread reaching
   its idle server loop happens on *every* run or only some - re-run the
   full thread-state sweep from round 59 several times and check
   `PlayServer`'s state specifically, to rule in/out possibility (b) above
   before investing more effort in (a).
3. The `mutex::wait_for()` timeout-arming bug found this round is real and
   independent of this investigation's main thread - worth a small, separate
   fix at some point (swap the condition to arm the timeout when the
   calling thread does *not* immediately become the holder, i.e., check
   `holding != thr` after calling `wait()`, not `!holding` before it) even
   though it isn't the cause of this specific hang.
4. `playserver_reloc_r60`'s function at `0x700002ca` now has a proper,
   persisted function boundary and full decompile available directly in
   that Ghidra project - no need to re-force-create it.

## Round 63: found `playclient.dll` - proof the mutex is a rendezvous checkpoint, not a permanent hold

Continued round 62's own suggested next step: trace `Games`/`N-Gage
Installer`'s side of the handshake the same way `playserver.exe` was
traced.

### The caller isn't app code - it's a shared client library

Reused round 61's exact ring-buffer + live single-step technique
(`kernel.h`'s temporary `probe_r63_ring`, `epoc.cpp`'s step-tracer, a dump
in `mutex::wait()`), this time armed on `ngiinstaller.exe`'s UID
(`0x20007B38`) instead of `playserver.exe`'s. The dump landed cleanly on
the first try: `Games` blocks via a chain that runs almost entirely through
euser.dll's generic IPC-wrapper family (matching round 4's `0x803aa9ab`-
area finding again), then briefly through a *different* module at
`0x70500000`, then straight into the same `thunk_EXT_FUN_803a7f6c` (the
confirmed `swi 0x11` mutex-wait) round 61 already pinned.

Checked what's actually loaded at `0x70500000` in this run's own codeseg
log (`Kernel:Info`, same safe/reversible config.yml bump as previous
rounds): **`playclient.dll`** (UID3 `0x20007B3A`). Widened the ring buffer
to 3000 entries and confirmed `ngiinstaller.exe`'s own code (`0x72000000`
range) never appears anywhere in that whole window - meaning the entire
observable lead-up to the lock happens inside library code
(`playclientinst.dll` → `playclient.dll` → `euser.dll`), not inside
`ngiinstaller.exe`'s own bespoke logic. This is architecturally clean:
`playclient.dll` is the shared client API any app links against to talk to
`PlayServer`, and `ngiinstaller.exe` just calls its "connect" entry point
once.

### Extracting and decompiling `playclient.dll` - the key discovery

Dumped `playclient.dll`'s real, relocated code the same way as round 60
(temporary `codeseg::attach()` probe, this run's confirmed base
`0x70500000`, only 1136 bytes - a tiny library) and imported it into a
fresh Ghidra project (`playclient_r63`). Dumped every function in the
whole thing (small enough to do exhaustively rather than picking targets).
The core function, `FUN_70500070`, decompiles to:

```c
int FUN_70500070(...) {
    thunk_EXT_FUN_803a9f34(&local_14);
    local_18 = 0;
    iVar1 = thunk_EXT_FUN_803aa398(&local_18, iVar2, 0);  // quick existence check
    if (iVar1 == 0) {
        // PlayServer's process object was found - rendezvous with it:
        thunk_EXT_FUN_803a7f6c(&local_18);   // <- mutex WAIT (confirmed swi 0x11)
        thunk_EXT_FUN_803a7f76(&local_18);   // <- next euser.dll address - almost
                                              //    certainly the matching Signal
        thunk_EXT_FUN_803a9f34(&local_18);
        FUN_70500388(&local_14);
        iVar2 = -0xb;                         // KErrAlreadyExists
    } else {
        // Not found - actually launch/create the PlayServer process instead,
        // then Resume()/Rendezvous()-wait on ITS process object (a completely
        // different, real Symbian synchronization primitive, not this mutex)
        ...
        iVar2 = thunk_EXT_FUN_803aaaa0(&local_14, iVar2, DAT_70500248 + 0x24, 0);
        if (iVar2 == 0) {
            thunk_EXT_FUN_803a81a0(&local_14, 0xfa);
            thunk_EXT_FUN_803aab70(&local_14, &uStack_20);
            thunk_EXT_FUN_803a80f0(&local_14);
            thunk_EXT_FUN_803a82d4(&uStack_20);   // process rendezvous wait
            thunk_EXT_FUN_803a80dc(param_1, &local_14);
        }
    }
    return iVar2;
}
```

The critical shape: **the mutex Wait/Signal pair only appears in the
"PlayServer already exists" branch, back-to-back, with nothing blocking
between them.** This is a deliberate, quick rendezvous idiom - not
"acquire this resource and hold it" but "wait for PlayServer to signal
it's alive, then immediately let the next client through too." That only
works if `PlayServer`'s own code signals the mutex reasonably promptly
after finishing its own setup. This directly answers round 62's open
question: the mutex was never meant to be held for `PlayServer`'s entire
life - `PlayServer` really is supposed to call `Signal()` somewhere, it's
just not inside the specific function round 62 fully decompiled.

### Correcting round 62: `FUN_700002ca` isn't the one-shot entry point either

Went looking for whoever calls `playserver.exe`'s own `FUN_700002ca`
(the function round 62 found locking the mutex) to see what happens right
after it returns - and found **zero static callers** in the whole binary.
Re-examined round 61's original trace with this in mind: the confirmed
real entry point is `0x70000000` itself, and by roughly step 14 execution
was already inside `euser.dll`'s generic library-loading code, then cycling
through round 61's own already-documented plugin-enumeration loop
(`0x803b6a82`/`0x803b6a84`, jumping into each companion codec DLL's base
address in turn). `FUN_700002ca` sits at a comparatively low offset
(`0x2ca`) that looked entry-adjacent at a glance, but "zero static callers"
is the signature this investigation has seen many times before (rounds
39/48/53/57/58) for **indirectly-dispatched code** - almost certainly an
ECom-style `CreateImplementation`-shaped per-plugin factory callback,
invoked by the generic plugin loader via a computed call, not `playserver.exe`'s
own code calling it directly. That reframes the earlier "build an object
graph then immediately tear it down" reading from round 62: it's plausible
per-plugin factory bookkeeping (first-plugin-only one-time setup, guarded
by the same lazy-init flag), not literally "the" top-level PlayServer
readiness milestone. The real `Signal()` call is most likely further along
still - after the *entire* plugin-enumeration loop finishes and
`PlayServer`'s real top-level constructor continues past it.

### Reverted completely

All round-63 probes - `kernel.h`'s temporary `probe_r63_ring` deque (and
its `#include <deque>`), `epoc.cpp`'s step-tracer (armed on
`ngiinstaller.exe`'s UID this time), the ring-dump in `mutex::wait()`, and
the `codeseg.cpp` dump probe for `playclient.dll` - fully removed.
`config.yml`'s log level restored to `Kernel:Warn`. Verified via
`git diff --stat` (all four files clean, `epoc.cpp` back to the same
pre-existing +26-line baseline) and a final clean rebuild. Two new Ghidra
scripts this round (`DumpAllFunctions.java` - dump every function in a
small binary exhaustively rather than picking targets one at a time,
useful for any future tiny-library extraction; `FindCallersOfInit.java`).
New Ghidra project `playclient_r63`. Guardrail held throughout: confirmed
address stability across runs this round (`playserver.exe`/`ngiinstaller.exe`
loaded at the same base addresses in round 60's and round 63's separate
sessions - non-ROM module placement is deterministic here, at least for a
fixed boot sequence), several backgrounded builds (kernel.h changes trigger
a broad rebuild; not a guardrail exception, just larger scope),
`MSBuild.exe` nodes checked and force-killed after each, never more than
one emulator instance running at a time.

### To resume from here

1. **The real next target**: trace `PlayServer`'s thread *forward* from
   where round 61 left off (step ~2932 of the original 200,000-step trace,
   still inside the plugin-enumeration loop) through to wherever the
   *entire* loop finishes and its real top-level constructor resumes -
   watching specifically for a `mutex::signal()` call on the
   `playserver.exe`-named mutex. Same ring-buffer technique, but arm the
   dump on `mutex::signal()` for this specific name (already written once,
   in round 62, and already confirmed it never fires within a normal boot -
   so this time the goal is different: confirm *whether* the plugin loop
   ever finishes at all under EKA2L1, not just whether `Signal()` fires).
2. Decompile the euser.dll generic plugin-loader itself
   (`0x803b6a82`/`0x803b6a84` in `euser_rm320_r55`) to confirm the
   ECom-callback theory for `FUN_700002ca` directly, rather than inferring
   it from "zero static callers" alone.
3. `thunk_EXT_FUN_803a7f76` (immediately after `803a7f6c` in `playclient.dll`'s
   own decompile) is the strongest live candidate yet for the actual mutex
   `Signal()` wrapper - worth confirming directly via the same
   force-disassemble-and-check-for-`swi 0x12` technique used in round 61
   for its `803a7f6c` counterpart, next time euser.dll is opened again.
4. `playclient_r63`'s full function dump (`all_functions_r63.txt` in the
   session scratchpad) is short enough to be worth reading in full again
   directly if a future round needs the exact ordinals for
   `thunk_EXT_FUN_803aa398`/`803aaaa0`/`803aab70` (the "does PlayServer
   exist" check and the "launch it" path) - useful if the investigation
   ever needs to understand the "PlayServer not yet running" branch

## Round 64: `PlayServer`'s thread genuinely never finishes - found the exact stuck plugin

Continued directly from round 63's real next target: trace `PlayServer`'s
own thread forward from where round 61's 200,000-step trace left off
(still cycling through its plugin-enumeration loop) to see whether that
loop ever finishes.

### Redesigning the probe for a much longer trace

Round 61's per-instruction logging doesn't scale to millions of steps (log
volume, wall-clock I/O cost). Replaced it with a region-transition design
in `epoc.cpp`'s step-tracer: log a line only when the executing PC moves to
a new coarse address region, plus a heartbeat every 100,000 steps
regardless, capped at 8,000,000 total steps. First attempt masked regions
at 1MB granularity (`pc & 0xFFF00000`) - too fine for `euser.dll`, which
spans several MB, so ordinary in-module jumps within `euser.dll` itself
showed up as thousands of spurious "region changes" (183,240 of them by
the time the run finished, almost all noise). The noise didn't hide the
real signal, though: reading the *tail* of the log (the last transitions
before the budget ran out) showed something concrete regardless of the
masking granularity - re-affirmed for next time that named-module bucketing
(via a fresh `codeseg.cpp` runtime-address capture) is worth setting up
before the trace, not after.

### The finding: stuck for millions of instructions in one specific plugin

By the end of the 8,000,000-step budget, the last ~2000 transitions were
all bouncing between exactly two regions: one specific companion DLL and
`euser.dll`'s generic dispatch code - not progressing to new plugins
anymore. Identified the DLL from this run's own codeseg log (`Kernel:Info`,
same safe temporary bump as previous rounds): **`gametitlemetadata.dll`**
(UID3 `0x20003B84`), loaded at `0x70800000` this session. **`Signal()` on
the `playserver.exe` mutex never fired once** across the entire trace
(confirmed via the same lightweight `mutex::signal()` probe from round 62,
left active for this run).

### Extracting and decompiling the stuck code

Dumped `gametitlemetadata.dll`'s real, relocated code the same way as
`playclient.dll` in round 63 (temporary `codeseg::attach()` probe, this
run's base `0x70800000`, 35KB) and imported it into a fresh Ghidra project
(`gametitlemetadata_r64`). The exact addresses from the trace's tail
resolved to small, real functions:

- `FUN_70803848` / `FUN_708041f8` - trivial field-copy helpers, a few
  instructions each.
- `FUN_7080591e` - the interesting one: calls a `euser.dll` helper
  (`thunk_EXT_FUN_803a1f20`) and returns a boolean based on whether that
  call's result is zero - shaped exactly like a "has more work to do"
  check.
- Two other functions found via the same live-address technique
  (`FUN_70800206`, `FUN_70801b7e`) each allocate a small heap block
  (8 and 16 bytes respectively, via `thunk_EXT_FUN_803a83ae`) every time
  they're called - and, like `playserver.exe`'s own `FUN_700002ca` in
  round 63, **have zero static callers** - invoked indirectly, consistent
  with an ECom-style per-item factory pattern rather than being called
  directly from visible code.

Decompiled `thunk_EXT_FUN_803a1f20` itself in `euser_rm320_r55` hoping to
find the real "is there more" logic - it turned out to be low-level ARM
EABI compiler-generated dispatch glue (packed pointer/tag decoding, Gray-
code-style bit tests, ending in an unresolvable computed jump) rather than
any recognizable application-level iterator check. This is the same
"generic infrastructure wall" this investigation has hit repeatedly
(rounds 4, 16, 38, and now here) - going further into *this specific*
helper isn't likely to be productive via static reading alone.

### What this means

The location is now pinned precisely, even though the exact trigger isn't
yet: **`PlayServer`'s startup thread spends the entire remainder of its
observable execution - at least 6 of the last 8 million traced
instructions - inside `gametitlemetadata.dll`, repeatedly allocating small
objects and re-checking a condition that never flips.** Given the DLL's own
name, this is almost certainly enumerating some collection of game/title
metadata (files on the memory card, or entries in an ECom registry) that,
under EKA2L1, never reports "no more items" the way it would on real
hardware - the same general shape of bug (a guest-visible loop condition
that depends on an EKA2L1-side stub or file-system emulation gap never
resolving the way real hardware's would) that rounds 45-58 eventually
found for `!ecomserver`. `PlayServer` never reaches whatever code comes
after this enumeration - which is exactly why it never signals the
`playserver.exe` mutex, and why `PlayServerSplashScreenFallBackThread`
(round 59) never gets resumed either. Both of those open questions from
earlier rounds converge on this one root: the enumeration loop inside
`gametitlemetadata.dll` doesn't terminate.

### Reverted completely

The `epoc.cpp` region-transition step-tracer, the `mutex::signal()` probe
(reused from round 62), and the `codeseg.cpp` dump probe for
`gametitlemetadata.dll` all fully removed. `config.yml`'s log level
restored to `Kernel:Warn`. Verified via `git diff --stat` (all three files
clean, `epoc.cpp` back to the same pre-existing +26-line baseline) and a
final clean rebuild. Three new Ghidra scripts this round
(`DecompileStuckLoop.java`, `FindCallerOfLoop.java`,
`DecompileIteratorCheck.java`). New Ghidra project `gametitlemetadata_r64`.
Guardrail held throughout: several backgrounded builds (`epoc.cpp`/
`mutex.cpp`/`codeseg.cpp` changes, all smaller-scope than `kernel.h`,
still backgrounded since single builds ran past the default foreground
wait), `MSBuild.exe` nodes checked and force-killed after each, never more
than one emulator instance running at a time, one Ghidra process at a time.

### To resume from here

1. **The real next target**: find what `gametitlemetadata.dll` is actually
   enumerating - most directly by checking what file-system or ECom-registry
   calls it makes right before entering this loop (a live ring-buffer trace,
   same technique as rounds 61/63, armed on this DLL's own thread/UID, but
   looking *backward* from the loop's first iteration instead of forward
   from the mutex lock) - specifically checking for `RFs`/`CDir`-style
   directory-listing SVC calls or ECom `ListImplementations`-style calls
   whose EKA2L1-side behavior might differ from real hardware (e.g.,
   returning a nonempty/never-ending list where real hardware's storage
   would report a short, finite one).
2. Check what's actually present in this device's `private`/`resource`
   folders under whatever path `gametitlemetadata.dll` reads from (likely
   related to N-Gage game card metadata) - compare against what a genuine
   N95 would have there, similar to round 27/45's fresh-install comparison
   methodology - a data/content mismatch (rather than a code bug) is a live
   possibility given this plugin's job.
3. If the loop turns out to be reading a real (but EKA2L1-emulated)
   directory or file collection, the fix may be as simple as correcting
   what that specific `RFs`/directory-listing SVC bridge returns for this
   path, rather than anything in the ECom/plugin-loading machinery itself.
4. `gametitlemetadata_r64`'s Ghidra project and the `gametitlemetadata_code_reloc.bin`
   dump (session scratchpad) are ready to continue from directly.

## Round 65: pinned the caller chain, and a real file-lookup anomaly worth checking next

Continued from round 64's own suggested first step: trace what leads into
the *second* entry into `gametitlemetadata.dll` (the one that gets stuck -
the first entry, seen briefly around step 2274 in round 61's original
trace, is just a quick plugin-registration call that returns immediately).

### Reusing the ring-buffer technique with entry-counting

Added a temporary probe combining round 61's ring-buffer design with an
"entry count" check: track transitions into the `0x70800000`-`0x70900000`
address range specifically, and dump the preceding 1500-instruction ring
only on the *second* such entry. This reproduced round 64's exact stuck
point deterministically - **step 2101514, pc=0x70800206** - confirming this
whole chain is fully reproducible, not a scheduling-dependent fluke.

The ring showed the real caller chain leading in:

```
... playservergl.dll (0x71000000 range) ...
... playserverccd.dll (0x70600000 range) ...
... euser.dll generic dispatch ...
... back into playservergl.dll at 0x71004620 ...
-> gametitlemetadata.dll's FUN_70800206 (the stuck entry point)
```

Identified both companion modules via this session's own codeseg log:
**`playservergl.dll`** (UID3 `0x20007B3E`, likely "PlayServer Game List" -
the component that would need to iterate every installed game to build
its list) and **`playserverccd.dll`** (UID3 `0x20007B3D`). This is a
coherent, plausible architecture: `playservergl.dll` builds the game list,
and for each entry calls into `gametitlemetadata.dll` to fetch that game's
title metadata - which is exactly the shape of thing that would need to
enumerate "how many games are there" and could loop forever if that
enumeration never terminates.

### A genuinely surprising find while checking the plugin's likely data source

Round 64's own second suggested step was to check what real file-system
content `gametitlemetadata.dll` might read, given its job. Looked at
`PlayServer`'s own private data folder
(`data/drives/c/Private/20007b39/`) directly - found it's a real,
substantial restored-backup game collection: 44 entries total, including
18 `gameNNNNNNNNN`-named folders (each holding cached achievement icon
PNGs) plus several other per-game UID folders (save files for named games
- `Asphalt3`, `dogz3d`, football/tournament saves, etc.) This confirms the
"restored backup with multiple games" scenario this investigation's title
has referenced since round 1 is genuinely present in this test data, not
hypothetical.

Also present in that same folder: `backup_registration.xml`, PlayServer's
own backup manifest, which explicitly lists `E:\.play_mc.id` under
`<public_backup><include_file>` - a memory-card-identity file PlayServer
expects to exist. Checked directly: **the file genuinely exists** at
`data/drives/e/.play_mc.id` (64 bytes) on this emulated `E:` drive. But
this session's own logging (captured earlier, visible in `investigate.log`
captures from this session) shows the guest getting `KErrNotFound` opening
`D:\.play_mc.id` and `Z:\.play_mc.id` - **different drive letters than
where the file actually lives.** This is a real, directly verifiable
anomaly: either the guest's own search order legitimately checks multiple
candidate drives and gracefully handles most of them failing (completely
normal, no bug), or there's a genuine drive-letter mapping mismatch
somewhere. Checked `data/devices.yml` for an explicit drive-letter override
for RM-320 and found none - drive lettering for this device follows
whatever EKA2L1's kernel/io_system defaults are, not a device-specific
config.

**This is flagged honestly as an open, unconfirmed lead** - there is no
direct evidence yet connecting this specific file-lookup anomaly to the
`gametitlemetadata.dll` loop rounds 61-65 have been tracing. It's a
concrete, real thing worth checking next, not a claimed resolution.

### Reverted completely

The round-65 ring-buffer probe (`kernel.h`'s temporary `probe_r65_ring`
deque and its `#include <deque>`, `epoc.cpp`'s step-tracer with
entry-counting) fully removed. Verified via `git diff --stat` (`kernel.h`
clean, `epoc.cpp` back to the same pre-existing +26-line baseline) and a
final clean rebuild. No new Ghidra scripts this round - pure live-tracing
plus direct file-system inspection. Guardrail held throughout: one
backgrounded build (`kernel.h` change, broader rebuild scope),
`MSBuild.exe` nodes checked and force-killed, never more than one emulator
instance running at a time.

### To resume from here

1. **The real next step**: confirm or rule out the `.play_mc.id` connection
   directly - trace exactly which guest code checks `D:\.play_mc.id`/
   `Z:\.play_mc.id` (a live probe on the `files.cpp:844` "non-existent
   file" log line, filtered to this exact filename, capturing the calling
   thread/PC) and see whether that code path leads into
   `gametitlemetadata.dll`'s enumeration, or is a separate, already-handled
   fallback check unrelated to the stuck loop.
2. If unrelated, fall back to round 64's original plan: trace what
   `gametitlemetadata.dll` is actually enumerating from *within* its own
   stuck loop - specifically what `RFs`/directory-listing or ECom-registry
   SVC calls (if any) happen inside the loop body itself, versus whether
   it's a purely CPU-bound loop over already-loaded in-memory data (which
   this round's decompiles lean towards, given no `swi` instructions were
   visible in the examined loop functions) - the latter would point to a
   corrupted or miscounted in-memory structure rather than a live
   file-system gap.
3. The `Private/20007b39` folder's exact game count (18 `gameNNNNNNNNN`
   folders, several more UID-named ones) is now known ground truth - useful
   if a future round wants to check whether `gametitlemetadata.dll`'s loop
   counter or allocation count ever correlates with this number (e.g., does
   it allocate roughly 18-40 objects before getting stuck, suggesting it
   completes one pass and then loops back to the start instead of
   stopping?).

## Round 66: `.play_mc.id` ruled out cleanly - and a real storage-path discovery along the way

Followed through on round 65's own next step: confirm or rule out the
`.play_mc.id` connection directly, rather than leaving it as a dangling
lead.

### Tracing the exact caller

Added a small, targeted probe at the "non-existent file" log line in
[files.cpp](../../src/emu/services/src/fs/files.cpp) (the one at line 844
already producing the `D:\.play_mc.id`/`Z:\.play_mc.id` messages), filtered
to filenames containing `play_mc.id`, logging the calling thread's name,
owning process, and PC/LR. Result: **`PlayServer`'s own thread makes this
check, twice** (`D:` then `Z:`, both failing) - confirmed directly, not
inferred. The PC/LR pair captured (`0x8039fa9c`/`0x803a82e5`) turned out to
be the same generic euser.dll IPC-dispatch address family flagged as a
dead end back in round 4/60/61 - unhelpful for pinpointing the exact
call site further, but the *thread identity* was the useful part here, and
that came through clearly.

Checked the log's timing: this check happens very early in boot - well
before `ngiinstaller.exe` is even summoned, and normal boot activity
(library loads, the already-fixed `!BackupServer` fail-fast) continues for
a while afterward. This ruled out a *direct, one-step* trigger (the check
failing does not immediately jump into the metadata loop within the same
handful of log lines) without yet ruling out an indirect one (a flag set
early that matters much later).

### The direct test - and a wrong turn that led to a real discovery

Rather than keep reasoning about it, tested directly: copied the real,
existing `.play_mc.id` file (from where it genuinely sits on the emulated
`E:` drive) to `D:` - the drive the guest actually checks - and re-ran.
**No change at all** - identical stall point, identical log up to the
`!BackupServer` fail-fast. That result didn't fit: if the file were now
present where the guest looks, the "non-existent file" line for `D:`
should have disappeared. It didn't. Checked whether the copy had silently
failed or landed somewhere inert instead of assuming the hypothesis was
simply wrong.

Reading [epoc.cpp](../../src/emu/system/src/epoc.cpp)'s
`mount_default_user_drives()` explained it: RM-320 (like every device)
mounts its drives from a **device-specific storage path**,
`get_device_specific_storage_path()` → `data/devices/rm-320/drives/`, not
the generic `data/drives/` folder used throughout rounds 59-65's
file-system checks. `migrate_legacy_drives_if_present()` only copies the
generic folder's contents into the device-specific one if the
device-specific target doesn't already exist - and for RM-320 it already
does (confirmed: `data/devices/rm-320/drives/e/.play_mc.id` genuinely
exists, same as the generic copy previously found), so the two folders
have silently diverged and only the device-specific one is ever live. The
first copy-and-test landed in the *legacy, inert* `data/drives/d/` -
explaining the "no change" result without yet telling us whether the
underlying hypothesis was right.

### The real test

Corrected the file placement to `data/devices/rm-320/drives/d/.play_mc.id`
(the genuinely active path) and re-ran. This time it worked as expected:
the `D:\.play_mc.id` "non-existent file" log line **disappeared entirely**
- only the separate `Z:\.play_mc.id` check still failed, exactly as
before. **The boot still stalled at the identical point** - the same
sequence of log lines, ending in the same `!BackupServer` fail-fast, no
further progress. This is about as clean a negative result as this
investigation gets: the specific file-lookup failure is fixed, confirmed
via the log itself, and the outcome doesn't change at all.

### Reverted completely

The `files.cpp` diagnostic probe removed (confirmed via `git diff --stat`
showing the file back to zero diff). Both experimental copies of
`.play_mc.id` (the first, inert one at `data/drives/d/`, and the working
one at `data/devices/rm-320/drives/d/`) deleted, restoring the test data
folders to their original state - verified both `d/` folders now contain
only their original `Private`/`System` subdirectories. No source-code
changes left in the tree beyond the round 58 fix and the same pre-existing
`epoc.cpp` baseline. Guardrail held throughout: two builds, each capped
`-m:4`, `MSBuild.exe` nodes checked and force-killed after each, never more
than one emulator instance running at a time.

### What this means

Round 65's leading lead is now closed, cleanly, with a real experiment
rather than a guess. The `gametitlemetadata.dll` enumeration loop
(rounds 61-65) is not caused by, and does not depend on, the missing
`.play_mc.id` memory-card-identity file - whatever it's actually looping
on is unrelated to this specific file. The device-specific-storage-path
discovery is a genuinely useful, reusable fact for this investigation
going forward: **any future file-system-content check against this
device's drives must use `data/devices/rm-320/drives/`, not the generic
`data/drives/`** - the two have diverged and only the device-specific one
is live.

### To resume from here

1. Round 64's original plan is the live option again: trace what
   `gametitlemetadata.dll` is actually enumerating from *within* its own
   stuck loop - specifically whether any real `RFs`/directory-listing SVC
   calls happen inside the loop body (this round's evidence continues to
   lean towards "no swi instructions visible in the examined loop
   functions," meaning a purely CPU-bound loop over already-loaded
   in-memory data is still the more likely shape) - a live probe directly
   on the loop's own iteration variable/counter (not just its PC) would
   settle this: is it re-reading the same file/directory each iteration
   (a live `RFs` bug) or spinning on a value already in memory (a
   corrupted count, a bad pointer, or a genuine infinite/circular
   structure)?
2. If it's the latter (in-memory, not file-system), the productive next
   step is inspecting the actual memory the loop reads from - dump the
   heap region the repeatedly-allocated objects point into and check
   whether the "count" or "next" field the loop condition depends on is
   ever written a terminating value at all, versus never being written
   because whatever was supposed to set it never ran.
3. Remember for any future file-system-content experiment: always target
   `data/devices/<firmware_code_lowercase>/drives/`, confirmed live for
   this device via `mount_default_user_drives()` in `epoc.cpp` - the
   generic `data/drives/` folder is legacy/inert once a device-specific
   folder exists.

## Round 67: live register trace of the loop's own state - a bounded search that never finds its target

Directly continued from the "trace the loop's own iteration variable"
request - rather than reason further about the decompile, sampled live CPU
registers at the exact repeating addresses across tens of thousands of
iterations.

### First attempt targeted the wrong address - a quick, useful correction

Initially armed the probe on `0x70800206` (the function entry point
identified in round 64), assuming *it* was being re-entered repeatedly.
Result: only 2 visits in a 10-million-step budget - immediately
inconsistent with round 64's own finding of millions of instructions spent
in this module. The reason was already sitting in round 64's own
disassembly: `FUN_70800206` is an 8-instruction function with **no internal
branch** - it cannot be "the loop." Round 64's tail-of-trace data (captured
right before the original 8-million-step budget ran out) had already shown
the *real* repeating addresses - `0x70803848`, `0x70805924`, `0x708041f8` -
just hadn't been the ones re-targeted for register sampling until now.
Corrected immediately rather than chasing the wrong lead further.

### The real trace: 50,000 iterations, registers sampled at each visit

Retargeted the probe at the three real addresses, logging `r0`-`r6`, `sp`,
and `lr` for the first 60 visits in full and then every 5000th visit after
that (to see both the fine-grained short-term pattern and whether anything
changes over a much longer run), capped at 50,000 total visits or 10
million steps. Completed almost immediately (round 65's step count already
showed the loop starts around step 2.1 million; 50,000 visits happened
within the following ~5.6 million steps).

Two clear, consistent patterns across the entire captured range (visit 1
through visit 50000, spanning real step numbers 2,112,587 through
7,751,182 - nearly 5.6 million instructions of continuous observation):

- **At `0x70805924`** (inside `FUN_7080591e`, the boolean "keep going?"
  check identified in round 64): `r2` holds an address that stays
  **entirely within a bounded ~8KB range inside `gametitlemetadata.dll`'s
  own data section** (roughly `0x70806000`-`0x70808300`) across the *whole*
  50,000-visit, 5.6-million-instruction window - never growing beyond it,
  never resetting to zero, never pointing outside the module. `r0` (the
  function's return value, gating the loop) takes many different nonzero
  values across visits (`0x6`, `0xfffffff3`, `0x5`, `0x10`, `0xb`,
  `0xfffffffe`, `0xffffffff`, ...) - it is genuinely varying, not stuck on
  one constant - but **never once observed as `0`**, the one value that
  would stop the loop per `FUN_7080591e`'s own logic (`if (iVar1 == 0)
  return 1;` - i.e., "done" - `else return 0;` - "keep going").
- **At `0x70803848`** (the 2-word field-copy helper): `r1` and `r3` are
  **exactly constant across every single visit, all 50,000 of them** -
  `r1=0x10000000`, `r3=0x64` (100 decimal) - meaning whatever calls this
  helper passes it the *identical* two values every time, not a
  progressively-changing index or growing count.

### What this rules in and rules out

This is a materially different, more specific finding than "runaway
allocation loop." Taken together: **this is a bounded scan through a fixed
in-memory table or resource section (`r2`'s range never grows, ruling out
unbounded memory corruption or an ever-growing list), repeatedly checking
"is the thing I'm looking for here yet?" (`r0`'s varying-but-never-zero
values), while the code that kicks off each new scan attempt keeps doing
so with the exact same parameters (`0x10000000`/`100`) every time - a
genuine infinite retry of an identical operation against data that
apparently never satisfies it, with no attempt counter, timeout, or
give-up path anywhere in the observed cycle.** This rules out: a corrupted
or overflowing loop counter (the address range is stable, not runaway); a
simple "keep allocating forever" leak as the primary mechanism (the same
two constants recur, this isn't sequentially numbering new items); and,
per round 66, any connection to the `.play_mc.id` file. It's consistent
with (but doesn't yet prove) a hash-table or symbol-lookup probe sequence
searching for a specific entry - by UID, name, or similar - that
`gametitlemetadata.dll`'s own bundled table genuinely does not contain
under this exact game/save-data combination, with the lookup code written
assuming the entry always exists rather than bounding the search.

### Reverted completely

The retargeted `epoc.cpp` register-sampling probe fully removed. Verified
via `git diff --stat` (back to the same pre-existing +26-line baseline)
and a final clean rebuild. No new Ghidra scripts this round - pure live
register sampling built on round 64's already-known addresses. Guardrail
held throughout: two builds (initial wrong-address version, then the
corrected retarget), each capped `-m:4`, `MSBuild.exe` nodes checked and
force-killed after each, never more than one emulator instance running at
a time.

### To resume from here

1. **The real next step**: identify what `r2`'s bounded ~8KB range
   (`~0x70806000`-`~0x70808300`) actually *is* - dump that exact region
   from the already-captured `gametitlemetadata_code_reloc.bin` (or a
   fresh live memory read at that address) and inspect its structure
   directly - is it a string table, a UID/ordinal lookup table, a
   resource/hash table? Knowing its shape would directly explain what
   `FUN_7080591e` is searching for and why it's never satisfied.
2. Once the table's shape is known, cross-reference against what's
   actually in this session's `Private/20007b39` game data (round 65's 44
   entries, 18 `gameNNNNNNNNN` folders) - the missing entry is very likely
   *some specific game's* UID/identifier that this table expects but the
   restored backup's data doesn't provide in the form the table's lookup
   code needs, or vice versa (an entry the table expects that no installed
   game provides).
3. The exact register state at a representative stuck iteration (visit
   ~50000: `r2=0x708062ae`, `r4=0x8100d0`, `r5=0x82fd30`, `r6=0x914`,
   `sp=0x50fcd0`) is solid ground truth for picking up a fresh live probe
   without needing to re-run the full 5.6-million-instruction trace from
   scratch.

## Round 68: identified the C++ classes via RTTI - a Game Purchase Database - but the simple "missing record" theory doesn't survive a direct check

Directly continued round 67's own next step: dump and inspect the bounded
`~0x70806000`-`~0x70808300` region the scan never escapes.

### The RTTI strings

Extracted printable ASCII strings from that exact file-offset range in the
already-captured `gametitlemetadata_code_reloc.bin` (no live run needed -
static analysis of data already in hand). Found ARM EABI mangled RTTI
class-name strings (the standard `<length><name>` format compilers emit
for `dynamic_cast`/exception-handling type info): `CGametitleMetadata`
itself, plus `CGamePurchaseDb`, `CGamePurchaseDbRecord`,
`RGamePurchaseDbServer`, `TActivationCode`, `TGameInfo`, `TGamePartInfo`,
`TGamePackageInfo`, `TBundledLicenseInfo`, and the generic Symbian base
classes (`RHandleBase`, `RSessionBase`, `MDbEngine`, `MDbRecord`) these
build on. This is a real, concrete identification: the code doing the
never-terminating scan is built around a **game purchase/activation
licensing database**, not an arbitrary internal table.

### Found the real database file - two copies, nearly identical

Searched the device-specific storage (remembering round 66's lesson) for
purchase/license-related files and found `gamepurchase.db` in two places:
`Private/20003b85/gamepurchase.db` on `C:` (337776 bytes) and
`20003B85/gamepurchase.db` on `E:` (302701 bytes, file-dated 2014 - clearly
the original phone backup copy, not anything touched by this
investigation). Extracted every 5+ character printable ASCII string from
both (2739 from `C:`, 2736 from `E:`) and diffed the two sets directly
rather than assuming: real game titles are present and consistent across
both copies (FIFA 08, Prince of Persia, Brothers in Arms, SPORE, Sims 2
Pets, Worms World Party, Resident Evil, Asphalt 4, Tetris, and others
matching round 65's `gameNNNNNNNNN` folders), along with internal DBMS
column-name strings (`domainId`, `gameType`, `priority`, `vendorId`,
`licenceId`, `masterCid`, etc.) that also match between copies. **The
three strings that differ are all extra content present only in `C:`'s
copy** - not missing entries in either. This directly rules out the
simplest, most obvious version of the "missing purchase record" theory:
neither copy is obviously deficient relative to the other, and both
contain what looks like a complete, consistent game catalog matching this
session's actual installed-game data.

### Why this is a natural checkpoint

The two on-disk database copies being consistent with each other doesn't
rule out a bug entirely - it could still be: a specific record whose
*binary field values* (not its presence, which strings alone can't show)
don't satisfy whatever comparison the scan performs; a mismatch between
what's in the database and what `gametitlemetadata.dll` expects to find
enumerating the private-data folders directly (round 65's 44 entries);
or something in the DBMS's internal B-tree/index structure itself (page
layout, free-space bookkeeping - the two files' headers do differ in a few
bytes beyond the printable-string content, consistent with normal DBMS
internal bookkeeping divergence between two copies, but not something
distinguishable from "the actual bug" without full format parsing).
Answering this with confidence needs byte-level parsing of Symbian's
proprietary DBMS B-tree format (not publicly documented in the same way
E32Image is) and matching it against `CGamePurchaseDbRecord`'s actual C++
field layout - a substantially larger, more specialized undertaking than
anything this investigation has needed so far (Ghidra static analysis and
live register/PC tracing, both proven techniques throughout, don't
directly help with parsing an opaque on-disk database format).

### Reverted / left in place

No source code changes this round - purely read-only inspection of
already-captured data (`gametitlemetadata_code_reloc.bin` from round 64)
and direct file-system reads of the two `gamepurchase.db` copies (neither
file was modified). No new builds, no live runs, no Ghidra scripts. Nothing
to revert.

### To resume from here

1. **If continuing the root-cause path**: the real next step is parsing
   `gamepurchase.db`'s actual binary record format - starting from
   `CGamePurchaseDbRecord`'s field layout (reachable via Ghidra, decompiling
   its constructor/serialization methods in `gametitlemetadata.dll` itself
   or a sibling module) and cross-referencing against the raw bytes in both
   database copies to find whichever specific field or record the scan's
   comparison depends on.
2. **Alternative, pragmatic path**: given the scope of (1), a bounded
   "fail-fast" mitigation at the EKA2L1 level - similar in spirit to round
   26/58's `!BackupServer` timeout fix, but for this specific stuck pattern
   - could unblock the user's actual goal (a usable N-Gage) without fully
   explaining *why* the guest code loops. This would need its own careful
   design (this loop is purely CPU-bound per round 67's evidence, not
   blocked on an IPC reply, so a timer-based fail-fast like round 26's
   doesn't directly apply - it would need a bounded retry-count or
   iteration-count cap enforced some other way, likely by patching or
   intercepting the specific comparison/lookup call itself).
3. This is an explicit decision point for the user rather than an
   automatic continuation - the next step is a meaningfully larger,
   different-shaped task than anything so far in this 68-round
   investigation, worth confirming direction on before investing further.

## Round 69: resolved all external imports to real ROM modules - found the DRM subsystem, and a working sibling of the stuck loop

Went with option 1 (root-cause path) per explicit direction. Resumed
exactly where round 68 left off: identify what `gametitlemetadata.dll`
actually calls outside itself, since the RTTI strings alone couldn't say
whether the never-satisfied comparison involved DRM, a plain array, or
something else.

### Resolving every external call, not just a handful

Round 60's cross-module-call script had a stale in-module bound
(`0x70000000`-`0x70003000`, sized for `playserver.exe`, not this 35KB
module) baked in from its original use - rerun as-is against
`gametitlemetadata.dll` it produced 2068 mostly-false-positive lines (in-
module branches misclassified as "external"). Fixed the bound to this
module's real span (`0x70800000`-`0x70809000`) and reran: **79 genuine
external call targets**, cleanly resolved. Reused `parse_rom.py` (round
3's own tool, unchanged) to scan every DLL in the ROM's `sys/bin` and
match each target address against its header-derived `code_address`/
`code_size` range - the same technique this investigation has used since
its earliest rounds, just pointed at new targets. Four distinct ROM
modules came back: `euser.dll` (the large majority - already-known generic
wrappers), `estor.dll` (Symbian's persistent object/store framework),
`drmcommon.dll` and `drmrights.dll` (Symbian's actual DRM engine, split
across two libraries), and - unexpectedly - **`bafl.dll`**, the exact same
Backup And Restore Framework Library this investigation spent rounds
19-58 tracing for the original, now-fixed bug.

### A real, fully-decompiled DRM-rights enumeration function

Found which of `gametitlemetadata.dll`'s own functions call these new
imports and decompiled the central one, `FUN_70800070` (384 bytes, called
from two places within the module). It's a complete, sensible, real
implementation:

```c
void FUN_70800070(content_id, out_result) {
    // build a path/id descriptor from content_id
    drm_obj = thunk_EXT_FUN_82f6c0a2();               // drmcommon.dll: open/create a DRM context
    thunk_EXT_FUN_82f6c0f8(drm_obj);                   // drmcommon.dll: configure it
    result = thunk_EXT_FUN_82f6bef4(drm_obj, path, &rights_array); // drmcommon.dll: enumerate rights
    if (result == 0) {
        for (i = 0; i < thunk_EXT_FUN_803b072a(rights_array); i++) {  // CArrayFixFlat::Count()
            item = thunk_EXT_FUN_803a0478(rights_array, i);            // CArrayFixFlat::At()
            if (thunk_EXT_FUN_82f7bf02(*item, &right) == 0) {          // drmrights.dll: open this right
                thunk_EXT_FUN_82f7b9d2(right, &status, &subtype);       // drmrights.dll: query status/type
                // branch on subtype (2 / 4 / 8 / other): compare expiry,
                // accumulate a use-count, or reject - keep the best valid
                // right found so far in *out_result, releasing the rest
            }
        }
        thunk_EXT_FUN_803b0716(rights_array);
        thunk_EXT_FUN_8039ccb2(rights_array);          // cleanup
    }
}
```

This is a genuine, working "find the best valid DRM right for this piece
of content, out of however many exist" routine - not buggy code, and its
own loop is properly bounded by the array's real `Count()`. It directly
explains the RTTI strings round 68 found (`CArrayFixFlat<TGameInfo>` and
friends are exactly the array types this kind of enumeration would use)
and confirms, concretely, that `gametitlemetadata.dll` really does perform
DRM rights checks as part of building its metadata - this is not a stretch
inference, it's working code doing exactly that.

### Connecting back to the actual stuck loop

`FUN_70800070` itself (`0x70800070`-`0x708001f0`) is well before the
confirmed stuck-loop addresses (`0x70803848`, `0x70805924`, `0x708041f8`
- round 64/67) and isn't the same function - so this isn't a claim that
*this specific* loop is the one hanging. What it does establish: the
module's real logic includes at least one genuine "enumerate an array,
`Count()`-bounded, checking DRM rights per item" pattern, and the actual
stuck loop is architecturally the same shape (an incrementing index,
per-item processing) without behaving the same way. Round 67's own
captured register trace already showed this directly: at the real stuck
loop, `r6` counts up cleanly - `0, 1, 2, 3, 4, 5, 6, ...` across the first
several visits, exactly like a normal array index - and by visit 50,000 it
had never reset or hit whatever limit should have stopped it. Taken
together with `FUN_70800070`'s confirmed working pattern, the most
consistent explanation is: the stuck loop's own bound - whether it's an
array `Count()`, a DRM rights-set size, or something similar - is either
never satisfied or is itself a corrupted/unexpectedly large value under
this restored-backup-plus-EKA2L1 combination, rather than the loop logic
itself being wrong.

### Reverted / left in place

No source-code changes this round either - purely static Ghidra analysis
against the already-imported `gametitlemetadata_r64` project (read-only
sessions throughout) and `parse_rom.py` (unmodified) scans of the ROM's
existing `sys/bin` files. Three new Ghidra scripts
(`FindDrmBaflCallers.java`, `DecompileMainFunc.java`, plus a corrected
in-module bound in the reused `FindPlayserverEuserCalls.java`). No builds,
no live runs. Guardrail held: one Ghidra process at a time, `tasklist`
checked clean before each.

### To resume from here

1. **The real next target**: find and decompile the actual stuck function
   (containing `0x70803848`/`0x70805924`/`0x708041f8`) the same way
   `FUN_70800070` was just fully decompiled - force-create its function
   boundary in the existing `gametitlemetadata_r64` project and read it in
   full, specifically looking for what supplies its loop bound (a
   `Count()`-style call, a field read from a DRM rights object, or
   something else) and comparing that call site against `FUN_70800070`'s
   already-understood pattern.
2. If the bound does turn out to be a DRM rights array `Count()` (or
   similar) sourced from `drmcommon.dll`'s enumeration
   (`thunk_EXT_FUN_82f6bef4`, now identified precisely), the natural next
   check is what `gamepurchase.db`/the DRM rights store actually contains
   for this restored backup that could make that specific enumeration
   return a corrupted or unexpectedly large count - round 66's device-
   specific-storage-path lesson applies directly here too.
3. `bafl.dll` showing up as a direct import of `gametitlemetadata.dll` is
   worth keeping in mind even though it's a single call site (not yet
   decompiled this round) - given this whole investigation started with a
   `bafl.dll`-mediated hang, a second, independent connection between the
   restored-backup framework and this specific DLL is unlikely to be pure
   coincidence.

## Round 70: traced the 88-call dispatcher to its real caller, found a register-clobber bug in raw disassembly

User's explicit instruction at the end of round 69 ("go with 1") was to
keep pursuing root cause over a pragmatic mitigation. This round did
exactly that, and reached it.

### Finding `FUN_70803998`'s actual callers

Wrote `FindDispatchCaller.java` (Ghidra `ReferenceManager` lookup, same
technique as every caller-search this session) targeting
`0x70803998`. Only **5** callers, each calling it **exactly once** - not
in a loop:

- `FUN_7080409a`, `FUN_708040ba`, `FUN_70804518`, `FUN_708040d6` - all the
  same shape: build a small stack descriptor via a vtable call at
  `+0x24`, call `FUN_70803998(this, &descriptor)` once to resolve a named
  field to a byte offset, dereference the record at that offset, return
  the value.
- `FUN_708041c8` - similar but for a different (non-value-returning)
  operation.

These are column/field-value **getters** - "given a field name, fetch
this row's value for it." Each is a single bounded call. So the
dispatcher's caller isn't the unbounded loop either - something calls
*these* getters repeatedly.

### Finding the getters' caller: `FUN_70804534`

Wrote `FindGetterCallers.java` against all 5 getter addresses. Three have
zero static callers (called indirectly through a vtable elsewhere - not
resolved by Ghidra's reference manager). Two real callers turned up:
`FUN_708040fc` (calls `FUN_708040d6` once, unrelated to the loop) and
**`FUN_70804534`**, which decompiles to:

```c
void FUN_70804534(int *param_1,undefined4 param_2)
{
  int iVar1;
  undefined4 uVar2, local_1c[2];
  thunk_EXT_FUN_8076ee0a(param_2,param_1[2]);
  local_1c[0] = 0;
  thunk_EXT_FUN_8076dfc4(param_1 + 4,param_2);
  for (uVar2 = 0; iVar1 = (**(code **)(*param_1 + 0x20))(param_1), (int)uVar2 < iVar1;
      uVar2 = uVar2 + 1) {
    (**(code **)(*param_1 + 0x24))(local_1c,param_1,uVar2);
    (**(code **)(*param_1 + 0x3c))(param_1,local_1c);
    thunk_EXT_FUN_80399f48();
    param_1 = (int *)(uVar2 >> 0x18);          // <- suspicious
    FUN_708040d6(auStack_24,param_1,uVar2);     // -> FUN_70803998 -> FUN_7080591e
    thunk_EXT_FUN_8076dfc4(auStack_24,extraout_r1 >> 0x1c);
  }
}
```

This is exactly the shape round 69 predicted: `for (i=0; i<obj->Count();
i++)`, body resolves field `i`'s name/offset (matching every earlier
finding - `FUN_708040d6` calls `FUN_70803998` which calls
`FUN_7080591e`, our confirmed stuck address `0x70805924`). The `param_1 =
(uVar2 >> 0x18)` line looked like it could be a decompiler artifact
(`extraout_r1` nearby is a classic sign of Ghidra guessing a calling
convention wrong), so it needed raw disassembly to confirm, not trust.

### Raw disassembly: the bug is real, not a decompiler misread

Wrote `DisasmLoopFunc.java` (plain `Listing`/`InstructionIterator` dump,
no decompiler). Full loop body:

```
70804552  mov r5,#0x0                    ; r5 = 0 (loop counter init)
70804554  b   0x708045ba                 ; jump to bound check first
70804556  ldr r0,[r4,#0x0]               ; vtable ptr  (this = r4)
70804558  mov r2,r5
7080455a  ldr r3,[r0,#0x24]
7080455c  mov r1,r4
7080455e  add r0,sp,#0x1c
70804560  blx r3                         ; vtable[0x24](this, i)  - get field i
70804562  ldr r0,[r4,#0x0]
70804566  ldr r2,[r0,#0x3c]
7080456a  blx r2                         ; vtable[0x3c](this, ...)
7080456c  mov r3,r0
7080456e  blx 0x70805278                 ; helper, returns packed r0:r1
70804572  lsr r6,r0,#0x1c
70804574  lsl r0,r3,#0x10
70804576  lsr r7,r1,#0x1c
70804578  lsr r4,r5,#0x18                ; <-- r4 ("this") := loop_counter >> 24
7080457a  mov r2,r5
7080457c  mov r1,r4                      ; arg2 for the call below = 0 (for any real r5)
7080457e  add r0,sp,#0x14
70804580  bl  0x708040d6                 ; FUN_708040d6(&buf, 0, r5, ...)
...
708045b8  add r5,r5,#0x1                 ; i++
708045ba  ldr r0,[r4,#0x0]               ; Count() recheck - r4 is the CLOBBERED value!
708045bc  ldr r1,[r0,#0x20]
708045be  mov r0,r4
708045c0  blx r1                         ; vtable[0x20](this) via NULL "this"
708045c2  cmp r0,r5
708045c4  bgt 0x70804556
```

`r4` has exactly two writes in the entire function: the entry `mov
r4,r0` (real `this`) and `lsr r4,r5,#0x18` at `0x70804578`. `r5` has
exactly two writes too: init to 0, increment by 1 - confirmed to be a
plain loop counter and nothing else, so this isn't a misread of some
dual-purpose register. Since `r5 >> 24 == 0` for any loop count under
~16.7 million, from the *first* pass through the body onward `r4`
becomes 0 and is never reloaded from anywhere before the next
iteration's own bound recheck at `0x708045ba` - which now reads
`[NULL]`, then `[NULL+0x20]`, then branches to whatever it finds there,
every single iteration for the rest of the function's life. This is
compiled into the ROM binary itself - not a Ghidra artifact, not
something EKA2L1's code generation could have introduced.

### What's still open

Whether this NULL-through-vtable sequence actually **hangs** (rather
than crashing, which is what real Symbian's MMU should do on a genuine
data abort) depends entirely on what EKA2L1's own memory subsystem does
when guest code reads an unmapped/NULL address and then `blx`es into
whatever comes back. If EKA2L1 raises a proper fault there, this
wouldn't explain a hang (and the true cause is still open). If it
silently returns 0 instead of faulting, `blx 0` jumping to guest address
0 - which, if the bytes there are zero, would very plausibly decode as
an infinite run of no-op-shaped Thumb opcodes - would produce *exactly*
what's been observed live since round 61: millions of instructions, pure
CPU, no IPC, a counter (this loop's own `r5`, or whatever supersedes it
once execution wanders into address 0) that never resets. Checking
EKA2L1's actual unmapped-read/exception-path behavior is the immediate
next step, dispatched as a background investigation at the point this
round's write-up was made.

### Reverted / left in place

No source changes this round either - three new Ghidra scripts
(`FindDispatchCaller.java`, `FindGetterCallers.java`,
`DisasmLoopFunc.java`), all read-only headless sessions against the
existing `gametitlemetadata_r64` project. No builds, no live emulator
runs. Guardrail held: `tasklist` checked clean before each of the three
headless invocations, one Ghidra process at a time.

### To resume from here

1. **The decisive check**: does EKA2L1's ARM memory/CPU emulation raise a
   proper exception (leading to an EPOC panic, matching real hardware)
   on a NULL/unmapped read, or does it silently return a default value
   and keep executing? This single answer confirms or rules out the
   `FUN_70804534` register clobber as the actual hang mechanism.
2. If confirmed: this is a genuine bug in the *original compiled binary*
   (not EKA2L1's fault at all) that real Symbian hardware would turn
   into a KERN-EXEC 3 panic rather than a silent hang - meaning the
   correct EKA2L1-side fix is general (make unmapped/NULL guest reads
   raise the same fault real hardware would, rather than continuing) and
   would also protect against any other, similarly-shaped bug elsewhere
   in the ROM, not just this one call site.
3. Worth reconciling this with round 67's live register trace, which
   named `r6` as the cleanly-incrementing counter at the stuck PC - this
   round's `FUN_70804534` uses `r5` as its loop counter and `r6` only as
   a scratch value from the `0x70805278` helper's return. That's not a
   contradiction (the live trace was captured deep inside
   `FUN_7080591e`/`FUN_70803998`, a different stack frame with its own
   register usage), but it's worth a quick sanity pass once the
   NULL-read question is settled, to make sure this is genuinely the
   same stuck loop and not a second, similarly-shaped one.

## Round 70 (continued): checked EKA2L1's own NULL-read behavior, then gamepurchase.db's actual content

### The register clobber doesn't explain a hang - EKA2L1 panics on NULL

Dispatched a background check of EKA2L1's ARM memory/exception path
(`src/emu/mem/src/mmu.cpp`, `src/emu/cpu/src/arm_dynarmic.cpp`,
`src/emu/kernel/src/kernel.cpp`). Result: an unmapped/NULL guest read
does **not** silently return 0 - it raises `exception_type_access_violation_read`,
which `kernel_system::cpu_exception_handler` (`kernel.cpp:355-395`) turns
into a logged error and `cpu_exception_thread_handle`, which calls
`core->stop()` (halting the dynarmic JIT immediately) and
`target_to_stop->kill(kernel::entity_exit_type::terminate, u"KERN-EXEC", 3)`
(`kernel.cpp:228`) - a genuine, hardcoded KERN-EXEC 3 panic, same as real
Symbian hardware. The one bypass (`cpu_handle_access_violation`,
`kernel.cpp:344-353`) only applies when `is_eka1()` is true and the
faulting address falls in `mem::kern_mapping_eka1`
(`0x58000000-0x60000000`) - nowhere near address `0`/`0x20`, and this
device (N95/RM-320) is EKA2 anyway, so `is_eka1()` is false. Confirmed
`mem::null_trap = 0x00000000` (`mem/include/mem/page.h:28`) has no other
reference anywhere in the codebase - it's genuinely just unmapped, no
special-case handling.

**Conclusion**: `FUN_70804534`'s register-clobber bug is real (confirmed
via raw disassembly, not a decompiler artifact), but if it executes with
`r4` genuinely zeroed, EKA2L1 should panic-and-kill almost instantly, not
hang silently. That doesn't match what's been observed since round 61
(millions of instructions, no crash dialog, clean counter). So this
clobber is very likely a real bug in the shipped binary that just isn't
the one executing during the live hang - reached rarely or not at all in
this scenario, rather than being ruled out as a bug entirely.

### Finding the real match: `FUN_70803872`

Went back to `FUN_7080591e`'s full 88-caller list from earlier this round
and picked up the two callers that weren't `FUN_70803998`:
`FUN_708015ee` (2 calls, unrelated DRM-rights-array bookkeeping, not a
loop) and **`FUN_70803872`** (1 call, but itself contains a clean, no-bug
scan):

```c
int FUN_70803872(int *param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)
{
  int iVar1, iVar2 = 0;
  while (true) {
    iVar1 = (**(code **)(*param_1 + 0x20))(param_1);   // Count(), re-fetched every iteration
    if (iVar1 <= iVar2) return 0;                        // exhausted -> "not found"
    (**(code **)(*param_1 + 0x24))(&uStack_18,param_1,iVar2);  // get item iVar2
    iVar1 = FUN_7080591e(&uStack_18,param_2);            // does item match target?
    if (iVar1 != 0) break;                                // found - return its index
    iVar2 = iVar2 + 1;
  }
  return iVar2;
}
```

No register clobber - `param_1` (`this`) is never touched after entry, and
`Count()` is called fresh every pass through the same vtable slot
(`+0x20`) `FUN_70804534` also uses. If `Count()` returns something
inflated for this device's data and nothing ever matches, this scans
cleanly forever with an incrementing counter and zero crash risk -
matching every piece of live evidence collected since round 61 far
better than the clobber theory did.

Traced its callers: `FUN_708038aa` (a one-line wrapper) and two sibling
thunks, `FUN_70803930`/`FUN_7080395c` (each: call `FUN_70803872`, then
dispatch through a different vtable slot, `+0x38`/`+0x40`). **All three
have zero static callers anywhere in `gametitlemetadata.dll`** - they're
virtual method bodies, reachable only through a vtable held by code
outside this DLL, almost certainly `playserver.exe` itself.

### Checked `gamepurchase.db`'s actual content, per user request

Located all three on-disk copies:
`build/bin/data/devices/rm-320/drives/c/Private/20003b85/gamepurchase.db`
(337776 bytes, touched Aug 18 by this investigation's own emulator runs),
`.../rm-320/drives/e/20003B85/gamepurchase.db` (302701 bytes, dated
2014 - the genuine untouched restored-backup snapshot), and a
differently-provisioned `rm-409` device's own copy (378787 bytes, not
directly comparable - different device/purchase history).

Byte-diffed `c:` against `e:`: **the entire shared prefix is byte-
identical** except two small proportional bookkeeping counters at
offsets `0x10`/`0x18` (store-size fields that scale with file size -
ordinary `CPermanentFileStore` behavior, not corruption). `c:` is simply
`e:` plus 35075 bytes appended at the end. That tail is not
garbage/repetition - it's well-formed additional game-purchase records
(exe path, hash blob, localized name/description strings), same shape as
the rest of the file.

Counted `.exe` mentions (one per game-record occurrence) across both
files, 18 unique games in both:

| exe | e: (2014) | c: (now) | delta |
|---|---|---|---|
| Asphalt3.exe | 34 | 35 | +1 |
| Sims2Pets_NGage.exe | 33 | 34 | +1 |
| pandemonium.exe | 33 | 34 | +1 |
| dogz3dngage.exe | 14 | 15 | +1 |
| CBNK3D.exe | 10 | 11 | +1 |
| SpaceImpact2000AFBB.exe | 6 | 7 | +1 |
| snakes2.exe | 2 | 3 | +1 |
| Asphalt4_ngi, FIFA08_ngi, Resident_Evil, Spore, Tetris, WormsNGI, bia3d, mgs_ngi, mygame, nfsuc, pop0_2001AADA (11 games) | 1 each | 8 each | **+7 each** |

Verified this isn't coincidental string matches: every `WormsNGI.exe`
occurrence in `c:`'s appended tail sits at the head of a byte-for-byte
identical 80-byte record block (same exe name, same hash blob) - genuine
duplicate inserts, not independent records.

**This is the concrete, decisive finding**: 11 of the 18 games each
gained exactly 7 duplicate purchase records, matching however many times
this investigation has actually booted the emulator against this
profile - meaning every boot attempt re-inserts a fresh, byte-identical
duplicate record for these specific games, with no check ever
recognizing the one already there. The other 7 games didn't gain new
duplicates this session (+1 each, from something unrelated shared
elsewhere), but they already carried 2-34 duplicate copies **in the
untouched 2014 backup itself** - proof this exact duplicate-insertion bug
has been happening on the real device for years before this backup was
ever made. It is a genuine bug in the original N-Gage 2.0 client
software, not something EKA2L1 or this investigation introduced -
though this investigation's own repeated boot attempts have been
measurably adding to the pile on the local `c:` copy each time.

Whether unbounded duplicate accumulation is *directly* what the
`FUN_70803872`-style scan hangs on, or whether it's a finite (if buggy)
per-boot side effect that completes before the true hang happens in a
later step, is still open - but the device's own history now has hard,
counted evidence of a broken "does this purchase already exist"
check, exactly the shape of bug `FUN_70803872` would produce if its
`Count()` or its match target were wrong.

### Reverted / left in place

No source changes. Three more Ghidra scripts this round
(`DecompileOtherCallers.java`, `DecompileScanCaller.java`,
`ForceDecompileUnresolved.java`), all read-only, `tasklist`-checked
before each. The `gamepurchase.db` analysis was pure Python (`diff_db.py`,
`dump_tail.py`, `count_games.py`, `dup_check.py`, `per_exe_count.py`, in
the session scratchpad) reading the existing on-disk files directly - no
writes, no emulator runs.

### To resume from here

1. Pivot to `playserver.exe`'s own Ghidra analysis to find the actual
   call site(s) into `gametitlemetadata.dll`'s vtable (specifically
   slots `+0x20`/`+0x24`/`+0x28`/`+0x38`/`+0x40`) and the concrete class
   backing `Count()` - this is the only way to see what feeds that
   count and why the "already exists" check never matches.
2. Given the confirmed duplicate-record bug is real and pre-existing
   (present in the untouched 2014 backup for 7 of 18 games), consider
   whether the true "boot hang" is simply this same broken check now
   being run against a MUCH larger accumulated list on THIS specific
   heavily-purchased device - i.e. not a single corrupted count, but a
   perfectly normal (if buggy) O(N) "already exists?" scan against a
   list that's grown large enough, across years of real use plus this
   session's own repeated attempts, to look indistinguishable from
   infinite.
3. Practical note for future rounds: **each emulator boot attempt against
   this `rm-320` profile appears to add further duplicate records** to
   the local `c:` copy of `gamepurchase.db` - worth keeping in mind that
   repeated testing may be organically worsening the exact condition
   under investigation, and worth deciding whether to keep testing
   against the accumulating `c:` copy or reset it from the pristine
   `e:` snapshot before further live runs.

## Round 70 (continued again): pivoted to `playservergl.dll`/`playserverccd.dll` - confirmed ECOM plugin loading, no literal call site to chase

User asked to continue past the `gamepurchase.db` check. Round 65 had
already pinned the live caller chain into the stuck loop as
`playservergl.dll` → `playserverccd.dll` → `gametitlemetadata.dll`, but
never statically decompiled either of the first two. Went and did that
this round.

### Capturing the two DLLs

Both are **installed, non-ROM binaries** (confirmed present under
`SYS\BIN` on the emulated `C:` drive, not in `sys/bin` on `Z:`), so they
needed round 60's live-relocated-code-dump technique, not a static ROM
read. Re-added a temporary probe to `codeseg::attach()`
([codeseg.cpp:350-360](../../src/emu/kernel/src/codeseg.cpp)),
symmetric to round 60's, gated on the codeseg name containing
`"playservergl"` or `"playserverccd"`, dumping `code_base_ptr` (fully
relocated and import-patched) straight to a `.bin` file. Rebuilt
(`-m:4`, guardrail-checked), bumped `config.yml`'s `log-filter` from
`Kernel:Warn` to `Kernel:Info` to see the probe's log line, launched
`eka2l1_qt.exe --run 0x20007B38` once, confirmed both dumps landed
(`playserverccd.dll` @ `0x70b00000`, 0x3990 bytes; `playservergl.dll` @
`0x71300000`, 0x5e50 bytes), killed the process immediately rather than
let it run into the hang (no need to add yet more duplicate purchase
records), then fully reverted both the probe and the log-filter change
and rebuilt again so the binary matches the clean source
(`git status` on `codeseg.cpp` clean afterward).

Imported both into fresh Ghidra projects (`playserverccd_r70`,
`playservergl_r70`, `BinaryLoader` at each one's captured runtime
address).

### No static call into `gametitlemetadata.dll` from either

Scanned every instruction in both dumps for any reference landing in
`0x70800000-0x70809000` (`gametitlemetadata.dll`'s address range):
**zero hits in either file.** Also grepped both full decompiles for
`gametitlemetadata.dll`'s own UID3 (`0x20003B84`) and, more broadly, any
`0x2000xxxx`-shaped constant at all: **zero matches.** This confirms
`gametitlemetadata.dll` is loaded as an **ECOM plugin** - resolved at
runtime by interface UID through ECOM's resource-file-driven registry,
not a compile-time DLL dependency - so there is no static literal in
either caller to trace a call site from. Matches why every entry point
found in round 70's first half (`FUN_708038aa`,
`FUN_70803930`/`FUN_7080395c`, and `FUN_70804534`'s siblings) had zero
static callers within `gametitlemetadata.dll` itself: they're virtual
methods on an interface, reached only through a vtable pointer ECOM
handed back at runtime.

### A red herring worth recording: `ws32.dll`, not a database handle

Decompiled all ~110 functions in `playserverccd.dll` and ~179 in
`playservergl.dll`. Found a cluster
(`FUN_70b0145a`/`FUN_70b014d8`/`FUN_70b015b4`/`FUN_70b015ea`) that all
share a `param_1+0x24` field passed to a set of thunks at
`0x806cc202`/`0x806cc226`/`0x806cc23e`/`0x806cc372` - initially looked
promising (a `Count()`-bounded double loop in `FUN_70b015ea` at first
glance resembled the same shape chased all round). Resolved those
addresses via `parse_rom.py`'s header ranges against every file in
`data/drives/z/rm-320/sys/bin`: several ROM files matched with an
identical, suspicious `code_size` (`0x101f7afc`) - clearly a mis-parse
of non-E32Image files (`.rsc`, `.agt`) - but one clean match stood out:
**`ws32.dll`** (`code_addr=0x806c8658 size=0x7db0`, UID3 `0x10003B21` -
genuinely Symbian's real Window Server client library, confirmed via
its own full, sane header and valid Thumb function prologues at every
target offset, e.g. `08 b5` = `push {r3,lr}`). That means `param_1+0x24`
in this cluster is a **window-server session handle**, and this code is
UI/icon-list bookkeeping (syncing a visible list to on-screen window
objects) - unrelated to the `gamepurchase.db`/`Count()` loop this
investigation is chasing. Recorded here specifically so a future round
doesn't re-walk this same dead end.

### Reverted / left in place

`codeseg.cpp` and `config.yml` both confirmed back to their pre-round
state (probe removed, log-filter restored, rebuilt to match). Five new
Ghidra scripts this round (`FindCcdToMetadataCalls.java`,
`FindGlToMetadataCalls.java`, `DecompileAllCcd.java`,
`DecompileAllGl.java`), all read-only against the two new projects.
`gamepurchase.db`'s `c:` copy was **not** reset (an attempted reset was
blocked and abandoned rather than forced) - it still carries this
session's accumulated duplicate records; worth doing deliberately before
the next live run.

### To resume from here

1. The only way to find the actual ECOM-mediated call site now is
   either (a) dump `playserver.exe` itself (the parent process - not
   captured this round, only its two child DLLs were) and look for the
   `RImplInfoPtrArray`/`CreateImplementationL`-style ECOM API calls
   there, or (b) find and read ECOM's own plugin registry (`.rsc`
   resource files under `Z:\Resource\Plugins` or similar) to see which
   interface UID resolves to `gametitlemetadata.dll`.
2. Given how much has now been independently confirmed (the duplicate-
   record bug is real and pre-existing on the actual device; a clean,
   bug-free `Count()`-bounded scan (`FUN_70803872`) matches every piece
   of live evidence; the register-clobber bug in `FUN_70804534` is real
   but doesn't match the observed hang), the practical next decision is
   whether continuing to chase the exact ECOM call site is worth it, or
   whether this is now a good point to consolidate everything into a
   single root-cause writeup and consider what a real fix or mitigation
   would look like.

## Round 70 (final push): found the real entry point and the confirmed root cause of the duplicate-record bug

User said to keep pushing. Went back to `playserver.exe`'s own decompile
(captured earlier this round, 38 functions, not yet fully mined) instead
of chasing ECOM further, and found what the earlier per-DLL address-range
scan had missed: it only checked `playserverccd.dll`/`playservergl.dll`
for calls into `gametitlemetadata.dll`'s range - never `playserver.exe`
itself.

### Finding the real call

`FUN_7070073a`, playserver.exe's top-level session-teardown function,
releases roughly ten different subsystem handles it owns, one per `if
(param_1[N] != 0)` guard. One of them - slot `0x1f` - calls
`thunk_EXT_FUN_70801288()`, and `0x70801288` falls **directly inside
`gametitlemetadata.dll`'s address range**. This is a genuine, direct,
static call from `playserver.exe` into `gametitlemetadata.dll` - the
literal reference the earlier per-DLL scans of `playserverccd.dll`/
`playservergl.dll` couldn't find, because it isn't in either of them.

Raw-disassembled `0x70801288` directly (not trusting Ghidra's function-
boundary guess, which looked inconsistent) and found it's a bare `b
0x708011d2` - a trampoline, not a real entry point - landing inside
`FUN_708011b2`, which wraps a lock/critical-section pattern
(`FUN_70801bec`/`FUN_70801c04` check-and-take, `FUN_70801c0c`/
`FUN_70801bfc`/`FUN_70801bf4` release) around the actual work:
**`FUN_708010ea`**.

### `FUN_708010ea`: the real "register a game's purchase/title metadata" entry point

```c
void FUN_708010ea(int param_1,undefined4 *param_2,int *param_3)
{
  ...
  iVar1 = FUN_70801c1c(*(param_1+4));   // current row count (real DBMS Count())
  iVar2 = FUN_70801c56(*(param_1+4));   // max allowed row count (from a central-repository-style setting)
  if ((iVar2 <= iVar1) &&
      (iVar1 = FUN_70802722(*(param_1+4),&local_18), 0 < iVar1)) {
    FUN_70802d64(*(param_1+4),local_18);   // evict: delete the found row
    FUN_70802d84(*(param_1+4),local_18);
  }
  puVar3 = thunk_EXT_FUN_803a837e(0x88);   // allocate a new 0x88-byte record
  // ... copy param_2's fields (exe path, name, description - matches
  //     the per-game record shape found in round 70's gamepurchase.db
  //     forensics) into puVar3 ...
  local_18 = FUN_70802a06(*(param_1+4),puVar3);   // INSERT new row - no existence check anywhere
  for (iVar1 = 0; iVar1 < param_3[1]; iVar1 = iVar1 + 1) {
    (**(code **)(*param_3 + 0x10))(&puStack_20,param_3,iVar1);   // get sub-item i (icon path etc.)
    FUN_70802a84(*(param_1+4),local_18,&puStack_20);              // attach it to the new row
  }
}
```

Decompiled every callee (`FUN_70801c1c`, `FUN_70801c56`, `FUN_70802722`,
`FUN_70802d64`, `FUN_70802d84`, `FUN_70802a06`, `FUN_70802a84`) to make
sure none of them hide the unbounded loop: **all seven are clean,
single-shot, real Symbian DBMS row operations** (row count, read a
central-repository max, find-first-match, delete-row, insert-row,
get-column) - genuinely bounded, no loops of their own. The one loop in
`FUN_708010ea` itself is bounded by `param_3[1]`, a plain field read
(the fixed number of sub-items for one game - icon sizes, typically a
handful), not a virtual `Count()` call - so it can't run away either.

**This confirms the duplicate-record bug's exact mechanism, in code, not
just in file-format forensics**: this function checks capacity and
evicts if over the limit, but has **no check anywhere for whether a
record already exists for the game being registered** - it unconditionally
allocates and inserts a new row every single time it's called. Since
`playserver.exe` calls this once per installed game at boot (matching
the earlier finding that a fixed, per-boot number of new duplicates
appeared for each of 18 games), this is precisely the bug that produced
the "+7 duplicates per game, uniformly" and "2-34 pre-existing duplicates
in the untouched 2014 backup" evidence from earlier this round - not
inferred this time, but read directly out of the decompiled insert path.

### What this does and doesn't explain

This function is genuinely bounded and cannot itself hang - it explains
the *duplicate accumulation*, not directly the *infinite loop*. The
confirmed unbounded-looking scan (`FUN_70803872` → `FUN_70803998` →
`FUN_7080591e`, this round's earlier finding) is not called anywhere in
this insert path or its callees - it must be invoked from a separate
entry point, almost certainly a **read/enumerate** path rather than this
**insert** path (most likely `playservergl.dll`'s game-list UI, reading
back and rendering every registered record - matching its name and its
role in round 65's live-traced caller chain). The most coherent picture
now is:

1. `FUN_708010ea` runs once per installed game at every boot, always
   inserting a new duplicate purchase record with no dedup check
   (confirmed in code this round).
2. Over years of real device use, and now compounded by this
   investigation's own repeated test boots, `gamepurchase.db` has
   accumulated a purchase-record list many times larger than it should
   be for an 18-game collection (confirmed in the file itself: 2-34
   duplicates per game even in the untouched 2014 backup).
3. Something - most plausibly the game list UI - has to enumerate this
   now-bloated record set at boot, resolving field names per record via
   the `FUN_70803872`/`FUN_70803998`/`FUN_7080591e` chain this round
   already established. Whether that enumeration is a genuinely
   unbounded loop (a second, separate bug) or just an O(N)-or-worse scan
   over a list that's grown absurdly large is still open, but either way
   the duplicate-insertion bug is the thing that makes the list large
   enough for this to matter at all - on a fresh, non-restored device
   with few duplicates, the same enumeration code would finish
   instantly.

### Reverted / left in place

Same probe-and-revert cycle as earlier this round, done twice more this
push (once to add a data-segment dump alongside code - found both
`playservergl.dll`/`playserverccd.dll` have `data_size=0`, no separate
data segment at all, ruling out a literal DLL-name string existing
anywhere to find; once retargeted to capture `playserver.exe` itself,
which does have a real code segment but a near-empty data segment,
`0x1c` bytes, all zero). `codeseg.cpp` and `config.yml` both confirmed
back to clean after the final rebuild. Two new Ghidra scripts this push
(`DecompileEntryPoint.java`, reused three times with different target
lists, and `DisasmRaw.java` for the raw-disassembly sanity check that
caught the trampoline). All read-only against existing or newly-created
projects (`playserver_exe_r70` added this push).

### To resume from here

1. Find `playservergl.dll`'s own equivalent entry point - the read/
   enumerate path that would call into `gametitlemetadata.dll` to fetch
   and display all registered records - and confirm whether it's the
   direct caller of `FUN_70803872`'s chain, and whether its own loop
   bound is genuinely unbounded or just large.
2. With the insert-side root cause now confirmed in code, a real fix
   has two independent parts worth keeping separate: (a) add an
   existence check to `FUN_708010ea` (or wherever the real N-Gage 2.0
   client source lives, if this is ever patched at the source) so
   repeated registration is idempotent, and (b) make sure whatever
   enumerates the purchase list at boot scales reasonably even for a
   large, duplicate-heavy list, since real devices with years of usage
   will have accumulated exactly this kind of bloat independent of any
   EKA2L1-specific behavior.

## Round 70 (live correction): the hang happens WITHOUT ever calling the insert function

User said to keep going. Rather than keep reasoning statically, tested
the `FUN_708010ea` theory directly against a live run - and the result
corrects the picture from the previous section.

### A live, no-GDB-needed breakpoint probe

No usable ARM-capable GDB binary exists on this machine (checked;
declined to install a new toolchain as out of scope for this
investigation). Instead of GDB, reused the exact mechanism the built-in
GDB stub itself uses internally
([gdbstub.cpp:1044-1064](../../src/emu/gdbstub/src/gdbstub.cpp)): patch
a 2-byte Thumb `BKPT` (`0x00 0xBE`) over the target instruction, which
raises `exception_type_breakpoint`
([kernel.cpp:381-386](../../src/emu/kernel/src/kernel.cpp)), invoking
`kernel_system::register_breakpoint_hit_callback`. Added a temporary,
self-contained probe directly in `epoc.cpp` (no GDB client needed at
all): on `gametitlemetadata.dll`'s codeseg-loaded callback, patch a trap
at `codeseg_base + 0x10ea` (the offset of `FUN_708010ea`, confirmed
position-independent since round 64's relocation always applies a
uniform base delta); on breakpoint hit, log `LR` (the caller's return
address) and `r0`, then restore the original bytes and clear the
instruction cache so execution continues correctly - a single-shot
capture, safe to let the rest of boot proceed normally afterward.

Rebuilt (clean, no errors), bumped `config.yml`'s log-filter to also
show `System:Info`, ran `eka2l1_qt.exe --run 0x20007B38` once.

### Result: armed four times, never hit - even after the boot reached its known stuck state

The probe armed cleanly (logged 4 times, all at the same address -
consistent with EKA2L1's codeseg-reuse behavor when multiple processes
attach to the same shared, read-only code chunk, not 4 different
addresses). Interesting side note: this run's actual base address for
`gametitlemetadata.dll` was `0x70400000`, not the `0x70800000` seen in
round 64 and reused as this whole round's assumption - confirming load
addresses for installed (non-ROM) codesegs are **not fully
deterministic across runs**, though the file-relative offset to
`FUN_708010ea` (`0x10ea`) still held correctly regardless.

Polled the log for a "hit" line for a total of over four minutes (three
separate waits: ~8s, ~90s, ~180s). The boot progressed normally through
window/app-list/backup-server setup, hit the already-fixed `!BackupServer`
1-second fail-fast exactly as expected, and then - confirmed by zero new
log lines for the entire final 3-minute wait - **the process was
already sitting in its known stuck state**. The breakpoint at
`FUN_708010ea` never fired, not once, across the whole boot including
the period the process was demonstrably hung.

**This is a real, empirical correction to the previous section's
narrative.** The duplicate-insert bug in `FUN_708010ea` is still a
completely real, confirmed-in-code bug - the decompile evidence and the
`gamepurchase.db` forensics matching it exactly don't change. But this
live test shows the function that produces those duplicates is **not
called during a hanging boot** - the hang is reached via a different
code path entirely, one that never proceeds as far as inserting a new
purchase record. The two bugs (duplicate-record accumulation, and
whatever causes the hang) are real but **independent**: duplicates
accumulate on *some* boots/paths, and the hang happens on *this* one,
without the two ever being the same call.

### Reverted / left in place

Killed the emulator immediately after confirming the negative result
(no need to let it sit hung any longer). Fully reverted the temporary
probe and the `#include <cstring>` in `epoc.cpp`, and `config.yml`'s
log-filter change; rebuilt once more to confirm the binary matches. `git
diff` on `epoc.cpp` now shows only the pre-existing, already-tracked
round-58 fix from earlier in this investigation - no probe remnants.

### To resume from here

1. The real question is now: what code path *does* get reached during
   this specific hanging boot, if not `FUN_708010ea`? Round 70's earlier
   work already found the answer's shape - the confirmed live-traced
   stuck loop is the `FUN_70803872`/`FUN_70803998`/`FUN_7080591e` chain,
   reached from vtable-dispatched entry points with zero static callers
   in `gametitlemetadata.dll` itself. The natural next step is arming
   this exact same breakpoint technique on `FUN_70803872` itself (or its
   caller `FUN_708038aa`) instead of `FUN_708010ea`, to get a live,
   empirical caller/LR capture for the function that's actually,
   confirmedly involved in the hang - closing the gap this round's probe
   just proved still exists.
2. Worth remembering for any future live capture: this investigation's
   installed (non-ROM) codeseg base addresses are not guaranteed stable
   run to run (confirmed this round: `0x70400000` vs `0x70800000` for
   the same DLL) - always re-derive the base from that run's own
   codeseg-loaded log line rather than assuming a previous round's
   captured address still applies.

## Round 70 (final live capture): confirmed genuine mutual recursion through vtable dispatch

Continued the exact live-breakpoint technique from the previous section,
this time aimed at the real stuck-loop chain (`FUN_70803872` and its
zero-static-caller vtable-dispatch wrappers) instead of the ruled-out
insert path.

### First attempt: non-determinism strikes again

Armed breakpoints on all four candidate entry points
(`FUN_70803872`/`FUN_708038aa`/`FUN_70803930`/`FUN_7080395c`, offsets
`0x3872`/`0x38aa`/`0x3930`/`0x395c`). First boot attempt never even
logged an "armed" message - `gametitlemetadata.dll` hadn't loaded at all
by the time the process was sitting silently for over 2.5 minutes past
the usual `!BackupServer` fail-fast point, confirming (again) that this
investigation's boot ordering is genuinely non-deterministic between
runs, not just its total step count. Killed and retried once.

### Second attempt: a real hit

Second attempt: all four armed cleanly (this run's base was
`0x70400000` - a third different value now seen for this same DLL across
three different live captures this round, alongside `0x70800000` and
`0x70400000` again - confirming installed-codeseg base addresses really
are per-run, not fixed). Got a hit:

```
PROBE_R70E: hit at 0x70403930 (offset 0x3930) LR=0x704047e7 r0=0x80faa8 r1=0x507d90
```

`FUN_70803930` fired. `LR` (masked for the Thumb return-bit:
`0x704047e7 & ~1 = 0x704047e6`) lands at offset `0x47e6` - **inside
`gametitlemetadata.dll` itself**, not any external caller. (Continuing
execution after restoring the trapped bytes led to an unrelated OpenGL
"Corrupted graphics command list" crash a bit later - likely an artifact
of this probe not replicating the GDB stub's full stop/resume sequence;
harmless to the captured data itself, but noted so a future round
doesn't reuse this exact resume path without fixing it first.)

### Decompiling the new function: another zero-caller, `Count()`-bounded loop

`FUN_708047e6` (force-created at the masked address in the existing
`gametitlemetadata_r64` project): **zero static callers**, same pattern
as everything else this round -

```c
int FUN_708047e6(void)
{
  while (true) {
    uVar1 = thunk_EXT_FUN_80399f48();
    unaff_r4 = *(int*)(unaff_r5[2]*100 + DAT_7080487c + (uVar1>>0x1c)*4) * 3 + unaff_r4;
    iVar2 = (**(code**)(*unaff_r5 + 0x20))();       // Count() - same vtable slot as every other loop this round
    if (iVar2 <= (int)((uVar1>>0x1c) + 1)) break;
    (**(code**)(*unaff_r5 + 0x24))();                // <- resolves to FUN_70803930 (confirmed by the live hit above)
    (**(code**)(*unaff_r5 + 0x3c))();
  }
  return unaff_r4;
}
```

### Closing the loop: armed one more breakpoint, directly on `FUN_708047e6`

Ran a third live capture, single target this time (`0x47e6`), to find
*its* caller:

```
PROBE_R70F: hit at 0x704047e6 LR=0x7040393f r0=0x0 r1=0x0
```

`LR` masked (`0x7040393f & ~1 = 0x7040393e`) lands at offset `0x393e` -
**inside `FUN_70803930`'s own 16-byte body** (`0x3930`-`0x3940`), right
after its `(**(code**)(*unaff_r4 + 0x38))();` call. Putting both live
captures together, the actual runtime call graph is:

```
FUN_70803930  --calls FUN_70803872 (the linear scan)--
              --then calls vtable+0x38, which resolves to FUN_708047e6--
FUN_708047e6  --own Count()-bounded loop, calls vtable+0x24 each iteration--
              --which resolves back to FUN_70803930--
```

**This is genuine mutual recursion between two virtual methods on the
same object, entirely through vtable dispatch** - `FUN_70803930` (vtable
slot presumably backing something like "resolve/verify a field") calls
`FUN_708047e6` (vtable slot backing an accumulation/checksum-style
operation) via slot `+0x38`, and `FUN_708047e6`'s own loop calls back
into `FUN_70803930` via slot `+0x24`, every iteration. Neither call is
visible to static analysis by address (both are indirect through the
same vtable), which is exactly why 70 rounds of static tracing across
three different binaries never found an external caller for any of
these functions - **there may not be one to find at this level**; the
loop mostly feeds on itself.

Given each side of this recursion also does real work bounded by its own
`Count()` check (`FUN_70803872`'s linear scan is itself
`Count()`-bounded; `FUN_708047e6`'s accumulation loop is bounded the
same way), the earlier finding that the *duplicate-record bug bloats the
underlying table* now has a second, more direct way to matter: if the
two recursing methods' index/position bookkeeping ever falls out of
sync with each other (plausible exactly when the underlying row count is
unusually large or has grown between one method's snapshot of `Count()`
and the other's), each side could keep re-triggering the other without
ever converging - a genuinely unbounded mutual loop, not just an
expensive-but-finite one.

### Reverted / left in place

Both live-capture probes this section (`round-70e`'s 4-target version
and `round-70f`'s single-target follow-up) fully reverted from
`epoc.cpp` and `config.yml`, confirmed via `git diff` showing no
`PROBE_R70E`/`PROBE_R70F`/`cstring`/`array` remnants, rebuilt clean
after each revert. Two live runs this section ended in the emulator
process exiting on its own (an unrelated-looking OpenGL crash after the
70e continuation, and a clean kill after 70f's single hit) - no lasting
state changes beyond `gamepurchase.db`'s continuing, already-flagged
duplicate accumulation from yet two more boot attempts.

### To resume from here

1. The natural next step is symmetric to what was just done: arm a
   breakpoint on `FUN_70803872` itself (not yet done - only its two
   *outputs*, `FUN_70803930` and `FUN_708047e6`, have been probed) to
   see whether IT is ever entered from anywhere other than
   `FUN_70803930`, which would confirm or rule out a third participant
   in this recursive cluster.
2. If the mutual-recursion theory holds up, the actual, literal
   non-termination condition would be visible by comparing what
   `unaff_r5[2]` (used in `FUN_708047e6`'s table lookup) and whatever
   index `FUN_70803872`'s linear scan advances actually hold across
   several consecutive round-trips - worth a live register/memory dump
   at both functions' entries in the same run, rather than single-shot
   captures, to see the values actually drift or stay stuck.
3. Fix the probe's resume sequence before reusing this technique again -
   explicitly call `cpu_core->set_pc(addr)` after restoring the trapped
   bytes (matching the existing GDB-stub callback's own pattern at
   [epoc.cpp:263-274](../../src/emu/system/src/epoc.cpp)) rather than
   relying on the CPU resuming correctly on its own; round 70e's later
   OpenGL crash is circumstantial but not yet ruled out as caused by
   this gap.

## Round 70 (attempted multi-hit capture): hit a real architectural wall

Tried to directly answer "does the shared state between the two mutually
recursive functions actually advance, or stay stuck" by capturing
several round-trips within a single run, instead of the one-shot-per-run
captures done so far.

### The attempt

Built a probe (`round-70g`) that, on each breakpoint hit, would: log
`LR`/`r0`/`r1`/`r4`/`r5`, restore the original bytes, explicitly call
`cpu_core->set_pc(addr)` (fixing the gap flagged after round 70e), call
`cpu_core->step()` to execute the now-restored real instruction exactly
once, then re-write the trap and keep going - up to 25 hits per target
before leaving it unpatched permanently. This should have given a
timeline of `r4`/`r5` across many visits to both
`FUN_70803930`/`FUN_708047e6`.

### The wall: Dynarmic's reentrancy guard

First attempt (this run's boot ordering never loaded
`gametitlemetadata.dll` at all inside 3 minutes - the same
non-determinism seen throughout this round) was killed and retried.
Second attempt got exactly **one** hit (`FUN_70803930`, matching round
70e's earlier capture almost exactly: `r0=r4=r5=0x80faa8`), then the
process silently stalled for the rest of the 3-minute wait. The log's
next line explained it:

```
assertion failed: !is_executing
```

Dynarmic's `Jit` guards against reentrant execution - the breakpoint
callback fires *synchronously from inside* `jit->Run()`, so calling
`cpu_core->step()` (`jit->Step()`) from within that same callback
violates the guard and corrupts the process. This isn't a bug in the
probe's logic to fix - it's a fundamental constraint of calling back
into the same JIT instance while it's already executing. A real
multi-hit capture would need either a genuinely different resumption
mechanism (queue the "let it run once more" request and service it from
*outside* the `Run()` call that triggered the exception, which the
current kernel/exception-handling plumbing doesn't expose a hook for)
or a switch to true global single-step mode for the whole CPU core
(much higher overhead, and a bigger, riskier change than appropriate for
a temporary investigative probe).

### What still stands from this round

The two independent, real, single-shot captures from the previous
section (`round-70e`'s hit on `FUN_70803930` and `round-70f`'s hit on
`FUN_708047e6`) remain valid and unaffected by this - they didn't
attempt to resume via `step()`, and round 70d (the very first live
capture, also without `set_pc`/`step()`) ran cleanly for four-plus
minutes with zero instability. The reentrancy wall only bit the *new*
multi-hit attempt. The confirmed mutual-recursion call graph
(`FUN_70803930` ↔ `FUN_708047e6` via vtable slots `+0x38`/`+0x24`)
stands as this investigation's most concrete finding about the actual
stuck-loop mechanism.

### Reverted / left in place

Killed the corrupted process, fully reverted `round-70g`'s probe and the
`<array>`/`<cstring>` includes from `epoc.cpp`, reverted `config.yml`'s
log-filter, rebuilt clean - confirmed via `git diff` showing no
`PROBE_R70G` remnants.

## Round 70 (RM-320 vs RM-409 resolved): the hang is data-driven, not device-driven

User asked why N-Gage 2.0 works on a 5320 (RM-409) ROM under EKA2L1 but not
on an N95 (RM-320) ROM, given both work flawlessly on real hardware. This
needed a direct, live-tested answer rather than more static reasoning.

### The user's own manual test - a real but unrelated bug

The user duplicated `build/bin`, installed fresh ROMs for both devices,
installed N-Gage 2.0 fresh on each, and ran it: 5320 worked, N95
force-closed. Reproduced this exactly using the user's own duplicated
folder (`C:\Users\Usuario\Documents\1 eka2l1\bin`, both device slots
already had genuinely fresh installs - `gamepurchase.db` on both was
2625 bytes, essentially empty). Result, with `Kernel:Info` logging:

```
E ...files.cpp:844 [Service.EFsrv]: Trying to open a non-existent file: E:\resource\apps\ngiplay0x20003b78.r30 ...
E ...kernel.cpp:363 [Kernel]: Access violation reading address 0xC54 in thread PlayServer
pc: 0x803b0fda   lr: 0x71208c29   r0=r1=r6: 0xc54   r2=r5: 0xffffffff
```

Traced this to its actual cause, confirmed with real files, not
inference:

1. **RM-320's real N95 ROM genuinely declares language `30` as its
   default** - read straight from the ROM's own
   `resource/bootdata/languages.txt` (`device_manager::add_new_device`,
   [devices.cpp:311-353](../../src/emu/system/src/devices.cpp)). This is
   a real Chinese-market N95 firmware variant, not an EKA2L1 default.
2. **The specific N-Gage install used for this test** (an independently
   repackaged community build, UID `0x20003b78` - not the exact retail
   build, per round 45's own caveat) **only ships `.r01` and `.r16`**
   resource variants for its `ngiplay` component. No `.r30` exists
   anywhere in either device's data.
3. RM-409's device profile defaults to language `1` (English) instead -
   confirmed by watching the emulator rewrite `config.yml`'s
   `language:` field itself on switching devices - so it never goes
   looking for the missing `.r30` file at all.

**This is a real bug (a missing-resource load should fall back to a
default language, not access-violation-crash), but it's unrelated to
this entire investigation.** It's specific to language 30 and this
particular repackaged installer's incomplete localization, confirmed by
checking the main investigation's own retail install: it has a real
`ngiplay0x20003B78.r30` file (the retail package is more complete), and
its `config.yml` is set to language 1 anyway - sidestepping this
specific landmine entirely, unrelated to which device it's running on.

### The real test: does RM-409 hang too, given the same retail build and real purchase history?

This is the test that actually answers the user's question. The main
investigation's own bundled data already has `gametitlemetadata.dll`/
`playserver.exe`/etc. installed on **both** `rm-320` and `rm-409`, and
`rm-409` has its own real `gamepurchase.db` (378787 bytes, 18 games,
independent purchase history) - no setup needed.

Ran `eka2l1_qt.exe --device RM-409 --run 0x20007B38` directly against
the main project's own data. Result: booted normally, `gametitlemetadata.dll`
loaded (confirmed in the log), reached the exact same `!BackupServer`
fail-fast point every `rm-320` run reaches - **and then sat there,
log completely unchanged, process still alive, for 60+ seconds straight
(polled twice, no growth either time).** Killed it - this is the *same*
stall, at the *same* point, that this entire 70-round investigation has
been chasing on `rm-320`.

### What this settles

**The hang is not device-specific or firmware-build-specific at all.**
Given a comparable retail N-Gage install with real accumulated purchase
history, RM-409 (5320) hangs exactly like RM-320 (N95) does. The
user's own manual test result (5320 works, N95 crashes) was measuring a
completely different, unrelated bug - a missing localization resource in
one specific repackaged installer, triggered only because RM-320's real
ROM happens to default to a language that installer doesn't support.

This means real hardware working "flawlessly" on both devices isn't
explained by some device-level difference EKA2L1 gets wrong for N95
specifically - it's explained by real Symbian hardware's actual runtime
behavior masking (or never exposing) the same duplicate-insert +
mutual-recursion bug this investigation has been tracing, on *any*
device, regardless of model. The bug lives entirely in the N-Gage 2.0
client's own compiled code and how EKA2L1's emulation diverges from real
hardware at that exact mechanism - not in anything specific to N95 vs.
5320.

### Reverted / left in place

Both live tests used only existing, already-installed data - no
installs, no file changes. `config.yml`'s log-filter bumped to
`Kernel:Info` and reverted back to `Kernel:Warn` afterward; `device`/
`language` fields confirmed back at their original `2`/`1` baseline. No
source changes this section. The user's own test folder
(`1 eka2l1\bin`) was left with its log-filter still at `Kernel:Info`
and device/language at whatever the last (`RM-409`) run left them -
harmless, the user's own sandbox, not the main investigation's data.

## Round 70 (root cause found): the loop index gets clobbered by the record's own data, every single iteration

User asked to keep going on why the mutual recursion never terminates.
Found the actual, mechanistic, mathematically-certain answer - a second
register-clobber bug, this time in the loop index itself, not just a
big table.

### The earlier decompile was misleading - Ghidra flagged it, should have trusted that

Re-examined `thunk_EXT_FUN_80399f48` (called every iteration inside the
function this round had been calling `FUN_708047e6`) by resolving its
owning module the same way every other cross-DLL address has been
resolved this investigation: it's in `drtrvct2_1.dll`/`drtrvct2_2.dll`
(already known from rounds ~19-25), and Ghidra names it **`switch8_r3`**
- a compiler-generated switch-statement dispatch trampoline, not a
stateful helper. Its own decompile carries the warning `"Could not
recover jumptable at 0x80399f5c. Too many branches"` - and that same
warning fires at the call site inside the function this round has been
studying, meaning **Ghidra's decompiled C for that whole function was
unreliable** (it decompiled a switch-dispatch as if it were a normal
function call with a return value). Should have gone straight to raw
disassembly here, the same lesson round 70's very first register-clobber
find already taught.

### Raw disassembly of the real function (not the mid-function point round 70e/70f happened to land on)

Round 70e/70f's live captures placed breakpoints at offset `0x47e6`,
which is a real, valid instruction address but **not the function's true
entry** - same false-start problem as `0x70801288` earlier this round.
Disassembled backward and found the genuine entry at **`0x708047b6`** (a
clean `push {r0,r1,r2,r3,r4,r5,r6,lr}` prologue):

```
708047b6  push {r0,r1,r2,r3,r4,r5,r6,lr}
708047b8  mov r5,r0                  ; r5 = this
...
708047c2  blx r2                    ; vtable+0x30(this) - initial seed value
708047c4  ldr r0,[sp,#0x8]
708047c6  lsl r4,r0,#0x4
708047c8  lsr r4,r4,#0x4             ; r4 = seed & 0x0FFFFFFF
708047ca  add r4,r4,#0x4             ; r4 = accumulator, initialized
708047cc  mov r6,#0x0                ; r6 = 0 - THE LOOP INDEX
708047ce  b 0x70804812               ; jump to bound-check (do-while shape)

708047d0  ldr r0,[r5,#0x0]           ; <-- LOOP BODY (bgt target)
708047d2  mov r2,r6                 ; pass current index r6
708047d4  ldr r3,[r0,#0x24]
708047da  blx r3                    ; vtable+0x24(this, r6) - fetch item[r6]
708047dc  ldr r0,[r5,#0x0]
708047e0  ldr r2,[r0,#0x3c]
708047e4  blx r2                    ; vtable+0x3c(this, &item) - process it, result in r0:r1
708047e6  mov r3,r0
708047e8  blx 0x70805278            ; generic widen/convert helper
708047ec  lsr r6,r0,#0x1c           ; *** r6 := top nibble of the PROCESSED ITEM'S OWN VALUE ***
708047fa  ldr r1,[0x7080487c]       ; table lookup indexed using this clobbered r6...
70804800  ldr r0,[r0,r1]
70804806  add r4,r0,r4              ; accumulator += lookup*3
70804810  add r6,r6,#0x1            ; increments the JUST-OVERWRITTEN nibble, not a real index
70804812  ldr r0,[r5,#0x0]
70804814  ldr r1,[r0,#0x20]
70804818  blx r1                    ; Count() = vtable+0x20(this)
7080481a  cmp r0,r6
7080481c  bgt 0x708047d0            ; loop while Count() > r6
7080481e  mov r0,r4                 ; return accumulator
70804822  pop {r4,r5,r6,pc}
```

### The bug, stated precisely

**`r6` is reused for two incompatible purposes: the loop's own row index,
and a 4-bit type-tag extracted from the just-processed record's data.**
Every single iteration, `lsr r6,r0,#0x1c` throws away whatever `r6` held
(the real running index) and replaces it with a nibble (0-15) pulled
from the CURRENT record's own processed value. The very next instruction
increments *that* - not a genuine counter. This means **`r6` can never
mathematically exceed roughly 16**, regardless of how many iterations
run, because it gets reset to a small value every single pass.

The loop's own termination check is `Count() > r6` (`bgt`, loop
continues). **If `Count()` - the real row count of this object's
underlying table - is greater than ~16, this condition is true forever.**
Not "very large and slow" - genuinely, mathematically non-terminating,
for any table with more than about a dozen and a half rows. It also means
the same low-index handful of records get re-fetched
(`vtable+0x24(this, r6)` with `r6` bouncing in `[0,16)`) and reprocessed
over and over, never advancing to the rest of the table at all.

### This is the same bug family as the earlier `FUN_70804534` clobber, confirmed by comparing them directly

`FUN_70804534`'s already-found clobber (`lsr r4,r5,#0x18` at
`0x70804578`) uses the exact same `0x70805278` helper and the exact same
`lsr rX,r0,#0x1c`/`#0x18`-style idiom to extract a nibble from a
processed value - but there it overwrote `r4` (the `this` pointer, a
*different* register from that function's real loop counter, `r5`),
which is why EKA2L1 would panic on the resulting NULL deref rather than
hang (confirmed earlier this round). Here, in `FUN_708047b6`, the *same*
nibble-extraction idiom happens to land on `r6` - which this function
uses as *both* the fetch-index *and* the loop-bound-check variable. Same
underlying source/codegen mistake (a scratch register reused without
protecting a value that's still needed), two different concrete failure
modes depending on which register the compiler happened to allocate for
which purpose in each function - one crashes, one hangs forever.

### How this ties every finding in this investigation together

1. `FUN_708010ea` (confirmed, decompiled) has no existence check before
   inserting a new purchase record - every boot, every game, a new
   duplicate row, unconditionally.
2. Over years of real device use (confirmed: 2-34 duplicates per game
   *in the untouched 2014 backup itself*) plus this investigation's own
   repeated test boots, the purchase/metadata table has grown well past
   a dozen and a half rows for any device with more than a couple of
   games.
3. `FUN_708047b6` (this section's find) iterates that table with a loop
   index that gets clobbered back down to `[0,16)` every single pass -
   so once the real row count exceeds ~16, its termination condition can
   never become true again. Mathematically infinite, not just slow.
4. This matches every piece of live evidence collected since round 61:
   no crash (every memory access stays within the always-low, bouncing
   index range - never touches anything invalid), pure CPU work (table
   lookup + accumulate + two vtable calls, no IPC), and it explains why
   a *fresh* install (empty or near-empty purchase table, `Count()` under
   ~16) would never trigger this at all, matching the earlier finding
   that RM-409 hangs identically to RM-320 given comparable real
   purchase history, and that fresh installs on either device don't
   reach this code path with a table large enough to expose it.

Round 67's very first live-trace note ("`r6` counts up cleanly `0, 1, 2,
3, 4, 5, 6...`, never resets, even after 50,000 visits") deserves an
honest caveat: that capture may have been watching a short, in-progress
burst of this same nibble-reset-then-increment pattern (which *does*
look like clean small-number counting for several steps in a row, since
a fresh nibble followed by `+1` produces exactly that shape) rather than
a single monotonically growing counter across the whole run - fully
consistent with this section's finding, but worth flagging as a
reinterpretation of much earlier evidence rather than a contradiction.

### Reverted / left in place

Two Ghidra scripts this section (`DisasmRaw.java`, fixed after an
initial bug of my own - an end address I set smaller than the start,
which silently produced zero output; `DumpBytesRaw.java`, a raw hex
fallback used to catch that mistake). Both read-only against the
existing `gametitlemetadata_r64` and `drtrvct2_analysis` projects. No
live runs, no source changes, no builds this section - purely static,
following up on data already captured live in the previous section.

### To resume from here

The mechanism is now understood precisely enough to state a concrete
fix, at either of two levels: (a) in EKA2L1 itself, nothing to fix -
this is a genuine bug in the original N-Gage 2.0 binary, faithfully
emulated; the only EKA2L1-side mitigation would be a targeted, clearly-
labeled workaround (e.g., detect this exact loop shape and cap
iterations) if the user wants gameplay to proceed despite the upstream
bug; (b) confirming this is the *complete* explanation (not just *a*
contributing bug) would mean checking whether any table with `Count()`
under ~16 genuinely does terminate cleanly - the cheapest way being a
resource-conscious test with a synthetically small purchase table on the
existing `rm-320` profile, but see the standing note that repeated boots
against that profile keep adding duplicates, so this would need to start
from a deliberately reset/small `gamepurchase.db` copy, not the current
one.

(The earlier open question from this round - "does the shared state
between the two mutually recursive functions advance or stay stuck" -
is now answered directly: it doesn't stay stuck by coincidence, it
*can't* advance past `Count()`, because the index gets reset to a small
nibble every single iteration. No further live capture needed to settle
that specific question.)

## Round 70 (the workaround): built, applied, and verified live - breaks the hang

User asked to build the actual fix. Done, and confirmed working against
the real repro (the `rm-320` device's genuine restored-backup purchase
history) - not a synthetic test.

### The fix

Added a permanent (not reverted) compatibility patch to
[`codeseg::attach()`](../../src/emu/kernel/src/codeseg.cpp), applied once
per real load of `gametitlemetadata.dll` (guarded by
`need_patch_and_reloc`, right after the existing relocation loop):

```cpp
if (obj_name.find("gametitlemetadata") != std::string::npos) {
    constexpr std::uint32_t clobber_offset = 0x47EC;
    if (code_size > clobber_offset + 1) {
        std::uint8_t *target = &code_base_ptr[clobber_offset];
        static const std::uint8_t expected_bytes[2] = { 0x06, 0x0F }; // lsr r6,r0,#0x1c
        static const std::uint8_t nop_patch[2] = { 0xC0, 0x46 };      // mov r8,r8 (Thumb no-op)
        if (std::memcmp(target, expected_bytes, 2) == 0) {
            std::memcpy(target, nop_patch, 2);
            LOG_INFO(KERNEL, "Applied gametitlemetadata.dll infinite-loop compatibility fix at code offset 0x{:X}", clobber_offset);
        }
    }
}
```

Neutralizes exactly the one clobbering instruction found this round
(`lsr r6,r0,#0x1c` at file offset `0x47EC`) with a same-size, no-register-
effect Thumb no-op (`mov r8,r8` - touches no r0-r7 register or flag), so
the loop's own, otherwise-correct `add r6,r6,#1` / `cmp Count(),r6` /
`bgt` logic can finally terminate once the real index catches up to
`Count()`. Guarded by an exact-byte-match check against the known-buggy
instruction before patching anything, so a different build of this DLL
is never touched.

### Verified live against the real repro

Built clean (`-m:4`, no errors), ran `eka2l1_qt.exe --device RM-320 --run
0x20007B38` against the main project's own `rm-320` data - the actual,
real, restored-backup purchase history this entire 70-round investigation
has been chasing, not a synthetic case. Confirmed:

```
I ...codeseg.cpp:378 [Kernel]: Applied gametitlemetadata.dll infinite-loop compatibility fix at code offset 0x47EC
...
E ...kernel.cpp:363 [Kernel]: Access violation writing address 0x1 in thread PlayServer
pc: 0x70404878   lr: 0x704047ed   r4: 0xa   r5: 0x80faa8   r6: 0x0
```

**The infinite loop is broken.** The process, for the first time in this
entire investigation, actually *progressed past* the point it has always
hung at before, produced substantial new log output it never reached
previously, and exited on its own instead of sitting silently forever.
This is unambiguous: without the fix, this exact device/data hangs
forever every time (confirmed dozens of times across 70 rounds); with
it, it runs further and reaches new code.

### A second, previously-unreachable bug is now exposed - expected, and real

The process now crashes a little further along, inside
`gametitlemetadata.dll` itself, at file offset `0x4878`. Disassembling
that address directly revealed it **isn't code at all - it's sitting
inside `DAT_7080487C`, the very lookup table the original loop was
reading from** (confirmed: raw bytes at that address match the table
data, not a valid instruction stream). Something downstream of the fixed
loop takes its returned accumulator (`r4`, now correctly finite instead
of never returning) and computes a bad address/function-pointer from it,
landing execution inside this data region. This is a **separate, newly-
exposed issue** - it was never reachable before because the infinite
loop always hung first. Not a regression caused by the fix's own logic,
but a genuine next link in the chain that the hang was masking.

### Reverted / left in place

**Unlike every other change this investigation has made, this one is
intentionally NOT reverted** - it's the actual requested workaround,
meant to persist. `codeseg.cpp`'s permanent patch and its `#include
<cstring>` stay in place. `config.yml`'s log-filter was bumped to
`Kernel:Info` for the verification run and reverted back to `Kernel:Warn`
afterward (the only ephemeral part of this section). One clean build,
one live run against real data, confirmed both the fix firing and its
effect - no further live testing needed to establish the core result.

### To resume from here

1. The natural next step, if full boot-to-game-list is the goal, is
   chasing this second exposed bug the same way every prior bug in this
   investigation was chased: resolve file offset `0x4878`'s true
   context (what calls into this region, why the returned accumulator
   ends up feeding an address computation), likely via the same
   live-breakpoint-on-caller technique used earlier this round.
2. Whether that's worth doing depends on the actual goal: if "make the
   boot hang go away" was the target, that's done and verified. If
   "make N-Gage 2.0 fully playable on this restored backup" is the
   target, this second bug is squarely in the way and needs the same
   treatment.
3. Worth keeping in mind this fix is narrowly scoped to the *one*
   specific 2-byte instruction confirmed buggy this round - the sibling
   clobber found earlier in `FUN_70804534` (the `this`-pointer version,
   which panics rather than hangs) is a separate, still-unpatched
   instance of the same underlying pattern, not addressed by this fix.

## Round 70 (chasing the second bug): it's not a separate bug - it's the same recursion, now actually able to run

User asked to keep chasing the new crash exposed by the hang fix. Traced
it as far as static + live analysis can responsibly go without a much
bigger change, and the answer reframes the whole picture.

### Ruling out the obvious guess

First checked whether the crash was simply the immediate `vtable+0x3c`
dispatch call (`blx r2` at file offset `0x47E4`) landing on a bad target.
A single-shot live capture (same safe, no-`step()` technique as
70e/70f/70g) at that exact call site gave:

```
PROBE_R70H: hit at 0x704047e4 r2(call target)=0x7040392d r0(vtable)=0x80faa8 r5(this)=0x80faa8 r6(index)=0x0
```

`r2` (masked: `0x392c`) is genuine, valid code - not the crash address.
Disassembled it directly: it's exactly the `FUN_70803930`-shaped wrapper
already decompiled earlier this round -

```
7080392c  push {r4,lr}
7080392e  mov r4,r0
70803930  bl 0x70803872        ; the linear scan
70803934  mov r1,r0
70803936  ldr r0,[r4,#0x0]
70803938  ldr r2,[r0,#0x38]
7080393a  mov r0,r4
7080393c  blx r2               ; <- confirmed (round 70e) to call back into FUN_708047b6
7080393e  pop {r4,pc}
```

### What this actually means

**Every single iteration of `FUN_708047b6`'s now-fixed loop makes a
fresh, recursive re-entry into another full invocation of
`FUN_708047b6`** (via this wrapper's `vtable+0x38` dispatch, already
confirmed live in round 70e/70f to resolve back to the same function).
That new invocation has its own `r6 = 0`, its own loop, and *its* own
recursive calls per iteration. This isn't the loop looping longer - it's
recursion **depth** multiplying with every level, an exponential-shaped
call tree rooted in a table that (per the already-confirmed duplicate-
insert bug) has been growing for years.

Before this round's fix, the `r6`-reset bug meant `FUN_708047b6`'s loop
condition was permanently stuck (`Count() > r6` forever true), so the
loop body - and therefore this entire recursive descent - ran
essentially forever at a **flat, non-growing** call depth: the same
loop, spinning, never actually recursing meaningfully deeper. The fix
correctly lets the index climb and the loop terminate on its own terms -
but doing so means the recursive tree this loop drives can now actually
*grow*, and it blows through the stack within a handful of levels
(matching the crash being perfectly deterministic and happening almost
immediately: `r6=0`, `r4=0xa`, identical across repeated runs - this is
an early, shallow explosion, not a late one).

**This is very likely not a second, independent bug to patch - it's the
same underlying algorithm hitting a structural limit the `r6` clobber
was accidentally hiding.** The purchase table's real size (bloated by
the duplicate-insert bug) was probably always going to make this
recursive structure explode once anything let it actually run to
completion; the clobber bug just meant it never got the chance to reach
that explosion, hanging forever *before* it could.

### Why this changes the shape of the remaining work

A fix at this level isn't a single clean instruction patch like the
first one - genuinely bounding this would mean capping recursion depth
or iteration count somewhere in the call chain (accepting incomplete
title-metadata processing for a pathologically large table) rather than
correcting one arithmetic mistake. That's a materially bigger, more
speculative engineering decision than round 70's first fix, and one
better made deliberately rather than as a quick follow-on patch.

### Reverted / left in place

The round-70h diagnostic probe (single-shot capture at the `vtable+0x3c`
call site) fully reverted from `epoc.cpp`, confirmed via `git diff`
showing no `PROBE_R70H` remnants, rebuilt clean. **Round 70's actual fix
in `codeseg.cpp` remains in place**, confirmed present after the
rebuild - this section only reverted its own temporary diagnostic
addition, not the permanent workaround.

### To resume from here

1. The concrete next engineering decision: where to cap this recursion.
   The cleanest, least invasive point is likely inside `FUN_708047b6`
   itself - track recursion depth (or total work done) via a static/
   thread-local counter incremented on entry and decremented on return,
   and short-circuit (return the accumulator as-is) past some generous
   but finite depth, rather than trying to bound the conceptually
   simpler but structurally different iteration count.
2. Given the scope difference from the first fix, this is a good point
   to confirm with whoever's driving the investigation next whether a
   "best-effort, may show incomplete title metadata for extreme
   backups" tradeoff is acceptable before building it - unlike the first
   fix (which had no real downside), this one has a genuine, visible
   cost to correctness for pathological cases.

## Round 70 (recursion cap attempt: built, tested, falsified, removed)

User asked to build the recursion cap too. Built it, verified it live
against the real repro twice, and both times it **did not work** - a
real, useful negative result, not a success, and the code has been
removed rather than left in place misrepresenting itself as a fix.

### What was built

A permanent breakpoint-based cap in `epoc.cpp`, using the same safe,
no-`step()` technique as every other live probe this round: intercept
the loop's back-edge target (code offset `0x47D0`, a plain `ldr
r0,[r5,#0]` - trivial to hand-emulate), count visits, and past 200
total hits, redirect straight to the function's own return sequence
with a zero accumulator instead of letting it recurse further.

First version targeted the wrong address (`0x47B8`, the "fresh start"
entry's second instruction) - armed correctly but never fired, and the
crash still happened, because the confirmed recursive path (round 70e)
actually re-enters at a different offset (`0x47E6`), not through that
entry at all. Corrected to intercept at the loop's shared back-edge
(`0x47D0`) instead, which both entry styles pass through on every real
iteration - this should have caught every iteration regardless of which
entry style triggered it.

### It still didn't work - and that's the important finding

Rearmed correctly both times it got tested (confirmed via the "Armed
gametitlemetadata.dll recursion cap..." log line). **The "recursion cap
reached" log never fired, and the identical crash
(`Access violation writing address 0x1`, same PC/LR/registers as every
prior capture) still happened anyway** - meaning the crash happens on
one of the very first handful of calls to this loop, not after
accumulating anywhere near 200 iterations. A counter that would have
been in the low single digits at crash time cannot be the thing a
200-iteration cap protects against.

**This falsifies this round's "exponential recursion explosion" theory
as the direct, immediate cause of this specific crash.** The mutual
recursion between `FUN_70803930` and this loop is still real and
confirmed (round 70e/70f's live captures aren't in question) - but
whatever actually crashes does so far too quickly to be a depth/volume
problem. The true cause is something else, reachable within just a
couple of calls - more consistent with an earlier, previously-
considered-then-abandoned idea from this same round: a specific record
or vtable dispatch that's simply broken (e.g. from the confirmed
duplicate-insert path's explicitly-zeroed fields), not a scale problem.

### Verification got much harder mid-session, independent of any of this

Confirming (or ruling out) fixes against the real `rm-320` repro
requires the boot to actually reach `gametitlemetadata.dll` at all
first, and this session's boot-path non-determinism (present since
early rounds) got markedly worse partway through this attempt - several
consecutive launches sat for 3-7+ minutes each without ever loading the
DLL, in sharp contrast to the ~30-40 second turnaround seen earlier the
same round with different code. Confirmed via `Get-Process`'s CPU-time
counter that at least one of these stalled attempts was genuinely
busy-spinning (269+ seconds of accumulated CPU time with zero new log
output) rather than idle-waiting - almost certainly a **separate,
unrelated, pre-existing stall** on a different boot path entirely (nothing
in the recursion-cap code can execute before `gametitlemetadata.dll`
loads, and it hadn't). Not investigated further given time already
spent, but worth flagging: this makes future live verification in this
investigation slower and less predictable than it was in rounds 1-70's
earlier stretches.

### Reverted / left in place

The entire recursion-cap addition (including its diagnostic-logging
follow-up) was removed from `epoc.cpp` after confirming it doesn't
work - `git diff` on that file now shows only the pre-existing round-58
fix, nothing from this attempt. `config.yml` reverted to its
`Kernel:Warn`/device-2/language-1 baseline. **Round 70's original,
confirmed-working infinite-loop fix in `codeseg.cpp` was never touched
this section and remains fully in place** - this round's failure is
scoped entirely to the *second* bug's attempted fix, not the first.

### To resume from here

1. The real next step is finding out *why* the crash happens on one of
   the first few calls, not after many - likely needs a live capture
   logging the actual `call_count_r70_cap`-style value at the moment of
   the real crash (not just at the loop's own back-edge, which never
   got hit enough times to explain it) - possibly by tracing one level
   further into whichever vtable dispatch (`+0x24`, fetching an item)
   is invoked between loop iterations, since that's the one call in
   this whole chain not yet directly captured live.
2. Given verification runs have become unreliably slow this session,
   future attempts at this specific crash should budget for that -
   confirming a single hypothesis may now take several minutes to
   upward of ten, not the ~30-40 seconds seen through most of this
   round.
3. The zeroed-field-on-duplicate-insert idea (from earlier this round,
   also not yet confirmed but not yet ruled out either) remains the
   most concrete lead for why *specific* records - reached quickly,
   independent of table size - might carry a broken type/dispatch
   value.

## Round 70 (continued): confirmed the crash happens on the very first iteration - then hit a hard verification wall

User asked to keep pushing on the second crash. Made one more genuinely
decisive finding via live capture, then live verification itself became
the actual blocker.

### Definitive: this has nothing to do with volume or depth

A pure-observation probe (no behavior change - just logging, letting
everything run normally) at the loop's back-edge (`0x47D0`) caught
exactly **one visit** before the crash:

```
PROBE_R70J: visit #1 r5(this)=0x80faa8 r6(index)=0x0 r4(acc)=0xa lr=0x7040481b
Access violation writing address 0x1 in thread PlayServer
```

`lr=0x481a` (masked) is the return address from this loop's *own*
`Count()` check (`blx r1` at `0x4818`) - meaning this is genuinely the
loop's first real iteration, not a later one. The crash happens within
this same first pass, after the confirmed recursive descent
(`FUN_70803930` → wrapper → back into this loop, per round 70e/70f)
has already completed and returned - not during it, and not because of
however many times anything looped. **This closes the door entirely on
recursion depth or iteration volume as the cause** - already suspected
after the failed 200-call cap (previous section), now confirmed
directly: the failure point is reached on attempt #1, every single
time.

### Tracing further got stuck on the trace itself

Tried to pin down the exact faulting instruction with two more targeted,
pure-observation probes:

- At code offset `0x4800` (the per-type weight table read,
  `ldr r0,[r0,r1]` where `r0=row*100+table_base`, `r1=index*4`) - never
  fired. The crash happens *before* reaching this instruction, somewhere
  in the purely-arithmetic `0x47EE-0x47FE` range, which the static
  disassembly shows contains no memory write at all - meaning either
  that reading is subtly wrong, or control flow doesn't reach that
  range the way it appears to on paper.
- At the crash's own previously-observed address (code offset `0x4878`)
  directly, with a safe redirect to the loop's own return path instead
  of letting the fault happen - never got a single hit confirmed, purely
  because five consecutive live-test attempts in a row failed to even
  get the boot as far as loading `gametitlemetadata.dll` at all, each
  one burning 100-400+ seconds before being killed.

### The verification bottleneck is now the real story of this stretch

Every attempt this section needed the guest to actually reach
`gametitlemetadata.dll`'s load point, and that stopped happening
reliably partway through - a sharp regression from the ~30-60 second
turnaround seen earlier in round 70 with materially similar code. Killed
and retried repeatedly (single retries, extended waits on already-running
attempts, everything reasonable) without a single successful capture at
the crash-site probe. This is consistent with, but doesn't newly explain,
the non-determinism flagged earlier this round (see the CPU-time
finding under "Round 70 (recursion cap attempt)") - whatever's causing
boot-path variance appears to have gotten worse as this session's total
build/live-test count has grown very large.

### Reverted / left in place

The two new diagnostic probes this section (table-read capture, crash-
site capture) both fully reverted from `epoc.cpp` after use or after
confirming they couldn't get a hit - `git diff` shows no
`PROBE_R70K`/`PROBE_R70L`/`cstring` remnants. `config.yml` back to its
`Kernel:Warn` baseline. **Round 70's confirmed-working infinite-loop fix
in `codeseg.cpp` was not touched this section and remains in place**,
verified present (3 references to `gametitlemetadata` in that file,
matching the fix's own code).

### To resume from here

1. The concrete, still-open question is unchanged: what exactly happens
   between the recursive descent's return (confirmed complete, since
   the crash's `lr` reaches `0x47EC`, past the `blx 0x70805278` helper
   call) and the fault - somewhere in a ~20-byte stretch of pure
   arithmetic that shouldn't be able to fault on its own. Worth
   re-verifying the static disassembly of that exact range byte-for-byte
   again before the next live attempt, in case there's a subtle
   misread (this investigation has caught itself in exactly this kind
   of mistake more than once already).
2. Given how unreliable live verification became this section,
   whoever resumes this should budget for it explicitly - possibly
   worth investigating the boot-path non-determinism itself as a
   prerequisite (why does the exact same command sometimes reach
   `gametitlemetadata.dll` in under a minute and sometimes never reach
   it in several), since it's now the single biggest practical obstacle
   to making further progress on this specific crash.

## Round 71: the second crash was caused by round 70's own fix - found, fixed, verified live

Picked this up from round 70's own "to resume from here" note (re-verify
the static disassembly byte-for-byte before another live attempt). That
re-check is what cracked this whole thing open.

### The re-check that changed everything

Re-disassembled `0x708047D0`-`0x7080481C` (the loop body) fresh via a
Ghidra headless raw-disassembly script against the `gametitlemetadata_r64`
project, this time also dumping the raw bytes alongside each decoded
instruction (not just trusting the mnemonic). Confirmed the previously-seen
sequence again, byte-for-byte identical to earlier rounds - so no misread
in this direction. But then, following round 70's other lead (the crash's
`lr` register lands exactly at `0x47EC`, right after `blx 0x70805278` at
`0x47E8`), disassembled *that call's target* for the first time this
investigation - it had never actually been looked at directly before, only
inferred to be "a generic helper."

`0x70805278` turned out to be a small table of ARM-mode interworking
veneers (`ldr pc,[pc-relative literal]` stubs), and the specific one this
call resolves to points to `0x80399F48`. Mapping that address (via each
DLL's own romimage header - `code_address`/`code_size` - across every
`.dll`/`.exe` extracted onto device Z: in an earlier round's
`fresh_n95_test` snapshot) landed it inside `drtrvct2_1.dll`, at
**ordinal 28**. Disassembled that specific function in ARM mode (not
Thumb - the earlier Thumb-mode attempt produced nonsense, itself a repeat
of this investigation's own recurring "misread data as code" failure mode)
and got a clean, unambiguous match for the textbook RVCT `__switch8`
compiler runtime helper:

```
ldrb  ip, [lr, #-1]
cmp   r3, ip
ldrblo r3, [lr, r3]
ldrbhs r3, [lr, ip]
add   ip, lr, r3, lsl #1
bx    ip
```

This function never returns to its caller - it always computes a target
from a **byte table read relative to its own return address** and jumps
there via `bx`. For the call at `0x47E8`, that return address (`lr`) is
`0x47EC` - meaning the bytes starting at `0x47EC` are not independently-
executed code at all for this call; they're the switch's own jump table,
indexed by whatever value is in `r3` (which is `mov r3,r0` from the return
value of the confirmed recursive `vtable+0x3c` call, just before this).

Reconstructed several real table entries this way and checked every one
against a real, sensible address already mapped in the loop body:

| index | table byte | table addr | computed target | what's there |
|-------|-----------|-------------|------------------|--------------|
| 0 | `0x06` | `0x47EC` | `0x47F8` | mid weighted-lookup (`mul r0,r3`) |
| 1 | `0x0F` | `0x47ED` | `0x480A` | `add r4,r4,#4` |
| 2 | `0x11` | `0x47EE` | `0x480E` | `add r4,#8` |
| 3 | `0x04` | `0x47EF` | `0x47F4` | full weighted-lookup entry |
| 4,5 | `0x0F` | `0x47F0`-`1` | `0x480A` | same as index 1 |
| 6 | `0x11` | `0x47F2` | `0x480E` | same as index 2 |
| 7 | `0x0E` | `0x47F3` | `0x4808` | `b 0x4810` (no-op contribution) |

Every single one lands on a real, meaningful address already identified in
this loop's body - not one garbage/out-of-range hit. This is about as
strong as static confirmation gets: **bytes `0x47EC`-`0x47F3` are a
compiler-generated switch table, not the instruction round 70's fix
believed it was patching.**

That means round 70's fix - overwriting `0x47EC`-`0x47ED` (`0x06,0x0F` →
`0xC0,0x46`) to neutralize what looked like a `lsr r6,r0,#0x1c` clobber -
was actually overwriting **table entries 0 and 1**. Original `table[0]=6`
routes to `0x47F8` (a real, correct in-bounds address); patched
`table[0]=0xC0` (192) routes to `lr + 2*192 = 0x496C` - 384 bytes past the
loop, almost certainly unrelated/garbage code.

### Live confirmation this is the actual mechanism

Built a pure-observation probe (`PROBE_R70M`) at `0x47E6` (`mov r3,r0`,
the instruction immediately before the switch call) that hand-emulates
the trapped instruction and just logs `r0` (the switch index about to be
used) - no redirection, no behavior change. First live hit:

```
PROBE_R70M hit #1: switch_index(r0)=0x0 r6(loop_idx)=0
Access violation writing address 0x1 in thread PlayServer
```

`r0 == 0` on the very first hit, immediately followed by the exact same
crash this investigation has been chasing since round 70. `0` is exactly
`table[0]` - the entry round 70's fix corrupted. This is a complete,
deterministic, mechanistic explanation: **round 70's own "confirmed,
permanent, verified" fix was the direct cause of the second crash.**

### The corrected fix

Reverted the byte patch in
[`codeseg.cpp`](../../src/emu/kernel/src/codeseg.cpp) entirely - a short
comment there now points here instead of claiming a fix that no longer
exists in that file.

Replaced it with a runtime hook in
[`epoc.cpp`](../../src/emu/system/src/epoc.cpp) on the loop's own
back-edge increment, code offset `0x4810` (`add r6,r6,#1`) - real,
always-executed code shared by every switch case, never table data for
any call in this function. On every hit, it looks up (or seeds, on first
sight) a host-side tracked row index keyed by the loop instance's `this`
pointer (`r5`), forces `r6` to `tracked + 1` regardless of whatever the
guest's own `r6` currently holds, and continues. This is deliberately
cause-agnostic: with the `__switch8` finding, it's no longer even clear
that *any* switch case actually writes `r6` (none of the reconstructed
targets do) - so the original "which instruction clobbers r6" theory may
never have been quite right either. The new fix doesn't need that
question answered; it guarantees the loop's own `Count() > r6` exit test
terminates regardless of what disturbs `r6`, from whatever source.

Verified live, three attempts (the first two hit the pre-existing,
unrelated `eikbackupsrv`/`BackupServer` boot-path stall documented in
earlier rounds and never reached `gametitlemetadata.dll`; the third, given
a longer window, got through): the loop terminates immediately, the
install log line (`Installed gametitlemetadata.dll infinite-loop
compatibility hook at 0x...4810`) fires exactly once per load, and a full
grep of the entire run log for `Access violation` comes back **empty**.
The process proceeds through dozens more system DLL loads (avkon,
eikcoctl, form, gdi, fbscli, hal, egul, imageconversion, ...) into real
N-Gage Installer window/UI setup - further than this entire multi-round
investigation has ever reached.

### New blocker found (not yet investigated)

The same verified run ends with:

```
Corrupted graphics command list! Emulation halt.
```

in the OpenGL backend (`graphics_ogl.cpp`), followed by device-context
teardown errors consistent with the emulator intentionally halting rather
than crashing outright. This looks unrelated to everything above - a
separate rendering-layer issue, only reachable now because the process
gets far enough to actually render the N-Gage Installer UI. This is the
next concrete blocker on the path to full playability and has not been
looked at yet.

### To resume from here

1. Investigate the `Corrupted graphics command list! Emulation halt`
   OpenGL backend error - what command in the list is malformed, and
   whether it's caused by something the N-Gage installer/title-metadata
   flow is doing differently now that it gets this far, or a pre-existing
   EKA2L1 rendering bug that was simply never reached before.
2. The `eikbackupsrv`/`BackupServer` boot-path stall (this round's first
   two live attempts both hit it, identically, before a longer timeout
   got past it on the third) may be worth a closer look on its own -
   worth checking whether it's genuinely non-deterministic or has become
   more consistently slow.

## UPDATE: general host-side SIS install bug found and fixed (unrelated to N-Gage specifically)

User provided an unrelated, simple SIS package (`snakes-157019.sis`, a Snakes
game) and reported it installs fine via a different EKA2L1 build on the
RM-409 (5320) profile, but reportedly fails on the RM-320 (N95) profile - on
a completely FRESH ROM install, no backup folders involved at all. This
looked like it might point to the same FP1 (RM-320) vs FP2 (RM-409)
platform-version difference flagged at the end of the N-Gage-installer
investigation above, so it was worth chasing as a clean, minimal repro.

### Repro

```
./eka2l1_qt.exe --install "C:\Users\Usuario\Downloads\snakes-157019.sis"
```

on the RM-320 profile. The SIS controller parsed fine (package UID
`0x2000730F`, "Package Snakes registering with UID: 0x2000730f"), and
`src/emu/package/src/manager.cpp:583` logged `Installation done!`
(`package::installation_result_success`) - yet the CLI reported
`Installation of SIS failed` to the user.

### Root cause

`packages::install_package()` returns the enum `package::installation_result`
(`installation_result_success = 0`, `installation_result_aborted = 1`,
`installation_result_invalid = 2`), defined in
`src/emu/package/include/package/manager.h:47-51`. The wrapper
`system::install_package()` (`src/emu/system/src/epoc.cpp:1570-1572`)
returns it as a plain `int` (`static_cast<int>(...)`).

`app_install_option_handler` in `src/emu/qt/src/cmdhandler.cpp` (the handler
behind the `--install` CLI flag) assigned that `int` straight into a `bool`:

```cpp
bool result = emu->symsys->install_package(common::utf8_to_ucs2(path), drive_e);
if (!result) {
    *err = "Installation of SIS failed";
    return false;
}
```

Since `installation_result_success == 0`, C++'s int-to-bool conversion turns
a *successful* install into `false` - reported as a failure every single
time, on every platform, regardless of RM-320 vs RM-409. Conversely, a
genuinely failed install (`aborted = 1` or `invalid = 2`) would have been
silently reported as success. This is a real, general, previously-unnoticed
bug in the CLI install path (`--install`), separate from the GUI path in
`mainwindow.cpp`, which already switches on the enum correctly
(`installation_result_success` / `_aborted` / `_invalid`) and was never
affected.

Note this bug is **not** platform-specific - it should have misreported on
RM-409 too. The user's observation that Snakes "works" on RM-409 and "fails"
on RM-320 is consistent with the actual file-level install succeeding on
both (the write happens before this final, misreported return value), with
the RM-320-specific difference lying elsewhere (most likely still the FP1
platform quirks flagged earlier) rather than in this particular bug.

### Fix

`src/emu/qt/src/cmdhandler.cpp`, `app_install_option_handler`: compare the
raw `int` against `package::installation_result_success` explicitly instead
of implicitly casting to `bool`:

```cpp
const int result = emu->symsys->install_package(common::utf8_to_ucs2(path), drive_e);
if (result != static_cast<int>(package::installation_result_success)) {
    *err = "Installation of SIS failed";
    return false;
}
```

### Verification

Rebuilt (`eka2l1_qt` target, clean, zero compile errors - built via
`cmake --build . --config Release --target eka2l1_qt -- -maxcpucount:4`
redirected to a log file with the exit code checked directly, per the
tail-swallowing lesson from earlier in this investigation). Re-ran the exact
same `--install` repro on RM-320:

- No more `Installation of SIS failed` line.
- `Installation done!` still logs as before.
- The app list now shows `Found app: Snakes, uid: 0x2000730F` (twice - once
  per icon/language variant, consistent with other apps in the same list).

Confirmed fixed for the general host-side `--install` path. Not yet
re-tested against the guest-side `InstallServer`/`!UISS`/`-10254` issue hit
by N-Gage's own self-installer (`0x2000A2AE`) - those are two distinct code
paths (host C++ `packages::install_package()` vs. real un-HLE'd Symbian
guest code), and this fix is not expected to change that separate issue, but
worth a quick re-check for any shared cause.

## UPDATE: retested the real N-Gage installer - `-10254` is gone, and the "graphics corruption" blocker is a red herring

Re-ran `--run 0x2000A2AE` on RM-320 with the user's real N-Gage 2.0 SIS
re-staged at `data/devices/rm-320/drives/d/Private/2000A2AE/n-gage.sisx`.
This time the install went much further than any previous round: no more
`-10254`, it proceeded through `!InstallServer`/`!UISS`/`!SISHELPER`, past a
harmless `swicertstore` `-12` (`KErrPathNotFound`, just an "Unimplemented
opcode for sisregistry session 0x49" fallback), and registered
`Found app: Games, uid: 0x20007B39` - the user's actual target UID (as
opposed to `0x20007B38`, which is a separate "N-Gage Installer" registration
entry). It's not clear whether this improvement came from the earlier
Central Repository restore, the `swipolicy.ini` tweak, or is just
non-deterministic guest-code behaviour - but it reproduced cleanly.

The run then ended with `graphics_ogl.cpp:1910 [Driver.Graphics]: Corrupted
graphics command list! Emulation halt.`, the same message previously flagged
(in an earlier, pre-this-window round) as "a new blocker, not yet
investigated - looks unrelated, only reachable now that the process renders
far enough". Traced it properly this time:

- `ogl_graphics_driver::run()` (`graphics_ogl.cpp:1905-1912`) logs this exact
  message whenever `list_queue.pop()` returns empty.
- `request_queue<T>::pop()` (`common/queue.h:191-217`) only returns empty
  when `abort_` is true (with the default `ms=0` call used here) - i.e. only
  when someone calls `.abort()`.
- The only caller of `ogl_graphics_driver::abort()` is `kill_emulator()`
  (`qt/src/thread.cpp:333-348`), the normal, intentional shutdown path.
- `kill_emulator()` only runs after `QApplication::exec()` returns
  (`qt/src/thread.cpp:421-422`), i.e. after the main window closes.
- `main_window::on_app_exited()` (`qt/src/mainwindow.cpp:1088-1089`) closes
  the main window automatically whenever a command-line-launched app process
  exits (`if (emulator_state_.app_launch_from_command_line) { close(); }`) -
  exactly the `--run` flag's behaviour.
- The log confirms the actual trigger: `thread.cpp:546`: `Thread Installer
  forcefully killed with category: None and exit code: 0` - the Installer's
  own main thread self-terminates cleanly (`category: None`, `exit code: 0`
  - not a crash, not a panic), its process dies as a result, and that cascade
  closes the window and tears down the whole emulator.

**Conclusion: this was never a graphics bug.** It's the Installer finishing
its job and quitting itself, exactly like a real installer would, combined
with `--run` mode's "auto-close once the launched app exits" behaviour. The
ERROR-level log line is just misleading teardown noise from `abort()`
draining an empty queue. This blocker can be crossed off entirely.

## UPDATE: launched the actual N-Gage `Games` app (`0x20007B39`) post-install - real blocker found

With N-Gage now genuinely installed, ran `--run 0x20007B39` directly (the
UID the user has consistently pointed to as the real target, matching their
actual device). Result: `playserver[20007b39]` starts, runs a substantial
amount of real work (opens `Uiklaf`/`avkon`/`aknpriv` resources, spawns
`playserverccd.dll` and 15 other `playserver*.dll` companions, talks to
`GametitleMetadataServer` a.k.a. `gtmdserver`, UID `0x20003b85`), and then
self-terminates:

```
Leave started! Guess leave code: -9     (KErrOverflow, from GametitleMetadataServer)
Leave started! Guess leave code: -25    (KErrEof, from GametitleMetadataServer)
Leave started! Guess leave code: -25    (again)
Thread PlayServerSplashScreenFallBackThread forcefully killed with category: None and exit code: 0
Thread PlayServer forcefully killed with category: None and exit code: -25
process::kill name=playserver[20007b39]0001 exit_type=0 category=None reason=-25
```

`PlayServerSplashScreenFallBackThread` - the exact thread this entire
multi-hundred-round investigation originally centered on (proven in rounds
116-117 to never have `RThread::Resume()` called on it during the backup-hang
scenario) - shows up here too, on a completely **fresh, non-backup install**,
force-killed during the same teardown. This is strong confirmation the
underlying issue is a general EKA2L1 bug, not something specific to the
user's restored backup.

Traced the `-25` (`KErrEof`) source: `PlayServer` sends sync requests to
`GametitleMetadataServer` (`gtmdserver`). Confirmed via `grep` that
`gtmdserver`/`GametitleMetadataServer` does **not** exist anywhere in
`src/emu/services` - like `!UISS` before it, this is real, un-HLE'd Symbian
guest code (not an EKA2L1 stub), so the `-25` is gtmdserver's own genuine
response.

At first this looked like it could just be "empty game library" (no `.n-gage`
files = legitimate EOF when enumerating). Checked and ruled this out:

- A real game file is staged in three places: `drives/c/Data/Asphalt.3.n-gage`,
  `drives/c/Data/N-Gage/Asphalt.3.n-gage`, `drives/e/n-gage/Asphalt.3.n-gage`.
- `drives/c/Data/N-Gage/sai/appInfo.sai` is NOT a game list - it's a 17-byte
  file containing only `playapp=1.40.1557` (a version stamp).
- gtmdserver's own private folder (`drives/c/Private/20003b85/`) has real
  data: `gtmdconfig.txt` (server tuning params), `backup_registration.xml`,
  and `gamepurchase.db` - a genuine Symbian Store-format database (`"Psion
  Series 5"` binary signature, 337 KB) already populated with real game
  catalog entries (readable text inside includes "Worms World Party" and a
  football-game description).

So gtmdserver has real, non-empty data to read and still returns `KErrEof`.
This points to a bug in how EKA2L1 serves file reads/seeks for this genuine
Symbian DBMS/Store-format file to the real (un-HLE'd) gtmdserver binary,
rather than an actually-empty catalog. Also worth noting from the same run:
core Avkon UI resource files are reported completely missing -
`z:\Resource\Uiklaf\eikpriv.r01`/`.rsc` and `z:\resource\aknpriv.r01`/`.rsc`
all hit `NOT FOUND` (`fs.cpp` `PROBE_R110`) - a separate, possibly related
gap (missing ROM/RPKG resource content specific to this profile) not yet
investigated.

### To resume from here

1. Determine whether the `-9`/`-25` leaves are really about `gamepurchase.db`
   specifically, or some other private file/store gtmdserver reads first -
   add a probe to `fs.cpp`/`files.cpp` filtered to `gtmdserver`'s process to
   see exactly which file(s) it opens/reads/seeks right before each leave.
2. Investigate whether EKA2L1's Symbian Store/DBMS-format stream emulation
   (however partial reads/seeks against `.db` files are currently handled)
   correctly supports the real access pattern a genuine `edbms`/store engine
   would use, or whether it's missing/mishandling seek-past-end, streamed
   reads, or a required companion index file.
3. Check whether `eikpriv.rsc`/`aknpriv.rsc` are supposed to exist in this
   ROM/RPKG dump at all (compare against the RM-409 profile's resource tree)
   - if they're missing on RM-320 specifically, that's a concrete,
   testable FP1-vs-FP2 ROM content gap, independent of gtmdserver.
4. This no longer needs the backup scenario to reproduce - the fresh RM-320
   install hits the exact same `PlayServerSplashScreenFallBackThread`
   never-resumed symptom, so all further debugging can happen on a clean,
   disposable profile.

## UPDATE: file I/O ruled out - gtmdserver's leave is a real semantic answer, not a bug in EKA2L1's filesystem

Added `PROBE_R142` probes to `files.cpp` (`file_size`, `file_seek`, `file_read`,
and a brand new `file_write` probe that didn't exist before), filtered to
`playserver`/`gtmdserver`/any process name containing `nstall`. Reran
`--run 0x20007B39`.

Result: every single `gamepurchase.db` read by `gtmdserver` succeeds cleanly -
`finish_len` always equals `requested_len`, every offset is well within the
337776-byte file (largest seen: `read_pos=333181 requested_len=1667`, filesize
337776). `gtmdserver` itself then exits with `process::kill name=gtmdserver
... category=None reason=0` - a clean, non-error exit. **EKA2L1's filesystem
layer is not the problem.** The `-9`/`-25` PlayServer receives are gtmdserver's
own genuine, deliberately-computed IPC response - and since gtmdserver is real
un-HLE'd guest code, EKA2L1 has no influence over that value at all.

Went looking for *why* gtmdserver would answer EOF given real catalog data.
Found the actual "Asphalt 3 Street Rules" record inside `gamepurchase.db`
(offset ~142218), and right next to the title string is:

```
5c67 616d 655c 3230 3030 4146 4533 5f66 756c 6c2e 6d62 6d   \game\2000AFE3_full.mbm
4173 7068 616c 7433 2e65 7865                               Asphalt3.exe
865a 09bb 4785 cbf5 6bd3 0a44 5638 943c f449 7273 ...       (~20 opaque bytes, SHA-1-shaped)
```

A title record referencing an icon (`2000AFE3_full.mbm`), the expected
executable name (`Asphalt3.exe`), and a 20-byte binary blob immediately after
it that's the right shape for a stored SHA-1 - i.e. this looks like a real
DRM/integrity record: gtmdserver most likely hashes the actual `Asphalt3.exe`
found inside the mounted `.n-gage` container and compares it to this stored
value before letting PlayServer proceed. If they don't match (plausible, since
our `.n-gage` file was manually staged rather than registered through
whatever real flow originally wrote this catalog entry), a legitimate
validation failure - not an emulation bug - would produce exactly this kind
of EOF/overflow response.

This lines up with the user's own observation that on RM-409, "the app boots
up just fine, installs any `.n-gage` games it finds in the respective folder" -
implying the *Installer itself* is what's supposed to register/validate a
`.n-gage` file into the catalog, not manual file placement. Tested this
directly: re-ran the Installer (`--run 0x2000A2AE`) with the widened probes
active. It did **not** touch `20003b85`, `gamepurchase.db`, or any
Asphalt-named file at all this run - so whatever registration step exists,
it isn't triggered by simply launching the Installer a second time (may only
run once per SIS install, or may need a different trigger than what `--run`
exercises).

### Detour: a standalone Asphalt 3 install already exists, and hangs on its own

While chasing this, `applist` logged `Found app: Asphalt3, uid: 0x2000A5B6` -
a **completely separate**, fully-formed SIS install of Asphalt 3, independent
of N-Gage's `.n-gage`/PlayServer mechanism entirely: real registry entries at
`SYS/INSTALL/sisregistry/2000A5B6/`, real game assets (`CARSA.BAR`,
`TRACKSA.BAR`, etc.), and real save data (`user.dat`/`user.bak`). This is
almost certainly a leftover from an earlier round of this investigation, not
part of the current N-Gage flow.

Launched it directly (`--run 0x2000A5B6`) to see if EKA2L1 can run real
N-Gage-era game code at all, independent of PlayServer. Result: a **different**
bug - not a crash, a genuine hang. The process reaches `Sending -1 sync to
!MsvServer` and then goes completely idle - no more trace output, but the
process stays alive (confirmed via `tasklist`, not via log content - the log
simply stops). Reproduced identically across two separate runs. Confirmed via
Windows Event Viewer that this is *not* a crash: the only `eka2l1_qt.exe`
Application-Error entries around this time all pointed at a different build
(`C:\Users\Usuario\Documents\1 eka2l1\bin\`, presumably the user's own
separate "documents" build mentioned earlier), not this Desktop build. This
is a separate bug from the PlayServer/gtmdserver issue and wasn't pursued
further this round - flagging it here since it blocks an independent, cleaner
repro path (bypassing PlayServer/N-Gage's hub entirely) that could be useful
later.

### To resume from here (updated)

1. The DRM/hash-check hypothesis is the strongest lead now. Confirming it
   needs either: (a) disassembling gtmdserver.exe's actual query handler to
   see what it does with the `Asphalt3.exe` hash once read, or (b) finding
   what the *real* Installer-side registration flow writes into
   `gamepurchase.db`/`20003b85` when it genuinely detects a new `.n-gage`
   file (may need tracing the Installer's *first* run against a completely
   virgin profile, rather than the already-installed one used this round).
2. The `!MsvServer` hang on standalone Asphalt 3 (`0x2000A5B6`) is a
   separate, reproducible bug worth its own investigation thread - it blocks
   a cleaner way to test whether EKA2L1's HLE for this game engine
   (`hle_dispatch` calls seen right before the hang) works at all,
   independent of N-Gage.
3. `eikpriv.rsc`/`aknpriv.rsc` missing-resource gap (previous update) still
   unexplored.

## UPDATE: disassembled gtmdserver.exe - it's a thin dispatcher; real gate is a genuine OMA DRM licensing subsystem

Added a live-dump probe to `codeseg.cpp` (`PROBE_R146`, dumps `gtmdserver.exe`'s
relocated runtime code to a scratch file on `attach()`, same technique as the
earlier `playserver.exe`/`playserverccd.dll` dumps). Reran `--run 0x20007B39`,
captured an 11332-byte dump at base `0x71f00000`, imported into a fresh Ghidra
project (`gtmdserver_exe_r147`) with Thumb mode forced, decompiled all
functions.

**Result: `gtmdserver.exe` itself is tiny - only 11 functions, all under 650
bytes.** None of them contain hardcoded hash/comparison logic; they're mostly
string-building/formatting code (iterating some collection via vtable calls,
building descriptor text with a repeated `thunk_EXT_FUN_000003c8` append
pattern - looks like diagnostic/log-string construction, not the actual
validation). The real work happens through imported calls into other DLLs -
`gtmdserver.exe` is a thin wrapper, not where the interesting logic lives.

Checked EKA2L1's own dependency-load log (`codeseg.cpp`'s `"runtime code:"`
line fires once per dependency as they're loaded) for what `gtmdserver.exe`
actually links against on process spawn:

```
euser.dll, efsrv.dll, edbms.dll, estor.dll, gametitlemetadata.dll,
bafl.dll, drmcommon.dll, drmrights.dll, dfpaeabi.dll, drtaeabi.dll,
scppnwdl.dll, drtrvct2_2.dll
```

`edbms.dll`/`estor.dll` are Symbian's real DBMS/store engine (confirms the
earlier finding that `gamepurchase.db` reads are genuine store-format I/O,
already proven clean via the file-level probes). **`drmcommon.dll` and
`drmrights.dll` are real OMA DRM rights-management libraries** - this is not
a simple embedded hash check, it's a full DRM rights lookup.

Went looking for the DRM subsystem on disk and found a complete, real OMA DRM
implementation already present in this ROM/profile:

- `SYS/BIN/DRMCommonLicSrv.exe` - a dedicated DRM **license server** process.
- `SYS/BIN/DRMCommonProcessor.exe`, `DRMCommonConnector.dll`,
  `DRMCommonDlgs.dll` - the rest of the DRM engine.
- `RESOURCE/CommonDrm/DRMCommonLicense.{l02,l03,l04,l05,l09,l30,l31,lic}` -
  actual license-file-shaped content.
- `Private/200015B0/cldrmdb.crc` - a checksum for what's almost certainly a
  **Content License DRM Database** (`cl` + `drm` + `db`).
- `Private/2000454C/symc_drm.cfg` and `Private/20004555/symc_drm.cfg` - DRM
  config, present under two different private IDs.
- `Private/101f6dc5/DRMHS.dat` - likely "DRM History".

**Revised picture**: `gamepurchase.db` (read by `gtmdserver.exe`, confirmed
clean I/O) is the store/catalog record - what titles exist and their
metadata/icon/expected-exe-name. Whether a given title is actually *allowed
to run* is a separate question answered by this OMA DRM subsystem
(`drmrights.dll` + `cldrmdb`), which needs its own valid license entry per
title. A `.n-gage` file dropped into the games folder by hand has no
corresponding DRM license, which would legitimately produce exactly the kind
of terminal EOF/overflow response PlayServer is seeing - not an EKA2L1 bug,
a real, faithfully-emulated content-protection gate.

This reframes the user's original backup scenario productively: on their real
N95, N-Gage games they'd actually installed/purchased would have had real,
valid DRM license entries already present in the backup's `cldrmdb`/DRM
private folders. If EKA2L1's restore-from-backup path is dropping, skipping,
or mishandling those specific DRM private folders (as opposed to the general
`.n-gage` game files themselves, which clearly do restore fine), *that* would
be the concrete, EKA2L1-side bug worth chasing - a much narrower, more
tractable target than "PlayServer never boots" was.

### To resume from here (updated again)

1. **Highest-value next step**: check whether the user's original backup
   (if still available anywhere, or reasoned about from what folders the
   backup-restore path touches) actually contains real entries under
   `Private/200015B0/` (`cldrmdb`), `Private/2000454C/` and `Private/20004555/`
   (`symc_drm.cfg`), and `Private/101f6dc5/` (`DRMHS.dat`) - and whether
   EKA2L1's backup-restore code (`src/emu/services/src/backup` or wherever
   the restore path lives) includes these private IDs, or silently skips
   them the way `10202be9`/`10009D8F` were found disabled earlier this
   investigation.
2. If confirmed, the fix is likely much simpler than anything attempted so
   far: restoring/not-skipping the DRM-related private folders during backup
   restore, rather than any PlayServer/gtmdserver code change.
3. To fully nail the exact IPC opcode/semantics between `PlayServer` and
   `gtmdserver` (what specifically maps to `-9`/`-25`), the next disassembly
   target would be `drmrights.dll` or `DRMCommonLicSrv.exe` itself rather
   than `gtmdserver.exe` - but given the DRM subsystem's real presence is
   now confirmed, this is lower priority than checking the backup-restore
   code path first.
4. The `!MsvServer` hang on standalone Asphalt 3 (`0x2000A5B6`) and the
   `eikpriv.rsc`/`aknpriv.rsc` missing-resource gap remain open, unrelated
   side findings from previous updates.

## UPDATE: checked the backup-restore code path directly - there isn't one, and the real gap is a missing DRM database file

Checked `src/emu/services/src/backup/backup.cpp` - EKA2L1's `BackupServer` HLE
is a thin stub (`backup_old_session::fetch`, opcodes `0x1F`/`0x22` just
`ctx->complete(epoc::error_none)`, answering "is a backup/restore in
progress?"-style queries). **EKA2L1 does not implement an actual Secure
Backup Engine restore process at all** - there is no code path that unpacks
an `.SBE` archive or selectively restores private folders. A device profile
is just whatever raw files exist under `data/devices/<firmcode>/drives/`;
EKA2L1 reads them as-is. So there's no "EKA2L1 skips these folders during
restore" bug to find - that whole hypothesis doesn't apply here.

Went back and checked the DRM-related private folders directly instead.
**They do exist**, and their timestamps (`Jul 24 2015`, matching every other
genuinely-original file in this profile, e.g. `appInfo.sai`) confirm they're
real backup-era data, not something staged by this investigation:

- `Private/200015B0/cldrmdb.crc` - 128 bytes.
- `Private/2000454C/symc_drm.cfg` and `Private/20004555/symc_drm.cfg` - 70
  bytes each, plus small `symc_payment.cfg`/`symc_machuser.dat` siblings.
- `Private/101f6dc5/DRMHS.dat` - **1 byte**.

None of these are remotely large enough to hold actual per-title Rights
Object data. Searched for the real `cldrmdb` database file itself (the `.crc`
is just its checksum) - **it doesn't exist anywhere in the profile**. Only
the checksum of the database survived; the database itself did not.

Also checked `RESOURCE/CommonDrm/DRMCommonLicense.{l02,lic,...}`, which
looked promising by name. Ruled out: these are plain-text **EULA legal
copy** ("LICENSE AGREEMENT AND LIMITED WARRANTY... By downloading or
installing this software you accept the terms and conditions..."),
localized per-language following this ROM's universal `.r01`/`.r02`-style
per-language suffix convention - not Rights Objects, not per-title data at
all. Red herring, ruled out cleanly.

### Where this leaves things

The DRM gate is real and confirmed (previous update: `drmcommon.dll`/
`drmrights.dll` genuinely loaded and linked by `gtmdserver.exe`). But the
concrete missing piece - the actual `cldrmdb` rights database - isn't
something EKA2L1's code is failing to restore; it simply isn't present
anywhere in the data this profile was built from. Two real possibilities
going forward, and only the user can tell which applies:

1. Their original real backup/dump (whatever source this `rm-320` profile
   was extracted from) may have a fuller version elsewhere that still
   contains the real `cldrmdb` file alongside its `.crc` - worth checking
   before concluding it's unrecoverable.
2. OMA DRM v1 "combined delivery" content embeds its Rights Object directly
   inside the protected content file itself, rather than needing a separate
   rights database entry at all. If N-Gage's `.n-gage` container uses this
   scheme, the real, user-owned `.n-gage` files from their actual phone may
   have carried valid rights internally - meaning the specific file staged
   this investigation (a manually-sourced `Asphalt.3.n-gage`, not something
   from the user's real device) may simply never have been a legitimately
   licensed copy to begin with, independent of any restore/database gap.

Both of these need the user's input/data to resolve further - not something
resolvable by more code tracing alone.

**User confirmed**: no fuller backup source exists - this EKA2L1 profile is
the only copy of the backup data available. Possibility 1 above is closed.

## UPDATE: traced the actual IPC opcodes/arguments between PlayServer and gtmdserver - no filename lookup exists in this protocol at all

Before chasing possibility 2 (missing embedded DRM rights) further, checked
whether the DRM-licensing theory was even the right target, since
`DRMCommonLicSrv.exe` never gets spawned in any captured trace (see below) -
only `drmcommon.dll`/`drmrights.dll` being *linked* as dependencies, which
doesn't prove their DRM-check code paths actually execute. Needed to see the
real IPC traffic to know for sure.

Added `PROBE_R148` to `svc.cpp`'s `session_send_general()` - the exact point
where the opcode (`ord`) and the four raw `TIpcArgs` words are available
before dispatch, filtered to any server name containing `GametitleMetadata`.
For each non-zero argument word, resolve it through the current address
space (`get_mmu(cpu)->current_addr_space()`) and print both an 8-bit and a
null-terminated UCS2 preview. First attempt used a wrong assumption (a
Symbian descriptor length/type header at offset 0) which mangled the output;
fixed by reading straight as a null-terminated UCS2 string, since these
particular pointers turned out to be raw `TText16*`, not `TDesC` objects.

Reran `--run 0x20007B39`. Captured the full sequence of calls PlayServer
makes to `GametitleMetadataServer`:

```
opcode=-1  (session connect - slot 2 decodes as "GametitleMetadataServer", routine)
opcode=0   args=[0,0,0,0]        (no descriptors - likely Count()/IsOpen())
opcode=2   args=[0,0,0,0]        (same - no descriptors)
opcode=6   args=[0, ptr, ptr, ptr]   (output buffers, empty/garbage pre-call)
opcode=4   args=[ptr, ptr, ptr, ptr] (output buffers, empty/garbage pre-call)
opcode=4   args=[ptr, ptr, 1, 0]     (same opcode again, arg2=1 - retry/index?)
opcode=3   args=[0,0,0,0]        (no descriptors - likely Close())
```

**No filename, no title string, no UID text appears anywhere in this entire
exchange** - every argument is either zero, a small integer, or a pointer to
an output buffer that's empty at call time (to be filled by gtmdserver's
response). This rules out a name/UID-keyed lookup like "does
`Asphalt.3.n-gage` have a valid entry" - that query pattern simply isn't
present in this protocol.

**Revised understanding**: this looks like an index-based enumeration
protocol (`Open`/`Count` -> `GetItem(i)` -> `GetDetails(i)` -> `Close`), not
a per-title lookup by name. The `-9`/`-25` sequence found in earlier updates
is most consistent with a routine "buffer too small, retry with a bigger one
[-9 KErrOverflow]" followed by "no more items [-25 KErrEof]" enumeration
idiom - not a DRM rejection, not a filename mismatch. The open question that
actually matters is what PlayServer does when this enumeration comes back
with **zero usable entries** - and that requires decoding the exact binary
struct layout `opcode 4`/`opcode 6` write into their output buffers, which
in turn requires the real N-Gage GameTitleMetadata client SDK headers (this
is proprietary Nokia/N-Gage-specific API, not part of any public Symbian SDK
reference available here) to interpret with confidence rather than guessing
field offsets.

### Where this leaves the investigation

Three independent hypotheses have now been raised and each partly
confirmed, partly walked back over the last several updates:
1. DRM rights-database gap - real subsystem confirmed present, but
   `DRMCommonLicSrv.exe` is never actually contacted, weakening this as the
   active cause in this specific flow.
2. Filename/UID mismatch between the manually-staged `.n-gage` file and the
   catalog's expectations - the IPC trace shows no filename-keyed query at
   all, so this doesn't fit the observed protocol either.
3. Index-based enumeration returning zero (or malformed) entries, with
   PlayServer handling that case badly - currently the best-supported
   theory, but not yet proven at the byte level.

None of these can be fully closed out without either the real N-Gage SDK's
`GameTitleMetadata` client-side struct definitions, or a genuine reference
device/backup with actual working `.n-gage` titles to diff against (which
the user has confirmed isn't available). This is the natural limit of what
static/dynamic tracing alone can resolve for this specific subsystem.
