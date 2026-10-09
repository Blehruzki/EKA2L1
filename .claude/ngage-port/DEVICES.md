# Devices and OS versions

What the port has learnt about each S60 edition and each phone: where they
differ, what each difference broke, what covers it now, and the round that
settled it. **Read it before proposing a hardware round on a phone or an
edition not yet confirmed**, and before trusting an offset, an ordinal or a
HAL answer measured on one ROM. When a round finds a new difference, add a
row here as well as in `BUGBOOK.md`.

The rule this file exists for, learnt three times (rounds 120, 123, 159):
**a layout measured on one ROM is a measurement of that ROM.** Every private
offset the port reads was first measured on 3.1 or 3.2 and then broke on 3.0.

How each difference reaches each title -- which titles take the path it
breaks, and which packages carry the fix -- is in `GAMES.md`.

Sources: `ROUNDS.md` (rounds and E-rows cited), `BUGBOOK.md` (sections
cited), `SYMBIAN.md`, the comments in `toolchain/port/gate6.cpp`.

## By edition

### S60 3.0 -- Symbian 9.1 (N91, N73, N80; possibly E65)

ARM9 phones. The ROM is at 0xF8xxxxxx and the game image loads high.

| Difference | What it broke | What covers it | Settled |
|---|---|---|---|
| `CCoeControl::iWin` is not at 0x28 (0 there; the window is at word 9 on the N91) | The game handed a null window to `CDirectScreenAccess::NewL`; the first StartL took a data abort at 0 (both Asphalts) | The window is validated rather than read: the 0x28 word if it passes the session-buffer test, else the control's one word that does, else `DrawableWindow()` (`gate6_create_window`) | round 120-121; BUGBOOK 1.p |
| `CDirectScreenAccess` carries one more word before the window (gc, device and region found at run time, not at 0x1c/0x20/0x24) | StartL read the wrong words | `dsaGcOff`/`dsaDevOff`/`dsaRgnOff` found before the first StartL by locating the window in the object (`dsa_gc_off`) | round 120 |
| `CCoeControl` keeps its flags behind a pointer at +0x2C (3.1/3.2: a plain word at +0x24) | Any cone method run on the game's old-layout control reads address 8: the base `Draw` (Ashen, N91) and `DrawNow` (One, N73) | Base-class veneers routed to the wrapper (round 123); `DrawNow` and the other control calls in `DIVERTS` swapped for the wrapper (`gen_shim.py`, round 159) | rounds 123, 159; BUGBOOK 1.o, 12.ag |
| `KAknFullOrPartialForegroundGained` (0x10281F36) is sent at launch (3.1 never sends it to these titles), so the framework redraws at once | That first redraw is what reached the base `Draw` above | Covered by the veneer routing | round 123 |
| Opening a thread by its full name fails (-1, KErrNotFound) where the N95 finds it | One's sound-thread handshake ran on a handle it never got: a ROM data abort at launch | The open is answered with a duplicate of the handle the port keeps for every thread it made (`wdThr`) | rounds 154-156; BUGBOOK 12.af |
| The game image loads at 0x7DA00000-0x7E400000 (N73; the N95 and the bench: 0x4600000) | The port's "is this a pointer" test stopped at 0x10000000, so the image's own literals (One's thread names) read as non-pointers | `user_ptr`: [0x400000, 0xFFFF0000) | round 155 |
| `CCoeEnv` layout | Nothing: the 12-byte bias measured on 3.1/3.2 holds on 3.0 | -- | round 120 (N91) |
| The screen is written through the LCD driver (`GenericLcd_Lcd.ldd`), not HAL attributes | Bench only: EKA2L1 lacked the kernel calls (0x82, 0xA) and the driver | Emulator, round 159: `src/emu/ldd/src/lcd/` | E872-E881 |
| The app list needs the loader's icon (MIF) | Bench only: a bench build without one never appeared | `emurun.sh` builds with the icon, as the release does | round 158 |

### S60 3.1 -- Symbian 9.2, Feature Pack 1 (N95, N95 8GB)

ARM11. The ROM's vtables are at 0x80000000; the image loads at 0x4600000.
The phone every hardware round before 154 ran on.

| Difference | What it broke | What covers it | Settled |
|---|---|---|---|
| HAL's display answers are not self-consistent: 16 bits, a 640-byte line and `EGray2`, for a panel that is 32 bits on a 1,280-byte line | Eight format guesses; banded, sheared pictures | HAL's mode is not believed; the reported stride is, if it makes sense; `HAL::Get` is given the live mode in its in/out argument (every build to 170 passed 0) | rounds 60, 70-76, 83; BUGBOOK 6 |
| The panel is natively 320x240 landscape (1,280 bytes = 320 pixels); `UserSvr::ScreenInfo` reports the rotated 240x320 | Every width derived from 240 was wrong | Believe the stride, not the logical width | round 72 |
| Status pane: Avkon reports 58 rows (`(0,58)-(240,293)`) | A hardcoded 56 for four rounds | The inset is asked of Avkon (`LayoutMetricsRect(EMainPane)`), falling back to 56 | rounds 74-78, 87 |
| `CFbsScreenDevice::Update(void)` kills the game | Full-screen mode | The port posts its own whole-panel region with `Update(const TRegion&)` | round 88 |
| The ROM's `mediaclientaudiostream` has 17 exports (the emulator's stand-in more) | -- | The patch's four extra exports have no slot; `NewL` is ordinal 3 | gate6.cpp (the MDA proxy) |
| The audio stream never reports `KErrUnderflow`, keeps ~375 ms queued by `Position`, copies the first buffer ~93 ms late | A title that runs dry is stopped on the bench and starved on the phone | SYMBIAN.md has the numbers | round 135 (ngtest) |
| euser ordinal 584 is not a fast counter (answers a constant) | Build 015's timing sums were void | `User::TickCount` (674, 64 a second) | round 152 |
| Threads preempt each other; a ring written in two steps tears | Bench-invisible races in the log writer | Single-writer logging | BUGBOOK 11 |

