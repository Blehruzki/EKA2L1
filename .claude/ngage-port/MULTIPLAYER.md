# Colin McRae 2005: Bluetooth multiplayer on S60v3

Started round 171 (E1094-E1105). The map of the engine's multiplayer path,
what differs between the N-Gage's Bluetooth (Symbian 6.1) and S60v3's (9.x),
what is fixed, and what is next. Sources are named per claim, as in SYMBIAN.md.

## The gate: LocalServices

Every Bluetooth socket and SDP session needs **LocalServices**
(oss.fcl.sf.os.bt: the Bluetooth SAP checks it; sdpnetdb.cpp checks
`KLOCAL_SERVICES`). The user's N95 refuses to grant it to an uncertified
package, unsigned (build 010) or self-signed (011): its install policy is
modified (it accepts unsigned packages, which the reference policy forbids)
and grants no user capability (SYMBIAN.md, round 171, from the installer's
own source). So on that phone multiplayer cannot work until the phone's
policy changes. A stock phone refuses every unsigned package but grants
LocalServices to a self-signed one: `GAME_CAPABILITIES 0x4000` plus mksis's
self-signing (round 169) is the package for those phones.

Everything below is the other half: the engine's own path, which has to work
on any phone that grants the capability.

## Who does what (from the binary)

- **The engine** (6r66.lxe) does all the Bluetooth: link check, RFCOMM
  listen/accept or connect, SDP record, the security registration, the
  device notifiers. The front end (6r66_2.app) has no Bluetooth import at
  all; it is the menu/licence shell.
- **The launcher** (I3D participant 2, the N-Gage's own, which the port
  plays) answers the engine's mailbox at block +0xe4:
  - `0x449214` *ask*: sound paused, process priority lowered, writes 3,
    busy-waits for a value above 3; 5 means cancelled. The port answers 4
    after 2 s.
  - `0x44926c` *post 1*: writes 1, busy-waits up to 8,000 ms
    (TTime::HomeTime >> 10) for a clear. Its "yield" (0x4a6b4c) only clears
    a flag -- these waits are pure spins.
  - `0x4492e8` *post 6*: writes 6, waits for a clear with no timeout (QUIT).
  - Script opcodes also post 1 and wait for the clear **with no timeout**:
    the port now clears any 1 (no 6 before it) after 2 s
    (`GAME_LAUNCHER_CLEARS_1`, E1104). Before that, Host sat behind the
    wrapper's window for good (the N95, round 171; the bench, E1103).
- **Host** (Bluetooth-off branch 0x47de9c, then 0x4ae890): link check, ask,
  post 1, `RSocketServ::Connect`, `RSocket::Open("RFCOMM")`,
  `GetOpt(KRFCOMMGetAvailableServerChannel)`, `Bind`, `Listen(1)`,
  `Accept`, the security registration (0x4ae9bc), the SDP record (0x40dfc8).
- **Join** (0x4ae4bc): RFCOMM socket, `TBTSockAddr` built from the
  device-selection notifier's answer (btextnotifiers), `Connect`.

## 6.1 against 9.x, item by item

