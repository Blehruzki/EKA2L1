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
| `EKA2L1_STMNEWBASE=1` | off (the original base is stored, as an ARM9 does) | An STM with writeback whose base register is in its list, and is not the list's lowest, stores the **written-back** base. ARM leaves the stored value UNPREDICTABLE; this is the N95's ARM11 as measured (ngage-port round 178: `push {r4-r12, sp, lr}` ... `pop {r4-r12, sp, lr}` returned SP one frame low). Each such STM is logged once by PC. Off, code written for an ARM9 that stores SP in a push runs here and dies on an ARM11 phone. | `cpu/src/dyncom/arm_dyncom_interpreter.cpp` (`STM_INST`) | ngage-port |
| `EKA2L1_KERNREQ=1` | off (the emulator clears `ERequestPending`) | A request's completion writes the status word only, as EKA2's `DThread::RequestComplete` does (`sizeof(TInt)`); the `iFlags` word is left for the active scheduler. Off, the emulator also clears the pending bit on completion, which a device never does, so an EKA1 one-word `TRequestStatus` followed by a field of the program's own works here and not on a phone. | `utils/include/utils/reqsts.h` (`request_status::set`), `utils/src/reqsts.cpp` | emulator |
| `EKA2L1_BTSPLIT=<n>` | off (a receive takes everything available) | A Bluetooth-over-IP stream receive that takes what is available (`RecvOneOrMore`) is handed **at most `<n>` bytes**, the rest kept for the next receive. RFCOMM is a byte stream that a phone's radio delivers in pieces; loopback TCP almost always hands a whole write over at once, so guest code that assumes one message per receive passes here and fails between phones. | `services/src/internet/protocols/socket.cpp` (`bt_split_cap`) | ngage-port |
| `EKA2L1_BTSTRICT=1` | off (only the inquiry's name bit is read) | A Bluetooth host-resolver inquiry is refused by os/bt's `CBTHostResolver::GetByAddress` rules (`btstack/linkmgr/hostresolver.cpp`): a cache-only request for an all-zero address `KErrNotFound`, neither inquiry nor name `KErrArgument`, an IAC other than `KGIAC`/`KLIAC` `KErrNotSupported`. That source is later than any S60 3.x phone, so these are its rules, not measured ones. Each request's raw address is logged. `duo.sh` turns it on. | `services/src/bluetooth/protocols/btlink/resolver_inet.cpp` (`get_by_address`) | ngage-port |
| `EKA2L1_BTSERVICE=<hex>` | 0x200 (telephony) | The major service classes every found device advertises. `0x2D0` is what an S60 3.x camera phone sets (networking, capturing, object transfer, telephony); an N-Gage title that filters hosts on that field's low byte drops it, as on phones (round 173). `duo.sh` defaults to it. | `services/src/bluetooth/protocols/btlink/resolver_inet.cpp` (`search_next_friend_and_report_to_client`) | ngage-port |
| `EKA2L1_BTOFF=1` | off (the radio is on) | The Bluetooth radio is switched off: host-resolver searches and name lookups, and outgoing Bluetooth connects, complete `KErrHardwareNotAvailable`, as os/bt's host resolver answers with its hardware off (`btstack/linkmgr/hostresolver.cpp`). | `services/src/bluetooth/protocols/btlink/resolver_inet.cpp` (`bt_radio_off`), `base_inet.cpp` (`connect`) | ngage-port |
| `EKA2L1_BTOFF_ANSWER=1` | off (the player says no) | How the player answers S60's Bluetooth power notifier (`KPowerModeSettingNotifierUid` 0x100059E2, now a built-in notifier) while the radio is off: 1 switches it on and answers ETrue; anything else answers EFalse. With the radio on it answers ETrue at once. | `services/src/notifier/bluetooth.cpp` (`bluetooth_power_mode_plugin`) | ngage-port |

## Diagnostics (observe only)

| Variable | What it logs | Where | Added |
|---|---|---|---|
| `EKA2L1_LEAVESTACK=1` | With every `User::Leave` (already logged with its code, and now its LR), the stack words above it that look like code, so the leaving caller can be named against a ROM's export table. | `kernel/src/svc.cpp` (`leave_start`) | ngage-port r158 |
| `EKA2L1_PCTRACE=lo:hi:path` | Every basic-block entry whose PC is in `[lo,hi)` as `(pc, sp)` word pairs; armed from the first entry at `EKA2L1_PCTRACE_START`, capped at `EKA2L1_PCTRACE_MAX`. With `EKA2L1_PCTRACE_REGS=pc,pc,...` also dumps `r0`–`r15` at those PCs to `<path>.regs`, wherever the PC is and after the cap, so a range that traces nothing (`0:4`) watches a whole run. `EKA2L1_PCTRACE_FLUSH=n` writes the buffer every `n` entries (1 to 4096, default 4096), so a run ended by SIGKILL keeps its tail. For finding where two runs of the same guest code part. | `cpu/src/dyncom/arm_dyncom_interpreter.cpp` | ngage-port r143 |
| `EKA2L1_BTRECVLOG=path` | Every step of a Bluetooth-over-IP stream receive, with host milliseconds: the guest's request (and the bytes already buffered), an immediate completion, the network thread's read start, a delivery, a completion, a cancel. For telling a receive the emulator never completes from one the guest never asks for. | `services/src/internet/protocols/socket.cpp` (`bt_recv_log`) | ngage-port |
| `EKA2L1_PCTRACE_REGRING=n` + `EKA2L1_PCTRACE_DUMPAT=pc` | With `EKA2L1_PCTRACE_REGS`: the register entries go round a ring of the last `n` in memory, written to `<path>.regs` (oldest first) when a block at `DUMPAT` is entered -- the moments before an event whose time is not known in advance. | `cpu/src/dyncom/arm_dyncom_interpreter.cpp` (`pc_trace`) | ngage-port |
| `EKA2L1_MEMSNAP=addr:len:ms:path` (+ `_PC=lo:hi`, `_MAX=n`) | Every `ms` of host time, guest `[addr,addr+len)` to `path.NNNNN`, taken only while the PC is in `[lo,hi)` (the right process's address space); at most `n` (1000). For finding a game's state by comparing snapshots with its screen. | `cpu/src/dyncom/arm_dyncom_interpreter.cpp` | emulator |
| `EKA2L1_BTDUMP=path` | Every Bluetooth-over-IP socket payload (`btinet_socket` send and completed receive), one line each: milliseconds, socket, `S`/`R`, length, hex. For reading what two instances say to each other. | `services/src/bluetooth/protocols/base_inet.cpp` | ngage-port r172 |
| `EKA2L1_WATCH=addr[:end]` | Guest writes into `[addr,end)` with value, PC and LR (capped ~400). | `cpu/src/dyncom/armstate.cpp` | emulator |
| `EKA2L1_WATCHVAL=v` | Narrows `EKA2L1_WATCH` to writes of value `v`. | same | emulator |
| `EKA2L1_WATCHPC=lo:hi` | Narrows `EKA2L1_WATCH` to writes from PC in `[lo,hi)`. | same | emulator |
| `EKA2L1_RWATCH=addr` or `lo:hi` | Reads of the word at `addr` -- by LDR, LDRB/LDRH of any of its bytes, or an LDM that covers it -- first few, then only on change, with PC/LR. A range `lo:hi` reports each reading PC once (the first 64). `EKA2L1_RWATCH_PC=lo:hi` keeps only reads made by code in that range, one line each time it enters another 4 KB page: the extent and order of a checksum's window (ngage-port E1382, E1393). | same | emulator |
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

**LockedInc and LockedDec (fast execs 0x0A and 0x0B on 9.1).** S60 3.0's
euser, built for ARMv5, has RCriticalSection::Wait and ::Signal ask the
kernel for these (S60 3.1 and later use ldrex/strex). EKA2L1 had them in
its EKA1 tables only, so on a 3.0 firmware the first contended Wait blocked
for good (ngage-port: Glimmerati's black screen, BUGBOOK 14.i). Now on the
9.x table (`svc.cpp`, `locked_inc_32` / `locked_dec_32`). The
"Unimplement system call" message now carries the caller's pc, lr, r0 and
r1 (`lib_manager::call_svc`). Found while doing this and fixed with it:
slow exec 0x53 is User::InactivityTime on 9.1 and 9.3 alike (euser export 601
calls its stub on the RM-92 and the RM-409; 634, ResetInactivityTime --
measured -- calls the 0x54 beside it), unregistered until now, so the guest
read r0's leftover for the seconds idle; and `safe_inc_32` / `safe_dec_32`
(fast execs 0x15/0x16) moved the pointer, not the value, so a SafeInc never
counted. With both, a Glimmerati race on either firmware makes no
unimplemented kernel call at all.

## A drawing command on an inactive window gc

Not a switch: always on. A graphics-context command other than Activate,
Deactivate or Free on a gc with no window attached now panics the client
**WSERV 9** (`EWservPanicGcNotActive`), as the S60 3.x window server does
(`CWsGc::CommandL` -> `DoDrawing0L`, `nonnga/SERVER/gc.cpp`). Every handler in
`services/src/window/classes/gctx.cpp` dereferences the window, so such a
command used to crash the emulator itself -- an EKA1 title calling the gc
through a GCC 2.x vtable slot reached `Clear()` before any `Activate`
(ngage-port E920). The check is in `graphic_context::execute_command` and
logs the opcode and the thread before the panic.

## A peer with no SDP responder up answers as an empty database

Not a switch: always on (`services/src/bluetooth/protocols/sdp/sdp_inet.cpp`).
A Bluetooth-over-IP friend whose midman answers but on whose SDP port nothing
listens -- here the ROM's `SdpServer`, which starts only with a guest's first
`RSdp::Connect`, so any instance not yet registering a service -- used to fail
the guest's SDP connect with `KErrCouldNotConnect`. A phone's Bluetooth stack
always answers SDP, with no records when it has none, so a TCP refusal
(`UV_ECONNREFUSED`) on that port now completes the connect, a service search
returns zero records, and an attribute request gets an SDP error. A joiner
that picked a friend not hosting yet went down an error path a phone never
takes and died there (ngage-port E1136-E1137).

## The inquiry address at the phones' offsets

`inquiry_info` (`services/include/services/bluetooth/protocols/common.h`) had
the IAC at user data +14 and the action at +18. S60 3.x phones' own
`bluetooth.dll` (RM-92, RM-409) read the major service class at +10, the major
device class at +12, the IAC at +16 and the action at +20 (the TSockAddr's +0x1a,
+0x1c, +0x20, +0x24), 6.1's layout; the struct is that now, held by a
`static_assert`. (For one commit it followed the later os/bt source's layout,
which no S60 3.x phone has.) ngage-port, round 173.

## A Bluetooth stream Write completes once it is queued

Not a switch: always on for Bluetooth-over-IP sockets (`btinet_socket::send`
sets `inet_socket::set_send_buffered`, `services/src/internet/protocols/
socket.cpp`). A phone's RFCOMM takes a Write into the mux's queue and ESock
completes the request once all of it is taken (`TRfcommStateOpen::Send`,
os/bt `rfcommstates.cpp`), so a short Write is complete before the writer
runs again. The emulator completed it only when its libuv thread had written
it to the host socket, so a guest that wrote twice in a row raced that thread:
Colin McRae's engine treats a send while the last Write still reads pending as
a lost link, and the host's Start (two messages back to back) went through or
dropped the session by chance (ngage-port E1161-E1163). The data is now copied,
the request completed at once, and the write queued to the libuv thread with
its own buffer; a later failure surfaces as the link's next receive error, as
on a device. (That ESock completes on queueing is read from the RFCOMM SAP's
contract; ESock's own source was not available.)

## A listen that succeeded read as failed

Not a switch: a bug fix (`inet_socket::set_listen_event`, `services/src/
internet/protocols/socket.cpp`). The libuv thread signalled the waiting
`listen()` before it stored the result, and the result field was never
initialised, so the guest thread could wake first and read garbage: an
intermittent `KErrGeneral` from `RSocket::Listen` on a socket that was
listening. Colin McRae's host left with -2 at "Host a Game" and sat behind
its wrapper's window for good about one run in five (ngage-port E1148-E1186).
The result is now stored first and starts at 0.

## A socket task that runs after the socket closed

Not a switch: a bug fix (`services/src/internet/protocols/socket.cpp`). A
receive cancel, a read delivery, a receive start or a send posted to the
libuv thread can run after the guest closed the socket, when `close_down()`
has already let the handle go; `uv_read_stop(NULL)` then crashed the whole
emulator (ngage-port E1194, mid-race). Each async step now checks the handle
and still completes the guest's request.

## Bluetooth over IP on a host without IPv6

Not a switch: always on. The Bluetooth emulation (`services/src/bluetooth/
protocols`) carried RFCOMM, L2CAP and SDP over IPv6 sockets only, so on a host
with no IPv6 -- a container, where every AF_INET6 socket fails EAFNOSUPPORT --
the first `RSocket::Bind` on a Bluetooth socket crashed the emulator
(`btinet_socket::bind` on an inet socket that was never created; ngage-port
E1100). `epoc::internet::host_supports_ipv6()` probes once; without IPv6 the
RFCOMM and L2CAP factories, the bind address, the discovery, SDP and asker
sockets use IPv4, and friend addresses stay IPv4 instead of being mapped -- both the ones learnt from a stranger's call and, since ngage-port E1121, the ones in the config's `internet-bluetooth-friends`, which `update_friend_list` used to prefix with `::ffff:` (so a direct-IP friend could never be asked for its device address and inquiry found nobody). And
`inet_socket::open` now reports libuv's own error on a failed init (it printed
`errno`) and frees the handle it never initialised, which used to reach
`uv_close` later.
