# Colin McRae 2005: Bluetooth multiplayer on S60v3

Started round 171 (E1094-E1105). The map of the engine's multiplayer path,
what differs between the N-Gage's Bluetooth (Symbian 6.1) and S60v3's (9.x),
what is fixed, and what is next. Sources are named per claim, as in SYMBIAN.md.

## The gate: LocalServices

Every Bluetooth socket and SDP session needs **LocalServices**
(oss.fcl.sf.os.bt: the Bluetooth SAP checks it; sdpnetdb.cpp checks
`KLOCAL_SERVICES`). Rounds 169-170 read the N95's refusal as the phone's
policy; it was the package: the exe's capabilities were never declared in
its SIS file description, which the installer requires to match the header
(E1280, SYMBIAN.md "Capabilities in a package"). Declared and self-signed
(build 013's multiplayer loader), the N95 installed it without a question
and hosted to "Waiting for players" (round 172).

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
| Link check `GetOpt(KSolBtLM 0x1011, n)` | n = 0 `KLMGetACLLinkCount` (caseinc/BT_SOCK.H) | 0 is `ELMOutboundACLSize`; the count is 3 (lmoptions.h) | **fixed for every title**: the loader's RSocket::GetOpt hook translates 6.1's 0 and 1 at KSolBtLM to 9.x's 3 and 4 (gate6_sock_getopt, E1283); was Colin's code patch 0x49e1c (E1099) |
| Link check failing (no capability) | -- | Leave | **fixed**: nop at 0x49e58, reads "no links" (round 170) |
| `RFCOMM GetOpt 1` | `KRFCOMMGetAvailableServerChannel` | same | ok |
| HCI scan-enable ioctls 14/15 (read 0x402b98, write 3 or 0 at 0x402c98), on an L2CAP socket | the host's way to be discoverable | deprecated; the L2CAP SAP answers KErrNotSupported (l2sap.cpp); the P&S replacement needs NetworkControl | **cannot be fixed by a self-signed app**: a host is visible only with "My phone's visibility: Shown to all" (round 173) |
| Inquiry address (joiner, 0x40e568) | IAC at +0x20, flags at +0x24 (SBTAddr rounded to 8) | IAC at +0x1c, flags at +0x20 (address inside SInquiryAddr): 6.1's reads as IAC 0, action 0x33 (cache-only) | **fixed for every title**: RHostResolver::GetByAddress/Next hooked (gate6_resolver_get_by_address, E1287); the bench refuses it as a device does under EKA2L1_BTSTRICT (E1286) |
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

## Where the race stands (E1160-E1181)

- **Start reaches the rally.** The host's Start sends type 3 (its player) and
  type 1 (the settings again, byte 0 = 1) back to back; the engine's send
  (0x402698) calls a second send while the first Write still reads pending a
  lost link. The emulator now completes a Bluetooth stream Write once queued,
  as RFCOMM does (EMU_PATCHES.md), and both instances go through the FINLAND
  briefing to the service area with the link up (E1161-E1163).
- **The race setup reads every player's driver profile.** At the RACE tab
  (0x4e9370) and in the race-init loop (0x509c5c, from the general race setup
  0x48f408) the engine reads player i's profile record (getters 0x4bad30,
  0x4baa44: 44 callers, none tests for NULL). Player 0 has none in a session,
  and the remote player never has one (its info lives in a separate network
  table, 0x4b024c). The loop is skipped only by 0x4c9034, a global test that
  needs game mode 4; mode 4 is set only from data (a championship file or the
  settings message), and the multiplayer path sends 1 or 2 (E1140).
- **No front-end route gives player 0 a driver that survives.** DRIVERS is a
  profile viewer that unbinds on leaving (E1172-E1179); RALLY's DRIVER SELECT
  binds with the valid bit 0 and its exit clears an invalid driver, Play As
  included (E1180-E1181). E1159's "per-race player setup" (0x4d7828) is the
  saved-championship loader.
- **Bench guesses written down:** a NULL name reads as "" at the joiner's info
  builder (GAME_NULL_NAME_SITES, E1160) and the RACE tab's NULL profile takes
  the zero-byte branch (GAME_NULL_SKIP_SITES, E1170); the next NULL read
  (0x509ca0) shows covering reads one by one is the wrong road.
