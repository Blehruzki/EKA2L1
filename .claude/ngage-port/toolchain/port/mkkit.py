#!/usr/bin/env python3
"""mkkit.py -- the portable kit: this toolchain, off this machine.

    mkkit.py <out dir>        -> <out dir>/ngage-port-kit.zip

Everything the build reads, and nothing it does not need:

  build.py            kit_build.py: a card dump in, a .sis out
  README.md           kit_README.md: what to install, how to run it, and how
                      the copy protection and Glimmerati were worked out
  port/               the scripts, the loader's sources (gate6.cpp/.s,
                      screen_fit.h, flat.ld) and every title's layer
                      (games/<name>/game.h, shim.cpp, gate_imports.h)
  port/kitdata/       what paths.py looks for in a kit: the Symbian release's
                      eabi/*.def files and the N-Gage euser .def, laid out as in
                      the release; EKA2L1's epoc6/epoc9 lists, its palette header
                      and the SIS template; an empty card folder (e/)
  docs/               the project's records, for reading

No game data: a title's layer is the port's code, and the card is the
user's own dump. A build never writes into the kit's port/ except a new
title's games/<name>/ and the card copy under kitdata/e/.
"""
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, '..', '..'))       # .claude/ngage-port
sys.path.insert(0, HERE)
import paths

PORT_EXT = ('.py', '.cpp', '.h', '.s', '.sh')
PORT_SKIP = {'mkkit.py', 'kit_build.py', 'kit_README.md'}
DOCS = ('PORTING.md', 'SYMBIAN.md', 'BUGBOOK.md', 'GAMES.md', 'KNOBS.md',
        'DEVICES.md', 'MULTIPLAYER.md')


def symbian_defs(dst):
    """The .def files gen_shim.py reads, keeping the release's layout so its
    relative paths (KERNEL, COMPSUPP, bmarm/7.0-euseru.def) still resolve."""
    root = paths.SYMBIAN_SOURCE
    n = 0
    for dirpath, _dirs, files in os.walk(root):
        low = dirpath.lower()
        for f in files:
            if not f.lower().endswith('.def'):
                continue
            if 'eabi' in low or f == '7.0-euseru.def':
                rel = os.path.relpath(os.path.join(dirpath, f), root)
                out = os.path.join(dst, rel)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                shutil.copy2(os.path.join(dirpath, f), out)
                n += 1
    return n


def build(out_dir):
    stage = os.path.join(out_dir, 'ngage-port-kit')
    shutil.rmtree(stage, ignore_errors=True)
    port = os.path.join(stage, 'port')
    os.makedirs(port)

    for f in sorted(os.listdir(HERE)):
        if f.endswith(PORT_EXT) and f not in PORT_SKIP and os.path.isfile(os.path.join(HERE, f)):
            shutil.copy2(os.path.join(HERE, f), port)
    shutil.copy2(os.path.join(HERE, 'flat.ld'), port)
    for g in sorted(os.listdir(os.path.join(HERE, 'games'))):
        src = os.path.join(HERE, 'games', g)
        if not os.path.isdir(src):
            continue
        os.makedirs(os.path.join(port, 'games', g))
        for f in os.listdir(src):
            if f.endswith(('.h', '.cpp')):
                shutil.copy2(os.path.join(src, f), os.path.join(port, 'games', g))

    kd = os.path.join(port, 'kitdata')
    ndefs = symbian_defs(os.path.join(kd, 'symbian'))
    for src, name in ((paths.EPOC6_DEF, 'epoc6.def'), (paths.EPOC9_DEF, 'epoc9.def'),
                      (paths.PALETTE_H, 'palette.h'), (paths.SIS_TEMPLATE, 'EKA21LHW.sis')):
        shutil.copy2(src, os.path.join(kd, name))
    for d in (('e', 'system', 'apps'), ('e', 'system', 'libs')):
        os.makedirs(os.path.join(kd, *d), exist_ok=True)
        open(os.path.join(kd, *d, '.keep'), 'w').close()

    shutil.copy2(os.path.join(HERE, 'kit_build.py'), os.path.join(stage, 'build.py'))
    shutil.copy2(os.path.join(HERE, 'kit_README.md'), os.path.join(stage, 'README.md'))
    docs = os.path.join(stage, 'docs')
    os.makedirs(docs)
    for f in DOCS:
        shutil.copy2(os.path.join(PROJECT, f), docs)
    shutil.copy2(os.path.join(HERE, 'README.md'), os.path.join(docs, 'PORT-HISTORY.md'))

    zpath = os.path.join(out_dir, 'ngage-port-kit.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, _dirs, files in os.walk(stage):
            for f in sorted(files):
                full = os.path.join(dirpath, f)
                z.write(full, os.path.relpath(full, out_dir))
    nfiles = sum(len(fs) for _d, _s, fs in os.walk(stage))
    print('kit: %d files (%d .def files), %s (%.1f MB)'
          % (nfiles, ndefs, zpath, os.path.getsize(zpath) / 1e6))
    return zpath


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build(sys.argv[1])
