// One -- 6r58. The hand-written half of this title's layer; the
// generated half (`gate_imports.h`, `shim.cpp`) sits beside it, out of
// gen_shim.py. Written by newgame.py with every knob at its safe default;
// each one that a round changes gets its reason here, as the Asphalts' did.
//
// Read off the image before anything ran: EKA1/GCC98r2, 532 imports across
// 21 DLLs, UID3 0x101fd409, code 0x156ff4 bytes.
#ifndef GATE_GAME_H     // not GAME_H: that is an enum below
#define GATE_GAME_H

// `\system\apps\6r58\6r58.app`. Characters rather than a string: the
// paths are built as u16 arrays, and this image has no writable data.
#define GAME_STEM_CHARS  '6','r','5','8'
#define GAME_STEM_LEN    4

// Our own application, not the game's: the game's UID3 is GAME_UID3 in
// gate_imports.h, and the loader refuses an image that does not match it.
#define GAME_APP_UID3    0xE0001009
#define GAME_CAPTION     "One"

// **Placeholders.** Every N-Gage title so far draws 176x208; the stride is
// measured by the frame dump, not assumed (Asphalt 2's is 192, Asphalt 1's
// 176). GAME_SRC_ORIGIN was 16 on both Asphalts and took eight rounds.
enum { GAME_W = 176, GAME_PITCH = 176, GAME_H = 208 };
enum { GAME_SRC_ORIGIN = 8 };
// Four bytes a pixel: One reads the screen device's display mode (EColor16MU
// on S60v3) and draws for it, 176 to a row -- stride.py on the E491 dump
// measured 704-byte rows. Every earlier title wrote 16-bit pixels.
#define GAME_SRC_BPP 32

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

// Round 149: the game's exit idiom calls CAknAppUi::PrepareToExit (old slot 2)
// on its own app UI before CEikAppUi::Exit; the 9.x method reads a member
// the old object has not got (G6FLT 28812). Answered with nothing; see
// gate6_appui_prepare_exit.
#define GAME_PREPARE_EXIT_NOOP 1

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
// One drives the stream buffer inside its RFileReadStream by the old vtable
// slot numbers: `ldr ip, [ip, #0x28]` is old slot 8, MStreamBuf::DoSeekL
// (the inline SeekL), and on the 9.x object estor built there +0x28 is
// EABI slot 10 -- the seek never happened and the pak chunks inflated as
// garbage (E475-E477, Z_DATA_ERROR). The four sites found by scanning
// every virtual call at +0x28 in a function that also calls estor
// (0x1d5f0's chunk reader, twice; 0x45520 and 0xf824c, a seek to the end
// for the size right after Open) read EABI slot 8 instead, +0x20.
// The scan missed the fighter-part reader, 0x65374, which calls estor only
// through a helper: four seeks into the .ppd before each section (0x653dc,
// 0x65438, 0x65494, 0x6554c), every one `ldr r0, [stream]; mov r1, #1`
// then the old slot. Unseeked, the first ReadInt32L read garbage, the
// AllocL of 12 bytes a vertex asked for 17 MB, and the loader thread left
// with KErrNoMemory; the game's handler for that event leaves the same way
// on the main thread with nothing to catch it, G6FLT 34200 (E512, E513).
#define GAME_CODE_PATCHES { 0x0001d618, 0xE59CC028, 0xE59CC020 }, { 0x0001d758, 0xE59CC028, 0xE59CC020 }, \
                          { 0x00045594, 0xE59CC028, 0xE59CC020 }, { 0x000f82dc, 0xE59CC028, 0xE59CC020 }, \
                          { 0x000653dc, 0xE59CC028, 0xE59CC020 }, { 0x00065438, 0xE59CC028, 0xE59CC020 }, \
                          { 0x00065494, 0xE59CC028, 0xE59CC020 }, { 0x0006554c, 0xE59CC028, 0xE59CC020 }
#define GAME_CODE_PATCH_COUNT 8
// The pak reader's TStreamBuf subclass (0x36e70), whose overrides 9.x estor
// calls: its eleven slots moved to EABI's address point (E555-E562).
#define GAME_VTABLE_SHIFTS { 0x00152940, 11 }
#define GAME_VTABLE_SHIFT_COUNT 1

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
// too (vptr in the image). One wraps RTimer in a CActive subclass of its own and
// cancels, closes and deques it when the splash ends; dropped, the Cancel left
// the object active and the Deque's real Cancel waited forever on the closed
// timer, swallowing every key (E493 to E502).
#define GAME_CANCEL_OWN_OBJECTS 1

