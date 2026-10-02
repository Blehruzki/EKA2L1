#!/usr/bin/env python3
"""shotpng.py <g6code.bin> <out prefix> -- the composited screen, as PNGs.

`DUMP_SCREEN` writes pairs of regions: a four-word descriptor (width, height,
pitch in bytes, bits per pixel) and then the framebuffer itself. The geometry
travels with the pixels on purpose -- phase 0's renderer was told its geometry
separately, got it wrong by sixteen columns, and drew a convincing artefact.

This is what the panel shows, after the blit, including the letterbox: the
picture is exactly as large and exactly where the mode put it.
"""
import struct, sys, zlib


def regions(path):
    d = open(path, 'rb').read()
    out, o = [], 0
    while o + 8 <= len(d):
        _addr, n = struct.unpack_from('<II', d, o)
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


def main(src, prefix):
    regs = regions(src)
    shot = 0
    i = 0
    while i + 1 < len(regs):
        head = regs[i]
        if len(head) != 16:
            i += 1
            continue
        w, h, pitch, bpp = struct.unpack_from('<IIII', head, 0)
        buf = regs[i + 1]
        i += 2
        if not w or not h or pitch * h > len(buf) + pitch:
            print('  skipped a malformed pair: %ux%u pitch %u bpp %u' % (w, h, pitch, bpp))
            continue
        rows = []
        for y in range(h):
            row = bytearray()
            base = y * pitch
            for x in range(w):
                if bpp == 32:
                    k = base + x * 4
                    b, g, r = buf[k], buf[k + 1], buf[k + 2]
                elif bpp == 24:
                    k = base + x * 3
                    b, g, r = buf[k], buf[k + 1], buf[k + 2]
                else:
                    k = base + x * 2
                    v = struct.unpack_from('<H', buf, k)[0]
                    r = ((v >> 11) & 0x1F) << 3
                    g = ((v >> 5) & 0x3F) << 2
                    b = (v & 0x1F) << 3
                row += bytes((r, g, b))
            rows.append(bytes(row))
        name = '%s%d.png' % (prefix, shot)
        png(name, w, h, rows)
        print('%s  %ux%u, pitch %u, %u bpp' % (name, w, h, pitch, bpp))
        shot += 1


if __name__ == '__main__':
    main(*sys.argv[1:3])
