// Colin McRae 2005 -- 6r66. The hand-written half of this title's layer; the
// generated half (`gate_imports.h`, `shim.cpp`) sits beside it, out of
// gen_shim.py. Written by newgame.py with every knob at its safe default;
// each one that a round changes gets its reason here, as the Asphalts' did.
//
// Read off the image before anything ran: EKA1/AirPlay LXCE, 326 imports across
// 18 DLLs, UID3 0x00000000, code 0x606600 bytes.
#ifndef GATE_GAME_H     // not GAME_H: that is an enum below
#define GATE_GAME_H

// `\system\apps\6r66\6r66.app`. Characters rather than a string: the
// paths are built as u16 arrays, and this image has no writable data.
#define GAME_STEM_CHARS  '6','r','6','6'
#define GAME_STEM_LEN    4

// Our own application, not the game's: the game's UID3 is GAME_UID3 in
// gate_imports.h, and the loader refuses an image that does not match it.
#define GAME_APP_UID3    0xE000100B
#define GAME_CAPTION     "Colin McRae 2005"

// **Placeholders.** Every N-Gage title so far draws 176x208; the stride is
// measured by the frame dump, not assumed (Asphalt 2's is 192, Asphalt 1's
// 176). GAME_SRC_ORIGIN was 16 on both Asphalts and took eight rounds.
enum { GAME_W = 176, GAME_PITCH = 176, GAME_H = 208 };
enum { GAME_SRC_ORIGIN = 16 };   // the engine writes at the ScreenInfo address + 0x20 (0x4a8224), as the N-Gage framebuffer starts
// Bytes a pixel the game writes: 16 for a title that draws as an N-Gage does,
// 32 for one that takes the display mode it is told (One). Measured, not
// assumed: GAME_DUMP_FRAME and stride.py (One: 704-byte rows, 176 pixels).
#define GAME_SRC_BPP 16

// Asphalt-specific image patches and watches: off until this title shows a
// need. See the Asphalt game.h files for what each one was for.
#define GAME_IMAGE_WATCH        0
#define GAME_IMAGE_WATCH_SITES  0
#define GAME_HOOK_UNCOMPRESS    0
#define GAME_Z_REAL             0
#define GAME_Z_SITES            0

// Hand CEikAppUi methods the real app UI in place of the game's own (E264/
// E265: eikcore reads a 9.x member the old object does not have). On for a
// title that calls CEikAppUi methods on its own app UI; harmless otherwise.
#define GAME_FIX_APPUI_THIS 1

// Forward the app UI event slots a title overrides (HandleForegroundEventL,
// HandleSystemEventL, HandleCommandL) to its own object. Ashen starts its game
// loop from the first; the Asphalts never needed it. On: a title that
// overrides none of them is unaffected (the wrapper reads its vtable).
#define GAME_UI_FORWARD_EVENTS 1

// The control's extent, and so what Rect().Size() answers: 0 is the whole
// screen. A title that sizes its frame bitmaps from its control wants the
// N-Gage's 176x208 here (Ashen, E372).
#define GAME_CONTROL_W 0
#define GAME_CONTROL_H 0