| Item | N-Gage (6.1) | S60v3 (9.x) | State |
|---|---|---|---|
| Link check `GetOpt(KSolBtLM 0x1011, n)` | n = 0 `KLMGetACLLinkCount` (caseinc/BT_SOCK.H) | 0 is `ELMOutboundACLSize`; the count is 3 (lmoptions.h) | **fixed**: code patch 0x49e1c (E1099) |
| Link check failing (no capability) | -- | Leave | **fixed**: nop at 0x49e58, reads "no links" (round 170) |
| `RFCOMM GetOpt 1` | `KRFCOMMGetAvailableServerChannel` | same | ok |
| HCI scan-enable ioctls 14/15 (0x402c50, 0x402d88) | supported | same numbers, deprecated for P&S (bt_subscribe.h) | **open**: check what 9.x answers |
| `RBTSecuritySettings`, `TBTServiceSecurity` | btmanclient | gone; security via `TBTSockAddr::SetSecurity` | stand-ins: registration "done", default security (round 166) |
| SDP record (0x40dfc8): the builder | sdpdatabase, GCC 2.x builder | 9.x `CSdpAttrValueDES` (EABI vtable) | **fixed**: `gate6_sdp_builder`, a GCC 2.x shadow per builder (E1108) |
| SDP record: `RSdpDatabase` | 0x10 bytes | 0x18 on RM-409, iBuffer at +0x14 (the engine's counter) | **fixed**: `gate6_sdpdb_this`, a 9.2-sized object on the side (E1110-E1111) |
| SDP server | -- | the ROM's own, checks LocalServices: -46 without it (E1106) | the capability gate again; the bench builds with 0x4000 to get past it |
| Discovery | the engine's own: RHostResolver inquiry, names, then CSdpAgent | same calls | **works** on the bench (E1121); `TBTDeviceResponseParams` is only a container |
| `MSdpAgentNotifier` (the engine's, called by 9.x) | GCC 2.x, 3 slots | EABI, 4 slots | **fixed**: `gate6_san_adapter` (E1123) |
| `MSdpAttributeValueVisitor` (the engine's) | GCC 2.x | EABI | **fixed**: `GAME_VTABLE_SHIFTS` 0x13d95c (E1123) |
| Socket statuses, one word each, side by side | EKA1 | 9.x sets ERequestPending in the next word | **fixed**: `keep_thunk` at the network object's sites (E1125) |
| `TBTSockAddr`, `TInquirySockAddr` | 6.1 layouts | 9.x adds security / fields | works for connect and inquiry on the bench (E1121-E1125) |
| Leave in the host path | caught | unwinds into the display's teardown; on 9.2 phones under the global bitmap heap lock (13.ag) | lock released on leave (012); E1105's leave was the SDP server's -46 (E1106); with the record fixed, Host reaches the game's own Bluetooth screens (E1111) |

## The bench

EKA2L1 emulates Bluetooth over IP (`btmidman_inet`: RFCOMM, L2CAP and SDP
over TCP; discovery by LAN, direct IP friends or a proxy server). Its
transport was IPv6 throughout and this container has no IPv6 (every
AF_INET6 socket fails EAFNOSUPPORT), which crashed the emulator on Host
(gdb: btinet_socket::bind on a socket never made, E1100). Patched:
`epoc::internet::host_supports_ipv6()`, probed once, and IPv4 at each site
when it is false; the inet socket's init failure reports libuv's code and
frees its half-made handle (E1101). The emulator enforces no capability.

Two instances: `toolchain/port/duo.sh` builds once, installs into the
usual tree and a copy of it under a second HOME, gives each direct-IP
discovery with its own midman port and port offset (35689/15000,
35690/16000) and the other as its friend, runs B on its own Xvfb display
(:98), drives A to Host and B to Join (`duo_host.sh`, `duo_join.sh`, a
frame every 4 s), puts A's config back and logs the run as an E row.
`DUO_ENV_A` / `DUO_ENV_B` give one instance its own variables (a trace
path each).

## Where Host stands (E1111-E1116)

With LocalServices (bench build), Host runs the whole N-Gage path on the
9.x platform: RFCOMM listen on channel 21, `Accept` pending, the security
stand-ins, the SDP record in the ROM's database. The engine then polls its
host object (0x4025ec error?, 0x402520 connected?) until its menu script
gives up -- "Bluetooth connection lost" about 25 s on, with no joiner -- and
the next MULTIPLAYER entry reads the network object's leftover state as
"busy" until a new Host or Join clears it (0x402264). The host object's RunL
(0x4ae7f4) takes a completed `Accept` to state 2 (connected) and
UpdateAttributeL(8, 0).

## Where Join stands (E1118-E1125)

`duo.sh` runs two instances (A hosts, B joins). B's search lists A
("Select a Host: EKA2L1"), B selects it, the RFCOMM link comes up, and the
game's own session runs: A at "SINGLE RALLY" (difficulty, country), B in the
lobby ("Waiting for game to start", car, transmission, Ready). B's Ready
then faults (E1127): `strcpy` of player 0's name (0x4ba274), NULL because
in mode 2 the name is the driver profile's and player 0 has none -- the
profile is loaded only by DRIVER SELECT's Load (0x4bcb68), which the
multiplayer path never passes, even with a profile on disk (E1132-E1134;
`duo_profile.sh` creates one in both trees).

## Where the session stands (E1135-E1159)

- The session's protocol, read off the link (`EKA2L1_BTDUMP`): after the
  SDP exchange only an 8-byte ping (`11 00 ...`) every 2.6 s, until the
  host presses OK on SINGLE RALLY and sends a 344-byte type-2 settings
  message; the joiner's handler (0x47d810) takes mode, car and transmission
  from it and at once sends its own 64-byte type-3 player info (0x47d90c),
  whose first field is player 0's name -- then the host's lobby offers Start
  (E1154).
- That name is the joiner's driver profile's (modes 1-2). It is NULL for a
  guest, and the joiner faults in `strcpy`. A driver does not survive into a
  join: DRIVER SELECT's own handler clears an invalid one (0x479114),
  quitting a stage clears all sixteen (0x468dc0 -> 0x4bdd28) unless a
  connected session is up, and the boot-time match against the save
  (0x4bcb68) has never matched here. Only once, with the host hung, did the
  joiner survive (E1148). The per-race player setup at 0x4d7868 ("USING
  CACHED DRIVER" / "CREATING AS GUEST" / "FORCING LOAD FROM MEMORY CARD") is
  where a joiner's driver should come from; not yet read.
- Selecting a device that is not hosting ends in the game's own "Connection
  request failed" (exception globals for worker threads, 13.aj; the
  emulator's empty SDP database).
- The host sometimes hangs behind the wrapper's window right after its
  launcher ask (E1148, E1153, E1155): intermittent, not yet dumped.
- Bench tools: `duo.sh` with `DUO_SAVE_A/B` (save snapshots), key scripts
  `duo_host*.sh`, `duo_join.sh`, `duo_loadjoin.sh`, `duo_loadrace.sh`,
  `duo_racejoin.sh`, `duo_profile.sh`, `duo_idle.sh`; the game's attract demo
  starts after about 65 s idle on the main menu (E1138).

## Next steps, in order

1. The joiner's driver: read the player setup at 0x4d7868 and how a guest is
   meant to get a name; whether a phone would fault the same way.
2. The intermittent host hang (E1148, E1153, E1155): the watchdog's dump.
3. HCI scan-enable ioctls on 9.x: what they answer; a stand-in if refused.
4. The phone: LocalServices is the gate. A self-signed package for phones
   that grant it, and on the N95 nothing until its install policy does.
