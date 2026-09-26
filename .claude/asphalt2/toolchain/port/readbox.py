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
BOX_EXC = BOX_STACK + 1
BOX_SPARE, BOX_CTXSZ, BOX_WRAPS = BOX_EXC + 1, BOX_EXC + 2, BOX_EXC + 3
BOX_NAME, BOX_NAME_WORDS = BOX_WRAPS + 1, 8
BOX_LAUNCH = BOX_NAME + BOX_NAME_WORDS
BOX_TICK = BOX_LAUNCH + 1
BOX_HITS = BOX_NAME + BOX_NAME_WORDS    # mirrors gate6.cpp
WRAPS = [(1, 'allocators'), (2, 'frees'), (4, 'open-result'), (8, 'open-arg'),
         (16, 'LEAK: nothing is freed')]
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
    exc = w[BOX_EXC] if BOX_EXC < len(w) else 0
    print('  User::SetExceptionHandler said %d%s'
          % (exc, '' if exc == 0 else '   <- the handler is NOT installed'))
    if BOX_WRAPS < len(w):
        print('  setup: %d bytes of spare arena left, context %d bytes'
              % (w[BOX_SPARE], w[BOX_CTXSZ]))
        got = [n for b, n in WRAPS if w[BOX_WRAPS] & b]
        miss = [n for b, n in WRAPS if not (w[BOX_WRAPS] & b)]
        print('  wraps installed: %s%s'
              % (', '.join(got) or 'none',
                 '   MISSING: ' + ', '.join(miss) if miss else ''))
    if BOX_LAUNCH < len(w) and w[BOX_LAUNCH]:
        n = w[BOX_LAUNCH]
        tick = w[BOX_TICK] if BOX_TICK < len(w) else 0
        print('  this launch wrote g6box%d.log, at tick %d' % (n % 10, tick))
    name = ''.join(chr(h) if 32 <= h < 127 else ''
                   for k in range(BOX_NAME, min(BOX_NAME + BOX_NAME_WORDS, len(w)))
                   for h in (w[k] & 0xFFFF, w[k] >> 16))
    if name:
        print('  last file opened  ...%s' % name)
    # The worker probe: six words a worker thread filled and the main thread
    # carried out. See worker_probe in gate6.cpp.
    base = BOX_HITS + 28
    any_probe = False
    for slot in range(2):
        wrk = base + 6 * slot
        if len(w) <= wrk + 5 or w[wrk] != 0x57524B31:
            continue
        any_probe = True
        step = ('nothing', 'asked for its allocator', 'got it, asking for 16 bytes',
                'got the 16 bytes')[min(w[wrk + 5], 3)]
        print('  WORKER PROBE %d  at import %s, sp 0x%08x'
              % (slot, readlog.label(w[wrk + 3], imports, {}).strip(), w[wrk + 1]))
        print('     allocator 0x%08x   alloc(16) -> 0x%08x   got as far as: %s'
              % (w[wrk + 2], w[wrk + 4], step))
    if not any_probe and len(w) > base:
        print('  WORKER PROBE  never ran')
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
