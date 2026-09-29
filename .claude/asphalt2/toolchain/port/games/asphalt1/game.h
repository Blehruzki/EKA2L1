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

// Likewise unmeasured. Asphalt 2 reads from source pixel sixteen, and taking
// that on faith for another game is the mistake that produced a convincing
// fake horizontal wrap once already.
enum { GAME_SRC_ORIGIN = 0 };

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

#endif