### S60 3.2 -- Symbian 9.3, Feature Pack 2 (C5-00, N79, the 5320 bench ROM)

Where most private offsets were measured (the RM-409 ROM on the bench):
`CCoeControl::iWin` at 0x28, the DSA's gc/device/region at 0x1c/0x20/0x24,
every vtable slot in the shim's tables.

| Difference | What it broke | What covers it | Settled |
|---|---|---|---|
| The framebuffer line is 2,048 pixels (C5-00: 16 bits on 4,096 bytes in mode 0, 24 on 8,192 in mode 1 -- the second pair true) | The 1,280-byte fallback streaked the picture | The reported stride is believed when it checks out | round 83 |
| Status pane: Avkon reports 48 rows on the 5320 | -- | Asked of Avkon | round 87 |
| Avkon ordinals are not guaranteed across feature packs | -- | Avkon calls the port makes itself are resolved at run time through its `RLibrary` | gate6.cpp (`ASK_AVKON_INSET`) |

### Every EKA2 edition

| Difference from the N-Gage (EKA1) | What it broke | What covers it | Settled |
|---|---|---|---|
| A user thread's stack is capped (64 KB): `RThread::Create` refuses more with `KErrTooBig` (-40), and a small one overflows sooner because 9.x client code runs on it | The sound server thread never started (rounds 60-61); One's 8 KB loading thread died KERN-EXEC 3 (round 126) | Clamp to 64 KB, halve on refusal, raise requests below the floor | BUGBOOK 4 |
| A bad, stale or wrong-thread handle panics KERN-EXEC 0 (the emulator returns an error) | Minimize deaths on the N95 | `EKA2L1_STRICTHANDLE=2` on the bench | rounds 137, 141; BUGBOOK 4 |
| A dead thread's heap is freed | Reads of a worker's memory after it exits | Emulator fixed to match (round 134) | BUGBOOK 11a |
| Icons are MIF, not MBM | An empty box in the app grid | The package carries a MIF | round 83 |
| A drive without ready media answers -18 (`KErrNotReady`), e.g. no card, or a card mounted over USB | A phone-memory install opened the game's pack on E: | The game's `E:` paths are rewritten to the install drive, with a retry | rounds 109-110; BUGBOOK 1.z |
| A SIS is integrity-checked (per-file SHA-1, CRCs, signature); the emulator checks none | -- | Nothing validated only on the bench is proven | root CLAUDE.md |
| `TRequestStatus` is two words (value, then `iFlags` with `EActive` at bit 0); EKA1's was one | An inlined `SetActive` that tests the second word read `iFlags` as "already active": Colin McRae's "Already Active" panic | The engine's three code words test and set bit 0 (`GAME_CODE_PATCHES`) | E884-E886 |
| `RWindow` is 24 bytes (`RDrawableWindow` adds a draw rect at +8); EKA1's was 8 | The constructors, `BeginRedraw(TRect)` and `EndRedraw` wrote 16 bytes past an 8-byte stack temporary, over saved registers | Those four run on a full-size copy (`IMPORT_RWIN_*`) | E886-E891 |
| C++ vtables are EABI's: the vptr points at slot 0, the destructor is two slots (complete, then deleting at +4), and later classes moved methods (`CWindowGc::Activate` +0xd8 -> +0x110) | A title calling a ROM object through its old slot calls something else: Colin's `Activate` was `Clear()` on an inactive gc (WSERV 9 on a phone; a host crash in EKA2L1), its deletes landed on another method | Per-title code words to the 9.x slots, read out of both ROMs' ws32 (`_ZTV9CWindowGc` against the N-Gage's `CWindowGc` vptr) | E920-E921 |
| `RWsSession::SimulateRawEvent` needs `SwEvent`, and its by-value `TRawEvent` gained `iTicks` | -- | Answered as an empty local | E891 |
| Several `RWsSession`/`RWindow` getters became `const` (`GetEvent`, `GetFocusWindowGroup`, `GetPriorityKey`, `FetchMessage`, `GetInvalidRegion`) | Unpaired imports | `gen_shim.py` MANUAL pairings | E891 |

