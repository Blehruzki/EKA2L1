#!/usr/bin/env python3
"""build_release.py -- the installer, for whichever title is asked for.

One SIS carrying the port, the original N-Gage icon, and -- when that title
says so -- the game's own files as well. Everything about *why* it can go on
either drive is in gate6.cpp beside `kLayoutApps`; the short of it is that
the game is told it is on `E:` whatever drive it is really on, and the file
calls translate.

    build_release.py [out dir] [--game <name>] [game tree]

Nothing here is written down per title any more: the caption, the vendor,
the install text, the four characters the files live under and whether the
data travels in the package all come out of `games/<name>/game.h`.

A **loader-only** package -- `GAME_BUNDLE_DATA 0` -- carries the port and the
icon and nothing else, and expects the game's own files to have been copied
to `\\system\\apps\\<stem>` by hand. The loader reads a hand-copied dump as
happily as an installed one: it looks for `<stem>.bin` first, because that is
how an installer has to carry an E32 image, and falls back to the plain
`<stem>.app` that a card dump has.

The game tree defaults to the emulator's E: drive, which is where the
unpacked N-Gage files live on the bench. It is still needed for a
loader-only build, because that is where the icon comes from.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

import buildapp
import mksis
import mkmbm
import build_gate6            # for the import list, so the two cannot drift
import picture                # for games/<name>/game.h, and its refusal to guess

DEFAULT_GAME = 'asphalt2'
GAMES_ROOT = '/root/.local/share/EKA2L1/data/drives/e/system/apps'

# Back on for build 171, and innocent all along. It was pulled for build 168
# on one argument -- it is the last entry, so the installer reaches it at the
# end, and the failure was at the end -- and round 81's probe 2 carries a
# text entry and installs. `buildapp` now puts it **first**, where the
# installer shows it before it copies anything, which is where it belongs
# and where it would have been visible in build 167 had it been the cause.
WITH_INSTALL_TEXT = True

# Just the 44x44 colour bitmap; `mkmbm` puts a mask of its own after it. The
# AIF also holds a 42x29 pair, which is the N-Gage's own list icon and not a
# shape S60 asks for -- shipping both would give the shell a size to choose
# wrongly between.
ICON_BITMAPS = (0,)


# One spelling of the stem, in picture.py, which the log readers use too.
stem = picture.stem


# `<stem>.app` is an E32 executable image, and Symbian will not install one
# anywhere but `\sys\bin` -- round 81's probe 4 is that one file on its own
# and the phone refuses it.
#
# Renaming is not enough. Round 82: probe 5 is the same bytes under another
# name and the phone still refuses it, probe 6 is the same bytes with the
# first 32 XORed and the phone takes it. The check reads the file, which is
# the sensible design -- a rename would have made the rule a formality.
#
# So the image travels as `<stem>.bin` with its header XORed, and the loader
# XORs it back after reading. Thirty-two bytes covers the UID triple, the
# checksum and the 'EPOC' signature. `IMAGE_SCRAMBLE` in gate6.cpp is the
# same constant and the two have to agree.
#
# None of this applies to a loader-only package: nothing is installed, so
# nothing is checked, and the files keep the names the card dump gave them.
SCRAMBLE_KEY, SCRAMBLE_BYTES = 0xA5, 32


def game_files(root, scratch, target_dir, rename, scramble):
    """-> [(local path, install target)], every file in the tree.

    Sorted, so two builds of the same tree produce the same package and a
    diff between them means something.
    """
    out = []
    for dirpath, dirnames, names in os.walk(root):
        dirnames.sort()
        rel = os.path.relpath(dirpath, root)
        for n in sorted(names):
            local = os.path.join(dirpath, n)
            sub = '' if rel == '.' else '\\' + rel.replace(os.sep, '\\')
            if n in scramble:
                key, count = scramble[n]
                src = open(local, 'rb').read()
                local = os.path.join(scratch, rename.get(n, n))
                open(local, 'wb').write(
                    bytes(b ^ key for b in src[:count]) + src[count:])
            out.append((local, target_dir + sub + '\\' + rename.get(n, n)))
    return out


def engine_image(game, tree, scratch, target_dir, have):
    """-> [(local, target)]: an AirPlay engine title's `<stem>.lxe`, if the
    tree does not already carry it.

    The loader runs the engine from `<stem>.lxe`, the image lxce.py inflates
    out of the game's own `<stem>.nax` (gate6.cpp, beside kLayoutApps). A card
    dump has the `.nax` and no `.lxe`; the bench tree had one only because a
    bench run left it there. So the package makes its own, and a `.lxe` that
    is already in the tree must be that same inflation -- a stale one would
    ship an engine the code patches were not measured against.
    """
    if picture.setting(game, 'GAME_ENGINE_LXCE') not in ('1',):
        return []
    import lxce
    st = stem(game)
    nax = os.path.join(tree, st + '.nax')
    if not os.path.exists(nax):
        sys.exit('%s: an engine title with no %s.nax in %s' % (game, st, tree))
    raw = lxce.unpack(nax)
    lxe = os.path.join(tree, st + '.lxe')
    if (st + '.lxe') in have:
        if open(lxe, 'rb').read() != raw:
            sys.exit('%s: %s is not the inflation of %s.nax -- remove it or regenerate it'
                     % (game, lxe, st))
        return []
    local = os.path.join(scratch, st + '.lxe')
    open(local, 'wb').write(raw)
    print('engine image: %s.lxe inflated from %s.nax (%d bytes)' % (st, st, len(raw)))
    return [(local, target_dir + '\\' + st + '.lxe')]


def data_package(out, game, tree=None):
    """The game's own files in a package of their own, for a split install.

    A loader update on a phone is then a few hundred kilobytes instead of the
    whole card. The data package carries its own UID, GAME_DATA_UID3, so
    installing a new loader never touches it, and the files go where the
    bundled package would put them: `!:\\system\\apps\\<stem>`, the image as
    the scrambled `<stem>.bin`. The loader finds them the same way.
    """
    os.makedirs(out, exist_ok=True)
    st = stem(game)
    tree = tree or os.path.join(GAMES_ROOT, picture.folder(game))
    caption = picture.setting(game, 'GAME_CAPTION')
    vendor = picture.setting(game, 'GAME_VENDOR')
    uid = picture.setting(game, 'GAME_DATA_UID3')
    if not uid:
        sys.exit('games/%s/game.h has no GAME_DATA_UID3: a split install needs one' % game)
    target_dir = '!:\\system\\apps\\' + picture.folder(game)
    extra = game_files(tree, out, target_dir,
                       {st + '.app': st + '.bin'},
                       {st + '.app': (SCRAMBLE_KEY, SCRAMBLE_BYTES)})
    extra += engine_image(game, tree, out, target_dir, set(os.listdir(tree)))
    name = picture.setting(game, 'GAME_APP_NAME') or 'gate6'
    dst = os.path.join(out, name + '_data.sis')
    mksis.build(dst, int(uid, 0), caption + ' data', vendor, extra)
    total = sum(os.path.getsize(s) for s, _t in extra)
    print('%s data: %d files, %.1f MB -> %s (%.1f MB)'
          % (caption, len(extra), total / 1e6, dst, os.path.getsize(dst) / 1e6))


def main(out='.', game=DEFAULT_GAME, tree=None, loader_only=False):
    os.makedirs(out, exist_ok=True)
    st = stem(game)
    tree = tree or os.path.join(GAMES_ROOT, picture.folder(game))
    caption = picture.setting(game, 'GAME_CAPTION')
    vendor = picture.setting(game, 'GAME_VENDOR')
    text = picture.setting(game, 'GAME_INSTALL_TEXT')
    bundle = int(picture.setting(game, 'GAME_BUNDLE_DATA') or 0) and not loader_only
    target_dir = '!:\\system\\apps\\' + picture.folder(game)

    icon = os.path.join(out, 'gate6.mbm')
    got = mkmbm.build(os.path.join(tree, st + '.aif'), icon, list(ICON_BITMAPS))
    print('%s (%s, stem %s): icon %s'
          % (caption, game, st, ', '.join(mkmbm.describe(f) for f in got)))

    if bundle:
        extra = game_files(tree, out, target_dir,
                           {st + '.app': st + '.bin'},
                           {st + '.app': (SCRAMBLE_KEY, SCRAMBLE_BYTES)})
        extra += engine_image(game, tree, out, target_dir, set(os.listdir(tree)))
        total = sum(os.path.getsize(s) for s, _t in extra)
        print('game files: %d, %.1f MB' % (len(extra), total / 1e6))
        for _local, target in extra:
            if target.lower().endswith('.bin'):
                print('renamed and scrambled: %s' % target)
    else:
        extra = []
        print('loader only: the package carries no game data. The title\'s own '
              'files go in %s, copied by hand.'
              % target_dir.replace('!:', '<drive>:'))

    build_gate6.build(out, caption=caption, icon=icon, extra=extra,
                      install_text=text if WITH_INSTALL_TEXT else None,
                      vendor=vendor, game=game)


if __name__ == '__main__':
    # --split: two packages, the loader alone and the data alone; a hardware
    # round then reinstalls only the first (One, round 125).
    args = sys.argv[1:]
    split = '--split' in args
    args = [a for a in args if a != '--split']
    g, rest = picture.take_game_arg(args)
    out = rest[0] if rest else '.'
    tree = rest[1] if len(rest) > 1 else None
    main(out, g or DEFAULT_GAME, tree, loader_only=split)
    if split:
        data_package(out, g or DEFAULT_GAME, tree)
