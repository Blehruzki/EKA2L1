#!/usr/bin/env python3
"""dumppng.py <g6code.bin> <out prefix> [pitch] [height] -- frames as PNGs.

The game's buffer is RGB444 in a 16-bit word (SCREEN_4K), 176 pixels to a row.
One PNG per region in the dump, so a frame can be looked at rather than
described. Round 83's lesson: check your own picture before asking anyone to
check theirs.
"""
import struct, sys, zlib


def png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c))
    out = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(out)


def main(src, prefix, pitch=176, height=208):
    pitch, height = int(pitch), int(height)
    d = open(src, 'rb').read()
    o, i = 0, 0
    while o + 8 <= len(d):
        addr, n = struct.unpack_from('<II', d, o)
        o += 8
        buf = d[o:o + n]
        o += n
        rows = []
        for y in range(height):
            row = bytearray()
            for x in range(pitch):
                k = (y * pitch + x) * 2
                v = struct.unpack_from('<H', buf, k)[0] if k + 2 <= len(buf) else 0
                row += bytes((((v >> 8) & 0xF) * 17, ((v >> 4) & 0xF) * 17, (v & 0xF) * 17))
            rows.append(bytes(row))
        name = '%s%d.png' % (prefix, i)
        png(name, pitch, height, rows)
        print('%s  (%d bytes from %#x, %dx%d at pitch %d)' % (name, n, addr, pitch, height, pitch))
        i += 1


if __name__ == '__main__':
    main(*sys.argv[1:])
