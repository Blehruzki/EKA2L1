// Asphalt: Urban GT -- the first one, 6r67. Everything about the layer that
// is this game and not another. The generated half lives beside this file in
// `gate_imports.h` and `shim.cpp`; this is the hand-written half.
//
// What is known about it before anything has run (read straight off the
// image): EKA1/GCC98r2, 391 imports across 21 DLLs of which gen_shim answers
// 370, UID3 0x101fd3fc, an 8 KB stack, no writable data, and the **same
// display architecture as Asphalt 2** -- bitgdi 105/111/137 and ws32 348/350,
// which is `SetAutoUpdate`, `SetClippingRegion`, `Update(const TRegion &)`,
// `CDirectScreenAccess::NewL` and `StartL`. Of the N-Gage-only libraries it
// needs only gamecomms and nokiafc, both already stubbed, and it asks zlib
// for the same `uncompress` that Asphalt 2 crashes inside.
#ifndef GATE_GAME_H     // not GAME_H: that is an enum below
#define GATE_GAME_H

// `\system\apps\6r67\6r67.app`. Characters rather than a string because the
// paths are built as u16 arrays: this image has no writable data, so a name
// cannot be assembled at startup.
#define GAME_STEM_CHARS  '6','r','6','7'
#define GAME_STEM_LEN    4

// Our own application, not the game's. The game's own UID3 is GAME_UID3 in
// gate_imports.h, and the loader refuses an image that does not match it.
#define GAME_APP_UID3    0xE0001007
#define GAME_CAPTION     "Asphalt Urban GT"

// **Placeholders, not measurements.** Asphalt 2's pitch of 192 against a
// width of 176 took rounds E133 to E135 to establish, and nothing yet says
// this game does the same. 176 square is the honest starting guess: if it is
// wrong the picture will shear, which is exactly what the stride probes are
// for. Phase 2 measures these; until then no claim is made about them.
enum { GAME_W = 176, GAME_PITCH = 176, GAME_H = 208 };

// **Sixteen, the same as Asphalt 2 -- and the reason it took eight rounds
// to say so is worth more than the number.**
//
// The two titles differ in exactly one thing that the blit reads. Neither
// GAME_PITCH nor GAME_W differs at runtime: `TELL_GAME_ITS_SIZE` is 0, so
// both games run at a source pitch of GAME_W = 176 and `GAME_PITCH` is
// used only by the offline tools. The whole difference is this constant,
// and Asphalt 2 has had 16 in it since its own display was settled.
//
// The arithmetic closes it. The person turning the keypad knob on the
// phone converged on a destination shift of **-22**, and the destination
// is the 176-wide source stretched to 240:
//
//     16 source columns * 240 / 176  =  21.818...  ->  22
//
// So the knob was compensating, on the destination side and to the nearest
// whole pixel, for exactly the sixteen source columns this constant was
// supposed to skip. That also accounts for the pixel of misalignment that
// survived the knob: 22 is not 21.818, and a rotation of the output can
// never be exactly a shift of the input.
//
// E288 measured 0 here, off a dumped frame's non-zero column extent
// (0..175). That measurement cannot answer this question: it says where
// data is, not where the picture begins, and sixteen columns of anything
// non-zero read the same as sixteen columns of picture. The first game had
// already answered it and the second was allowed to disagree on weaker
// evidence.
enum { GAME_SRC_ORIGIN = 16 };

// Nothing here has been measured in this image, so nothing is switched on.
// The watch needs three offsets that hold the app UI's vtable slot, and the
// zlib hook needs the two call sites that reach `uncompress` plus the
// address of `uncompress` itself. Asphalt 2's numbers are not these numbers:
// E253 read one of them past the end of this 690 KB image and faulted, and
// wrote the other into the middle of some unrelated function without
// faulting at all. The sites are a single 0 so the arrays stay well formed.
#define GAME_IMAGE_WATCH        0
#define GAME_IMAGE_WATCH_SITES  0
#define GAME_HOOK_UNCOMPRESS    0
#define GAME_Z_REAL             0
#define GAME_Z_SITES            0

// Hand CEikAppUi methods the real app UI in place of the game's own.
// E264 measured it: the game calls CEikAppUi::ApplicationRect() with
// `this` = its own GCC98r2 app UI, and eikcore reads a member at +0x48
// that is not there. E265 swapped the pointer and the fault went away.
#define GAME_FIX_APPUI_THIS 1

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

// GCC98r2 keeps a double's high word first (the FPA order); EABI keeps it
// last. 1: every double crossing to a 9.x helper or Math function has its
// words swapped both ways (round 113: Ashen's pitch table, built in doubles,
// came out as denormals and the music as a slow staircase). A title that
// imports no double helper is untouched either way.
// 0 here until a round of its own: this title imports __adddf3, __muldf3,
// __floatsidf, __extendsfdf2 and __truncdfsf2, so the swap would change
// what it computes with them, and it has shipped and run on hardware as it is.
#define GAME_FPA_DOUBLES 0

// The picture modes, cycled by holding C (EStdKeyBackspace), for a title
// that draws through the window gc into a GAME_CONTROL_W x H control: the
// wrapper takes the whole screen, the game keeps its own size, and each
// frame's BitBlt lands at the fitted rectangle (round 114). Needs
// GAME_CONTROL_W; a title on the port's own buffer has the modes already.
#define GAME_SCREEN_MODES 0

