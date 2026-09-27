#!/usr/bin/env python3
"""aificon.py <in.aif|in.mbm> <out-prefix> -- render an AIF/MBM's bitmaps to PNG.

Not part of any build: this exists so an icon can be *looked at* before it is
shipped. Every header field can read right and the picture still be wrong,
and this project has paid for that reading twice.

Decoding follows EKA2L1's own code: `decompress_rle<8>` in
`src/emu/common/src/runlen.cpp` (signed count byte -- non-negative means the
next byte repeats count+1 times, negative means -count literal bytes), and
the 256-colour table in `src/emu/services/include/services/fbs/palette.h`.
The N-Gage is EPOC 7, so the *old* table is the right one.
"""
import os
import re
import struct
import sys
import zlib

HEADER_LEN = 40
FIRST = 20
PALETTE_H = os.path.join('/home/user/EKA2L1', 'src', 'emu', 'services',
                         'include', 'services', 'fbs', 'palette.h')


def palette(which='old'):
    s = open(PALETTE_H).read()
    i = s.index('color_256_palette_' + which)
    body = s[s.index('{', i) + 1:s.index('};', i)]
    vals = [t.strip() for t in body.split(',') if t.strip()]
    out = []
    for t in vals:
        if not re.fullmatch(r'0[xX][0-9a-fA-F]+|0', t):
            raise ValueError('unexpected palette entry %r' % t)
        out.append(int(t, 16) & 0xFFFFFF)
    assert len(out) == 256, len(out)
    return out


def bitmaps(d):
    out, o = [], FIRST
    while o + HEADER_LEN <= len(d):
        f = struct.unpack_from('<10i', d, o)
        if f[0] <= 0 or f[1] != HEADER_LEN or not (0 < f[2] <= 1024) or not (0 < f[3] <= 1024):
            break
        if o + f[0] > len(d):
            break
        out.append((o, f))
        o += f[0]
    return out


def unrle8(src, want):
    out = bytearray()
    i = 0
    while i < len(src) and len(out) < want:
        c = src[i]
        i += 1
        if c < 0x80:                       # count >= 0: one byte, count+1 times
            if i >= len(src):
                break
            out += bytes([src[i]]) * (c + 1)
            i += 1
        else:                              # count < 0: -count literal bytes
            n = 256 - c
            out += src[i:i + n]
            i += n
    return bytes(out[:want])


def rows(d, off, f):
    """-> (width, height, bpp, list of row bytes), unpacked but not coloured."""
    bs, _sl, w, h, _tw, _th, bpp, _col, _pal, comp = f
    stride = ((w * bpp + 31) // 32) * 4     # Symbian rows are word-aligned
    raw = d[off + HEADER_LEN:off + bs]
    if comp == 1:
        raw = unrle8(raw, stride * h)
    elif comp != 0:
        raise ValueError('compression %d not handled' % comp)
    raw = raw.ljust(stride * h, b'\0')
    return w, h, bpp, [raw[y * stride:(y + 1) * stride] for y in range(h)]


def png(path, w, h, rgb_rows):
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c))
    raw = b''.join(b'\0' + r for r in rgb_rows)
    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(raw, 9))
    out += chunk(b'IEND', b'')
    open(path, 'wb').write(out)


def main(src, prefix, which='old'):
    d = open(src, 'rb').read()
    pal = palette(which)
    found = bitmaps(d)
    made = []
    for i, (off, f) in enumerate(found):
        w, h, bpp, rr = rows(d, off, f)
        out = []
        for r in rr:
            line = bytearray()
            for x in range(w):
                if bpp == 8:
                    c = pal[r[x]]
                elif bpp == 1:
                    bit = (r[x >> 3] >> (x & 7)) & 1
                    c = 0xFFFFFF if bit else 0x000000
                else:
                    raise ValueError('%d bpp not handled' % bpp)
                line += bytes(((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF))
            out.append(bytes(line))
        path = '%s-%d-%dx%d-%dbpp.png' % (prefix, i, w, h, bpp)
        png(path, w, h, out)
        made.append(path)
        print('  [%d] %3dx%-3d %d bpp -> %s' % (i, w, h, bpp, path))
    return made


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else 'old')
