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
| `EKA2L1_LEAVESTACK=1` | With every `User::Leave` (already logged with its code, and now its LR), the stack words above it that look like code, so the leaving caller can be named against a ROM's export table. | `kernel/src/svc.cpp` (`leave_start`) | ngage-port r158 |
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
  `.claude/ngage-port/BUGBOOK.md` §12.w: it made the bench deterministic (50 instr/us
  reliably live, 200 reliably dead), which is how the game's tick-seeded PRNG was
  found; the phone fix itself is in the port (`GAME_TICK_SEED_*`, KNOBS.md).

## Patch maps

`src/patch/mediaclientaudiostream/group/mediaclientaudiostream.dll.map` gained
an `[epoc91]` section (ngage-port r158): without it an S60 3.0 firmware ran the
ROM's own audio-stream DLL instead of the emulator's stand-in, and an app's
`CMdaAudioOutputStream::NewL` resolved to an unrelocated `0x817f`. The N80
(RM-92) DLL exports the same 17 entries as 3.2's, and audio ran with the 3.1
order applied. A rebuild copies the maps into `build/bin/patch`, so edit the
source copy.

## S60 3.0 direct screen access

Not a switch: always on, and only reached by an S60 3.0 (epoc91) firmware.
The emulator swaps in its own `scdv.dll` on 3.1 and up, but its patch map has
no `[epoc91]` section, so a 3.0 firmware runs the ROM's screen driver -- which
talks to the phone's LCD driver, `GenericLcd_Lcd.ldd`, through an
RBusLogicalChannel (ngage-port r159):

- **Kernel calls.** `0x82` ChannelCreate and `0x0A` ChannelRequest on the
  9.1/9.3 table (`svc_register_funcs_v93`), with `0x80`/`0x81`
  DeviceLoad/DeviceFree. The 9.x slow numbering is two lower than the ^3 list
  from 0x7D up. ChannelRequest follows us_exec.cpp: a positive function is
  DoControl(fn, a1, a2), a negative one DoRequest(~fn, status, args[0],
  args[1]), and KMaxTInt a cancel (stubbed). `kernel/src/svc.cpp`.
- **The LCD channel.** `ldd/src/lcd/lcd.cpp` (factory "Lcd", registered as
  `lcd` and `genericlcd`) answers what scdv asks, read off the N80 ROM's
  own LDD (its control dispatcher at 0xf80b7780): 0x1001 the frame buffer (the
  window server's screen chunk), 0x2001 the update session (a shared
  0x2000-byte chunk holding 0x1A0 dirty rectangles and their count at 0x1A00,
  the mode asked for, a mask of one bit per TDisplayMode, a flag and four
  orientation words; a mode outside the mask is KErrNotSupported), 0x2003
  flush, 0x2005 the mode, 0x2006 whether a mode is supported, 0x2008
  orientation 0, 0x200B scale 1. A flush (0x2003) and every request put the
  frame buffer on screen through `dispatch::update_screen`, the call the
  emulator's own scdv makes, then complete at once. Anything else is logged
  as `Unhandled Lcd control` (class `Ldd.Lcd`).
