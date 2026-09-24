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


def late_crumbs():
    """Markers planted after the decryptor has run, read out of gate6.cpp."""
    import os, re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        text = open(src).read()
        body = text[text.index('static const u32 kLateCrumb[] = {'):]
        body = body[:body.index('};')]
        return {940 + i: v for i, v in enumerate(re.findall(r'0x0[0-9a-f]+', body))}
    except Exception:
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
        out = {CRUMB_FIRST + i: v for i, v in enumerate(re.findall(r'0x0[0-9a-f]+', body))}
    except Exception:
        out = {}
    out.update(late_crumbs())
    out.update(probes())
    return out


def probes():
    """Probe markers, read out of kProbe in gate6.cpp."""
    import os, re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        text = open(src).read()
        body = text[text.index('static const Probe kProbe[] = {'):]
        body = body[:body.index('};')]
        return {990 + i: m for i, m in
                enumerate(re.findall(r'\{\s*(0x0[0-9a-f]+)', body))}
    except Exception:
        return {}


# Notes whose payload is an address: the same run on two machines loads the
# image somewhere different, so the number is worth printing and not comparing.
ADDRESS_NOTES = {850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 875, 876}

NOTES = {860: 'slot entered', 850: 'Cancel on', 851: 'STRAY Cancel on',
         852: 'image loaded at', 853: 'chunk ends at',
         854: 'APP UI VPTR CHANGED to', 855: 'decrypted literal',
         856: '  and it points at a word', 857: 'r5 =', 858: '    word', 859: '    text', 870: 'returned', 871: '  by import',
         872: '  asked for', 873: 'about to call import',
         874: 'ordinal asked of a library that is not open',
         875: 'lookup on handle', 876: '   answered', 877: 'DRIVER CALL refused, euser ordinal',
         878: 'tick', 879: 'cell size', 881: 'buffer sits in the cell at',
         880: 'OVERFLOW: the server was given a maximum of',
         882: 'probe b', 883: 'free', 884: '  matched a live cell of',
         885: '  DOUBLE FREE of', 886: '  STRAY: never allocated'}

# How many slots of each wrapper's vtable are a copy of a real one. Past that
# is the margin gate6.cpp pads with, and a call landing there is the framework
# asking for a slot the class was not measured to have.
OBJECTS = {1: ('app', 18), 2: ('doc', 23), 3: ('appui', 45),
           4: ('control', 44), 5: ('timer', 6)}


def slot_name(code):
    obj, i = code >> 8, code & 0xFF
    if obj not in OBJECTS:
        return 'slot %x' % code
    name, n = OBJECTS[obj]
    return '%s slot %d%s' % (name, i, '   PAST THE END' if i >= n else '')


def label(code, imports, marks):
    if code >= CRUMB_FIRST:
        return 'marker %d at %s' % (code, marks.get(code, '?'))
    if code == 860:
        return '-- entered'
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
        if '--no-slots' in args:
            # The framework works its way through our vtables in its own order,
            # and two feature packs do not agree on it. That is noise in a diff
            # and detail in a listing, so it comes out here and stays there.
            events = [e for e in events if e[0] != 860]
            other = [e for e in other if e[0] != 860]

        def same(a, b):
            """A note whose payload is an address says nothing across machines."""
            if a is None or b is None:
                return False
            if a[0] != b[0]:
                return False
            return a[0] in ADDRESS_NOTES or a[1] == b[1]

        for i in range(max(len(events), len(other))):
            a = events[i] if i < len(events) else None
            b = other[i] if i < len(other) else None
            if not same(a, b):
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
        if code == 860:
            shown = slot_name(frm)
        elif code == 859:
            # two UTF-16 characters to a record, low half first
            shown = ''.join(chr(h) if 32 <= h < 127 else '.'
                            for h in (frm & 0xFFFF, frm >> 16))
        else:
            shown = '%x' % frm
        print('%6d  %-44s from %s' % (i, label(code, imports, marks), shown))


if __name__ == '__main__':
    main()
