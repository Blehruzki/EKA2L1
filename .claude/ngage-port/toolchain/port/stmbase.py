#!/usr/bin/env python3
"""Does a title's image push or pop SP through an STM/LDM that writes back to it?

    stmbase.py <image.app> [...]

`push {..., sp, ...}` with writeback stores a value ARM calls UNPREDICTABLE:
an ARM9 (the N-Gage, and the bench) stores the original SP, the N95's ARM11
the written-back one -- so a matching `pop {..., sp, ...}` returns SP one
frame low on a phone and the caller returns through garbage. Glimmerati's
four span routines did exactly that (BUGBOOK 14.f, round 178). This lists
every unconditional `stmdb sp!` / `ldmia sp!` whose list holds SP; encrypted
or data words that decode as one are listed too, so read each hit back.
Exits 1 when any is found.
"""
import struct, sys


def code_of(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 0x64)[0]
    size = struct.unpack_from('<I', d, 0x30)[0]
    return d[off:off + size]


def hits(code):
    for x in range(0, len(code) - 3, 4):
        w = struct.unpack_from('<I', code, x)[0]
        if (w & 0xFFFF2000) in (0xE92D2000, 0xE8BD2000):    # stmdb sp! / ldmia sp!, SP in the list
            yield x, w


def main(paths):
    found = 0
    for p in paths:
        for x, w in hits(code_of(p)):
            print('%s  0x%06x  %08x  %s' % (p, x, w, 'push' if w >> 20 == 0xE92 else 'pop'))
            found += 1
    print('%d found' % found)
    return 1 if found else 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1:]))