// Words of the image rewritten after loading, {offset, expected word, new
// word}: for a bug of the game's own that EKA1 forgave (Ashen reads the
// null page in its rasteriser). The expected word is checked; a mismatch
// refuses to start.
// **The engine's own SetActive reads EKA1's CActive (E885).** On EKA1
// TRequestStatus is one word and CActive::iActive the next, at +8; on EKA2
// TRequestStatus is two -- the value, then iFlags -- and +8 is the flags. A
// 9.x request that marks the status pending sets ERequestPending there, so
// the engine's inlined SetActive (0x538a0c: `if (word8) Panic("Already
// Active"); word8 = 1`) panicked on its first request. Its objects are real
// 9.x CActives on the 9.x scheduler, so it is made to do what 9.x SetActive
// does -- test and set EActive, bit 0, and keep the pending bit -- and its
// destructor's "Still Active on Destruct" check (0x538d58) tests the same
// bit. Offsets from the image base 0x400000.
// The engine calls ROM objects through their vtables at the N-Gage's GCC 2.x
// slot offsets, and 9.x's EABI vtables have moved them (E921, read out of both
// ROMs' ws32: the N-Gage CWindowGc vptr 0x505938c0, RM-409 _ZTV9CWindowGc).
// Its window gc's Activate and Deactivate at +0xd8/+0xdc are Clear() and
// Clear(TRect) on 9.x -- a Clear on a gc never activated, WSERV 9 on a phone
// and a host crash in EKA2L1 -- so they become +0x110/+0x114; BitBlt(TPoint,
// CFbsBitmap*, TRect) at +0xc4 becomes +0xe8; and the teardown's GCC 2.x
// deleting destructor (+0x08, flag 3) on its ROM objects -- the gc, both
// CFbsBitmaps, the CDirectScreenAccess, the CActiveScheduler -- becomes EABI's
// deleting destructor, +0x04 (CBase declares the destructor first, so every
// one of them has D1, D0 in its first two slots). CWsScreenDevice::CreateContext
// sits at +0x18 in both layouts and needs nothing.
// The flip's own choice (0x4a8440) is the window gc except for ten seconds
// after a key goes down (the key handler 0x4a7658 stamps 0x54b45c); it is
// made to answer "direct" always, so every frame takes the port's path: the
// engine copies it to the ScreenInfo address and says ERedraw, which the port
// posts through the engine's own CDirectScreenAccess (E931). The window-gc
// path shows only white on the bench (E922-E930), for a reason not yet found.
#define GAME_CODE_PATCHES { 0x00138a18, 0xE3530000, 0xE3130001 }, \
                          { 0x00138a28, 0xE3A03001, 0xE3833001 }, \
                          { 0x00138d70, 0xE3530000, 0xE3130001 }, \
                          { 0x000a7cb4, 0xE596C0D8, 0xE596C110 }, /* redraw: Activate */ \
                          { 0x000a83d8, 0xE596C0D8, 0xE596C110 }, /* begin draw: Activate */ \
                          { 0x000a8514, 0xE595C0D8, 0xE595C110 }, /* flip: Activate */ \
                          { 0x000a7d08, 0xE593C0DC, 0xE593C114 }, /* redraw: Deactivate */ \
                          { 0x000a842c, 0xE593C0DC, 0xE593C114 }, /* end draw: Deactivate */ \
                          { 0x000a85a4, 0xE593C0DC, 0xE593C114 }, /* flip: Deactivate */ \
                          { 0x000a86d8, 0xE59CC0C4, 0xE59CC0E8 }, /* draw: BitBlt */ \
                          { 0x000a8734, 0xE59CC0C4, 0xE59CC0E8 }, /* draw: BitBlt */ \
                          { 0x000a8760, 0xE59CC0C4, 0xE59CC0E8 }, /* draw: BitBlt */ \
                          { 0x000a71e8, 0x1593C008, 0x1593C004 }, /* delete the gc */ \
                          { 0x000a8004, 0x1593C008, 0x1593C004 }, /* delete a CFbsBitmap */ \
                          { 0x000a8028, 0x1593C008, 0x1593C004 }, /* delete a CFbsBitmap */ \
                          { 0x000a8044, 0x1593C008, 0x1593C004 }, /* delete the CDirectScreenAccess */ \
                          { 0x000a7260, 0x1593C008, 0x1593C004 }, /* delete the CActiveScheduler */ \
                          { 0x000a8440, 0xE92D4000, 0xE3A00001 }, /* the flip always direct: mov r0, #1 */ \
                          { 0x000a8444, 0xE59F3024, 0xE12FFF1E }  /*   bx lr */
#define GAME_CODE_PATCH_COUNT 19

// The environment's screen device, as the game reads it off the view
// (old iScreen, 0x3c): 1 hands it a stand-in that answers font requests by
// the N-Gage font names (E384); 0 leaves the real 9.x device there, which a
// title that passes it on to direct screen access needs (E390). Off until a title asks for system fonts by the N-Gage names.
#define GAME_SCREEN_FONTS 0

// The N-Gage card's `E:\Game.Id` -- six bytes, "N-Gage", at the card's root.
// Ashen's engine init reads it (image 0x72214) and gives up when the read
// fails (round 112); a SIS installs nothing at a drive's root. 1: a failed
// read of a `\Game.Id` is answered with those six bytes, the real file first.
// Harmless when the title never asks.
#define GAME_ANSWER_GAME_ID 1

// GCC98r2 keeps a double's high word first (the FPA order); EABI keeps it
// last. 1: every double crossing to a 9.x helper or Math function has its
// words swapped both ways (round 113: Ashen's pitch table, built in doubles,
// came out as denormals and the music as a slow staircase). A title that
// imports no double helper is untouched either way.
#define GAME_FPA_DOUBLES 1

