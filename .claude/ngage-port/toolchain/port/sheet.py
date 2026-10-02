#!/usr/bin/env python3
"""sheet.py <out.png> <label:file> ... -- one labelled contact sheet.

Four modes side by side beats four files: the point of comparing them is the
comparison. Each panel keeps its own pixels at 1:1 and is padded, not scaled,
so what is on the sheet is what is on the screen. The label strip is drawn
with a five-by-seven bitmap font rather than a library, because this has to
run wherever the rest of the toolchain does.
"""
import struct, sys, zlib

FONT = {
 'A':("01110","10001","10001","11111","10001","10001","10001"),
 'B':("11110","10001","10001","11110","10001","10001","11110"),
 'C':("01111","10000","10000","10000","10000","10000","01111"),
 'D':("11110","10001","10001","10001","10001","10001","11110"),
 'E':("11111","10000","10000","11110","10000","10000","11111"),
 'F':("11111","10000","10000","11110","10000","10000","10000"),
 'G':("01111","10000","10000","10111","10001","10001","01111"),
 'H':("10001","10001","10001","11111","10001","10001","10001"),
 'I':("11111","00100","00100","00100","00100","00100","11111"),
 'L':("10000","10000","10000","10000","10000","10000","11111"),
 'M':("10001","11011","10101","10101","10001","10001","10001"),
 'N':("10001","11001","10101","10011","10001","10001","10001"),
 'O':("01110","10001","10001","10001","10001","10001","01110"),
 'P':("11110","10001","10001","11110","10000","10000","10000"),
 'R':("11110","10001","10001","11110","10100","10010","10001"),
 'S':("01111","10000","10000","01110","00001","00001","11110"),
 'T':("11111","00100","00100","00100","00100","00100","00100"),
 'U':("10001","10001","10001","10001","10001","10001","01110"),
 'X':("10001","10001","01010","00100","01010","10001","10001"),
 'Y':("10001","10001","01010","00100","00100","00100","00100"),
 '0':("01110","10001","10011","10101","11001","10001","01110"),
 '1':("00100","01100","00100","00100","00100","00100","01110"),
 '2':("01110","10001","00001","00110","01000","10000","11111"),
 '3':("11111","00010","00100","00010","00001","10001","01110"),
 '4':("00010","00110","01010","10010","11111","00010","00010"),
 '5':("11111","10000","10000","11110","00001","10001","01110"),
 '6':("00110","01000","10000","11110","10001","10001","01110"),
 '7':("11111","00001","00010","00100","01000","01000","01000"),
 '9':("01110","10001","10001","01111","00001","00010","01100"),
 '8':("01110","10001","10001","01110","10001","10001","01110"),
 ':':("00000","00100","00100","00000","00100","00100","00000"),
 '.':("00000","00000","00000","00000","00000","01100","01100"),
 '%':("11001","11010","00010","00100","01000","01011","10011"),
 '+':("00000","00100","00100","11111","00100","00100","00000"),
 '-':("00000","00000","00000","11111","00000","00000","00000"),
 ' ':("00000","00000","00000","00000","00000","00000","00000"),
 '/':("00001","00010","00010","00100","01000","01000","10000"),
 '(':("00010","00100","01000","01000","01000","00100","00010"),
 ')':("01000","00100","00010","00010","00010","00100","01000"),
}


def regions(path):
    d = open(path, 'rb').read()
    out, o = [], 0
    while o + 8 <= len(d):
        _a, n = struct.unpack_from('<II', d, o)
        o += 8
        out.append(d[o:o + n])
        o += n
    return out


def shot_rows(head, buf):
    w, h, pitch, bpp = struct.unpack_from('<IIII', head, 0)
    rows = []
    for y in range(h):
        row = bytearray()
        base = y * pitch
        for x in range(w):
            if bpp == 32:
                k = base + x * 4
                b, g, r = buf[k], buf[k + 1], buf[k + 2]
            else:
                k = base + x * 2
                v = struct.unpack_from('<H', buf, k)[0]
                r, g, b = ((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3
            row += bytes((r, g, b))
        rows.append(bytes(row))
    return w, h, rows


def text_rows(s, w, scale=2, pad=3):
    s = s.upper()
    gh, gw = 7, 6
    out = [bytearray(w * 3) for _ in range(gh * scale + pad * 2)]
    x0 = pad
    for chx in s:
        g = FONT.get(chx, FONT[' '])
        for gy in range(gh):
            for gx in range(5):
                if g[gy][gx] != '1':
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        px, py = x0 + gx * scale + sx, pad + gy * scale + sy
                        if px < w:
                            o = px * 3
                            out[py][o] = out[py][o + 1] = out[py][o + 2] = 255
        x0 += gw * scale
        if x0 >= w:
            break
    return [bytes(r) for r in out]


def png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def ch(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
        + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
        + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))


def main(out, *pairs):
    panels = []
    for spec in pairs:
        label, path, idx = spec.split(':')
        regs = regions(path)
        w, h, rows = shot_rows(regs[int(idx) * 2], regs[int(idx) * 2 + 1])
        panels.append((label, w, h, rows))
    gap = 8
    total_w = sum(p[1] for p in panels) + gap * (len(panels) + 1)
    label_h = 7 * 2 + 3 * 2
    total_h = max(p[2] for p in panels) + label_h + gap * 2
    sheet = [bytearray(total_w * 3) for _ in range(total_h)]
    x = gap
    for label, w, h, rows in panels:
        lab = text_rows(label, w)
        for i, r in enumerate(lab):
            sheet[gap + i][x * 3:(x + w) * 3] = r
        for i, r in enumerate(rows):
            sheet[gap + label_h + i][x * 3:(x + w) * 3] = r
        x += w + gap
    png(out, total_w, total_h, [bytes(r) for r in sheet])
    print('%s  %ux%u, %d panels' % (out, total_w, total_h, len(panels)))


if __name__ == '__main__':
    main(*sys.argv[1:])
