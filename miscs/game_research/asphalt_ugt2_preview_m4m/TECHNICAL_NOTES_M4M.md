# M4M: restoring the Pause → CHEATS entry (preview 2.0.157)

**Evidence level:** static analysis plus a Unicorn emulation of the patched call path. **Not yet tested in EKA2L1 or on hardware.** Every address below applies to the preview APP only (`file = VA − 0x10000000 + 0x7C`).

## 1. How localized strings are looked up (this was the missing piece)
- The English table `6RBC.DAT` stream `0x8F161` (binary, 1169 offsets, entry 0 is a header) is the one the game uses at runtime. The text file at `0x911BE` is an older generated listing, and its line numbers do **not** match the runtime IDs.
- Code fetches a string with `game = [[0x100BDBFC()] + 0]`, then `tm = [game + 0x1270C]`, then `char* = [tm + 4*(id−1)]`. For example, the dev builder reads `tm+0x160/0x170/0x174`, which are UNLOCK CHAMPIONSHIPS, UNLOCK ARCADE CATEGORIES and UNLOCK CHALLENGES.
- Runtime IDs and table offsets: CHEATS 0x1CB → `+0x728`, JUKEBOX `+0x72C`, PAUSE `+0x730`, QUIT RACE `+0x734`, RESUME RACE `+0x738`, RESTART RACE `+0x73C`, SOUND `+0x740`, EXIT GAME 0x398 → `+0xE5C`.

## 2. The live Pause menu
- The Pause screen case sits inside the factory `0x100AC660` and starts at `0x100ADC1C`. It allocates a 0x380-byte object and calls the base constructor `0x100CD22C`. The vtables are primary `0x1015E234` at +0x300, interface `0x1015E2C4` at +0x370 and interface `0x1015E2B8` at +0x374.
- Items are added with `addItem(menu, std::string*, id)` = `0x100CD9F0`, after the title is set with `0x100CD9A8`:
  - JUKEBOX 0, SOUND 1, RESUME 2, RESTART 3 (skipped when `game[0x12334]==0x14`), QUIT 4 at the merge point `0x100ADE64`, and EXIT GAME 7 at `0x100ADEA0`.
  - id 5 (`tm+0xBBC`) is an alternative row used in mode 0x14.
  - **id 6 is never added.**
- The base constructor pre-allocates 8 item slots at `+0x34C..+0x368`, with the count at `+0x36C`.

## 3. The dormant CHEATS code (still intact)
| Purpose | VA | Notes |
|---|---|---|
| Add CHEATS row | `0x100DB22C` | If `game[0x12334]!=0x14` and `byte game[0x7438]==0` (the multiplayer-session flag), it calls `addItem(this, TXT_INGAME_CHEATS, 6)`. It is the override in the +0x374 interface, reached through thunk `0x100F4B9C` and vtable `0x1015E2B8+8`. The base implementation, `0x100DB2C0`, is just `bx lr`. **No code ever forms `this+0x374` or calls this slot.** |
| Cheat selector widget | ctor `0x100AB020`, vtable `0x1015C248` | Built live by the Pause builder at `0x100AE3E8` and stored at Pause+0x37C. Its 16 entries are in §5. |
| Focus handler | `0x100E1F9C` / `0x100DB35C` | Show the jukebox when the focused id is 0, and the cheat widget when it is 6. |
| Left / right | `0x100DD2B8` / `0x100DE23C` | For id 6, call widget vtbl+0x14 / +0x18. |
| Select | `0x100DDA54` (8-way jump table) | Case 6, `0x100DDBE8`, calls widget vtbl+0x1C = `0x100DD478`, which runs the current entry's action through item vtbl+0x40. |

In short, the CHEATS UI is fully built and wired. The only thing missing is the call that adds the menu row.

## 4. The patch (5 words plus the E32 checksum)
| VA | File | Old | New | Meaning |
|---|---|---|---|---|
| `0x100ADEA0` | `0xADF1C` | `EB007ED2` | `EBFDD59A` | The EXIT GAME `bl addItem` now calls the stub |
| `0x10023510` | `0x2358C` | `E92D4070` | `E92D4007` | `push {r0,r1,r2,lr}` |
| `0x10023514` | `0x23590` | `E1A06000` | `EB02DF44` | `bl 0x100DB22C` (original "add CHEATS", id 6) |
| `0x10023518` | `0x23594` | `E3A00034` | `E8BD4007` | `pop {r0,r1,r2,lr}` |
| `0x1002351C` | `0x23598` | `EB0110D0` | `EA02A933` | `b 0x100CD9F0` (EXIT GAME with the original arguments) |

- The checksum at `0x18` becomes `0x3B32A682` (M4M, on M4B) or `0xB452A731` (clean variant, on the original).
- The cave is the unreferenced developer-widget constructor `0x10023510`. It has no B/BL or literal references from outside itself. The stub stays clear of the relocated literal at `0x10023548`. I parsed all 9,455 code relocations, and none of the patched words is a relocation target.
- Only ARMv4T instructions are used, the stub is position-independent, and it keeps the caller's stack and registers intact.

## 5. Verification done
- Byte diff against the base: exactly the 5 words above plus the checksum.
- Unicorn emulation from `0x100ADE94` with the platform calls stubbed:
  - It calls `addItem(menu,"CHEATS",6)`, then `addItem(menu,"EXIT GAME",7)` with the caller's original sp, and returns to `0x100ADEA4` with sp, r4–r11 unchanged.
  - With `game[0x7438]=1`, or with mode 0x14, only EXIT GAME is added.
- Not verified: whether the 7-row layout fits on screen, and how each individual cheat behaves at runtime.

Entries in the cheat widget, from its builder (`0x100AB4D0`):
- localized: UNLOCK CHAMPIONSHIPS, UNLOCK CHALLENGES, UNLOCK ARCADE CATEGORIES, FREEZE VEHICLES, SHOW VEHICLE COORDINATES, GIVE $10,000
- literal: Win All Arcade Categories, Win All Championships, Show FPS, Camera Front, Hide HUD, Show MP Lantecy, Show AI Errors, Gimme All Cars!, Raise Cop Level, Win Race

The widget only fills its list when `[game+0xB460]!=0` and mode is not 0x14.

## 6. Next steps
1. Get user feedback on M4M. If the rows overflow the box, the same hook can be moved to `0x100ADE64` (before QUIT), or one row can be dropped.
2. Retail 2.0.194: repeat the analysis from scratch. Find `tm+0x728`-style loads of the retail TXT_INGAME_CHEATS ID, the matching "add CHEATS" virtual and the Pause builder. Do not reuse preview addresses.

Reproduce: `python3 scripts/patch_preview_cheats_m4m.py IN.APP OUT.APP` (IN must be the original or M4B; it checks the SHA-256 and every word before changing anything). Then `python3 scripts/verify_m4m.py IN.APP OUT.APP` (needs capstone and unicorn).
