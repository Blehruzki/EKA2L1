# Emulator patches and instruments

Every switch below is an **environment variable, read once at start, and off
when unset** — so a normal build behaves exactly as upstream and nothing here
ships "on". To activate one, set the variable before launching `eka2l1_qt`; to
deactivate, unset it. This is the registry to consult before reaching for a new
probe: check whether one already answers the question.

Two kinds are listed. **Diagnostics** only observe (logging, tracing) and never
change what the guest sees. **Behaviour switches** change emulator behaviour when
on; they are experiments, kept off by default, and want deliberate use.

The N-Gage port work (`.claude/ngage-port/`) leans on several of these; the
column "added" says which were built for that investigation versus shipped with
the emulator.

## Behaviour switches

| Variable | Default | What it does when set | Where | Added |
|---|---|---|---|---|
| `EKA2L1_CYCLETICK=<ips>` | off (wall clock) | The guest tick SVCs (`User::TickCount`, `NTickCount`, the fast counter) read a monotonic count of **emulated guest instructions** divided by `<ips>` (instructions per microsecond) instead of host wall time, so guest-visible time advances with emulated work, not host load. The host event scheduler keeps its own wall clock. **dyncom only** — the counter is fed from the dyncom core; under dynarmic it stays zero and time would freeze, so do not set it there. | counter + rate in `common/{include/common/time.h,src/time.cpp}`, fed in `cpu/src/dyncom/arm_dyncom.cpp`, read in `kernel/src/svc.cpp` (`tick_count`/`ntick_count`/`fast_counter`) | ngage-port r143 |
| `EKA2L1_DETTICK=<us>` | off (wall clock) | Swaps the host wall clock behind the tick for a virtual clock that advances a fixed `<us>` (default 50) **per read**. Cruder than `CYCLETICK`: it distorts pacing and stalls boot at small steps. Kept as the clean before/after against `CYCLETICK`. | `common/src/time.cpp` (`deterministic_teletimer`) | ngage-port r143 |
| `EKA2L1_STRICTHANDLE=1\|2` | off (lenient) | `1` logs every rejected handle with guest PC/LR; `2` panics the thread **KERN-EXEC 0** as a device would, instead of returning an error code or nothing. | `kernel/src/kernel.cpp` (`get_kernel_obj_raw`) | emulator |

## Diagnostics (observe only)

| Variable | What it logs | Where | Added |
|---|---|---|---|
| `EKA2L1_PCTRACE=lo:hi:path` | Every basic-block entry whose PC is in `[lo,hi)` as `(pc, sp)` word pairs; armed from the first entry at `EKA2L1_PCTRACE_START`, capped at `EKA2L1_PCTRACE_MAX`. With `EKA2L1_PCTRACE_REGS=pc,pc,...` also dumps `r0`–`r15` at those PCs to `<path>.regs`. For finding where two runs of the same guest code part. | `cpu/src/dyncom/arm_dyncom_interpreter.cpp` | ngage-port r143 |
| `EKA2L1_WATCH=addr[:end]` | Guest writes into `[addr,end)` with value, PC and LR (capped ~400). | `cpu/src/dyncom/armstate.cpp` | emulator |
| `EKA2L1_WATCHVAL=v` | Narrows `EKA2L1_WATCH` to writes of value `v`. | same | emulator |
| `EKA2L1_WATCHPC=lo:hi` | Narrows `EKA2L1_WATCH` to writes from PC in `[lo,hi)`. | same | emulator |
| `EKA2L1_RWATCH=addr` | Reads of `addr` (first few, then only on change) with PC/LR. | same | emulator |
| `EKA2L1_INACTIVITY=1` | Counts guest `User::ResetInactivityTime` calls at the bridged SVC. | `kernel/src/svc.cpp` (`clear_inactivity_time`) | emulator |
| `EKA2L1_SCREEN` | Window-server screen instrument. | `services/src/window/window.cpp` | emulator |
| `EKA2L1_AUDIO_CAPTURE_DIR=dir` | Dumps played audio to `dir`. | `drivers/src/audio/audio.cpp` | emulator |

## Notes

- All of the above are also summarised in `CLAUDE.md` under "Known gaps"; this
  file is the activatable index. When the two disagree, the source is truth.
- `EKA2L1_CYCLETICK` was built to settle the N-Gage port's round-143 fight
  glitch (the game's archive loader branches on `User::TickCount`, which the
  emulator otherwise serves from jittery host wall time). See
  `.claude/ngage-port/BUGBOOK.md` §12.w for the full account and the chosen
  `<ips>`.
