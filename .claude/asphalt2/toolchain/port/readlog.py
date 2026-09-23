#!/usr/bin/env python3
"""Read the port's execution log, and diff two of them.

    readlog.py <log> [--game <6rbc.app>] [--tail N] [--counts]
    readlog.py <log> --diff <other log>

`gate6.cpp` appends one eight-byte record per event to C:\\g6box.log: what
happened, and the address it was called from as an offset into the loaded
image. Codes under 900 are import indices, which `gen_shim` names; 900 and up
are breadcrumbs planted in the game's own code, which `kCrumb` in gate6.cpp
places.

The point of the log over the sixteen-entry ring it replaced is the diff: run
the same build in the emulator and on the phone, line the two up, and the
first place they part company is the answer.
"""
import struct
import sys

GAME = '/root/.local/share/EKA2L1/data/drives/e.ngage/system/apps/6rbc/6rbc.app'
CRUMB_FIRST = 900


def read(path):
    d = open(path, 'rb').read()
    return [struct.unpack_from('<II', d, 8 * i) for i in range(len(d) // 8)]


def names(game):
    """-> {import index: signature}, best effort."""
    try:
        import gen_shim
        rows, _dlls = gen_shim.build(game)
        return {r[0]: (r[3] or '?') for r in rows}
    except Exception as exc:                        # the log still reads fine
        print('(no import names: %s)' % exc, file=sys.stderr)
        return {}


def crumbs():
    """-> {marker: planted offset}, read out of gate6.cpp so it cannot drift."""
    import os
    import re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        text = open(src).read()
        body = text[text.index('static const u32 kCrumb[] = {'):]
        body = body[:body.index('};')]
        return {CRUMB_FIRST + i: v for i, v in enumerate(re.findall(r'0x0[0-9a-f]+', body))}
    except Exception:
        return {}


NOTES = {860: 'slot entered', 850: 'Cancel on', 851: 'STRAY Cancel on',
         852: 'image loaded at', 853: 'chunk ends at'}


def label(code, imports, marks):
    if code >= CRUMB_FIRST:
        return 'marker %d at %s' % (code, marks.get(code, '?'))
    if code in NOTES:
        return '-- %s' % NOTES[code]
    return 'import %-4d %s' % (code, imports.get(code, '?'))


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    path = args[0]
    game = args[args.index('--game') + 1] if '--game' in args else GAME
    events = read(path)
    imports, marks = names(game), crumbs()

    if '--diff' in args:
        other = read(args[args.index('--diff') + 1])
        for i in range(max(len(events), len(other))):
            a = events[i] if i < len(events) else None
            b = other[i] if i < len(other) else None
            if a != b:
                print('part company at record %d of %d / %d' % (i, len(events), len(other)))
                for k in range(max(0, i - 4), min(max(len(events), len(other)), i + 5)):
                    ea = events[k] if k < len(events) else ('-', 0)
                    eb = other[k] if k < len(other) else ('-', 0)
                    mark = '  <<' if k == i else ''
                    print('%6d  %-44s | %-44s%s'
                          % (k,
                             'end' if ea[0] == '-' else label(ea[0], imports, marks) + ' from %x' % ea[1],
                             'end' if eb[0] == '-' else label(eb[0], imports, marks) + ' from %x' % eb[1],
                             mark))
                break
        else:
            print('identical, %d records' % len(events))
        return

    if '--counts' in args:
        counts = {}
        for code, _from in events:
            counts[code] = counts.get(code, 0) + 1
        for code in sorted(counts, key=lambda c: -counts[c]):
            print('%8d  %s' % (counts[code], label(code, imports, marks)))
        return

    tail = int(args[args.index('--tail') + 1]) if '--tail' in args else 40
    print('%d records' % len(events))
    for i, (code, frm) in enumerate(events[-tail:], start=len(events) - min(tail, len(events))):
        print('%6d  %-44s from %x' % (i, label(code, imports, marks), frm))


if __name__ == '__main__':
    main()
