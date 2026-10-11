#!/usr/bin/env python3
"""mkkit.py -- the compiler kit: an SDK-free S60v3 build, for handing on.

    mkkit.py <out dir>        -> <out dir>/s60v3-compiler-kit.zip

Only the build itself and its notes. The port stays here: no loader, no shim,
no title layers, no project records.

  README.md     kit/README.md: install, build, write your own, and the
                copy-protection method as a description
  build.py      kit/build.py: sources in, .sis out
  flat.ld       kit/flat.ld, the linker script
  hello/        kit/hello: a working example
  tools/        the steps after the linker -- mke32, importsec, relocs, mkreg,
                mkloc, rscwrite, mksis, sisrw, sisadd -- with kit/paths.py and
                EKA2L1's sample .sis, which mksis builds from; their
                comments that tell the port's history and their self-tests
                (which read the port's bench files) are left out

LLVM is not in it: the README sends the user to llvm.org's own release.
"""
import os
import re
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
KIT = os.path.join(HERE, 'kit')
sys.path.insert(0, HERE)
import paths

NAME = 's60v3-compiler-kit'
# The port's own history in the tools' comments stays here.
PORT_WORDS = re.compile(r'gate6|colin|glimmerati|asphalt|ashen|n-?gage|\bround \d|'
                        r'\bbuild \d|\bport\b|\bport\'s|N95', re.I)

TOOLS = ('mke32.py', 'importsec.py', 'relocs.py', 'mkreg.py', 'mkloc.py',
         'rscwrite.py', 'mksis.py', 'sisrw.py', 'sisadd.py')


def clean(src, dst):
    """Copy a tool without the port in it: drop each run of `#` comment lines
    that mentions the port, and the self-tests (they rebuild files from the
    port's own bench and import modules the kit does not carry)."""
    lines = open(src).read().split('\n')
    for i, line in enumerate(lines):
        if line.startswith('def selftest'):
            lines = lines[:i]
            break
    out, block = [], []
    for line in lines + ['']:
        if line.lstrip().startswith('#'):
            block.append(line)
            continue
        if not any(PORT_WORDS.search(b) for b in block):
            out += block
        block = []
        out.append(line)
    text = '\n'.join(out[:-1]).rstrip() + '\n'
    left = [l for l in text.split('\n') if PORT_WORDS.search(l)]
    if left:
        sys.exit('mkkit: %s still names the port:\n  %s' % (src, '\n  '.join(left)))
    open(dst, 'w').write(text)


def build(out_dir):
    stage = os.path.join(out_dir, NAME)
    shutil.rmtree(stage, ignore_errors=True)
    tools = os.path.join(stage, 'tools')
    os.makedirs(tools)
    for f in TOOLS:
        clean(os.path.join(HERE, f), os.path.join(tools, f))
    shutil.copy2(os.path.join(KIT, 'paths.py'), tools)
    shutil.copy2(paths.SIS_TEMPLATE, os.path.join(tools, 'EKA21LHW.sis'))
    for f in ('README.md', 'build.py', 'flat.ld'):
        shutil.copy2(os.path.join(KIT, f), stage)
    shutil.copytree(os.path.join(KIT, 'hello'), os.path.join(stage, 'hello'),
                    ignore=shutil.ignore_patterns('out', '__pycache__'))

    zpath = os.path.join(out_dir, NAME + '.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, _dirs, files in os.walk(stage):
            for f in sorted(files):
                full = os.path.join(dirpath, f)
                z.write(full, os.path.relpath(full, out_dir))
    n = sum(len(fs) for _d, _s, fs in os.walk(stage))
    print('kit: %d files, %s (%.0f KB)' % (n, zpath, os.path.getsize(zpath) / 1e3))
    return zpath


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build(sys.argv[1])