// The picture modes, cycled by holding C (EStdKeyBackspace), for a title
// that draws through the window gc into a GAME_CONTROL_W x H control: the
// wrapper takes the whole screen, the game keeps its own size, and each
// frame's BitBlt lands at the fitted rectangle (round 114). Needs
// GAME_CONTROL_W; a title on the port's own buffer has the modes already.
#define GAME_SCREEN_MODES 1

// CActive::Cancel from the game on an object that is neither the port's
// wrapped timer nor its DSA shadow is dropped; 1 forwards it when the object
// is a 9.x one (its vptr in the ROM), which a title that makes its timers
// through CPeriodic::NewL needs: Ashen cancels its frame timer on going to
// the background and starts it again on return (round 115).
#define GAME_CANCEL_ROM_OBJECTS 1

// 1 forwards a CActive::Cancel on an active object of the game's own class
// too (vptr in the image). A title that builds its own active objects around RTimer
// needs it (One, E493 to E502); the old CActive vtable order coincides with
// EABI's, so the real Cancel reaches the game's DoCancel.
#define GAME_CANCEL_OWN_OBJECTS 1

// 1 carries the wrapped CTimer's status and flags into the game's own object
// after After and a forwarded Cancel, not only at RunL. A title that tests
// iActive on its own CTimer needs it (One, E514-E516).
#define GAME_TIMER_MIRROR 1
// A stand-in per game CTimer, not one for the last made (One, E569).
#define GAME_MULTI_TIMER 1

// The game's variadic wrappers hand `TDes16::FormatList` a GCC98r2 VA_LIST:
// a one-element array, passed as its address. The 9.x euser takes the va
// pointer itself, so handed the address it reads the game's stack as the
// arguments, and every `%s` comes out as a few glyphless characters. 1: the
// hook passes the array's element (round 116). A title that imports
// FormatList needs this; one that does not is untouched by it.
#define GAME_VA_LIST 1

// The allocator: cells of 4 KB and up go straight back, smaller ones through
// the quarantine; nothing is leaked; every allocation padded by 512 bytes,
// which is what keeps a 9.x constructor's overrun off the next cell (E277).
#define GAME_FREE_BACK_FROM 4096
#define GAME_LEAK_ALL 0
#define GAME_ALLOC_PAD 512

// Map the diversions rather than substitute (E282: a title that adds more
// than one active object needs the map).
#define GAME_DIVERT_MATCH 1

// Not measured yet: the picture mode picker stays available and nothing is
// claimed about the stride.
#define GAME_PICTURE_MEASURED 0

// Diagnostics, per title and off: the frame dump and the screen dump.
#define GAME_DUMP_FRAME 0
#define GAME_DUMP_SCREEN 0
#define GAME_SHIFT_PICKER 0
// A tick every sixteenth traced event, for the boot's shape. On for the
// first rounds.
#define GAME_LOG_CLOCK 1

// A diagnostic, per title and off: every UseFont and DrawText through the gc
// stand-ins, with the text's descriptor raw (E385-E388). Thousands of
// records a second when on.
#define GAME_LOG_TEXT 0

// Packaging: the card dump travels in the SIS beside the loader.
#define GAME_BUNDLE_DATA 1
#define GAME_VENDOR      "DeltaCharlie"
// No backslashes and no apostrophes: a packager reads this out of the header.
#define GAME_INSTALL_TEXT "Colin McRae 2005 N-Gage version, ported to S60v3 by DeltaCharlie."

// The name every installed file carries; it has to differ from every other
// title's (Symbian will not let one package own another's file).
#define GAME_APP_NAME "gate6coli"

// The card the copy protection expects, answered to the N-Gage MMC driver
// by the port: four words. A retail dump carries them in its own name
// (`MMC-ID 567857f1-7d011234-b2b1879-6000400` -> 0x567857f1, 0x7d011234,
// 0x0b2b1879, 0x06000400: restore the leading zeros). Left out, the port
// answers the Asphalt 2 crack's card (round 125).
// #define GAME_CARD_CID 0x00000000, 0x00000000, 0x00000000, 0x00000000

// Written into the log as its third record; bump with every package.
#define GAME_BUILD 11