// 1 carries the wrapped CTimer's status and flags into the game's own object
// after After and a forwarded Cancel, not only at RunL. One tests its own
// iActive before re-arming its name-entry timer (E514-E516).
#define GAME_TIMER_MIRROR 1
// Every CTimer the game makes gets its own stand-in, kept in a table, and
// its destructor and base DoCancel go to that stand-in: One makes four, and
// the single pair the Asphalts need killed the phone at the fighter's save
// (round 125, E569).
#define GAME_MULTI_TIMER 1
// One's frame loop is an active object of its own that completes itself
// every frame (the kick at 0x26564; its class's constructor, 0x264e8, passes
// priority -1): it is moved below
// EPriorityIdle, as the port's frame timer is for the Asphalts, so it runs
// when nothing else is ready. At -1 it starved the audio stream's callbacks
// for a whole fight and left start-up objects to run at the first pause or
// minimize (round 128). {return address after `bl CActive::CActive`, priority}.
#define GAME_AO_PRIORITIES { 0x000264f8, -101 }
#define GAME_AO_PRIORITY_COUNT 1
// The audio writer (image 0x1195c) keeps its stream callback at +0xc: its
// written count and the stream's Position, every 256th poll (round 129).
#define GAME_MDA_WRITER_CB 0xc

// Round 132: the sound thread's Stop of its own stream, made under the
// game's channel mutex, never returned on the N95 while the main thread
// waited for that mutex. A Stop from any thread but main is made from the
// stream's next callback instead (gate6_mda_call2, mda_owed_stop).
#define GAME_DEFER_WORKER_STOP 1

// The frame object's RunL in its vtable (class at 0x156294, slot word at
// +0x10 holding 0x265ac), timed per beat (gate6_kick_runl).
#define GAME_KICK_RUNL_SLOT 0x001562a4
#define GAME_KICK_RUNL_FN   0x000265ac
// Round 143: the archive reader seeds its PRNG from User::TickCount at code+c5d28
// (return address c5d2c); answered with a tick whose seed reads the archive whole
// (0x12c: 7/7 live on the bench, E787-E789/E795-E798). See gate6_tick_count.
#define GAME_TICK_SEED_LR    0x000c5d2c
#define GAME_TICK_SEED_VALUE 0x12c
// Round 145: the arena's REPE ambience list, so a minimize does not lose it
// (gate6.cpp, ambience_forget). The game's TLS object comes from code+aae0c;
// the sound manager is at +0x6914 of it, its arena list at +0x58: an RArray of
// 0x64-byte entries -- TBuf<32> name, type at +0x48 (0 ONCE, 1 REPE), period
// and weight, the sample id at +0x5c (-1 until resolved), last fired at +0x60.
#define GAME_GLOBAL_FN       0x000aae0c
#define GAME_SOUNDMGR_OFF    0x6914
#define GAME_AMBIENCE_LIST   0x58
#define GAME_AMBIENCE_ENTRY  0x64
#define GAME_AMBIENCE_ID     0x5c

// Round 132: the writer keeps only 20-55 ms queued against the N95's Position;
// Position is answered this far ahead, so it keeps that much more.
#define GAME_MDA_POSITION_LEAD_US 100000

// Round 133, a candidate and OFF: One's Open package carries no rate or
// channels (E664); these would give it the ones its setup sets straight after
// (16000 Hz, mono). Not shipped: the fast first fights opened with the same
// empty package, so it does not explain the slow ones (ROUNDS E664).
#define GAME_MDA_OPEN_RATE     0
#define GAME_MDA_OPEN_CHANNELS 0

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
#define GAME_INSTALL_TEXT "One N-Gage version, ported to S60v3 by DeltaCharlie."

// The name every installed file carries; it has to differ from every other
// title's (Symbian will not let one package own another's file).
#define GAME_APP_NAME "gate6one"
// The package the game's files travel in for a split install
// (build_release.py --split): its own UID, so a loader update leaves it be.
#define GAME_DATA_UID3   0xE0001109

// The card the protection expects, as the port answers the N-Gage MMC driver
// (gate6_mmc_control): the four words of the dump's own name, `MMC-ID
// 567857f1-7d011234-b2b1879-6000400`, with the leading zeros the name drops
// (round 125, E488: Asphalt 2's words made the check decode garbage).
#define GAME_CARD_CID 0x567857f1, 0x7d011234, 0x0b2b1879, 0x06000400

// Written into the log as its third record; bump with every package.
#define GAME_BUILD 25
#endif
