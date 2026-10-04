#!/usr/bin/env python3
"""readstall.py <g6stall-<stem>.dat> [--game <title>]

The watchdog's dump (gate6.cpp, gate6_watchdog): written once, from a
thread of the port's own, when the main thread's heartbeat has stopped for
WATCHDOG_STALL_S seconds. Counters, the last BOX_RING traced calls (oldest
first, as the ring wraps), the kick objects' self-completion counts, and the
main thread's active scheduler queue read from memory.
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

def main():
    args = sys.argv[1:]
    game = None
    if '--game' in args:
        i = args.index('--game'); game = args[i + 1]; del args[i:i + 2]
    d = open(args[0], 'rb').read()
    w = list(struct.unpack('<%dI' % (len(d) // 4), d))
    names = {}
    if game:
        import readlog
        n = readlog.names(readlog.game_image(game))
        names = n if isinstance(n, dict) else dict(enumerate(n))
    if w[0] != 0x57A11000:
        sys.exit('not a stall dump (magic %08x)' % w[0])
    beats, traced, last, frames, completes, kicks = w[1:7]
    print('heartbeats %d   traced calls %d   last import %d %s   frames %d   completions %d' %
          (beats, traced, last, names.get(last, ''), frames, completes))
    k = 7
    print('kick objects built: %d' % kicks)
    for i in range(16):
        obj, cnt = w[k], w[k + 1]; k += 2
        if obj: print('  kick %2d  %08x  %d self-completions' % (i, obj, cnt))
    ring = w[k]; k += 1
    ent = [(w[k + 2 * i], w[k + 2 * i + 1]) for i in range(ring)]; k += 2 * ring
    start = traced % ring
    print('the last %d traced calls, oldest first:' % ring)
    for i in range(ring):
        idx, frm = ent[(start + i) % ring]
        print('  import %-4d %-46s from %x' % (idx, names.get(idx, '')[:46], frm))
    objs = w[k]; k += 1
    print('main thread scheduler queue, %d objects:' % objs)
    for i in range(objs):
        o, vt, runl, st, fl, pr = w[k:k + 6]; k += 6
        if pr >= 1 << 31: pr -= 1 << 32
        print('  %5d  obj %08x vt %08x RunL %08x  iStatus %08x flags %x' % (pr, o, vt, runl, st, fl))

if __name__ == '__main__':
    main()
