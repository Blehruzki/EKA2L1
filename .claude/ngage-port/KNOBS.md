# What each title has switched on

One row per fix, feature or knob in `toolchain/port/games/<title>/game.h`
and the shared `gate6.cpp`, as the shipping builds stand. "Universal" means
the mechanism is not specific to one game's code; a universal fix still
stays off for a title until a round of its own has run it there. Rounds are
hardware; E-numbers are bench runs (ROUNDS.md).

Shipping builds: Asphalt Urban GT **030** (`gate6a1`), Asphalt 2 **196**
(`gate6`), Ashen **009** (`gate6ashe`). All verified on a Nokia N95 (S60 3.1)
and the bench (RM-409 ROM, S60 3.2). A C5-00 run of Asphalt Urban GT 027 was
never confirmed. No S60 3.0 phone has run any build to its menu (the N91
logs of round 118 are the open item).

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

## Shared mechanisms with no knob (every title)

| mechanism | since | settled |
|---|---|---|
| old-shaped stand-ins for CFbsBitGc, CWindowGc, CDirectScreenAccess (shadow), CEikAppUi, CEikDocument, CApaApplication, the control | gates 3-6 | all three titles, hardware |
| the picture modes, C held, choice saved at `C:\` root | round 86 / 114 | hardware |
| the exit path: `CEikAppUi::Exit()` leaves KLeaveExit on 9.x; the hook exits the thread with 0 | round 113 | hardware (Ashen 005+; the Asphalts quit through their own `User::Exit`) |
| the frame timer wrapper at priority -101, the DSA restart after an abort | rounds 100-103 | hardware |
| the fault box (`g6box-<stem>.dat`) and the log (`g6box-<stem>.log`) | gate 6 | every round reads them |
| the control's window and the DSA object's layout found at run time, validated against the session buffer, rather than read at offsets measured on 3.1/3.2 | round 120 | bench E454-E456; the N91 (S60 3.0) is the phone it was made for |

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
