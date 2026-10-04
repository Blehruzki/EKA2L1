# What each title has switched on

One row per fix, feature or knob in `toolchain/port/games/<title>/game.h`
and the shared `gate6.cpp`, as the shipping builds stand. "Universal" means
the mechanism is not specific to one game's code; a universal fix still
stays off for a title until a round of its own has run it there. Rounds are
hardware; E-numbers are bench runs (ROUNDS.md).

Shipping builds: Asphalt Urban GT **030** (`gate6a1`), Asphalt 2 **196**
(`gate6`), Ashen **011** (`gate6ashe`). All verified on a Nokia N95 (S60 3.1)
and the bench (RM-409 ROM, S60 3.2); the Asphalts also on a Nokia N91 (S60 3.0,
round 121). Asphalt Urban GT 027 was confirmed on a Nokia C5-00 (S60 3.2, round 122). Ashen 010 reached the N91 and fell on a second 3.0 difference
(round 123); 011 carries that fix, and round 124 confirmed all three titles on
the N95 and the N91. One (`gate6one`) is on the bench only, not shipped.

## Fixes and features, by knob

| knob | Asphalt UGT | Asphalt 2 | Ashen | universal | what it does | settled |
|---|---|---|---|---|---|---|
| `GAME_FIX_APPUI_THIS` | 1 | 0 | 1 | yes | CEikAppUi methods get the real 9.x app UI as `this` instead of the game's old one | E264/E265, hardware since UGT 00x; Asphalt 2 never needed it |
| `GAME_UI_FORWARD_EVENTS` | 0 | 0 | 1 | yes | the wrapper forwards foreground, system event and command to the game's own app UI overrides | E368; Ashen hardware rounds 113-117 |
| `GAME_CONTROL_W/H` | 0 | 0 | 176x208 | yes | the control's extent for a title that sizes bitmaps from `Rect()`; 0 = whole screen | E372; Ashen hardware |
| `GAME_CODE_PATCHES` | none | none | 2 words | per image | words of the image rewritten after loading (Ashen: a reciprocal-table clamp that read page 0) | E372; Ashen hardware |
| `GAME_SCREEN_FONTS` | 0 | 0 | 1 | yes | a stand-in screen device that answers the N-Gage font names with the system font at that pixel height (slot 18) and releases through the map table | E384, round 116 (E437/E438); hardware round 117 |
| `GAME_ANSWER_GAME_ID` | 0 | 0 | 1 | yes | a failed read of `\Game.Id` is answered with "N-Gage" | round 112; hardware |
| `GAME_FPA_DOUBLES` | 0 | 0 | 1 | yes | doubles crossing to 9.x helpers and `Math::` are word-swapped (FPA vs EABI order) | round 113; hardware. The Asphalts import five double helpers and stay 0 until a round of their own |
| `GAME_SCREEN_MODES` | - | - | 1 | yes (window-gc titles) | hold C cycles the picture mode for a title drawing through the window gc; the Asphalts get the same through `screen_layout` | round 114; hardware (both paths) |
| `GAME_CANCEL_ROM_OBJECTS` | 0 | 0 | 1 | yes | a stray `CActive::Cancel` on a 9.x object (vptr in ROM) is forwarded instead of dropped | round 115 (E426/E427); hardware rounds 115-117 |
| `GAME_CANCEL_OWN_OBJECTS` | 0 | 0 | 0 | yes | a `CActive::Cancel` on an object of the game's own class (vptr in the image) is forwarded instead of dropped; One 1 | E493-E503 (One, bench) |
| `GAME_VTABLE_SHIFTS` | -- | -- | -- | yes | a game class's vtable moved down two words to EABI's address point, for a class whose overrides 9.x calls; One `{ 0x152940, 11 }` | E555-E562 (One, bench) |
| `GAME_MULTI_TIMER` | 0 | 0 | 0 | yes | every game CTimer gets its own stand-in in a table; ~CTimer and the base DoCancel go to it; One 1 | E569 (One, bench, strict handles) |
| `GAME_DATA_UID3` | -- | -- | -- | -- | the data package's UID for a split install (`build_release.py --split`); One `0xE0001109` | round 125 |
| `GAME_TIMER_MIRROR` | 0 | 0 | 0 | yes | the wrapped CTimer's status and flags reach the game's object after After and a forwarded Cancel, not only at RunL; One 1 | E514-E515 (One, bench) |
| `GAME_SRC_BPP` | 16 | 16 | 16 | yes | bytes a pixel the game writes into the ScreenInfo buffer; One 32 (EColor16MU) | E489-E490 (One, bench) |
| `GAME_CARD_CID` | default | default | default | per title | the card identity the MMC driver answers Codewave with: the dump's MMC-ID; One 567857f1-7d011234-0b2b1879-06000400 | E488 (One, bench) |
| `GAME_VA_LIST` | 0 | 0 | 1 | yes | `FormatList` gets the va pointer, not the GCC98r2 array's address | round 116 (E435/E437); hardware round 117. UGT imports no FormatList; Asphalt 2 imports `Format` only |
| `GAME_IMAGE_WATCH`, `GAME_HOOK_UNCOMPRESS`, `GAME_Z_*` | 0 | 1 | 0 | no (Asphalt 2 addresses) | re-reads the app UI vtable slot every milestone; logs zlib `uncompress` calls | E253 is the warning: on any other image these addresses are wrong |
| `GAME_FREE_BACK_FROM` | 4096 | 4096 | 4096 | yes | cells this size and up go straight back to the heap, smaller ones through the quarantine | round 92 |
| `GAME_LEAK_ALL` | 0 | 0 | 0 | yes | diagnostic: nothing freed. Shipped by mistake in UGT 003-005 (round 92's `G6FLT 25100`) | off everywhere |
| `GAME_ALLOC_PAD` | 512 | 0 | 512 | yes | a cushion on every allocation, for 9.x objects the game allocates at their 7.0s size (CEikDialog, E277) | UGT and Ashen hardware; Asphalt 2 runs without it on three phones |
| `GAME_DIVERT_MATCH` | 1 | 0 | 1 | yes | active-object diversions matched by object rather than substituted (E282, EReqAlreadyAdded) | UGT and Ashen hardware; Asphalt 2 ships the older substitution |
| `GAME_PICTURE_MEASURED` | 1 | 1 | 0 | per image | the source pitch and origin are measurements (352 and 0; 352 and 16) rather than defaults | E288; rounds 74-78 |
| `GAME_LOG_CLOCK` | 1 | 0 | 1 | yes | a tick every sixteenth traced event (boot timing) | diagnostic, cheap |
| `GAME_LOG_TEXT` | 0 | 0 | 0 | yes | diagnostic: every UseFont, DrawText, Format and FormatList, with callers | bench only; never shipped on |
| `GAME_DUMP_FRAME`, `GAME_DUMP_SCREEN` | 0 / 1 | 0 / 0 | 0 / 0 | yes | raw frame and composited framebuffer dumps to `C:\g6code-<stem>.bin` at a frame number; DUMP_SCREEN alone does nothing | round 95-97 |
| `GAME_SHIFT_PICKER` | 0 | 0 | 0 | yes | gone for good: the offset it hunted was `GAME_SRC_ORIGIN` (round 97) | off everywhere |
| `GAME_BUNDLE_DATA` | 1 | 1 | 1 | yes | the game's files ship inside the SIS | since UGT 022 |
| `GAME_AO_PRIORITIES` | none | none | none | yes | rewrites the priority a named `CActive::CActive` call site passes: a title's own self-completing frame loop goes below EPriorityIdle (One: {0x264f8, -101}) | round 128; bench E590 |
| `GAME_MDA_WRITER_CB` | 0 | 0 | 0 | per image | where a title's audio writer keeps its stream callback; on, every 256th Position call logs the writer's written count and the stream's Position (One: 0xc) | round 129 |

## Shared mechanisms with no knob (every title)

| mechanism | since | settled |
|---|---|---|
| old-shaped stand-ins for CFbsBitGc, CWindowGc, CDirectScreenAccess (shadow), CEikAppUi, CEikDocument, CApaApplication, the control | gates 3-6 | all three titles, hardware |
| the picture modes, C held, choice saved at `C:\` root | round 86 / 114 | hardware |
| the exit path: `CEikAppUi::Exit()` leaves KLeaveExit on 9.x; the hook exits the thread with 0 | round 113 | hardware (Ashen 005+; the Asphalts quit through their own `User::Exit`) |
| the frame timer wrapper at priority -101, the DSA restart after an abort | rounds 100-103 | hardware |
| the fault box (`g6box-<stem>.dat`) and the log (`g6box-<stem>.log`) | gate 6 | every round reads them |
| the control's window and the DSA object's layout found at run time, validated against the session buffer, rather than read at offsets measured on 3.1/3.2 | round 120 | bench E454-E456; the N91 (S60 3.0) is the phone it was made for |
| an old control slot that is a base-class veneer into cone is not forwarded; the wrapper's own base function runs instead | round 123 | bench E461-E464; Ashen on the N91 |
| a game thread's stack request clamped to 64 KB and raised to it (`STACK_RAISE`, 32 KB in build 003): the main thread's size | rounds 126, 127 | bench E583; 32 KB did not save One's loading thread on the N95 |
| every game thread takes the port's exception handler; its fault frame is logged by the main thread's heartbeat (`NOTE_WRK_FAULT`) | round 127 | bench E582 (installs counted; EKA2L1 delivers no user exception) |
| the free quarantine's slot exchanged with `swp`, so two threads cannot free one cell twice | round 127 | bench E583 |
| `RFsBase::Close` is efsrv `RFile::Close` (it was `RHandleBase::Close`, which closed nothing) | round 127 | bench E584-E585: One's save persists |
| `G6PUR` carries the caller's return address (it said 0) | round 128 | -- |
| from a direct-screen abort on, every record is flushed (the endgame budget) | round 129 | -- |
| the stream's Position calls are logged for the first few only, like WriteL | round 129 | bench E613 |
| hold C noted on the app UI's key path as well as the control's (`hold_key`), with the 100 ms hold timer, for a title that takes its keys in `HandleKeyEventL` (One) | round 126 | bench E577 |
| per-call tracing without `TTrap::Trap`/`UnTrap`/`User::AllocL`, and `ScreenInfo` logged for its first 16 polls only; the allocator wraps on the title's own `kAlloc` (they were Asphalt 2's indices) | round 126 | bench E577-E580 |

## Where each title saves

All three write to `C:\System\Apps\<STEM>\`, the N-Gage convention, and
create that folder themselves; no package lists it, so an uninstall leaves
the saves behind.

| title | folder | settings | progress |
|---|---|---|---|
| Asphalt Urban GT | `c:\system\apps\6R67\` | `user.dat` | `user.dat` |
| Asphalt 2 | `c:\system\apps\6RBC\` | `user.dat` | `user.dat` |
| Ashen | `C:\System\Apps\6R21\` | `options.dat` (E445) | `savegameNN.sav` (from the image's format string; not yet seen written) |

## Bench knobs in `gate6.cpp` that must be 0 in a shipped build

`BENCH_BACKGROUND_TICK`, `BENCH_FOREGROUND_TICK`, `LEAVE_RAW`, `WORKER_LOG`,
`TRACE_IMPORTS`. `rules.py` and the regression rows (E428, E439 and the like)
are where that is checked.

Per title in `game.h`, absent (so off) in a shipped build:
`GAME_RANGE_PROBES` / `GAME_RANGE_PROBE_COUNT` (a register logged at game
addresses, out of range or sampled every 256th pass; E522-E550) and
`GAME_FPA_SELFTEST` (every float and double import called once with known
answers; One's import numbers only; E535-E536).

## One (6r58, `gate6one`), bench only

Built from `newgame.py`'s template; on top of its defaults: `GAME_SRC_BPP 32`,
`GAME_CARD_CID`, `GAME_CODE_PATCHES` (eight DoSeekL sites, old slot 8 to EABI
slot 8), `GAME_CANCEL_OWN_OBJECTS 1`, `GAME_TIMER_MIRROR 1`. Shared fixes it
needed that are always on: the session path on every Connect (any thread), the
TEntry/TVolumeInfo stand-ins, `old_deletable`, `PushL(CBase*)`, thread open by
full name, the null-object AppUiFactory, the ScreenInfo buffer on every poll,
the two-argument struct-return stub, `KIND_DBL1` for a double passed by value.
Asphalt 2's `kNop` is now applied to Asphalt 2 only (in One it broke the
rasteriser's reciprocal table: the fight-start fault, E516-E527).
`GAME_VTABLE_SHIFTS { 0x152940, 11 }`: the pak reader's TStreamBuf vtable at
EABI's address point, without which the animation table read as zeros and
no fighter moved (E555-E562). Fights play on the bench: AI attacks, health,
rounds. Round 125 on the N95: three bad handles the bench forgave, fixed
in E567-E569; build 002 ships split, the loader and the data apart.
Round 126: the loading thread's KERN-EXEC 3, hold C, the origin (`GAME_SRC_ORIGIN 8`)
and the log volume behind the music's hiccups, in build 003 (E574-E580).
