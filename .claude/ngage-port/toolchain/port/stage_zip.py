#!/usr/bin/env python3
"""stage_zip.py <out dir> --game <name>: the game's files for a zip release.

Lays out `<out>/stage/system/apps/<folder>/` with exactly the files, names and
bytes the bundled package installs -- build_release's own list (game_files,
engine_image): the image as the scrambled `<stem>.bin`, an engine title's
generated `<stem>.lxe`, and no `<stem>.app`. One's build 021 zip was put
together by hand in a layout no bench run had used, and the game asked for
its own .app and quit on every phone (BUGBOOK 12.ae, round 149). A zip made
here is the package's layout; test it all the same on a bench with no dump on
either drive, as E1028-E1030 did, before it goes out (RELEASE.md).
"""
import os
import shutil
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_release as r
import picture


def stage(out, game):
    st = r.stem(game)
    folder = picture.folder(game)
    tree = os.path.join(r.GAMES_ROOT, folder)
    scratch = os.path.join(out, '_scratch')
    os.makedirs(scratch, exist_ok=True)
    target_dir = '!:\\system\\apps\\' + folder
    extra = r.game_files(tree, scratch, target_dir, {st + '.app': st + '.bin'},
                         {st + '.app': (r.SCRAMBLE_KEY, r.SCRAMBLE_BYTES)})
    extra += r.engine_image(game, tree, scratch, target_dir, set(os.listdir(tree)))
    for local, target in extra:
        dst = os.path.join(out, 'stage', target.replace('!:\\', '').replace('\\', '/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(local, dst)
    shutil.rmtree(scratch)
    print('%s: %d files -> %s' % (game, len(extra), os.path.join(out, 'stage')))


if __name__ == '__main__':
    g, rest = picture.take_game_arg(sys.argv[1:])
    if not g or not rest:
        sys.exit(__doc__)
    stage(rest[0], g)
