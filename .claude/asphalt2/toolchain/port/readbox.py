#!/usr/bin/env python3
"""readbox.py <g6box.dat>... -- the black box: how far, where, and the last
sixteen traced events in the order they happened.

The log says everything and costs a write per block; the box says the last
sixteen and costs one write however often it is taken. On a phone that goes
down hard, what survives is what was written, so the box is the instrument
that scales."""
import struct, sys
import readlog

# The ring is read out of gate6.cpp so the two cannot drift: it grew from
# sixteen to sixty-four the moment the box became the instrument.
import os, re


def ring_size():
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        return int(re.search(r'enum \{ BOX_RING = (\d+)', open(src).read()).group(1))
    except Exception:
        return 16


BOX_RING = ring_size()
BOX_FROM = 4 + BOX_RING
BOX_PATH = BOX_FROM + BOX_RING
BOX_SLOT, BOX_FRAMES, BOX_STACK = BOX_PATH + 1, BOX_PATH + 2, BOX_PATH + 3
MAGIC = 0x47364234
FLAGS = [(1, 'abort'), (2, 'restart'), (4, 'docancel'), (8, 'runerror'),
         (16, 'a slot of ours'), (32, 'THE FRAME LOOP RAN'),
         (64, 'User::Leave'), (128, 'User::Exit')]


def show(path, imports):
    d = open(path, 'rb').read()
    w = [struct.unpack_from('<I', d, i)[0] for i in range(0, len(d) & ~3, 4)]
    if not w or w[0] != MAGIC:
        print('%s: not a box (magic %08x)' % (path, w[0] if w else 0))
        return
    count = w[3]
    print('%s' % path)
    print('  %d traced events at the last write, so %d..%d in all' % (count, count, count + 31))
    print('  last import %d  %s' % (w[1], imports.get(w[1], '?')))
    print('  reached %s' % (', '.join(n for b, n in FLAGS if w[2] & b) or 'nothing'))
    print('  path %d   last slot %x   frames %d   stack high-water %d bytes'
          % (w[BOX_PATH], w[BOX_SLOT], w[BOX_FRAMES], w[BOX_STACK]))
    for i in range(BOX_RING):
        k = (count + i) & (BOX_RING - 1)
        code, frm = w[4 + k], w[BOX_FROM + k]
        if not code and not frm:
            continue
        print('  %5d  %-42s from %x'
              % (count - BOX_RING + i, readlog.label(code, imports, {}), frm))


def main():
    imports = readlog.names(readlog.GAME)
    for p in sys.argv[1:]:
        show(p, imports)


if __name__ == '__main__':
    main()
