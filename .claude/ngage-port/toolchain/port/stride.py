#!/usr/bin/env python3
"""stride.py <g6code.bin> -- measure a game's row stride and picture origin
off its own buffer, without being told either.

Why this exists rather than looking at a rendering: a rendering needs the two
numbers before it can be made, so it can only ever confirm the guess it was
given. Asphalt 2's origin of sixteen, applied to a buffer that does not have
one, puts a bright seam down the left edge and reads convincingly as
horizontal wrapping -- which fooled this port once (PORTING.md, "The
renderer, not the port") and is exactly what must not happen to the next
game. So: measure first, render second.

Three measurements, none of which assumes anything about the other:

1. **The stride**, from vertical continuity. Rows of a photograph resemble
   the row above; rows of a buffer cut at the wrong stride do not. For every
   candidate byte-stride S, the mean |b[i] - b[i + S]| over the whole buffer
   is a smooth curve with a sharp minimum at the true stride and weaker ones
   at its multiples. Pixel format does not matter: adjacent rows of an image
   are similar byte for byte whatever the bits mean.

2. **The hard vertical seam**, from horizontal discontinuity. With the stride
   known, the mean |column x - column x+1| over all rows is flat inside the
   picture and spikes wherever the picture stops and padding begins. That
   spike is where a left-hand margin ends -- the origin.

3. **The wrap signature**, which is what tells a real horizontal wrap from
   something that merely looks like one: in a wrapped picture the first K
   columns of row y are the last K columns of row y-1. Counted, per K, as a
   fraction of rows. A wrap scores near 1. Anything else scores near 0.
"""
import struct
import sys


def regions(path):
    d = open(path, 'rb').read()
    out, o = [], 0
    while o + 8 <= len(d):
        addr, n = struct.unpack_from('<II', d, o)
        o += 8
        out.append((addr, d[o:o + n]))
        o += n
    return out


def stride_curve(buf, lo=64, hi=1600, step=2, sample=1 << 18):
    """{stride in bytes: mean |b[i] - b[i+S]|}. Sampled, because the whole
    buffer at every stride is 150 million comparisons."""
    n = min(len(buf), sample)
    out = {}
    for s in range(lo, hi + 1, step):
        if s >= n:
            break
        total = 0
        count = 0
        # Every 7th byte: a prime step, so the sample cannot land in phase
        # with the stride under test and flatter it.
        for i in range(0, n - s, 7):
            total += abs(buf[i] - buf[i + s])
            count += 1
        out[s] = total / count
    return out


def best_strides(curve, n=8):
    """The lowest points of the curve that are also local minima."""
    ks = sorted(curve)
    mins = []
    for i, s in enumerate(ks):
        if i == 0 or i == len(ks) - 1:
            continue
        if curve[s] <= curve[ks[i - 1]] and curve[s] <= curve[ks[i + 1]]:
            mins.append((curve[s], s))
    mins.sort()
    return mins[:n]


def column_delta(buf, stride, px=2):
    """Mean |column x - column x+1| across all rows, at one byte per pixel
    pair. Returns a list indexed by pixel column."""
    w = stride // px
    rows = len(buf) // stride
    out = []
    for x in range(w - 1):
        total = 0
        for y in range(rows):
            a = y * stride + x * px
            b = a + px
            va = int.from_bytes(buf[a:a + px], 'little')
            vb = int.from_bytes(buf[b:b + px], 'little')
            total += abs(va - vb)
        out.append(total / max(rows, 1))
    return out


def wrap_score(buf, stride, k, px=2):
    """Fraction of rows whose first k pixels equal the previous row's last k."""
    rows = len(buf) // stride
    hit = 0
    tried = 0
    for y in range(1, rows):
        head = buf[y * stride:y * stride + k * px]
        tail = buf[y * stride - k * px:y * stride]
        if not head or len(head) != len(tail):
            continue
        tried += 1
        if head == tail:
            hit += 1
    return hit / tried if tried else 0.0


def main(path):
    regs = regions(path)
    print('%s: %d region(s)' % (path, len(regs)))
    for i, (addr, buf) in enumerate(regs):
        print('\n=== region %d: %d bytes from %#x' % (i, len(buf), addr))
        curve = stride_curve(buf)
        for score, s in best_strides(curve):
            print('   stride %5d bytes (%6.1f px at 16bpp)   mean |dy| %7.2f'
                  % (s, s / 2, score))
        if not curve:
            continue
        s = best_strides(curve)[0][1]
        print('   -- taking %d bytes = %g pixels --' % (s, s / 2))
        cd = column_delta(buf, s)
        top = sorted(range(len(cd)), key=lambda x: -cd[x])[:6]
        flat = sum(cd) / len(cd)
        print('   mean column delta %.1f; sharpest seams at columns %s'
              % (flat, ', '.join('%d (%.0f)' % (x, cd[x]) for x in sorted(top))))
        print('   wrap signature: %s'
              % ', '.join('K=%d %.0f%%' % (k, 100 * wrap_score(buf, s, k))
                          for k in (4, 8, 12, 16, 20, 24)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'g6code.bin')
