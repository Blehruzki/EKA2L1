#!/usr/bin/env python3
"""Reconstruct an EKA1 image's vtables, slot by slot, with names.

    vtables.py <image> [dll=exact.def ...] [--epoc6 <file>]

A derived class's vtable records, in order, which base-class method fills each
inherited slot -- so the binary itself states the layout of every framework
class it derives from.  The slots are readable because imports in an EKA1 image
go through a 16-byte veneer

    ldr r12, [pc, #4] ; ldr r12, [r12] ; bx r12 ; .word <IAT slot>

whose literal is relocated, which both identifies the veneer and says which
import it serves.  A vtable is then a run of consecutive relocated words with
at least one veneer among them.

The vptr stored in an object points 8 bytes before slot 0: two zero words
(offset-to-top and typeinfo, null with RTTI off).  That is asserted here rather
than assumed -- see gcc98r2_vptr_bias().
"""
import struct, sys
import e32imports, epocdb, gnuv2, symdef

VPTR_BIAS = 8              # the vptr points this far before the first slot
VENEER_LEN = 16            # ldr/ldr/bx plus the literal


def relocations(d, h):
    off = h['code_reloc_offset']
    size, _count = struct.unpack_from('<II', d, off)
    out, o = [], off + 8
    while o < off + size:
        base, bsize = struct.unpack_from('<II', d, o)
        if bsize < 8 or bsize % 2:
            break
        for i in range((bsize - 8) // 2):
            e, = struct.unpack_from('<H', d, o + 8 + 2 * i)
            if e:
                out.append(base + (e & 0xFFF))
        o += bsize
    return sorted(out)


def veneers(d, h, relocs):
    """-> {veneer address: import index}"""
    out = {}
    for r in relocs:
        v, = struct.unpack_from('<I', d, h['code_offset'] + r)
        off = v - h['code_base']
        if h['text_size'] <= off < h['code_size']:
            out[h['code_base'] + r - (VENEER_LEN - 4)] = (off - h['text_size']) // 4
    return out


def pointer_runs(relocs):
    runs, start, prev = [], relocs[0], relocs[0]
    for x in relocs[1:]:
        if x == prev + 4:
            prev = x
            continue
        runs.append((start, (prev - start) // 4 + 1))
        start = prev = x
    runs.append((start, (prev - start) // 4 + 1))
    return runs


def find(d, h):
    """-> [(code offset of slot 0, [slot values])] for every table holding imports"""
    relocs = relocations(d, h)
    ven = veneers(d, h, relocs)
    out = []
    for start, n in pointer_runs(relocs):
        slots = [struct.unpack_from('<I', d, h['code_offset'] + start + 4 * i)[0]
                 for i in range(n)]
        if any(v in ven for v in slots):
            out.append((start, slots))
    return out, ven


def gcc98r2_vptr_bias(d, h, tables):
    """Confirm, from the image, that a stored vptr is (table - 8)."""
    starts = {t for t, _ in tables}
    at_header = at_slot0 = 0
    for r in relocations(d, h):
        v, = struct.unpack_from('<I', d, h['code_offset'] + r)
        off = v - h['code_base']
        at_header += (off + VPTR_BIAS) in starts
        at_slot0 += off in starts
    return at_header, at_slot0


def main(image, def_dirs, pinned, epoc6):
    d = open(image, 'rb').read()
    h = e32imports.header(d)
    flat = [(n.split('[')[0].split('{')[0].lower(), o)
            for n, os_ in e32imports.imports(d, h) for o in os_]
    db = epocdb.load(epoc6) if epoc6 else {}
    tabs = {symdef.base_name(k): symdef.load(v) for k, v in pinned.items()}

    def nameof(k):
        lib, o = flat[k]
        if lib in tabs and o in tabs[lib]:
            return '%s::%s' % (lib, tabs[lib][o][1] or gnuv2.demangle(tabs[lib][o][0]))
        t = db.get(lib)
        if t and 0 < o <= len(t):
            return '%s::%s' % (lib, gnuv2.demangle(t[o - 1]))
        return '%s ord %d' % (lib, o)

    tables, ven = find(d, h)
    header, slot0 = gcc98r2_vptr_bias(d, h, tables)
    print('%s\n  %d vtables, %d veneers' % (image, len(tables), len(ven)))
    print('  stored vptrs pointing at table-%d: %d ; at table+0: %d  -> the vptr is '
          'biased by %d' % (VPTR_BIAS, header, slot0, VPTR_BIAS))
    zeros = sum(1 for t, _ in tables
                if struct.unpack_from('<II', d, h['code_offset'] + t - VPTR_BIAS) == (0, 0))
    print('  tables preceded by two zero words: %d of %d (the rest abut another table)'
          % (zeros, len(tables)))
    for start, slots in sorted(tables, key=lambda t: -sum(v in ven for v in t[1])):
        own = sum(1 for v in slots if v not in ven)
        print('\nvtable at code+0x%x : %d slots, %d inherited, %d overridden'
              % (start, len(slots), len(slots) - own, own))
        for i, v in enumerate(slots):
            print('  [%2d] %s' % (i, nameof(ven[v]) if v in ven
                                  else 'overridden, code +0x%x' % (v - h['code_base'])))


if __name__ == '__main__':
    args = sys.argv[2:]
    e6 = None
    if '--epoc6' in args:
        i = args.index('--epoc6'); e6 = args[i + 1]; del args[i:i + 2]
    main(sys.argv[1], [a for a in args if '=' not in a],
         dict(a.split('=', 1) for a in args if '=' in a), e6)
