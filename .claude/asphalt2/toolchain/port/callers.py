#!/usr/bin/env python3
"""Who in the game calls an import, and what they do around the call.

    callers.py <image.app> <dll> <old ordinal> [--around N]

A GCC98r2 image calls an import through a veneer -- `ldr pc, [pc, #-4]`
over a word the loader fills -- that sits in the import address table at
the end of the code section, one word per import in import-section order.
The call sites are the `bl`s whose target is that veneer. This finds the
import's table slot, the veneer that reads it, every `bl` to it, and prints
each caller with a few instructions either side -- the thing the trace
cannot show for a hooked or diverted import, which never reaches it.
"""
import struct, sys, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e32imports
import capstone

CODE_BASE = 0x10000000


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    path, dll, ordinal = argv[0], argv[1].lower(), int(argv[2])
    around = int(argv[argv.index('--around') + 1]) if '--around' in argv else 6
    d = open(path, 'rb').read()
    h = e32imports.header(d)
    co, cs, ts = h['code_offset'], h['code_size'], h['text_size']
    code = d[co:co + cs]
    imps = e32imports.imports(d)
    # the import's slot: imports are laid out DLL by DLL in section order
    idx, found = 0, None
    for name, ords in imps:
        lib = name.split('[')[0].split('{')[0].lower().replace('.dll', '')
        for o in ords:
            if lib == dll and o == ordinal:
                found = idx
            idx += 1
    if found is None:
        raise SystemExit('%s %d is not imported' % (dll, ordinal))
    iat_addr = CODE_BASE + ts + 4 * found
    print('%s %d: import #%d, table word at 0x%08x' % (dll, ordinal, found, iat_addr))

    # The veneer, as GCC98r2 emits it: `ldr ip, [pc, #4]; ldr ip, [ip]; bx ip`
    # with the table word right after -- so the veneer starts twelve bytes
    # before the literal.
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    targets = set()
    lit = struct.pack('<I', iat_addr)
    pos = code.find(lit)
    while pos != -1:
        if pos >= 12 and struct.unpack_from('<3I', code, pos - 12) == (0xE59FC004, 0xE59CC000, 0xE12FFF1C):
            targets.add(pos - 12)
        pos = code.find(lit, pos + 4)
    veneers = targets
    if not targets:
        print('no veneer found for that table word')
        return 1
    print('veneer(s) at: %s' % ', '.join('0x%08x' % (CODE_BASE + t) for t in sorted(targets)))

    # every bl to a veneer
    callers = []
    for at in range(0, ts, 4):
        w = struct.unpack_from('<I', code, at)[0]
        if (w & 0x0F000000) == 0x0B000000:          # bl, any condition
            off = w & 0x00FFFFFF
            if off & 0x800000:
                off -= 0x1000000
            dest = at + 8 + off * 4
            if dest in targets:
                callers.append(at)
    print('%d call site(s)' % len(callers))
    for at in callers:
        lo = max(0, at - 4 * around)
        hi = min(ts, at + 4 * (around + 1))
        print('-- caller at code+0x%05x' % at)
        for ins in md.disasm(code[lo:hi], lo):
            mark = '>>' if ins.address == at else '  '
            print('   %s 0x%05x  %-8s %s' % (mark, ins.address, ins.mnemonic, ins.op_str))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
