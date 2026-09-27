#!/usr/bin/env python3
"""build_release.py -- the standalone Asphalt 2 installer.

One SIS carrying the port, the game's own files, and the original N-Gage
icon, installable to phone memory or a card. Everything about *why* it can go
on either drive is in gate6.cpp beside `kLayoutApps`; the short of it is that
the game is told it is on `E:` whatever drive it is really on, and the file
calls translate.

    build_release.py [out dir] [game tree]

The game tree defaults to the emulator's E: drive, which is where the
unpacked N-Gage files live on the bench.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir))

import buildapp
import mkmbm
import build_gate6            # for the import list, so the two cannot drift

GAME = '/root/.local/share/EKA2L1/data/drives/e/system/apps/6rbc'
TARGET_DIR = '!:\\system\\apps\\6rbc'
CAPTION = 'Asphalt 2'
VENDOR = 'DeltaCharlie'
# Off for build 168. The phone refused build 167 at nine tenths of the
# progress bar, on both drives, and this is the newest thing in the package
# and the only one that acts at the *end* of an install -- it is the last
# entry, so the installer reaches it after every file is copied, which is
# where the failure is. `build_probes.py` tests it on its own; until one of
# those comes back this stays out, because a package that installs is worth
# more than a line of text in it.
WITH_INSTALL_TEXT = False
INSTALL_TEXT = ('Asphalt 2 N-Gage version, ported to S60v3 by DeltaCharlie.')

# Just the 44x44 colour bitmap; `mkmbm` puts a mask of its own after it. The
# AIF also holds a 42x29 pair, which is the N-Gage's own list icon and not a
# shape S60 asks for -- shipping both would give the shell a size to choose
# wrongly between.
ICON_BITMAPS = (0,)


def game_files(root):
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
            out.append((local, TARGET_DIR + sub + '\\' + n))
    return out


def main(out='.', game=GAME):
    os.makedirs(out, exist_ok=True)
    icon = os.path.join(out, 'gate6.mbm')
    got = mkmbm.build(os.path.join(game, '6rbc.aif'), icon, list(ICON_BITMAPS))
    print('icon: %s' % ', '.join(mkmbm.describe(f) for f in got))

    extra = game_files(game)
    total = sum(os.path.getsize(s) for s, _t in extra)
    print('game files: %d, %.1f MB' % (len(extra), total / 1e6))

    build_gate6.build(out, caption=CAPTION, icon=icon, extra=extra,
                      install_text=INSTALL_TEXT if WITH_INSTALL_TEXT else None,
                      vendor=VENDOR)


if __name__ == '__main__':
    main(*sys.argv[1:])