## By phone

| Phone | Edition | Screen and framebuffer | Keypad | Image at | Titles confirmed | Open |
|---|---|---|---|---|---|---|
| Nokia N95 | 3.1 (FP1) | 240x320 logical, native 320x240; 32 bpp, 1,280-byte line; status pane 58 | numeric (C key) | 0x4600000 | Asphalt Urban GT 030, Asphalt 2 196, Ashen 016, One 022 | -- |
| Nokia N95 8GB | 3.1 (FP1) | as the N95 | numeric | -- | One 022 (round 154, beside the N73) | -- |
| Nokia N73 | 3.0 | 240x320 | numeric | 0x7DA00000 / 0x7E400000 | none yet | One dies in `DrawNow` at the first frame (12.ag); build 028 fixes it on the bench, waiting on the phone |
| Nokia N91 | 3.0 | 176x208; 32-bit framebuffer, first pixel 32 bytes in | numeric | -- | Asphalt Urban GT 030, Asphalt 2 196 (round 121), Ashen 011 (round 124) | One never run on it |
| Nokia C5-00 | 3.2 | 240x320; 2,048-pixel line | numeric | -- | Asphalt Urban GT 027 (round 122) | Asphalt 2, Ashen, One not reported |
| Nokia N79 | 3.2 | predicted 2,048-pixel line, as the C5-00 | numeric | -- | none reported (logs in round 83) | untested |
| Nokia E65 | recorded as 3.0 from its log (ROM at 0xF8xxxxxx); Nokia lists the E65 as 3.1 -- unverified | -- | numeric | -- | none | Asphalt 2 192 died in the first direct-screen start (round 109), before the round-120 fixes; never re-tested |
| 5320 (RM-409), bench | 3.2 (FP2) | 240x320; status pane 48 | -- | 0x4600000 | every title, every build | EKA2L1 swaps in its own `scdv.dll` |
| N80 (RM-92), bench | 3.0 | 352x416, EColor16MU (its own wsini.ini); one of the panels in `fittest.cpp`'s eight known failures (the fitted modes' map clamp) | -- | 0x4700000 | One 028 (E881) | EKA2L1's own `scdv` has no 3.0 map, so the ROM's runs on the emulator's LCD driver |

Panels the port has only been designed against (`fittest.cpp`, no phone has
reported): 320x240 landscape (E71), 360x640 (5800/N97), 800x352 inner (E90),
and 176x208 apart from the N91. The RM-409 ROM has no Avkon layout for them
(AVKON 61, E232-E233, E442). **352x416 is on the bench now**: the N80
firmware is that panel, with its own layouts (One ran on it, E881).

## Before a round on a new phone

1. Find its edition. If it is **3.0**, every row in the 3.0 table applies,
   and anything measured only on 3.1/3.2 is suspect.
2. If it is 3.0 and the title calls cone or ws32 methods on its own objects,
   check those calls against the 3.0 layout on the **N80 bench**
   (`DEVICE=RM-92 emurun.sh`) before asking for the phone.
3. Check its panel against the table above and `fittest.cpp`.
4. Note its keypad: the hold-C picker uses `EStdKeyBackspace`, which works on
   both layouts.
5. After the round, add its row here.