// **An AirPlay engine, not an app** (PORTING.md, Colin McRae). The image
// is `6r66.lxe`, the engine lxce.py inflated out of `6r66.nax`: the loader
// lays it out, relocates it, fills its import directory from the shim
// and runs its entry in a thread of its own, with the 9.x application
// standing in for the card's launcher.
#define GAME_ENGINE_LXCE 1
#define GAME_ENGINE_LAUNCHER 1   // the port plays the N-Gage launcher: answers the engine's I3D mailbox, hands its window group the focus (E962-E968)
// The engine's I3D shared memory: made by the launcher on the N-Gage, and by
// the port here (engine_shared_memory). The name is in the image, after the
// engine's thread name COLIN.
// The front end's port, which the engine's StartApp starts in place of 6r66_2.app (gate6_start_frontend).
#define GAME_ENGINE_FRONTEND_EXE 'g','a','t','e','6','c','o','l','2','.','e','x','e'
// The engine's own set-up its N-Gage load never needed: 0x45b0b0 sets the
// block interpreter's link base from its limit (E964; see gate6.cpp).
#define GAME_ENGINE_INIT 0x45b0b0
#define GAME_ENGINE_SHM_CHARS 'I','3','D','_','S','H','A','R','E','D','_','M','E','M','O','R','Y','_','C','O','L','I','N'

#endif

// Bench: the audio path on RDebug -- NewL, every stream call, every callback
// (GAME_MDA_RDEBUG, gate6.cpp). The engine's thread drives its stream and its
// records never reach the file (E961). Diagnostic; 0 in a shipped build.
#define GAME_MDA_RDEBUG 0

// Round 163: the sound stutters on the N95. The engine's feeder (0x50157c)
// writes clamp(target - (written - Position), min, max) -- target 94 ms, at
// most 47 ms a write, one buffer in flight at its 40 ms timer (E1060-E1061) --
// One's writer exactly (BUGBOOK, round 132), whose 20-55 ms cushion against
// the N95's Position stuttered. Position is answered this far ahead, so the
// feeder keeps that much more queued: about 244 ms in all, inside the N95
// stream's measured ~375 ms (round 135). A guess at the size: One's 100 ms
// gave it ~180 ms; Colin's feeder refills only 1.18x faster than it plays.
#define GAME_MDA_POSITION_LEAD_US 150000

// Bench: a phone's app switch from the window server's side (gate6.cpp,
// bench_switch): another app's group in front at AT seconds, the wrapper's
// group brought back FOR seconds later. 0 in anything shipped.
#define GAME_BENCH_SWITCH_AT 0
#define GAME_BENCH_SWITCH_FOR 10
// Bench: a phone's DSA abort and restart on the engine's thread at that
// post (gate6_engine_add_event, E1016); KICK puts the launcher's kick
// between them (E1017-E1018). 0 in anything shipped.
#define GAME_BENCH_DSA_ABORT_AT 0
#define GAME_BENCH_DSA_ABORT_KICK 0
// Bench: the end key's close event to the wrapper's group at that second
// (gate6.cpp, round 162, E1049-E1050). 0 in anything shipped.
#define GAME_BENCH_ENDKEY_AT 0
// Bench: the task switcher's close (EEventUser, EApaSystemEventShutdown) to
// the wrapper's group at that second (gate6.cpp, round 163, E1066-E1067). 0 in
// anything shipped.
#define GAME_BENCH_SHUTDOWN_AT 0
// ... and 1 sends it to the engine's group, the focused one, where the
// phone's end key lands (round 165, E1071-E1072). 0 in anything shipped.
#define GAME_BENCH_ENDKEY_ENGINE 0
// Bench: at the engine thread's Nth GetEvent, the multiplayer host's request
// completion (gate6_complete_r2) and then its N95 death, G6IMP 326031, which
// the launcher's watch must turn into an exit (round 166). 0 in anything
// shipped.
#define GAME_BENCH_ENGINE_DIES 0
// Round 167: the watchdog runs (at absolute 23) and ends the process when the
// main thread has not moved for this many seconds, its stall dump written at
// 4 s and again at the end (gate6.cpp, gate6_watchdog). Build 008's host froze
// the whole N95 with the engine thread alive and the main thread stopped.
#define GAME_WATCHDOG_KILL_S 12
// Round 168: the loader's capabilities (the exe header, TCapabilitySet bits).
// LocalServices (14) is what a Bluetooth socket needs on a phone: without it
// the engine's "is Bluetooth on?" (0x449d64) got KErrPermissionDenied and
// left -- multiplayer, and a charger plugged in mid-intro, froze the N95.
// User-grantable; the emulator enforces no capability at all.
#define GAME_CAPABILITIES 0x4000
// Bench: the main thread blocks for 30 s at that second (round 167). 0 in
// anything shipped.
#define GAME_BENCH_MAIN_HANG_AT 0
