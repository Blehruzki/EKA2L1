// Asphalt 2: Urban GT 2 -- everything about the layer that is this game and
// not the next one. The generated half of that lives beside this file in
// `gate_imports.h` and `shim.cpp`; this is the hand-written half.
#ifndef GATE_GAME_H     // not GAME_H: that is an enum below
#define GATE_GAME_H

// The card directory and the image stem, `\system\apps\6rbc\6rbc.app`. Given
// as characters because the paths are built as u16 arrays with no runtime
// string handling to speak of -- this image has no writable data, so a name
// cannot be assembled at startup.
#define GAME_STEM_CHARS  '6','r','b','c'
#define GAME_STEM_LEN    4

// Our own application's UID, caption and installer line. Not the game's:
// the game's UID3 is in gate_imports.h as GAME_UID3, and the loader checks
// the image it is handed against it.
#define GAME_APP_UID3    0xE0001006
#define GAME_CAPTION     "Asphalt 2"

// GAME_W is what shows; GAME_PITCH is how far apart the game puts its rows.
// Measured (E133-E135): the game writes rows 176 apart and draws up to **192**
// wide, so sixteen columns of every row land on the next row's left edge. On
// an N-Gage those sixteen would be off-screen padding, which says the real
// panel pitch is 192 with 176 visible. Reading the buffer differently cannot
// undo it -- the overflow has already overwritten the next row -- so the pitch
// the game uses has to be 192, and the only number we hand it is iScreenSize.
enum { GAME_W = 176, GAME_PITCH = 192, GAME_H = 208 };

// Where the picture starts inside the game's own buffer. Sixteen, measured:
// the blit reads from source pixel sixteen, and reading from column zero
// produces a convincing fake horizontal wrap that cost a round to unpick.
enum { GAME_SRC_ORIGIN = 16 };

// Addresses inside **this** image, and worthless in any other.
//
// `IMAGE_WATCH` re-reads three words every milestone to catch the app UI's
// vtable slot being poisoned. `Z_SITES` are the two call sites that reach
// zlib's `uncompress`, and `Z_REAL` is `uncompress` itself; the hook wraps
// them to log what the game asked for and what zlib answered.
//
// E253: both were left switched on against a different game. The watch read
// 0x13c1fc out of a 690 KB image and took an access violation; the zlib hook
// wrote a patch into the middle of whatever happens to live at 0x33a74
// there, and did **not** crash, which is the more dangerous of the two.
#define GAME_IMAGE_WATCH        1
#define GAME_IMAGE_WATCH_SITES  0x0013c1fc, 0x0013c424, 0x0013c478
#define GAME_HOOK_UNCOMPRESS    1
#define GAME_Z_REAL             0x000d4f88
#define GAME_Z_SITES            0x00033a74, 0x0011562c

// Replace a null `this` on a CEikAppUi method with the real app UI.
// Asphalt 2 makes these calls and they work today. A build that is out
// with people is not the place to find out otherwise.
#define GAME_FIX_APPUI_THIS 0

// The wrapper keeps the three app UI event slots (foreground, system event,
// command) for avkon, as it always has on this title; the game's own
// overrides of them, where it has any, were never called in any round it
// shipped. Ashen needs them forwarded (E368) and turns this on.
#define GAME_UI_FORWARD_EVENTS 0

// The wrapper control stays the whole screen (0), as it has since E137; this
// title draws by direct screen access and never sizes anything from it.
#define GAME_CONTROL_W 0
#define GAME_CONTROL_H 0

// No words of the image rewritten after loading (see Ashen's game.h).
#define GAME_CODE_PATCHES { 0, 0, 0 }
#define GAME_CODE_PATCH_COUNT 0

// The environment's screen device, as the game reads it off the view
// (old iScreen, 0x3c): 1 hands it a stand-in that answers font requests by
// the N-Gage font names (E384); 0 leaves the real 9.x device there, which a
// title that passes it on to direct screen access needs (E390).
#define GAME_SCREEN_FONTS 0

// The N-Gage card's `E:\Game.Id` -- six bytes, "N-Gage", at the card's root.
// Ashen's engine init reads it (image 0x72214) and gives up when the read
// fails (round 112); a SIS installs nothing at a drive's root. 1: a failed
// read of a `\Game.Id` is answered with those six bytes, the real file first.
// Never asked for by this title: it runs on phones that have no such file.
#define GAME_ANSWER_GAME_ID 0

// Cells this big or bigger go straight back to the heap; smaller ones wait
// in the quarantine. 4 KB is where this title's use-after-free stops
// reaching, measured over builds 150 to 172.
#define GAME_FREE_BACK_FROM 4096

// The quarantine and FREE_BACK_FROM do the work here; nothing is leaked
// outright, which a ninety-second run cannot afford.
#define GAME_LEAK_ALL 0

// No cushion: this title's allocation sizes have been exercised over
// ninety-second runs on three phones and adding memory pressure to a
// shipping build to fix a fault it does not have is not a trade.
#define GAME_ALLOC_PAD 0

// Substitute rather than map on the diversions: what a phone has been
// running. See the note beside kDiverts.
#define GAME_DIVERT_MATCH 0

// The three picture numbers above are measurements, not guesses: the pitch
// over rounds 74 to 78 and again in E133 to E135, the origin from the seam
// at column 15/16. Tools render with them and say so.
#define GAME_PICTURE_MEASURED 1

// No frame dump: the stride here was measured long ago and a shipping build
// should not write 115 KB to a memory card four times.
#define GAME_DUMP_FRAME 0

// No composited dump.
#define GAME_DUMP_SCREEN 0

// Packaging: the game's own files travel in the SIS, which is what every
// build a phone has run does.
// **Off, and it should never have been on.** The keypad knob for the
// picture's horizontal position (`*`/`#`/`7`/`9`/`0`) went into build 178
// for both titles because the wrap was reported on both. It is not wanted
// here: this title's picture is right, and a knob that swallows five keys
// on a game that works can only make it worse. Build 179 is build 177 plus
// the backgrounding fix and nothing else.
#define GAME_SHIFT_PICKER 0

// The boot clock -- a tick every sixteenth traced event -- is for finding
// where Asphalt 1's thirteen seconds go. It costs a record and a kernel
// call per sixteen imports, which a working title has no reason to pay.
#define GAME_LOG_CLOCK 0

// A diagnostic, per title and off: every UseFont and DrawText through the gc
// stand-ins, with the text's descriptor raw (E385-E388). Thousands of
// records a second when on.
#define GAME_LOG_TEXT 0

#define GAME_BUNDLE_DATA 1
#define GAME_VENDOR      "DeltaCharlie"
#define GAME_INSTALL_TEXT "Asphalt 2 N-Gage version, ported to S60v3 by DeltaCharlie."

// The name every installed file carries: gate6.exe, gate6.rsc,
// gate6_reg.rsc, gate6.mbm, gate6.mif. **Do not change it for this
// title.** Three phones have it installed, and a build under another
// name installs beside the old one instead of replacing it.
#define GAME_APP_NAME "gate6"

// **The build number, written into the log as its third record.** A log
// pulled off the phone can be from any build that has run since that file
// was last overwritten -- round 71 read two build-139 logs as build 141's
// and spent the round on it. Bump this with every package that goes to the
// phone; `rules.py` checks it against the newest package in build/.
#define GAME_BUILD 193

#endif
