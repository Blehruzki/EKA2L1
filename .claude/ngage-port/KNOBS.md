# What each title has switched on

One row per fix, feature or knob in `toolchain/port/games/<title>/game.h`
and the shared `gate6.cpp`, as the shipping builds stand. "Universal" means
the mechanism is not specific to one game's code; a universal fix still
stays off for a title until a round of its own has run it there. Rounds are
hardware; E-numbers are bench runs (ROUNDS.md).

Shipping builds: Asphalt Urban GT **030** (`gate6a1`), Asphalt 2 **196**
(`gate6`), Ashen **013** (`gate6ashe`: 012 confirmed on the N95 in round 148, 013 adds the port scaler, confirmed smoother on the N95 in round 150; 014 adds the area filter, run on the N95 in round 151 -- fine until a level restart halved the frame rate; 015 measured it in round 152: the main thread's own frame doubled, the scaler constant; 016 adds a scheduler dump at each restart). All verified on a Nokia N95 (S60 3.1)
and the bench (RM-409 ROM, S60 3.2); the Asphalts also on a Nokia N91 (S60 3.0,
round 121). Asphalt Urban GT 027 was confirmed on a Nokia C5-00 (S60 3.2, round 122). Ashen 010 reached the N91 and fell on a second 3.0 difference
(round 123); 011 carries that fix, and round 124 confirmed all three titles on
the N95 and the N91. One (`gate6one`) ships as **028** (round 159: the N73's death reproduced on the N80 firmware -- `CCoeControl::DrawNow` on the old control, which 3.0's cone reads a flags pointer off -- and DrawNow diverted to the wrapper; round 158: the N80 firmware on the bench; an unactivated context repaired after StartL, unproven on the N73; round 157: the N73's StartL leaves a whole-looking context whose device reads null inside the ROM's SetClippingRegion, instrumented; round 156: the N73 reaches its screen setup and the context off the direct-screen-access shadow is null, instrumented; round 155: the N73 loads the image at 0x7DA00000 and the port's pointer test stopped at 0x10000000; 022 in round 149): 021 died at launch on every install without the original `6r58.app` beside the package's `6r58.bin`.

## Fixes and features, by knob

| knob | Asphalt UGT | Asphalt 2 | Ashen | universal | what it does | settled |
|---|---|---|---|---|---|---|
| `GAME_FIX_APPUI_THIS` | 1 | 0 | 1 | yes | CEikAppUi methods get the real 9.x app UI as `this` instead of the game's old one | E264/E265, hardware since UGT 00x; Asphalt 2 never needed it |
| `GAME_PORT_SCALER` | -- | -- | 1 | yes, for a window-gc title with `GAME_SCREEN_MODES` | the fitted modes' frames are scaled by the port (the Asphalts' Bresenham maps, a 4K-to-16MU table) into a bitmap of the panel's format and blitted one to one, instead of the window server's scaled `DrawBitmap`, which ran the stretched modes slower than OG; the four CFbsBitmap entries come off the game's own imports (0x5CA0..0x5CA3) | round 150: bench E856 (every mode's geometry and colours as before); the N95, smoother in the full stretched mode |
| `GAME_SCALE_FILTER` | -- | -- | 1 | yes, on the port scaler | in the non-integer modes a destination pixel that straddles a source boundary is the two source pixels blended by area (`filter_frame`, weights from `screen_fit`, no division); pixels inside one source pixel are copied, so 1:1 and integer stay sharp. Even strokes instead of every fourth column doubled | round 150: bench E857 (zoom and colour count), host check of the weights; hardware pending (the cost per frame) |
| the blit-rate instrument (window-gc titles): every 64 blits, 0x5CA5 how many changed bitmap, 0x5CA6/0x5CA7 fast-counter ticks in the scaler and elapsed, the tick, and 0xC9i00mmm CPU ms per thread (`blit_rate_note`, `thread_cpu_note`) | -- | -- | 1 | yes | four records every 64 blits, a few a second; for round 151's halved frame rate after a restart | round 151: bench E858-E860; round 152 on the N95 (the restart slowdown is the game's frame, not the scaler); 016 sums ticks, 015's euser-584 sums were not a counter |
| `dsa_gc_activate` (all titles on direct screen access): after StartL, a CFbsBitGc whose device word (+0x70) is null is activated on the device StartL made, with bitgdi 148 (0x57B0) | -- | -- | -- | yes | for the N73's first-frame death (12.ag); not its cause (round 159), kept as harmless | round 158: bench E873 (works), E874 (a null device alone is harmless on 3.2); the N73 pending |
| `DIVERTS` cone 53, `CCoeControl::DrawNow()` (any title importing it; One only so far): the game's control swapped for the wrapper | -- | -- | -- | yes | 3.0's cone reads the control's flags through a pointer at +0x2C, zero in the old object: read 8 (12.ag, the N73) | round 159: N80 firmware bench E878 (reproduced), E879/E881 (fixed, runs), E882 (3.2 unchanged); the N73 pending |
| `GAME_UI_FORWARD_EVENTS` | 0 | 0 | 1 | yes | the wrapper forwards foreground, system event and command to the game's own app UI overrides | E368; Ashen hardware rounds 113-117 |
| `GAME_CONTROL_W/H` | 0 | 0 | 176x208 | yes | the control's extent for a title that sizes bitmaps from `Rect()`; 0 = whole screen | E372; Ashen hardware |
| `GAME_CODE_PATCHES` | none | none | 2 words | per image | words of the image rewritten after loading (Ashen: a reciprocal-table clamp that read page 0) | E372; Ashen hardware |
| `GAME_SCREEN_FONTS` | 0 | 0 | 1 | yes | a stand-in screen device that answers the N-Gage font names with the system font at that pixel height (slot 18) and releases through the map table | E384, round 116 (E437/E438); hardware round 117 |
| `GAME_ANSWER_GAME_ID` | 0 | 0 | 1 | yes | a failed read of `\Game.Id` is answered with "N-Gage" | round 112; hardware |
| `GAME_FPA_DOUBLES` | 0 | 0 | 1 | yes | doubles crossing to 9.x helpers and `Math::` are word-swapped (FPA vs EABI order) | round 113; hardware. The Asphalts import five double helpers and stay 0 until a round of their own |
| `GAME_SCREEN_MODES` | - | - | 1 | yes (window-gc titles) | hold C cycles the picture mode for a title drawing through the window gc; the Asphalts get the same through `screen_layout` | round 114; hardware (both paths) |
| `GAME_CANCEL_ROM_OBJECTS` | 0 | 0 | 1 | yes | a stray `CActive::Cancel` on a 9.x object (vptr in ROM) is forwarded instead of dropped | round 115 (E426/E427); hardware rounds 115-117 |
| `GAME_CANCEL_OWN_OBJECTS` | 0 | 0 | 0 | yes | a `CActive::Cancel` on an object of the game's own class (vptr in the image) is forwarded instead of dropped; One 1 | E493-E503 (One, bench) |
| `GAME_VTABLE_SHIFTS` | -- | -- | -- | yes | a game class's vtable moved down two words to EABI's address point, for a class whose overrides 9.x calls; One `{ 0x152940, 11 }`; Colin `{ 0x13d95c, 3 }`, its SDP attribute visitor | E555-E562 (One, bench); E1123-E1125 (Colin, duo bench) |
| `GAME_NULL_NAME_SITES` / `_COUNT` | -- | -- | -- | yes | image offsets of `bl`s to name getters whose NULL is read as "" (`null_name_plant`); a guess, written down in gate6.cpp; Colin: the joiner's player-info builder, five sites | E1160 (Colin, duo bench) |
| `GAME_SOCK_WRITE_KEEP`, `GAME_SOCK_RECV_KEEP0/1` | -- | -- | -- | yes | return addresses of a title's `RSocket::Write` / `RecvOneOrMore` whose EKA1 one-word status has a live word after it; that word is put back after 9.x sets ERequestPending in it (`keep_thunk`); Colin 0x2734, 0x25d4, 0x2840 | E1124-E1125 (Colin, duo bench) |
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
| `RThread::Suspend`/`Resume` (euser 1122/954) refused on a handle with no instance bits that is not a pseudo-handle, logged `NOTE_THREAD_REFUSED` (719): One's protection suspends handle 5 at focus lost | round 130 | bench E621-E625 |
| a watchdog thread (`g6wd`, from beat 3, for a title with a heartbeat) writes `C:\g6stall-<stem>.dat` after 4 s without one: the box ring and the main scheduler's queue; `readstall.py` | round 130 | bench E627-E628 |
| every kick object's construction (0x0B1E) and self-completes per beat (0x0BEA), for a title with `GAME_AO_PRIORITIES` | round 130 | bench E626 |
| every record flushed from an `EEventFocusLost` on, as from a DSA abort | round 130 | bench E622 |
| the watchdog writes a 6 KB slot per stall, four at most, each with every thread's registers (`RThread::Context`, euser 1796) twice 100 ms apart, and the main thread's stack from sp; thread handles duplicated process-owned (euser 121) at creation | round 131 | bench E634-E635 |
| from focus lost to focus gained each audio stream is turned down to 0 on its next WriteL, from its own thread, and given back the game's volume after | round 131 | bench E638-E640 |
| a screen update dropped while the screen was away is posted again once the port has it back (`replay_missed_frame`) | round 131 | -- (the bench never loses the screen) |
| a partial screen frame (box or rectangles) counts as missed as well as a dropped one, and is posted again when the region is whole | round 132 | bench E654 |
| each thread's CPU per beat (`RThread::GetCpuTime`, euser 1782) logged by the heartbeat | round 132 | bench E655 |
| the trap bridge finds a thread's handler by its vtable (`c->trapVt`), with no table: it held eight threads, never emptied, and the ninth thread to trap panicked E32USER-CBase 66 at its first PushL | round 134 | bench E692-E694 (ngtest's ninth worker), regressions E700-E703 |
| stream and callback proxies given back at the stream's deleting destructor and reused (`mda_take`, `mda_release`, one held back a free): ~1 KB each NewL had come out of the spare arena for good, and about the twentieth stream of a launch went to the platform bare and crashed | round 134 | bench E693-E694 (ngtest), regressions E700-E703 |
| the CTrapCleanup stand-in's vtable and old_deletable's tables come from the spare arena (`lasting_allocz`), not the calling thread's heap, which dies with the thread | round 134 | N95 (ngtest build 001: G6FLT 13112), bench E704-E716 |
| a stream callback logs the state of its own stream (by its callback proxy), not of the last stream made | round 134 | N95 (ngtest build 001), bench E704-E705 |
| every `RThread::Suspend`/`Resume` logged with its handle and caller (NOTE 719: 0x5051/0x4E51); a call from the game's decrypted code chunk is allowed only on a thread handle the game got from `RThread::Create` or `Open` and has not closed (`thr_live_*`), a call from the image keeps round 130's well-formedness test | round 137 | bench E723-E725; N95 round 138 |
| after the game's `StartL` with the whole region (a return from a minimize), the heartbeat posts the game's frame again at its next two beats (0x57AA): the window server's repaint of the window it brings to the front can land after the game's one post-return frame | round 138 | bench E728; N95 round 139: fired twice after all seven returns and changed nothing -- the frame it re-posted was the one lacking the menu (below). Round 140: off (`REPOST_AFTER_START 0`) -- never changed anything, and one more whole-buffer blit after a return is one more chance to paint over what the game draws through the gc |
| **the first frame back from a minimize is compared with the frame showing at AbortNow** (`RESTORE_RETURN_FRAME`; 0x57AB: words differing, first row<<16\|last row, updates asked while away) **and, when the game drew nothing while away and the frame lost something, the AbortNow snapshot is put back into the game's buffer before the blit** (0x57AC). One draws its pause menu in dirty rectangles over a buffer it expects to persist, and its one post-return frame sometimes arrives without the menu (round 138's video, frame by frame). A game that drew while away (a fight, a load) keeps its frame | round 139 | bench E731-E732; **N95 round 140: the frame back differs from the snapshot only in the bottom ten rows in seven returns of eight, the menu band identical, and the panel still shows no menu -- so the game's buffer is not where the menu goes missing. Restore off (`RESTORE_RETURN_FRAME 0`), the comparison kept as an instrument** |
| the red key: `HandleCommandL(EEikCmdExit)` on the wrapper flushes the record and leaves with reason 0 (0xE818) instead of reaching the game's handler, whose first virtual call dies in the ROM on the old-layout app UI (G6FLT 38212) | round 139 | N95 rounds 138-139 (the fault); **N95 round 140: closes cleanly** |
| the screen address `UserSvr::ScreenInfo` answers is checked at every poll (the game polls once a frame) and followed when it moves (`REBIND_SCREEN_ADDRESS`, 0x5C1F: the old, the new); HAL's `EDisplayMemoryAddress` (78) logged when it moves (0x5C1E); the frame buffer's middle band summed after each blit and at each heartbeat (0x5C0E: now, as the blit left it, which blit) | round 140 | bench E736-E737 (the band reads exactly what the blit left; HAL answers nothing on the emulator); N95 round 141 |
| **the port no longer starts the DSA at a restart the game did not answer** (`START_FOR_THE_GAME 0`; 0x57AE instead of 0x57A7): the screen is nobody's (clip NONE, `dsaIdle`) until the game's own StartL. One's frame function starts the DSA only when the object reads inactive and draws nothing when it reads active with its own flag clear; our StartL left it active on an empty region, so the frame that carries the pause menu was skipped whenever the request had not completed by the return (rounds 139-141) | round 141 | bench E739-E740; N95 round 142 |
| **diagnostic, One build 015 only** (`DUMP_AT_ABORT`, `DUMP_AFTER_RETURN`, 0 when shipping): the game's buffer and the frame buffer written to `C:\g6code-6r58.bin` at every AbortNow (0xFB01) and the frame buffer after the first blit back (0xFB02); `dump32.py` renders them | round 140 | bench E737; N95 round 141 (5.2 MB, read: the menu was not yet drawn at AbortNow, the fight was running; off again in 016) |
| **the game's own image under its installed name** (round 149): `RFs::Entry` gets the opens' `.app`-to-`.bin` rewrite (`on_the_real_drive`), and a read through the renamed open has the 32 scrambled bytes put back (`image_read_fix`, NOTE 697); universal, active only when the loader itself read a `.bin` | round 149 | bench E852 (the death reproduced), E854 (Entry alone is not enough), E855 (clean); hardware pending |
| **`GAME_PREPARE_EXIT_NOOP`** (One 1, the others 0): `CAknAppUi::PrepareToExit` called by the game on its own old-layout app UI is answered with nothing (0xE819); the `CEikAppUi::Exit` that follows does the leaving. Without it the game's quit path is a data abort at 0x80 (12.z, round 149) | round 149 | bench E853 (a clean exit down the quit path) |
| ROM objects from `CPeriodic::NewL`, `CBufFlat::NewL` and the `CDesC8/16ArrayFlat` constructors get a shadow vtable whose slot +8 takes a GCC 2.x delete (`gate6_shadow`) | E958 | bench: Colin's front end; inert on Asphalt 2 (E977) |
| `CEikonEnv::CreateBitmapL("*")` sent to the game's own `.mbm`, on `CCoeEnv::Static()` | E952-E953 | bench: Colin's front end |
| `Cba()` and `StatusPane()` answered with one hidden stand-in control | E955-E956 | bench: Colin's front end |
| `GAME_DIR_CHARS`: a title whose folder is not its stem (default: the stem) | E952 | bench: Colin's front end (`6r66\6r66_2.app`) |
| the DSA stand-in has a vtable whose every slot deletes the real CDirectScreenAccess (`gate6_dsa_shadow_delete`) | E997-E1001 | bench: Colin's QUIT; Asphalt 2 and One unchanged (E1002-E1003) |
| shadow vtables refuse a class whose slot +8 (through one DLL veneer) is not CBase's or CActive's Extension_; the list now also CMsvSession, CWsScreenDevice, CApaWindowGroupName, CFileMan, CApaCommandLine, the SDP pair | E999-E1001 | bench: Colin; One shadows one class, clean (E1002) |
| **the E: fiction for estlib and RFs's named calls**: `fopen`, `wfopen`, `mkdir`, `unlink`, `RFs::SetSessionPath`, `MkDirAll`, `Modified` get the same E:-to-real-drive rewrite as RFile's opens (`gate6_fopen` and the rest) | E1005-E1007 | bench: Colin installed on C:; the Asphalts' fopen/MkDirAll pass through on E: (E1021, E1023) |
| Avkon (status pane, main-pane inset) and the picture-mode file read only on the main thread; an engine title does both in `engine_start`, before its thread exists (`status_pane_off`, `avkon_inset`, `cfg_read`) | round 161, E1031-E1046 | bench: Colin under STRICTHANDLE=2; One, the Asphalts, Ashen unchanged (E1042-E1045) |
| `RSystemAgent::RSystemAgent()` (sysagt 18) zeroes its handle, as RHandleBase's constructor does (`LOCAL_HANDLE_CTOR`) | round 161, E1035-E1036 | Colin, Colin's front end, Ashen (the titles that import it) |
| an engine's `RWsSession::EventReady` keeps the word after its one-word status (`gate6_engine_event_ready`) | round 162, E1047-E1048 | Colin (`GAME_ENGINE_LXCE`) |
| the end key's close event caught in the engine's own GetEvent (it goes to the focused group, the engine's), the main thread leaving at its next tick (`gate6_engine_get_event`, 0xE81C) | round 165, E1071-E1072 | Colin |
| an engine-mode app UI takes the end key's close event and `KAknShutOrHideApp` in HandleWsEventL and leaves, before Avkon's shut-or-hide and its shutter (`gate6_engine_wsevent`, 0xE81D) | round 164, E1069 | Colin |
| an engine-mode app UI answers EApaSystemEventShutdown (the task switcher's close) with flush-and-leave, before Avkon's shutter (`gate6_engine_sysevent`, 0xE81F) | round 163, E1066-E1067 | Colin |
| an engine-mode app UI answers EEikCmdExit (the end key) with `gate6_ui_command`: flush and leave, 0xE818 | round 162, E1049-E1050 | Colin |
| a StartL on a DSA that is already running is answered without a second Request, which wserv would panic EWservPanicDirectMisuse (`gate6_dsa_startl`, 0x57A2) | E1017-E1018 | bench: Colin; never taken on One or the Asphalts (E1020-E1023) |

## Where each title saves

All three write to `C:\System\Apps\<STEM>\`, the N-Gage convention, and
create that folder themselves; no package lists it, so an uninstall leaves
the saves behind.

| title | folder | settings | progress |
|---|---|---|---|
| Asphalt Urban GT | `c:\system\apps\6R67\` | `user.dat` | `user.dat` |
| Asphalt 2 | `c:\system\apps\6RBC\` | `user.dat` | `user.dat` |
| Ashen | `C:\System\Apps\6R21\` | `options.dat` (E445) | `savegameNN.sav` (from the image's format string; not yet seen written) |
| One | `C:\System\Apps\6R58\` | `6R58.set` (52 bytes) | `6R58.prf`, the fighter profile (12.8 KB; E584-E585 made it persist) |
| Colin McRae 2005 | `C:\system\apps\6r66\` | not identified (`configfile0.cfg` is opened beside the data, E1006, but not shown to be settings) | `cmr_save_info.dat`, `gameinfo.dat` (written at QUIT, E1000; on C: whatever the install drive, E1027) |

## Bench knobs in `gate6.cpp` that must be 0 in a shipped build

Exception: One builds 011 to 013 (rounds 136-138) ship `WORKER_LOG 1` on purpose, as a
diagnostic -- single-writer since round 136, so it cannot be the fault it was
in round 64 -- together with the `RHandleBase::Close` handle record (NOTE 707).
Both go back to their shipping state after the round.
`WORKER_LOG 1` then stayed on through One builds 014 to 020 for the sound-thread
work of rounds 139-145 (`C:\g6wrk.log`, 64 KB); build 021, the first public One
build, has it at 0 again.

`BENCH_BACKGROUND_TICK`, `BENCH_FOREGROUND_TICK`, `BENCH_DEACTIVATE_TICK`,
`BENCH_SCHED_BEATS`, `BENCH_FOCUSLOST_TICK`, `BENCH_FOCUSGAINED_TICK` (a real
focus event through `HandleWsEventL` at that hold-timer tick), `BENCH_HANG_BEAT`
(the main thread blocked 8 s at that beat, to fire the watchdog), `BENCH_MINIMIZE_TICK` /
`BENCH_RESTORE_TICK` / `BENCH_MINIMIZE_BOX` (a whole phone minimize: abort, restart into
no region or a partial one, focus lost; focus gained, the region back), `LEAVE_RAW`, `WORKER_LOG`,
`TRACE_IMPORTS`, `BENCH_DROP_UNDERFLOW` / `BENCH_ABORT_COPY_AT` (round 147's N95 stream model). `rules.py` and the regression rows (E428, E439 and the like)
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

One, round 132: `GAME_DEFER_WORKER_STOP 1` -- a Stop from any thread but
the main one is held back, never made under the game's channel mutex. One
only. Until build 016 it was made only at the stream's next Open ("the
next callback" never came: the writer stops writing and the N95's stream
never underflows), and when no sound was opened for a whole fight the owed
MaoscPlayComplete kept One's intro screen alive under the fight -- the
fight glitch, rounds 130-142. **Round 142 (build 017): the Stop hook arms
a CPeriodic made in the sound thread itself for a millisecond, and its
callback carries every owed Stop out once the thread is back in its
scheduler (0x5A0E armed, 0x5A0F done).** **Round 143 (build 018): that one-shot killed the sound thread on the bench (E746-E747: a fight nobody moves in) and on the N95; off. The game is told PlayComplete(KErrCancel) inside its own Stop instead (`FAKE_STOP_COMPLETE`, 0x5A2E), the real Stop waits for the next Open with its own complete swallowed (0x5A2F), an owed Stop of a dead thread is dropped (0x5A2D).**

One, round 133: `GAME_MDA_POSITION_LEAD_US 100000` -- the stream's Position
answered 100 ms ahead, so the writer (0x1195c, 80 ms capacity) keeps that much
more queued against the stutter; `GAME_KICK_RUNL_SLOT` / `_FN` -- the frame
object's RunL timed per beat (0x0BEF records: runs, ticks inside, ticks in the
beat). One only.

One, round 143: `GAME_TICK_SEED_LR 0x000c5d2c` / `GAME_TICK_SEED_VALUE 0x12c`
(**on**, build 018) -- the archive reader seeds its Mersenne Twister from
`User::TickCount` at construction and decodes `one.cwa` by it, so the fight
glitch was the seed landing wrong (BUGBOOK 12.w). The loader diverts the game's
TickCount import through an lr-passing thunk and answers the one read whose
return address is the seed site with a tick whose seed reads the archive whole;
every other read is real. 6/6 live on the wall-clock bench that split 6:11
(E823-E828), full fights E829-E832. A title without the two defines has it
off. The emulator-side `EKA2L1_CYCLETICK` (EMU_PATCHES.md) is the deterministic
bench the seed was found and verified on. One only.

Universal, round 144 (build 019 of One): **`TLex16::Val(TReal64&)` in FPA word
order** -- a parsed double written through a reference by the 9.x euser now
reaches the game high word first, as every other double already did
(`kFpaMath`, shape `M_LEXVAL`, hook `IMPORT_LEX16_VAL_REAL`). It is what made
every arena's ambience silent (BUGBOOK 12.aa). A fix, not a knob: on wherever
the import exists, absent and harmless where it does not.

One, round 145 (build 020): `GAME_GLOBAL_FN 0x000aae0c` / `GAME_SOUNDMGR_OFF
0x6914` / `GAME_AMBIENCE_LIST 0x58` / `GAME_AMBIENCE_ENTRY 0x64` /
`GAME_AMBIENCE_ID 0x5c` (**on**) -- the arena's REPE ambience list, whose
sample ids the port sets back to -1 at focus gained (`ambience_forget`) so the
game's own reload after a minimize resolves them again; without it the arena's
samples alone were never reloaded and the wind stayed silent (BUGBOOK 12.ab,
E837/E841). The ids stay -1 until the game's reload at the resume (E842: put
back sooner, the fix is undone). A title without the defines has it off. One only.

Ashen, round 147 (build 012): `GAME_MDA_UNDERFLOW_TICKS 32` (**on**) -- the
port tells a stream's owner thread `MaoscPlayComplete(KErrUnderflow)` once
the stream it wrote has had nothing queued for 32 ticks (500 ms), from a
timer made in that thread (`mda_underflow_watch`, 0x5A36 armed, 0x5A35 told).
Ashen's sound thread writes one buffer per copy and restarts only on that
complete, which the N95's stream never sends (BUGBOOK 12.ac). Off (0) on every
other title. Bench knobs beside it, 0 when shipping: `BENCH_DROP_UNDERFLOW`
(the emulator's own underflow swallowed, as the phone) and `BENCH_ABORT_COPY_AT`
(the Nth copy made KErrAbort) -- the model E846/E847 ran on. Also traced now
on every title: `RFile::Replace`, `Write`, `Flush`, `RFs::MkDir`, `Delete`
(gen_shim MILESTONES), and each Replace's result and drive letter (NOTE 698).

## AirPlay engine titles (`GAME_ENGINE_LXCE`; Colin McRae only)

| knob | Colin McRae | meaning | settled |
|---|---|---|---|
| `GAME_ENGINE_LAUNCHER` | 1 | the port plays the N-Gage launcher, I3D participant 2: answers the engine's mailbox (0xe4: 3 -> 4; 6 cleared), ends the process when the block reaches state 5 (QUIT), and, once the engine's group is in slot 1, the wrapper's root group declines the focus so the engine hears its keys; hold C is read from the engine's own GetEvent. Was the bench stand-in `GAME_ENGINE_FRONTEND_STUB` | E962-E971, bench |
| `GAME_ENGINE_INIT` | 0x45b0b0 | engine functions (image addresses) called once before its thread starts: Colin's sets the block interpreter's 24-bit link base | E965, bench |
| `GAME_MDA_RDEBUG` | 0 | bench: NewL, every stream call and callback through the port's MDA proxy on RDebug (any thread). Showed Colin's engine never enters the proxy | E984, diagnostic |
| `GAME_ENGINE_FRONTEND_EXE` | gate6col2.exe | what the engine's `RApaLsSession::StartApp` starts in place of the N-Gage front end `.app` | installed E962; never yet called |
| (with `GAME_ENGINE_LAUNCHER`) the hand-back | -- | back from an app switch: when the wrapper's group arrives at ordinal 0 with the focus elsewhere, the engine's group goes to the front (0x6B), and the engine's next post cancels and restarts its DSA (`dsa_kick`, "G6K") | E1009-E1015, E1019, bench |
| `GAME_BENCH_SWITCH_AT` / `_FOR` | 0 / 10 | bench: another app's group in front at AT s, the wrapper's group back FOR s later (`bench_switch`, 0x5B) | E1008-E1015, diagnostic |
| `GAME_MDA_POSITION_LEAD_US` | 150000 | the stream's Position answered 150 ms ahead, so the engine's feeder keeps that much more queued (One's knob, round 132) | round 163, E1060-E1065; a guess at the size |
| `GAME_BENCH_ENDKEY_ENGINE` | 0 | bench: `GAME_BENCH_ENDKEY_AT`'s event sent to the engine's group, where the phone's end key lands | E1071-E1072, diagnostic |
| `GAME_BENCH_ENGINE_DIES` | 0 | bench: at the engine thread's Nth GetEvent, the host's request completion (verdict in the panic number) and then G6IMP 326031, for the launcher's engine watch (0xE820) | E1077-E1080, diagnostic |
| `BENCH_COMPLETE_VIA_USER` | 0 | bench: `gate6_complete_r2` through User::RequestComplete, to test the test (326902) | E1079, diagnostic |
| `GAME_WATCHDOG_KILL_S` | Colin 12 | the watchdog for an engine title, at absolute 23; the stall dump at 4 s, again and RProcess::Kill (0x57A11) when the main thread has not moved for this many seconds | round 167, E1084-E1086 |
| `GAME_BENCH_MAIN_HANG_AT` | 0 | bench: the main thread blocks 30 s at that second, for the watchdog's dumps and kill | E1084, diagnostic |
| `GAME_CAPABILITIES` | none anywhere (Colin 0x4000 in builds 010-011, refused by the N95) | the exe header's capability set (mke32 `caps`), what the title's protected calls need from a phone's kernel; the emulator enforces none | round 168, E1091 |
| `GAME_BMU_YIELD` | Colin 1 | the engine's TBitmapUtil lock (9.1/9.2's global bitmap heap mutex) counted and let go around its sleeps, waits and leaves | round 170, E1094-E1097 |
| `GAME_BENCH_CALL_AT` / `_FN` | 0 | bench: call an engine function (image offset) on its thread at the Nth GetEvent | E1094-E1095, diagnostic |
| `GAME_BENCH_SHUTDOWN_AT` | 0 | bench: the task switcher's close event to the wrapper's group at that second (0x5B04) | E1066-E1067, diagnostic |
| `GAME_BENCH_ENDKEY_AT` | 0 | bench: Avkon's end-key close event sent to the wrapper's group at that second (0x5B03) | E1049-E1050, diagnostic |
| `GAME_BENCH_DSA_ABORT_AT` / `_KICK` | 0 / 0 | bench: a phone's DSA abort and restart on the engine's thread at that post; KICK puts the hand-back's kick between them | E1016-E1018, diagnostic |
