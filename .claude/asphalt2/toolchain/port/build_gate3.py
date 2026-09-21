#!/usr/bin/env python3
"""Gate 3: call a clang-built C++ class through a GCC98r2 vtable.

Links twice, a page apart, so the absolute words fall out by comparison
(relocs.diff) instead of having to be read out of the ELF -- and an assumption
that breaks raises rather than emitting a wrong table.

The app reports its result by panicking with category GATE3 and the mask as the
reason, because a phone shows a panic on screen and never shows a fault address.
All seven checks passing reads "GATE3 127".
"""
import os, subprocess, sys

import mke32, mkloc, mkreg, mksis, relocs, subprocess as sp

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = 'gate3'
UID3 = 0xE0001003
UID2_APP = 0x100039CE
BASES = (0x8000, 0x9000)
# The decorated name the SDK emits, carrying the module version and the UID.
EUSER = 'euser{000a0000}[100039e5].dll'

CXXFLAGS = ['--target=armv5-none-eabi', '-marm', '-O1', '-fno-exceptions',
            '-fno-rtti', '-fno-builtin', '-ffreestanding']


def sh(*args):
    subprocess.run(args, check=True, cwd=HERE)


def main(out='.'):
    p = lambda n: os.path.join(out, n)
    sh('clang', *CXXFLAGS, '-c', '-o', p('gate3.o'), os.path.join(HERE, 'gate3.cpp'))
    sh('clang', '--target=armv5-none-eabi', '-c', '-o', p('gate3s.o'),
       os.path.join(HERE, 'gate3.s'))

    flats = []
    for base in BASES:
        script = p('flat_%x.ld' % base)
        with open(script, 'w') as f:
            f.write(open(os.path.join(HERE, 'flat.ld')).read().replace('BASE', hex(base)))
        sh('ld.lld', '-T', script, '-o', p('gate3_%x.elf' % base),
           p('gate3s.o'), p('gate3.o'))
        sh('llvm-objcopy', '-O', 'binary', p('gate3_%x.elf' % base), p('gate3_%x.bin' % base))
        flats.append(open(p('gate3_%x.bin' % base), 'rb').read())

    offsets = relocs.diff(flats[0], flats[1], BASES[1] - BASES[0])

    # Each import stub's ordinal word is labelled <name>_ord in the assembly, so
    # the linked symbol table says where the loader has to patch.
    syms = sp.run(['llvm-nm', p('gate3_%x.elf' % BASES[0])],
                  check=True, capture_output=True, text=True).stdout
    ords = sorted((int(a, 16) - BASES[0]) for a, _t, n in
                  (l.split() for l in syms.splitlines() if l.strip()) if n.endswith('_ord'))
    assert ords, 'no import stubs found in the link'
    imports = [(EUSER, ords)]

    open(p(NAME + '.exe'), 'wb').write(
        mke32.build(flats[0], UID3, uid2=UID2_APP, reloc_offsets=offsets, imports=imports))
    open(p(NAME + '_reg.rsc'), 'wb').write(mkreg.build(NAME, UID3, mkloc.CAPTION_RES_ID))
    open(p(NAME + '.rsc'), 'wb').write(mkloc.build(UID3, 'Gate3'))
    mksis.build(p(NAME + '.sis'), UID3, 'Gate3', 'EKA2L1 port', [
        (p(NAME + '.exe'), '!:\\sys\\bin\\%s.exe' % NAME),
        (p(NAME + '.rsc'), '!:\\resource\\apps\\%s.rsc' % NAME),
        (p(NAME + '_reg.rsc'), '!:\\private\\10003a3f\\import\\apps\\%s_reg.rsc' % NAME),
    ])
    print('%d bytes of code, %d relocations, %d imports -> %s' % (
        len(flats[0]), len(offsets), len(ords), p(NAME + '.sis')))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
