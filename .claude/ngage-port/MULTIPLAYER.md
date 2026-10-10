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
| SDP record (0x40dfc8) | sdpdatabase, GCC 2.x builder | 9.x `CSdpAttrValueDES` (EABI vtable) | **open**: leaves on the bench (E1105) |
| Device-selection notifier, `TBTDeviceResponseParams` | btextnotifiers 6.1 layout | 9.x layout | **open**: compare layouts (join) |
| `TBTSockAddr`, `TInquirySockAddr` | 6.1 layouts | 9.x adds security / fields | **open**: compare (join) |
| Leave in the host path | caught | unwinds into the display's teardown; on 9.2 phones under the global bitmap heap lock (13.ag) | lock released on leave (012); the teardown itself still hangs on the bench in a `CActive::Cancel` (E1105) |

## The bench

EKA2L1 emulates Bluetooth over IP (`btmidman_inet`: RFCOMM, L2CAP and SDP
over TCP; discovery by LAN, direct IP friends or a proxy server). Its
transport was IPv6 throughout and this container has no IPv6 (every
AF_INET6 socket fails EAFNOSUPPORT), which crashed the emulator on Host
(gdb: btinet_socket::bind on a socket never made, E1100). Patched:
`epoc::internet::host_supports_ipv6()`, probed once, and IPv4 at each site
when it is false; the inet socket's init failure reports libuv's code and
frees its half-made handle (E1101). The emulator enforces no capability.

Next for the bench: two instances (a second HOME, a copy of the drives,
direct-IP friends on 127.0.0.1 with their port offsets), host on one and
join on the other.

## Next steps, in order

1. The SDP record build (0x40dfc8): find the leave; shadow the DES
   builder's vtable for the engine's GCC 2.x slots, or answer the record
   with a stand-in.
2. The display-teardown hang in `CActive::Cancel` (E1105): which active
   object, and why its DoCancel never completes on the bench.
3. HCI scan-enable ioctls on 9.x: what they answer; a stand-in if refused.
4. Join: notifier and address layouts; then the two-instance bench.
5. A self-signed LocalServices package for phones that grant it.
