#!/usr/bin/env python3
"""build.py -- an N-Gage card dump in, an S60v3 package out.

    python build.py <card dump: folder or .zip> [--name <title>] [--out <dir>]
                    [--loader-only]

What it does, in order -- each step is one of the toolchain's own scripts in
port/, which you can also run by hand:

1. Finds the game on the card: system/apps/<stem>/<stem>.app.
2. Picks the title: --name, or the title in port/games/ whose card is this
   one (same stem and card ID), or a new one named after the dump.
3. A new title: newgame.py writes port/games/<name>/game.h with safe
   defaults and generates its shim (shim.cpp, gate_imports.h) from the
   game's own import table, and prints what it could not answer.
4. Copies the card's files into the kit's own card folder
   (port/kitdata/e/system/apps/<stem>), names lower-cased as the packager
   expects. A phone's FAT is case-blind; your disk may not be.
5. The card ID: if the dump's name carries one (`MMC-ID b1e97d1-d8011855-
   b37d817-6000400`), it goes into game.h as GAME_CARD_CID -- the four words
   the copy protection asks the N-Gage card driver for. Without one the port
   answers a default card, which a game checking its own card will refuse.
6. Builds with build_release.py: the loader compiled with clang for ARMv5,
   linked with ld.lld, wrapped as an S60v3 executable, and packaged with the
   game's files into one .sis (or the loader alone with --loader-only).

Needs Python 3.8+ and LLVM's clang, ld.lld, llvm-objcopy, llvm-nm and
llvm-readelf on the PATH (README.md). No Symbian SDK.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, 'port')
sys.path.insert(0, PORT)

TOOLS = ('clang', 'ld.lld', 'llvm-objcopy', 'llvm-nm', 'llvm-readelf')
MMC_ID = re.compile(r'MMC-ID\s+([0-9a-fA-F]+)-([0-9a-fA-F]+)-([0-9a-fA-F]+)-([0-9a-fA-F]+)')


def fail(msg):
    sys.exit('build.py: ' + msg)


def check_tools():
    missing = [t for t in TOOLS if not shutil.which(t)]
    if missing:
        fail('not on the PATH: %s. Install LLVM (README.md, "What you need") and '
             'open a new terminal.' % ', '.join(missing))


def unpack(src):
    """-> (a directory holding the card, the dump's own name). The dump's name
    is the dumper's folder name -- `Glimmerati (v04215-...) (MMC-ID ...)` --
    which carries the title and the card ID; for a .zip, its one top folder
    (or the .zip's own name if it has none)."""
    if os.path.isdir(src):
        return src, os.path.basename(os.path.abspath(src))
    if zipfile.is_zipfile(src):
        work = os.path.join(tempfile.gettempdir(), 'ngport-card')
        shutil.rmtree(work, ignore_errors=True)
        with zipfile.ZipFile(src) as z:
            z.extractall(work)
        tops = [t for t in os.listdir(work) if os.path.isdir(os.path.join(work, t))]
        if len(tops) == 1:
            return work, tops[0]
        return work, os.path.splitext(os.path.basename(src))[0]
    fail('%s is neither a folder nor a .zip' % src)


def card_id(text):
    m = MMC_ID.search(text)
    return tuple(int(g, 16) for g in m.groups()) if m else None


def title_cid(game):
    path = os.path.join(PORT, 'games', game, 'game.h')
    m = re.search(r'^#define\s+GAME_CARD_CID\s+(.*)$', open(path).read(), re.M)
    return tuple(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', m.group(1))) if m else None


def pick_title(stem, cid, name):
    import picture
    if name:
        return name
    for g in picture.games():
        try:
            if picture.stem(g).lower() == stem and (cid is None or title_cid(g) == cid):
                return g
        except SystemExit:
            continue
    return None


def set_cid(game, cid):
    path = os.path.join(PORT, 'games', game, 'game.h')
    text = open(path).read()
    line = '#define GAME_CARD_CID 0x%08x, 0x%08x, 0x%08x, 0x%08x' % cid
    if re.search(r'^#define\s+GAME_CARD_CID\s', text, re.M):
        text = re.sub(r'^#define\s+GAME_CARD_CID\s.*$', line, text, flags=re.M)
    elif '// #define GAME_CARD_CID' in text:
        text = re.sub(r'^// #define GAME_CARD_CID.*$', line, text, flags=re.M)
    else:
        text = text.replace('#endif', line + '\n#endif', 1)
    open(path, 'w').write(text)


def copy_card(appsdir, stem):
    import paths
    dst = os.path.join(paths.APPS, stem)
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst)
    for f in os.listdir(appsdir):
        s = os.path.join(appsdir, f)
        if os.path.isfile(s):
            shutil.copy2(s, os.path.join(dst, f.lower()))
    return dst


def main(argv):
    args = list(argv)
    def opt(flag):
        if flag in args:
            i = args.index(flag)
            v = args[i + 1]
            del args[i:i + 2]
            return v
        return None
    name, out = opt('--name'), opt('--out') or os.path.join(HERE, 'out')
    loader_only = '--loader-only' in args
    args = [a for a in args if a != '--loader-only']
    if len(args) != 1:
        sys.exit(__doc__)

    check_tools()
    import newgame
    card, dump_name = unpack(args[0])
    stem, appsdir, _libs, _app = newgame.find_app(card)
    cid = card_id(dump_name) or card_id(os.path.basename(args[0]))
    caption = dump_name.split(' (')[0].strip() or stem
    print('card: stem %s, card ID %s' % (stem, ' '.join('%08x' % w for w in cid) if cid else 'none in the name'))

    game = pick_title(stem, cid, name)
    if not game or not os.path.isdir(os.path.join(PORT, 'games', game)):
        game = game or re.sub(r'[^a-z0-9]', '', caption.lower()) or 'title'
        if not re.match(r'^[a-z]', game):
            game = 't' + game
        print('new title %r (%s): newgame.py' % (game, caption))
        r = subprocess.run([sys.executable, os.path.join(PORT, 'newgame.py'), game, card,
                            '--caption', caption], cwd=PORT)
        if r.returncode:
            fail('newgame.py failed')
    else:
        print('title %r (port/games/%s)' % (game, game))
    if cid:
        set_cid(game, cid)
        print('GAME_CARD_CID set in port/games/%s/game.h' % game)
    else:
        print('warning: no MMC-ID in the dump\'s name; the port will answer its default card')

    tree = copy_card(appsdir, stem)
    cmd = [sys.executable, os.path.join(PORT, 'build_release.py'), out, '--game', game, tree]
    if loader_only:
        cmd.append('--split')
    r = subprocess.run(cmd, cwd=PORT)
    if r.returncode:
        fail('build_release.py failed')
    sis = [f for f in os.listdir(out) if f.endswith('.sis')]
    print('\ndone: %s' % ', '.join(os.path.join(out, f) for f in sorted(sis)))


if __name__ == '__main__':
    # Windows opens text files in the local code page unless told otherwise;
    # the scripts and their files are UTF-8. Run in Python's UTF-8 mode, and
    # every script build.py starts inherits it.
    if os.environ.get('PYTHONUTF8') != '1':
        os.environ['PYTHONUTF8'] = '1'
        sys.exit(subprocess.call([sys.executable] + sys.argv))
    main(sys.argv[1:])
