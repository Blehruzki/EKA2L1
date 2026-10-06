# EKA2L1 (fork)

## Scope

**This repository is the EKA2L1 emulator. That is the default and only subject of
work here.**

`.claude/ngage-port/` holds a separate project: N-Gage titles (Asphalt Urban
GT, Asphalt 2, Ashen) ported to S60v3 phones through a loader and API shim,
with its own record and rules in `.claude/ngage-port/CLAUDE.md`. It lives in
this fork so it survives, and it is **not part of the emulator**. Ignore it
entirely — do not read it, cite it, or let it steer emulator work — unless the
current task is explicitly about that port. If the task is about EKA2L1, that
directory does not exist. (The emulator carries a few bench instruments made
for it, such as `EKA2L1_SCREEN`; those are emulator code and documented here.)

This is a personal fork. Do not open pull requests, issues, or any other
contribution against the upstream repository, and do not push anywhere but this
fork's branches. Changes stay local unless asked otherwise.

## Build

Configured with CMake, `RelWithDebInfo`, into `build/`; the Qt frontend lands at
`build/bin/eka2l1_qt`. System dependencies that are not vendored and must be
present: SDL2, and Qt6 `LinguistTools`, `Svg`, `Network` and `OpenGLWidgets`.

## Gotchas

- **Translation churn.** Qt's `lupdate` regenerates all 22 `.ts` files on every
  build, so `git status` is dirty after any compile. Revert them; never commit
  them as part of an unrelated change.
- **Running the emulator headless.** It needs a display; `Xvfb :99` works. It
  chdirs to `~/.local/share/EKA2L1/`, so a relative `storage` setting resolves
  there rather than the working directory.
- **Killing it.** Match the process exactly (`pkill -x eka2l1_qt`). A pattern
  match on the command line (`pkill -f`) also matches the shell that launched it.

## Known gaps in the emulator

EKA2L1 gives every thread's user stack a chunk of its own, megabytes from
every other thread's. A real EKA2 device packs a process's thread stacks
together, kilobytes apart. Code that identifies a thread by how far its stack
is from another one's therefore works here and fails on hardware. Related:
`thread_create` in `src/emu/kernel/src/svc.cpp` accepted any stack size at all
until it was taught the EKA2 ceiling.

EKA2L1 declines a bad handle quietly where a device kills the process. The
handle table does reject it -- `object_ix::get_object` refuses a freed slot,
and on EKA2 a handle whose instance bits do not match the record -- but the
executive call then returns `KErrBadHandle`, or, for the ones that return
void such as `RThread::SetPriority` and `RThread::RequestSignal`, returns
nothing at all. A real EKA2 kernel panics the calling thread **KERN-EXEC 0**:
an unresolvable handle is a programming error, not an error code. So guest
code that uses a closed, stale or wrong-thread handle runs here and dies on
hardware. `EKA2L1_STRICTHANDLE=1` logs every rejected handle, with the guest PC and LR, and `=2` panics
the thread as a device would (`kernel_system::get_kernel_obj_raw`).

Handle ownership is part of this: `EOwnerThread` handles are valid only in
the thread that created them, so a worker touching the main thread's RFile
is KERN-EXEC 0 on a device and silent here.

`src/emu/bridge/include/bridge/epoc9.def` is not an ordinal table. Its euser
list has **2,184** entries against the RM-409 ROM's **2,229** exports, so an
index into it is not a proof of an ordinal -- the two agree at the low numbers
and there is nothing in the file that says where they stop agreeing. Resolve a
hard-coded ordinal against the ROM (`romimg.py` reads its export directory), or
better, confirm it by measurement: call the function at two different rates and
watch the count move.

`EKA2L1_INACTIVITY=1` counts guest calls to `User::ResetInactivityTime` at the
bridged SVC (`clear_inactivity_time` in `src/emu/kernel/src/svc.cpp`). Counting
in `kernel_system::reset_inactivity_time` instead mixes in the window server's
own host-side resets, which are an order of magnitude more frequent. The other
probes are `EKA2L1_WATCH`, `EKA2L1_WATCHVAL`, `EKA2L1_WATCHPC` and
`EKA2L1_RWATCH` in `src/emu/cpu/src/dyncom/armstate.cpp`.

A dead thread's own heap is freed, as EKA2's `DThread::CloseCreatedHeap`
does (`thread::close_created_heap` in `src/emu/kernel/src/thread.cpp`): the
first allocator the thread switched to loses a reference, and at the last its
handles are closed, unmapping the chunk. Before this, memory a dead thread had
allocated stayed readable here and was a page fault on hardware. The process's
own heap is left alone explicitly: a worker that shares it reads an access
count one lower than EKA2 would (after euser's `RAllocator::Open`), a separate
discrepancy not yet run down.

EKA2L1 accepts SIS packages that a real device rejects, because it verifies none
of the integrity fields a device checks: the per-file SHA-1 in each
`SISFileDescription`, the E32 image header CRC32, `SISControllerChecksum` /
`SISDataChecksum`, and the package signature. Anything validated only against
this emulator may still fail on hardware. Making the installer check these would
be a genuine improvement, and is unimplemented.
