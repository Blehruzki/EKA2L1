// Ashen -- 6r21. The hand-written half of this title's layer; the
// generated half (`gate_imports.h`, `shim.cpp`) sits beside it, out of
// gen_shim.py. Written by newgame.py with every knob at its safe default;
// each one that a round changes gets its reason here, as the Asphalts' did.
//
// Read off the image before anything ran: EKA1/GCC98r2, 354 imports across
// 20 DLLs, UID3 0x101fd3e9, code 0xf2bfc bytes.
#ifndef GATE_GAME_H     // not GAME_H: that is an enum below
#define GATE_GAME_H

// `\system\apps\6r21\6r21.app`. Characters rather than a string: the
// paths are built as u16 arrays, and this image has no writable data.
#define GAME_STEM_CHARS  '6','r','2','1'
#define GAME_STEM_LEN    4

// Our own application, not the game's: the game's UID3 is GAME_UID3 in
// gate_imports.h, and the loader refuses an image that does not match it.
#define GAME_APP_UID3    0xE0001008
#define GAME_CAPTION     "Ashen"

// **Placeholders.** Every N-Gage title so far draws 176x208; the stride is
// measured by the frame dump, not assumed (Asphalt 2's is 192, Asphalt 1's
// 176). GAME_SRC_ORIGIN was 16 on both Asphalts and took eight rounds.
enum { GAME_W = 176, GAME_PITCH = 176, GAME_H = 208 };
enum { GAME_SRC_ORIGIN = 0 };

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

// The game's own app UI overrides HandleForegroundEventL, HandleSystemEventL
// and HandleCommandL (old slots 7, 9, 17) and starts its game loop from the
// first (E368); the wrapper forwards those three slots to it.
#define GAME_UI_FORWARD_EVENTS 1

// The control's extent, and so Rect().Size(), which is what this title sizes
// its frame bitmaps from (0xb5cfc/0xb5d38 -> CFbsBitmap::Create): the
// N-Gage's 176x208, at the top left. 0 would be the whole screen (E372: a
// 240-wide bitmap rendered at a 176 pitch, a sheared picture).
#define GAME_CONTROL_W 176
#define GAME_CONTROL_H 208

// Words of the image rewritten after loading: {offset, expected, new}. The
// rasteriser's first edge setup at 0x8e1dc clamps the reciprocal-table
// *pointer* to 0x1000 instead of the index (its twin at 0x8e5bc clamps the
// index, sb): `cmp sb,#0x1000; movgt sb,#0x1000` with the table in sb and
// the index in r8, so it read the first page of the address space, which an
// N-Gage maps and EKA2 does not (E372: KERN-EXEC 3 at 0x1010 the moment the
// attract mode drew its first span taller than two rows). Clamp r8 instead.
#define GAME_CODE_PATCHES { 0x0008e1dc, 0xE3590A01, 0xE3580A01 }, { 0x0008e1e0, 0xC3A09A01, 0xC3A08A01 }
#define GAME_CODE_PATCH_COUNT 2

// The environment's screen device, as the game reads it off the view
// (old iScreen, 0x3c): 1 hands it a stand-in that answers font requests by
// the N-Gage font names (E384); 0 leaves the real 9.x device there, which a
// title that passes it on to direct screen access needs (E390).
#define GAME_SCREEN_FONTS 1

// The N-Gage card's `E:\Game.Id` -- six bytes, "N-Gage", at the card's root.
// Ashen's engine init reads it (image 0x72214) and gives up when the read
// fails (round 112); a SIS installs nothing at a drive's root. 1: a failed
// read of a `\Game.Id` is answered with those six bytes, the real file first.
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
#define GAME_INSTALL_TEXT "Ashen N-Gage version, ported to S60v3 by DeltaCharlie."

// The name every installed file carries; it has to differ from every other
// title's (Symbian will not let one package own another's file).
#define GAME_APP_NAME "gate6ashe"

// Written into the log as its third record; bump with every package.
#define GAME_BUILD 9
#endif
