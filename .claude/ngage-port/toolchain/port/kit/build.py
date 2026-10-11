#!/usr/bin/env python3
"""build.py -- C/C++/assembly in, an S60 3rd Edition .sis out. No Symbian SDK.

    python build.py <project folder>        -> <project folder>/out/<NAME>.sis

The folder holds the sources (*.cpp, *.c, *.s) and project.py (NAME, UID3,
CAPTION, IMPORTS; see hello/project.py). The steps:

1. clang compiles each source for ARMv5, ARM state, freestanding.
2. ld.lld links them twice, at two bases a page apart (flat.ld). The two flat
   images differ exactly in the words that hold an absolute address, which is
   the relocation table the loader needs.
3. Each import stub's ordinal word is found by its `<name>_ord` symbol.
4. tools/mke32.py wraps the code as an E32 executable (V-format header,
   imports, relocations); tools/mkreg.py and mkloc.py write the two resource
   files that put it in the menu; tools/mksis.py packages the three.
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'tools'))

TOOLS = ('clang', 'ld.lld', 'llvm-objcopy', 'llvm-nm', 'llvm-readelf')
CFLAGS = ['--target=armv5-none-eabi', '-marm', '-O1', '-fno-exceptions',
          '-fno-rtti', '-fno-builtin', '-ffreestanding']
BASES = (0x8000, 0x9000)


def run(*args):
    subprocess.run(args, check=True)


def no_writable_statics(elf):
    """The image declares no data section, so a writable static would sit in
    read-only code and fault on its first write on a phone (an emulator maps
    it writable and will not tell you)."""
    out = subprocess.run(['llvm-readelf', '-S', elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if ' .data' not in line or '.data.rel.ro' in line:
            continue
        parts = line.replace('[', ' ').replace(']', ' ').split()
        try:
            size = int(parts[parts.index('PROGBITS') + 3], 16)
        except (ValueError, IndexError):
            continue
        if size:
            sys.exit('build.py: %d bytes of writable statics; make them const' % size)


def main(folder):
    missing = [t for t in TOOLS if not shutil.which(t)]
    if missing:
        sys.exit('build.py: not on the PATH: %s (README.md, "Install")' % ', '.join(missing))
    import mke32, mkloc, mkreg, mksis, relocs

    folder = os.path.abspath(folder)
    proj = {}
    exec(open(os.path.join(folder, 'project.py')).read(), proj)
    name, uid3, caption = proj['NAME'], proj['UID3'], proj['CAPTION']
    out = os.path.join(folder, 'out')
    os.makedirs(out, exist_ok=True)
    p = lambda n: os.path.join(out, n)

    objs = []
    for src in sorted(glob.glob(os.path.join(folder, '*.[cs]')) +
                      glob.glob(os.path.join(folder, '*.cpp'))):
        obj = p(os.path.basename(src) + '.o')
        flags = ['--target=armv5-none-eabi'] if src.endswith('.s') else CFLAGS
        run('clang', *flags, '-c', '-o', obj, src)
        objs.append(obj)

    flats = []
    for base in BASES:
        script = p('flat_%x.ld' % base)
        open(script, 'w').write(open(os.path.join(HERE, 'flat.ld')).read().replace('BASE', hex(base)))
        elf = p('%s_%x.elf' % (name, base))
        run('ld.lld', '-T', script, '-o', elf, *objs)
        no_writable_statics(elf)
        run('llvm-objcopy', '-O', 'binary', elf, p('%s_%x.bin' % (name, base)))
        flats.append(open(p('%s_%x.bin' % (name, base)), 'rb').read())
    offsets = relocs.diff(flats[0], flats[1], BASES[1] - BASES[0])

    syms = {}
    nm = subprocess.run(['llvm-nm', p('%s_%x.elf' % (name, BASES[0]))], check=True,
                        capture_output=True, text=True).stdout
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16) - BASES[0]
    blocks = []
    for dll, names in proj.get('IMPORTS', ()):
        for n in names:
            if n + '_ord' not in syms:
                sys.exit('build.py: no import stub %s_ord in the sources' % n)
        blocks.append((dll, [syms[n + '_ord'] for n in names]))

    open(p(name + '.exe'), 'wb').write(mke32.build(
        flats[0], uid3, uid2=0x100039CE, reloc_offsets=offsets, imports=blocks))
    open(p(name + '_reg.rsc'), 'wb').write(mkreg.build(name, uid3, mkloc.CAPTION_RES_ID))
    open(p(name + '.rsc'), 'wb').write(mkloc.build(uid3, caption))
    sis = p(name + '.sis')
    mksis.build(sis, uid3, caption, proj.get('VENDOR', caption), [
        (p(name + '.exe'), '!:\\sys\\bin\\%s.exe' % name),
        (p(name + '.rsc'), '!:\\resource\\apps\\%s.rsc' % name),
        (p(name + '_reg.rsc'), '!:\\private\\10003a3f\\import\\apps\\%s_reg.rsc' % name),
    ])
    print('%s: %d bytes of code, %d relocations, %d imports -> %s'
          % (name, len(flats[0]), len(offsets), sum(len(o) for _d, o in blocks), sis))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
