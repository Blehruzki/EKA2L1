#!/usr/bin/env python3
"""readhold.py <log> -- what a held mode key actually did.

Prints every mode change with the tick it happened at and the gap since the
last, so the cadence can be checked against what was asked for rather than
assumed: the first change about a second after the key goes down, then one
every half second. One tick is 1/64 s.
"""
import struct, sys

MODE_NOW, CFG_READ, CFG_WROTE, TICK, KEY = 825, 823, 824, 878, 835
NAMES = ('1:1', 'aspect', 'fill', 'integer', 'full')


def main(path):
    d = open(path, 'rb').read()
    ev = [struct.unpack_from('<II', d, 8 * i) for i in range(len(d) // 8)]
    # The clock nearest each event, for turning record order into seconds.
    tick, last = 0, None
    changes, writes = [], []
    for code, v in ev:
        if code == TICK:
            tick = v
        elif code == MODE_NOW:
            changes.append((tick, v))
        elif code == CFG_WROTE:
            writes.append((v >> 16, v & 0xFFFF))
        elif code == CFG_READ:
            print('saved choice found: mode %d, inset override %d' % (v >> 16, v & 0xFFFF))
    if not changes:
        print('no mode change recorded')
    else:
        print('%d mode change(s):' % len(changes))
        for t, m in changes:
            gap = '' if last is None else '  +%.2f s' % ((t - last) / 64.0)
            print('   tick %-8d -> %-8s%s' % (t, NAMES[m] if m < len(NAMES) else m, gap))
            last = t
    if writes:
        ok = sum(1 for _m, e in writes if e == 0)
        print('%d config write(s), %d returned KErrNone' % (len(writes), ok))
        bad = [(m, e) for m, e in writes if e != 0]
        if bad:
            print('   failures: %s' % bad)
    keys = sum(1 for c, _v in ev if c == KEY)
    print('%d key records in the run' % keys)


if __name__ == '__main__':
    main(sys.argv[1])
