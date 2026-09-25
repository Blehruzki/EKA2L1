"""Build a native S60v3 app end to end: clang -> E32 -> resources -> SIS.

Absolute relocations are found by linking twice a page apart and comparing, and
each import stub's ordinal word is labelled `<symbol>_ord` in the assembly, so
the linked symbol table says where the loader has to patch.
"""
import os
import subprocess, subprocess as sp, sys

import mke32, mkloc, mkreg, mksis, relocs

HERE = os.path.dirname(os.path.abspath(__file__))
BASES = (0x8000, 0x9000)
CXXFLAGS = ['--target=armv5-none-eabi', '-marm', '-O1', '-fno-exceptions',
            '-fno-rtti', '-fno-builtin', '-ffreestanding']

# The decorated names the SDK emits, carrying the module version and the UID.
EUSER = 'euser{000a0000}[100039e5].dll'
EFSRV = 'efsrv{000a0000}[100039e4].dll'


def no_writable_statics(elf):
    """Refuse an image that wants a writable static.

    There is nowhere to put one: mke32 declares data size and bss zero, so the
    linker would place it in the read-only code segment and the first write
    would fault. The emulator maps that memory writable and notices nothing,
    which is exactly how build 52 reached the phone.
    """
    out = subprocess.run(['llvm-readelf', '-S', elf], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        if '.data' not in line or '.data.rel.ro' in line:
            continue
        parts = line.replace('[', ' ').replace(']', ' ').split()
        try:
            size = int(parts[parts.index('PROGBITS') + 3], 16)
        except (ValueError, IndexError):
            continue
        if size:
            raise SystemExit(
                'buildapp: %d bytes of writable statics.\n'
                '  This image has no data section -- they would land in the\n'
                '  read-only code segment and fault on the first write.\n'
                '  Make them const, or build the value on the stack.' % size)


def sh(*args):
    sp.run(args, check=True, cwd=HERE)


def build(name, uid3, caption, out, imports=(), sources=None, **e32):
    """imports: [(dll name, [stub symbol names])], resolved via the `_ord` labels."""
    p = lambda n: os.path.join(out, n)
    cpp = sources or (name + '.cpp',)
    objs = []
    for src in cpp:
        obj = p(os.path.basename(src) + '.o')
        sh('clang', *CXXFLAGS, '-c', '-o', obj, os.path.join(HERE, src))
        objs.append(obj)
    asm = p(name + '.s.o')
    sh('clang', '--target=armv5-none-eabi', '-c', '-o', asm, os.path.join(HERE, name + '.s'))

    flats, elf = [], None
    for base in BASES:
        script = p('flat_%x.ld' % base)
        open(script, 'w').write(
            open(os.path.join(HERE, 'flat.ld')).read().replace('BASE', hex(base)))
        elf = p('%s_%x.elf' % (name, base))
        sh('ld.lld', '-T', script, '-o', elf, asm, *objs)
        no_writable_statics(elf)
        sh('llvm-objcopy', '-O', 'binary', elf, p('%s_%x.bin' % (name, base)))
        flats.append(open(p('%s_%x.bin' % (name, base)), 'rb').read())
        if base == BASES[0]:
            first_elf = elf

    offsets = relocs.diff(flats[0], flats[1], BASES[1] - BASES[0])

    syms = {}
    for line in sp.run(['llvm-nm', first_elf], check=True, capture_output=True,
                       text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16) - BASES[0]
    blocks = []
    for dll, names in imports:
        blocks.append((dll, [syms[n + '_ord'] for n in names]))
        for n in names:
            assert n + '_ord' in syms, 'no stub for %s' % n

    open(p(name + '.exe'), 'wb').write(
        mke32.build(flats[0], uid3, uid2=0x100039CE, reloc_offsets=offsets,
                    imports=blocks, **e32))
    open(p(name + '_reg.rsc'), 'wb').write(mkreg.build(name, uid3, mkloc.CAPTION_RES_ID))
    open(p(name + '.rsc'), 'wb').write(mkloc.build(uid3, caption))
    mksis.build(p(name + '.sis'), uid3, caption, 'EKA2L1 port', [
        (p(name + '.exe'), '!:\\sys\\bin\\%s.exe' % name),
        (p(name + '.rsc'), '!:\\resource\\apps\\%s.rsc' % name),
        (p(name + '_reg.rsc'), '!:\\private\\10003a3f\\import\\apps\\%s_reg.rsc' % name),
    ])
    print('%s: %d bytes of code, %d relocations, %d imports -> %s'
          % (name, len(flats[0]), len(offsets),
             sum(len(o) for _d, o in blocks), p(name + '.sis')))