// Quarantine the small cells only, as Asphalt 2 does. This was
// 0x7fffffff -- hold everything -- from E271, when a vtable pointer written
// over a freed cell's `next` link was sending the allocator into the ROM.
// **E277 found what was doing it** (the 9.x `CEikDialog` constructor
// overrunning a cell sized for 7.0s) and fixed it with GAME_ALLOC_PAD below;
// the quarantine was a stand-in for that fix and simply never came back off.
// Round 92 is the bill: 34,142 frames with nothing ever handed back, and a
// fault at the `User::AllocL` that finally had nowhere to go.
#define GAME_FREE_BACK_FROM 4096

// Off. E274 turned it on to tell a free of the game's from a free of the
// framework's while boot was being debugged, and said in the same breath
// that it is not a shipping setting. It shipped anyway, in builds 003 to
// 005, and round 92's `G6FLT 25100` is it: a heap that only grows, with
// GAME_ALLOC_PAD on top of every cell, for half an hour of play.
#define GAME_LEAK_ALL 0

// A cushion on every allocation the game makes. E277: it allocates a
// CAknNoteWrapper at its 7.0s size and the 9.x CEikDialog constructor
// writes past the end of the cell, onto the next cell's free-list link.
#define GAME_ALLOC_PAD 512

// Map rather than substitute on the diversions. E282: this game adds more
// than one active object to the scheduler, and substituting made every Add
// an add of the timer -- E32USER-CBase 41, EReqAlreadyAdded.
#define GAME_DIVERT_MATCH 1

// **Measured, E288**, off four raw frames of the game's own buffer. The
// stride from the spacing of the zero runs the game leaves at the end of
// each drawn row -- 352 bytes, exactly -- and the origin from the non-zero
// column extent, which is 0 to 175, so there is no left margin to skip.
// The wrap signature is 4 to 11 per cent for K of 8 to 24, which is noise.
#define GAME_PICTURE_MEASURED 1

// Off again, and the round it was on for answered the question it was
// asked. Build 006's dump has the phone's own frame buffer holding a clean
// 240x320 picture in columns 0 to 239 of a 320-pixel line, and the game's
// own 176x208 source clean behind it -- while the panel showed that same
// picture wrapped. So the wrap is not in what we draw or in what we write,
// and no further dump can say more about it; it is a scan-out question,
// and build 007 asks it with a knob instead (SHIFT_PICKER in gate6.cpp).
//
// Set it to a frame number to drop that frame's raw buffer, and the ones
// at +17, +34 and +51, into C:\g6code-<stem>.bin; with GAME_DUMP_SCREEN
// the composited buffer comes too. It costs about 750 KB on C:.
#define GAME_DUMP_FRAME 0

// The composited framebuffer too, at GAME_DUMP_FRAME and +34: what the panel
// actually shows, with its geometry in a descriptor in front of it. Needs
// GAME_DUMP_FRAME to be non-zero as well.
#define GAME_DUMP_SCREEN 1

// Packaging. GAME_BUNDLE_DATA puts the game's own files in the SIS beside
// the loader; without it the installer carries the loader alone and the
// data is copied to the phone by hand. Loader-only through build 021;
// bundled from build 022, the same shape as Asphalt 2's package, now that
// the port is finished and the 20 MB Asphalt 2 installer has shown the
// size is no obstacle.
// **Off, and gone for good.** The knob existed to find the horizontal
// offset by hand, and round 97 found it properly instead: the offset was
// GAME_SRC_ORIGIN, 16, the same constant Asphalt 2 has always had. The
// knob's -22 was that 16 scaled to the destination and rounded, which is
// why it never quite landed. With the origin right there is nothing to
// nudge, and five keys go back to the game.
#define GAME_SHIFT_PICKER 0

// A tick every sixteenth traced event, to find where the thirteen-second
// boot goes. Round 92 blamed the leak settings and round 93 disproved it
// with the same boot time and the leak off, so the record has to say more
// than one reading at each end of it. Off again once it has.
#define GAME_LOG_CLOCK 1

// A diagnostic, per title and off: every UseFont and DrawText through the gc
// stand-ins, with the text's descriptor raw (E385-E388). Thousands of
// records a second when on.
#define GAME_LOG_TEXT 0

#define GAME_BUNDLE_DATA 1
#define GAME_VENDOR      "DeltaCharlie"
// No backslashes and no apostrophes in the text: it lives in a C header
// and is read out of it by a packager that does not run a C compiler, so
// an escape here would reach the phone as an escape.
#define GAME_INSTALL_TEXT "Asphalt Urban GT N-Gage version, ported to S60v3 by DeltaCharlie."

// The name every installed file carries. It has to differ from every
// other title's: Symbian will not let one package write a file another
// package owns, and the installer answers "Update error". The package
// UIDs already differed (0xE0001007 against 0xE0001006) and that is not
// the thing it checks -- the file names are.
#define GAME_APP_NAME "gate6a1"

// **The build number, written into the log as its third record.** A log
// pulled off the phone can be from any build that has run since that file
// was last overwritten -- round 71 read two build-139 logs as build 141's
// and spent the round on it. Bump this with every package that goes to the
// phone; `rules.py` checks it against the newest package in build/.
#define GAME_BUILD 27

#endif
