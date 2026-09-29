#!/usr/bin/env python3
"""cmpdump.py <a.bin> <b.bin> -- compare two g6code.bin frame dumps.

`dump_region` writes each region as an eight-byte header (the address it was
read from, then the length) followed by the bytes. The address differs between
runs because the allocation does, so only the payloads are compared.

Prints, per region, whether the bytes are identical and -- when they are not
-- which 16-bit columns differ, because that is the question the dump exists
to answer: whether what the game draws moves when the reported screen size
changes.
"""
import struct, sys


def regions(path):
    d = open(path, 'rb').read()
    out, o = [], 0
    while o + 8 <= len(d):
        addr, n = struct.unpack_from('<II', d, o)
        o += 8
        out.append((addr, d[o:o + n]))
        o += n
    return out


def main(pa, pb, pitch=176, height=208):
    A, B = regions(pa), regions(pb)
    print('%s: %d region(s)   %s: %d region(s)' % (pa, len(A), pb, len(B)))
    for i, ((aa, a), (ab, b)) in enumerate(zip(A, B)):
        same = a == b
        print('\nregion %d: %d vs %d bytes, read from %#x / %#x -> %s'
              % (i, len(a), len(b), aa, ab, 'IDENTICAL' if same else 'DIFFERENT'))
        if same:
            continue
        n = min(len(a), len(b))
        diff = [k for k in range(0, n - 1, 2) if a[k:k+2] != b[k:k+2]]
        print('   %d of %d 16-bit words differ (%.2f%%)'
              % (len(diff), n // 2, 100.0 * len(diff) / (n // 2)))
        if not diff:
            continue
        cols = sorted({(k // 2) % pitch for k in diff})
        rows = sorted({(k // 2) // pitch for k in diff})
        print('   first differing word at %d (row %d, col %d at pitch %d)'
              % (diff[0] // 2, diff[0] // 2 // pitch, diff[0] // 2 % pitch, pitch))
        print('   columns touched: %d..%d   rows touched: %d..%d (of %d)'
              % (cols[0], cols[-1], rows[0], rows[-1], height))
        # Non-zero extent of each, which says how wide the game actually drew.
        for name, buf in (('A', a), ('B', b)):
            last = 0
            for k in range(0, n - 1, 2):
                if buf[k:k+2] != b'\0\0':
                    last = max(last, (k // 2) % pitch)
            print('   %s: rightmost non-zero column at pitch %d is %d' % (name, pitch, last))


if __name__ == '__main__':
    main(*sys.argv[1:3])
