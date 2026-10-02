#!/usr/bin/env python3
"""mkmif.py -- turn the game's MBM icon into the .mif an S60v3 shell will draw.

Build 170 shipped a perfectly good `.mbm` and the phone drew an empty box.
EKA2L1 drew it correctly, which is the same gap this project has been caught
by twice before: the emulator is more permissive than the device. S60 3rd
Edition wants a **MIF** -- a scalable icon file -- named in the application's
caption resource, and the two bytes that change `gate6.mbm` to `gate6.mif`
there are the rest of the fix.

Nothing is redrawn. The 44x44 indexed bitmap is decoded, each horizontal run
of one colour becomes one rectangle path, and the result is an SVG Tiny
document wrapped in a MIF container. Same geometry, same colours, same
opaque pixels.

**The palette is stored as 0x00BBGGRR**, not 0xRRGGBB, and `aificon.palette`
turns it round once. Reading it the wrong way is what made this project's
own renderer show the icon in blue while the emulator's app list showed it
in orange -- the table was right and the channel order was not.

This reproduces, byte for byte, the file confirmed on the phone.
"""
import collections
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import aificon
import mkmbm


def decode(path):
    """-> (width, height, [rgb or None per pixel]) from an icon+mask MBM."""
    d = open(path, 'rb').read()
    trailer, = struct.unpack_from('<I', d, 16)
    n, = struct.unpack_from('<I', d, trailer)
    offs = struct.unpack_from('<%dI' % n, d, trailer + 4)
    if len(offs) < 2:
        raise ValueError('%s has no icon/mask pair' % path)
    pal = aificon.palette('old')

    off = offs[0]
    f = struct.unpack_from('<10i', d, off)
    w, h, bpp = f[2], f[3], f[6]
    body = d[off + mkmbm.HEADER_LEN:off + f[0]]
    if bpp == 8:
        stride = ((w * 8 + 31) // 32) * 4
        pix = aificon.unrle8(body, stride * h) if f[9] == 1 else body
        colour_at = lambda y, x: pal[pix[y * stride + x]]
    elif bpp == 12:
        # EColor4K: a 16-bit word a pixel, 0x0RGB, rows word-aligned. Its
        # compression (2, ETwelveBitRLECompression) is one 16-bit word per
        # run: the top nibble is the run length less one, the low twelve
        # bits the colour. Ashen's icon is this; the Asphalts' are 8 bpp.
        stride = ((w * 16 + 31) // 32) * 4
        if f[9] == 2:
            words = []
            for i in range(0, len(body) - 1, 2):
                v = body[i] | (body[i + 1] << 8)
                words += [v & 0xFFF] * ((v >> 12) + 1)
                if len(words) >= (stride // 2) * h:
                    break
        elif f[9] == 0:
            words = [body[i] | (body[i + 1] << 8) for i in range(0, len(body) - 1, 2)]
        else:
            raise ValueError('12 bpp icon with compression %d' % f[9])
        words += [0] * ((stride // 2) * h - len(words))
        colour_at = lambda y, x: (lambda v: (((v >> 8) & 0xF) * 0x110000) | (((v >> 4) & 0xF) * 0x1100) | ((v & 0xF) * 0x11))(words[y * (stride // 2) + x])
    else:
        raise ValueError('icon is %d bpp, expected 8 or 12' % bpp)

    moff = offs[1]
    mf = struct.unpack_from('<10i', d, moff)
    mstride = ((mf[2] + 31) // 32) * 4
    mask = d[moff + mkmbm.HEADER_LEN:moff + mf[0]]

    out = []
    for y in range(h):
        row = []
        for x in range(w):
            opaque = (mask[y * mstride + (x >> 3)] >> (x & 7)) & 1
            c = colour_at(y, x)
            # `aificon.palette` already hands back 0xRRGGBB -- it swaps the
            # table's 0x00BBGGRR once, and swapping again here is how the
            # first build of this file came out with the channels back to
            # front. Caught by comparing the bytes against the MIF the phone
            # accepted, which is the only reason to keep that file around.
            row.append(((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF) if opaque else None)
        out.append(row)
    return w, h, out


def svg(w, h, rows):
    groups = collections.defaultdict(list)
    for y in range(h):
        x = 0
        while x < w:
            c = rows[y][x]
            end = x + 1
            while end < w and rows[y][end] == c:
                end += 1
            if c is not None:
                groups[c].append('M%d %dh%dv1h-%dz' % (x, y, end - x, end - x))
            x = end
    body = ''.join('<path fill="#%02x%02x%02x" d="%s"/>' % (c[0], c[1], c[2], ''.join(p))
                   for c, p in groups.items())
    return ('<svg xmlns="http://www.w3.org/2000/svg" version="1.1" baseProfile="tiny" '
            'width="%d" height="%d" viewBox="0 0 %d %d">%s</svg>' % (w, h, w, h, body)), len(groups)


def build(src, dst):
    w, h, rows = decode(src)
    doc, colours = svg(w, h, rows)
    d = doc.encode()
    mif = (b'B##4' + struct.pack('<III', 2, 16, 2)
           + struct.pack('<IIII', 32, len(d) + 32, 32, len(d) + 32)
           + b'C##4' + struct.pack('<7I', 1, 32, len(d), 1, 0, 0, 0) + d)
    open(dst, 'wb').write(mif)
    opaque = sum(1 for r in rows for c in r if c is not None)
    return dict(width=w, height=h, colours=colours, opaque=opaque, bytes=len(mif))


if __name__ == '__main__':
    info = build(sys.argv[1], sys.argv[2])
    print('%s -> %s' % (sys.argv[1], sys.argv[2]))
    print('  %(width)dx%(height)d, %(colours)d colours, %(opaque)d opaque pixels, %(bytes)d bytes' % info)
