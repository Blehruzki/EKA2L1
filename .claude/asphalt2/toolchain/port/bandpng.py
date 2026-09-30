#!/usr/bin/env python3
"""bandpng.py --game <name> <dump.bin> <out.png> [y0] [y1] [zoom] -- stack one band from every
region of a dump, so a line that blinks can be told from one that is never
drawn. Each region becomes one strip, in order, separated by a black rule.
"""
import struct, zlib, sys

import picture


def regions(p):
    d = open(p, 'rb').read()
    out, o = [], 0
    while o + 8 <= len(d):
        _a, n = struct.unpack_from('<II', d, o)
        o += 8
        out.append(d[o:o + n])
        o += n
    return out


def png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def ch(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
        + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
        + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))


def main(g, src, out, y0=148, y1=164, zoom=3):
    # No default pitch or origin: see picture.py. One game's sixteen is
    # another game's horizontal wrap.
    y0, y1, zoom = int(y0), int(y1), int(zoom)
    pitch, origin = g['pitch'], g['origin']
    regs = regions(src)
    print('%d region(s)' % len(regs))
    for i in range(len(regs)):
        for j in range(i + 1, len(regs)):
            if regs[i] == regs[j]:
                print('   region %d == region %d' % (i, j))
    rows = []
    for buf in regs:
        for y in range(y0, y1):
            row = bytearray()
            for x in range(pitch):
                v = struct.unpack_from('<H', buf, (origin + y * pitch + x) * 2)[0]
                row += bytes((((v >> 8) & 0xF) * 17, ((v >> 4) & 0xF) * 17, (v & 0xF) * 17)) * zoom
            for _ in range(zoom):
                rows.append(bytes(row))
        for _ in range(zoom):
            rows.append(bytes(pitch * zoom * 3))
    png(out, pitch * zoom, len(rows), rows)
    print('wrote %s: %d strips, rows %d..%d' % (out, len(regs), y0, y1))


if __name__ == '__main__':
    game, rest = picture.take_game_arg(sys.argv[1:])
    g = picture.geometry(game)
    picture.banner(g)
    main(g, *rest)