- **Next:** the type 4 handler (0x44962c -> 0x4b0304, 0x4ae03c) and whatever
  should send it; whether it sets mode 4 or binds the session's players.

## Where it stands (E1182-E1195): a two-player race runs on the bench

- **The launcher's answer is "Bluetooth on".** The engine's mailbox ask
  (0x449214) takes 5 as true; every caller reads it as Bluetooth on. The port
  answered 4, so MULTIPLAYER showed "Bluetooth connection is busy" and went to
  Host/Join by a back way that skips its DRIVER SELECT (0x47d204 -> 0x47d0c0)
  and the real HOST GAME screen -- the reason every session before reached the
  race with no driver. GAME_LAUNCHER_BT_ON answers 5 (E1184-E1187).
- **The emulator's listen race** (set_listen_event signalled before storing an
  uninitialised result) was every "host hang" at Host a Game (E1186).
- **A receive completing inside RSocket::Write** left the receive status 2
  (ERequestPending ORed into the EKA1 word after the Write's status); the
  engine called it a failed link 20 s into a race. keep_thunk now puts an
  exact 2 back to 0 at the Write site (E1191-E1193). A phone has this window
  too.
- **A closed socket's queued libuv task** crashed the host emulator in
  uv_read_stop(NULL) mid-race (E1194); guarded.
- **Result (E1195):** guests from MULTIPLAYER's DRIVER SELECT, HOST GAME,
  join, Rally, OK, Ready, Start, the briefing, RACE -- both instances race
  FINLAND STAGE 1 for over two minutes with each other's car on screen.
  Protocol: type 3 player info (names), type 1 start, type 4 race-start
  handshake (each side once, waits for the peer's), then type 5 (40 B car
  state, sequenced) acked by type 6 (12 B) at about 15 a second each way.
- **Bench guesses still on for Colin** (GAME_NULL_NAME_SITES,
  GAME_NULL_SKIP_SITES) were needed only without a driver; with the real flow
  they should never fire -- a run with both off is the next check.
- **Next:** that check; a stage to the finish (steering in the key script);
  then the hardware question: the N95 refuses LocalServices (round 170), so the
  Bluetooth calls need a signed package or a phone that grants it.

## The protocol, from the dispatch table (read from the binary)

The receive pump (0x44976c, once a frame) reads an 8-byte header (type,
length) and the body, and finds the handler in an 18-entry table at 0x53ee54.
Every send goes through 0x449a54(type, data, length).

| type | handler | sent by | what it is |
|---|---|---|---|
| 1, 2 | 0x47d810 | 0x47d7f8, 0x47dadc | the start and the settings (2 is the 344-byte lobby settings) |
| 3 | 0x449610 | 0x47da00 | player info (the name) |
| 4 | 0x44962c | 0x486e08 | the race-start handshake |
| 5 | 0x449578 | the car update | car state, 40 B, sequenced; acked only in game state 3 |
| 6 | 0x4495d4 | 0x4495c0 | the ack of a type 5 (its sequence); taken only in state 3 |
| 7 | 0x44964c | 0x4ca488 (0x4b0394) | a split time: checkpoint index and time, filed per player (0x4afd94); index 8 is the finish |
| 8 | 0x449678 | 0x4870e0 (0x4b034c) | a time penalty (the added time and the running total) |
| 9, 10, 11, 12, 15 | 0x4496ac ... 0x47d150 | 0x4b03f4 ... 0x4b0454 | session ends and quits; 11 and 12 put up a message (0x46211c) |
| 13, 14 | 0x44951c, 0x44955c | 0x48eac8, 0x48eb88, 0x49b800 | not yet read |
| 17 | 0x44974c (nothing) | 0x449808 | the keep-alive, every 250 ms when idle |

## Where it stands (E1196-E1227)

- **The drop is named (E1199): the type-5 flow control (0x44999c).** A type 5
  is held while two are unacked; held over a second and 20 more, the link is
  lost. The peer acks only in game state 3, so the window fills only when one
  side stops acking. Not latency (25-41 ms round trips against a 55 ms frame).
  3 of 11 runs dropped (E1196, E1197, E1199), none of the next 6.
- **The pump could not take a message in pieces (E1226-E1227) -- fixed.** Its
  body read is ten back-to-back polls of the one RecvOneOrMore the engine
  keeps going; a body still short after them went to its handler part-filled
  and the rest was read as the next header (the stream out of step from
  there: no acks, a lost link), and one finished on the tenth read exactly was
  called a lost link. RFCOMM between two phones is a byte stream with no
  promise of one receive per Write; loopback TCP nearly always hands a write
  over whole, which is why the bench rarely showed it. Two code patches
  (game.h: 0x449890 a short body waits for the next frame, as the header
  does; 0x4498c4 a finished one always dispatches). The emulator's
  `EKA2L1_BTSPLIT=<n>` splits every Bluetooth receive to show it: unpatched,
  the first 344-byte message ended the session (E1226); patched, a whole
  session and race ran in 23-byte pieces with no drop (E1227). Whether it is
  also the cause of the rare drop is open -- it is a mechanism that ends a
  race the way the drop does.
- **A bench trap, now closed (E1215-E1225).** Hosting needs LocalServices:
  the ROM's SdpServer refuses the host's record without it (-46, E1106), the
  joiner's SDP query then finds nothing ("SDP peer has no responder up") and
  its Join reads "Connection request failed", while the host sits in its
  HOST GAME wait looking hung. The committed game.h carries no capability
  (round 170), so every duo after a commit built without it. duo.sh now
  builds with LocalServices itself (NGPORT_CAPS).
- **The engine runs 0x4300000 above its image addresses** (its chunk is at
  0x4700000): every PCTRACE range, start and register PC for the engine is
  image address + 0x4300000 (E1222, E1223).
- **The bench guesses are not needed** with the real flow (E1196: both off,
  no faults).

## Where it stands (E1228-E1278): a whole rally stage, and on to the next

- **The drop is gone.** Races dropped 20 s to 7 min in: 9.x's `RSocket::Write`
  sets ERequestPending in the word after its status -- the engine's EKA1
  receive status -- with a load, OR and store, and a receive completed in
  between was lost (BUGBOOK 13.ak). The receive now runs on a status of the
  port's own (`own_status_thunk`); no drop since, in every long run.
- **A stage, both cars, to the finish** (E1259, E1278): every split time
  (type 7) crosses both ways, the finish (index 8) included, each send answered
  by the other side's receive with the same hundredths. Then on both STAGE TIME
  (both players' splits), the results, and OPTIONS: the host's Continue /
  Restart Rally / Exit, with "Waiting for other player" and Continue greyed
  until the other car is in; the joiner's only "Quit game". A pause on either
  side pauses both ("adg has paused the game", E1255).
- **Stage 2** (E1278): the host's Continue loads FINLAND STAGE 2 on both, and
  both sit at its start line with the clock running and the other car on the
  strip.
- The bench drives with `autopilot.py`: from the screen, or -- reliably --
  along a recorded path (`games/colin/path_full.npy`, Finland stage 1) steered
  by the car's pose read from the game's memory (EKA2L1_MEMSNAP live; the car
  records at 0x48f6924 single-player, 0x48f6ab4 and 0x48f75f8 two-player,
  record 0 the local car on both sides).

## Next steps, in order

1. The rest of a rally on the bench: stage 2 onward needs a path per stage
   (mkpath.py / extendpath.py from a drive), and the rally's end screens.
2. The phone. Round 172: the self-signed multiplayer loader installs on the
   N95 (LocalServices granted silently) and Host a Game reaches "Waiting for
   players" without a freeze. **Pinned by the user:** a join from a second
   phone, and the single-player 013 loader after uninstalling 012 (N95, E72).
3. Round 173 (N95 host, N73 join): no host seen; the N73 listed a 6630.
   Fixed the joiner's inquiry address (E1287); a host is visible only when
   its owner sets "Shown to all" (the scan-enable ioctls are refused on 9.x).
   Open: Join with Bluetooth off leaves the game alive and its screen gone --
   the launcher answers every ask "Bluetooth on" (GAME_LAUNCHER_BT_ON).
