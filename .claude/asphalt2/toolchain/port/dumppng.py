#!/usr/bin/env python3
"""dumppng.py <g6code.bin> <out prefix> [pitch] [height] -- frames as PNGs.

The game's buffer is RGB444 in a 16-bit word (SCREEN_4K), 176 pixels to a row.
One PNG per region in the dump, so a frame can be looked at rather than
described. Round 83's lesson: check your own picture before asking anyone to
check theirs.

**The origin defaults to 16 because the blit's does.** `gate6_screen_update`
reads from `src + SRC_ORIGIN + y * srcPitch`, and SRC_ORIGIN is 16 -- rounds
74-78 measured that the game's picture starts at source pixel sixteen. A
render starting at column 0 is the same frame sixteen columns out of phase:
it puts a bright vertical seam at column 15 (mean neighbouring delta 1041,
against 431 for the strongest seam in the picture itself) and reads
convincingly as horizontal wrapping. It is not. Pass 0 for the raw buffer,
16 for what the screen actually shows.
"""
import struct, sys, zlib

import picture


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


def main(g, src, prefix):
    pitch, height, origin = g['pitch'], g['height'], g['origin']
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
                k = (origin + y * pitch + x) * 2
                v = struct.unpack_from('<H', buf, k)[0] if k + 2 <= len(buf) else 0
                row += bytes((((v >> 8) & 0xF) * 17, ((v >> 4) & 0xF) * 17, (v & 0xF) * 17))
            rows.append(bytes(row))
        name = '%s%d.png' % (prefix, i)
        png(name, pitch, height, rows)
        print('%s  (%d bytes from %#x, %dx%d at pitch %d, origin %d)'
              % (name, n, addr, pitch, height, pitch, origin))
        i += 1


if __name__ == '__main__':
    game, rest = picture.take_game_arg(sys.argv[1:])
    src, prefix, nums = rest[0], rest[1], rest[2:]
    g = picture.geometry(game, *nums)
    picture.banner(g)
    main(g, src, prefix)
