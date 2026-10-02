#!/usr/bin/env python3
"""Read the port's worker-thread log (C:\\g6wrk.log, WORKER_LOG=1 in gate6.cpp).
    readwrk.py <g6wrk.log> --game <name>
Eight bytes a record, (code, from), written in order by whichever worker
thread logged; no header, no ring. The codes are the ring log's: import
indices below 900, notes, crumbs. Round 113: Ashen's sound thread, whose
records the ring log never carried."""
import struct, sys
import readlog
def main():
    args = sys.argv[1:]
    game = readlog.game_image(args[args.index('--game') + 1] if '--game' in args else None)
    imports, marks = readlog.names(game), readlog.crumbs()
    d = open(args[0], 'rb').read()
    n = len(d) // 8
    print('%d records' % n)
    for i in range(n):
        code, frm = struct.unpack_from('<II', d, 8 * i)
        print('%6d  %-45s from %x' % (i, readlog.label(code, imports, marks), frm))
if __name__ == '__main__':
    main()
