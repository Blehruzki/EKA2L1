#!/usr/bin/env python3
"""Gate 1: build a native S60v3 app with no Symbian SDK, end to end.

    clang -> flat ARM code -> our own E32 writer -> our own registration
    resources -> our own SIS package

The app faults on purpose at 0xDEAD0000.  That is the whole point: a fault the
emulator reports with our exact address, at a pc inside our own code section,
proves the image was accepted, mapped and executed.  Nothing here links against
anything, so the observable does not depend on a single resolved import.

Run from this directory; needs clang and llvm-objcopy on PATH.
"""
import os, subprocess, sys

import mke32, mkreg, mkloc, mksis

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = 'gate1'
UID3 = 0xE0001001          # unprotected range, so no signature is needed
UID2_APP = 0x100039CE      # KUidApp: apparc will list and launch it


def sh(*args):
    subprocess.run(args, check=True, cwd=HERE)


def main(out_dir='.'):
    out = lambda n: os.path.join(out_dir, n)

    sh('clang', '--target=armv5-none-eabi', '-c', '-o', out(NAME + '.o'),
       os.path.join(HERE, NAME + '.s'))
    sh('llvm-objcopy', '-O', 'binary', '--only-section=.text',
       out(NAME + '.o'), out(NAME + '.bin'))

    code = open(out(NAME + '.bin'), 'rb').read()
    open(out(NAME + '.exe'), 'wb').write(mke32.build(code, UID3, uid2=UID2_APP))
    open(out(NAME + '_reg.rsc'), 'wb').write(mkreg.build(NAME, UID3, mkloc.CAPTION_RES_ID))
    open(out(NAME + '.rsc'), 'wb').write(mkloc.build(UID3, 'Gate1'))

    mksis.build(out(NAME + '.sis'), UID3, 'Gate1', 'EKA2L1 port', [
        (out(NAME + '.exe'), '!:\\sys\\bin\\%s.exe' % NAME),
        (out(NAME + '.rsc'), '!:\\resource\\apps\\%s.rsc' % NAME),
        (out(NAME + '_reg.rsc'), '!:\\private\\10003a3f\\import\\apps\\%s_reg.rsc' % NAME),
    ])

    for n in (NAME + '.exe', NAME + '.rsc', NAME + '_reg.rsc', NAME + '.sis'):
        print('%-16s %6d bytes' % (n, os.path.getsize(out(n))))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
