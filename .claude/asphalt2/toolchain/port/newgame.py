#!/usr/bin/env python3
"""Start a new title: the folder, the data on the bench, the shim, the report.

    newgame.py <name> <card dump: .zip or directory> [--caption "..."]

A title's hand-written half is `games/<name>/game.h`; the generated half,
`shim.cpp` and `gate_imports.h`, comes out of gen_shim.py from its `.app`.
This writes the first with conservative defaults (every diagnostic and every
Asphalt-specific patch off, the data bundled), puts the card dump where the
bench and the packager look for it, generates the second, and prints what
is unanswered -- which is the port's to-do list before the first bench run.
Nothing here is a measurement: the picture, the audio and the input paths
are what the first runs are for.

    GAME=<name> ./emurun.sh "E<n>: <name> first launch"
"""
import os, re, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import e32imports

GAMES = os.path.join(HERE, 'games')
E_DRIVE = '/root/.local/share/EKA2L1/data/drives/e'
APPS = os.path.join(E_DRIVE, 'system', 'apps')
LIBS = os.path.join(E_DRIVE, 'system', 'libs')
FIRST_UID = 0xE0001006

TEMPLATE = r'''// %(caption)s -- %(stem)s. The hand-written half of this title's layer; the
// generated half (`gate_imports.h`, `shim.cpp`) sits beside it, out of
// gen_shim.py. Written by newgame.py with every knob at its safe default;
// each one that a round changes gets its reason here, as the Asphalts' did.
//
// Read off the image before anything ran: %(abi)s, %(nimports)d imports across
// %(ndlls)d DLLs, UID3 0x%(uid3)08x, code 0x%(code)x bytes.
#ifndef GATE_GAME_H     // not GAME_H: that is an enum below
#define GATE_GAME_H

// `\system\apps\%(stem)s\%(stem)s.app`. Characters rather than a string: the
// paths are built as u16 arrays, and this image has no writable data.
#define GAME_STEM_CHARS  %(stemchars)s
#define GAME_STEM_LEN    %(stemlen)d

// Our own application, not the game's: the game's UID3 is GAME_UID3 in
// gate_imports.h, and the loader refuses an image that does not match it.
#define GAME_APP_UID3    0x%(appuid)08X
#define GAME_CAPTION     "%(caption)s"

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
#define GAME_CODE_PATCHES { 0, 0, 0 }
#define GAME_CODE_PATCH_COUNT 0

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
#define GAME_INSTALL_TEXT "%(caption)s N-Gage version, ported to S60v3 by DeltaCharlie."

// The name every installed file carries; it has to differ from every other
// title's (Symbian will not let one package own another's file).
#define GAME_APP_NAME "%(appname)s"

// Written into the log as its third record; bump with every package.
#define GAME_BUILD 1
#endif
'''


def find_app(root):
    """-> (stem, apps_dir, libs_dir or None) under an extracted card dump."""
    for dirpath, dirs, files in os.walk(root):
        for f in files:
            if f.lower().endswith('.app'):
                stem = f[:-4]
                if os.path.basename(dirpath).lower() == stem.lower():
                    sysdir = os.path.dirname(os.path.dirname(dirpath))
                    libs = os.path.join(sysdir, 'libs')
                    return stem, dirpath, libs if os.path.isdir(libs) else None
    raise SystemExit('no system/apps/<stem>/<stem>.app in %s' % root)


def next_app_uid():
    used = set()
    for g in os.listdir(GAMES) if os.path.isdir(GAMES) else []:
        p = os.path.join(GAMES, g, 'game.h')
        if os.path.isfile(p):
            m = re.search(r'GAME_APP_UID3\s+(0x[0-9A-Fa-f]+)', open(p).read())
            if m:
                used.add(int(m.group(1), 16))
    u = FIRST_UID
    while u in used:
        u += 1
    return u


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    name, src = argv[0], argv[1]
    caption = None
    if '--caption' in argv:
        caption = argv[argv.index('--caption') + 1]
    if not re.match(r'^[a-z][a-z0-9]*$', name):
        raise SystemExit('name: lower-case letters and digits, it is a directory and a GAME= value')
    gdir = os.path.join(GAMES, name)
    if os.path.exists(gdir):
        raise SystemExit('%s exists already; this starts a title, it does not redo one' % gdir)

    # 1. The card dump, unpacked.
    work = src
    if zipfile.is_zipfile(src):
        work = os.path.join('/tmp', 'newgame-' + name)
        shutil.rmtree(work, ignore_errors=True)
        with zipfile.ZipFile(src) as z:
            z.extractall(work)
        if not caption:
            top = sorted(os.listdir(work))[0]
            caption = top.split(' (')[0].strip()
    stem, appsdir, libsdir = find_app(work)
    caption = caption or stem

    # 2. On the bench, where picture.py, emurun.sh and build_release.py look.
    dst = os.path.join(APPS, stem)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(appsdir, dst)
    nlibs = 0
    if libsdir:
        os.makedirs(LIBS, exist_ok=True)
        for f in os.listdir(libsdir):
            shutil.copy2(os.path.join(libsdir, f), os.path.join(LIBS, f))
            nlibs += 1
    app = os.path.join(dst, stem + '.app')
    d = open(app, 'rb').read()
    h = e32imports.header(d)
    imps = e32imports.imports(d)

    # 3. game.h.
    os.makedirs(gdir)
    appname = 'gate6' + re.sub(r'[^a-z0-9]', '', name)[:4]
    open(os.path.join(gdir, 'game.h'), 'w').write(TEMPLATE % dict(
        caption=caption, stem=stem, stemchars=','.join("'%s'" % c for c in stem),
        stemlen=len(stem), appuid=next_app_uid(), appname=appname,
        abi=('EKA1/' if h['eka1'] else 'EKA2/') + h['abi'],
        nimports=sum(len(o) for _n, o in imps), ndlls=len(imps),
        uid3=h['uid3'], code=h['code_size']))

    # 4. The generated half, and its report.
    print('%s: %s (stem %s), %d files to %s, %d DLLs to %s' % (
        name, caption, stem, len(os.listdir(dst)), dst, nlibs, LIBS))
    print('   image: %s, %d imports across %d DLLs, code 0x%x' % (
        ('EKA1/' if h['eka1'] else 'EKA2/') + h['abi'], sum(len(o) for _n, o in imps), len(imps), h['code_size']))
    print('   %s written: app UID 0x%08X, app name %s' % (os.path.join(gdir, 'game.h'), next_app_uid() - 1, appname))
    r = subprocess.run([sys.executable, os.path.join(HERE, 'gen_shim.py'), app, os.path.join(gdir, 'shim.cpp')],
                       capture_output=True, text=True)
    print(r.stdout.rstrip())
    if r.returncode:
        print(r.stderr); return 1
    hp = os.path.join(gdir, 'gate_imports.h')
    absent = [l.split()[0] for l in open(hp) if 'absent' in l and l.strip().startswith('IMPORT_')]
    print('   hooks this title has no import for (%d): %s' % (len(absent), ' '.join(absent)))
    print()
    print('next:  GAME=%s ./emurun.sh "E<n>: %s first launch"' % (name, caption))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
