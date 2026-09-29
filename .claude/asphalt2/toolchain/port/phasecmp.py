#!/usr/bin/env python3
"""phasecmp.py <a.bin> <b.bin> -- compare two multi-shot frame dumps that may
not be in step.

The title screen's "PRESS ANY KEY" blinks on a **sixty-frame** cycle, so two
shots sixty frames apart always catch the same phase. That is how phase 0's
first A/B pair appeared to show the reported screen size changing the layout
when each run had merely caught a different half of a blink (E227/E228,
retracted by E229/E230).

So do not pair by index, and do not threshold on brightness -- an earlier
version of this script did and classified every frame as "text on", because
the background alone is bright enough. Cluster the regions of each dump by
exact content instead, which needs no parameter, then ask whether every
distinct picture in A also occurs in B. If it does, the two configurations
draw the same frames and differ only in which phase they were sampled at.
"""
import struct, sys


def regions(p):
    d = open(p, 'rb').read()
    out, o = [], 0
    while o + 8 <= len(d):
        _a, n = struct.unpack_from('<II', d, o)
        o += 8
        out.append(d[o:o + n])
        o += n
    return out


def cluster(regs):
    """-> [(first index, [all indices])], grouping regions by exact content."""
    seen = []
    for i, r in enumerate(regs):
        for first, members in seen:
            if regs[first] == r:
                members.append(i)
                break
        else:
            seen.append((i, [i]))
    return seen


def main(pa, pb):
    A, B = regions(pa), regions(pb)
    ca, cb = cluster(A), cluster(B)
    print('%s: %d shots -> %d distinct picture(s) %s'
          % (pa, len(A), len(ca), [m for _f, m in ca]))
    print('%s: %d shots -> %d distinct picture(s) %s'
          % (pb, len(B), len(cb), [m for _f, m in cb]))
    unmatched = 0
    for fa, ma in ca:
        hit = next((fb for fb, _mb in cb if B[fb] == A[fa]), None)
        if hit is None:
            unmatched += 1
            print('  A picture %s has NO counterpart in B' % ma)
        else:
            print('  A picture %s == B picture (shot %d)' % (ma, hit))
    for fb, mb in cb:
        if not any(A[fa] == B[fb] for fa, _ma in ca):
            unmatched += 1
            print('  B picture %s has NO counterpart in A' % mb)
    print('\n%s' % ('IDENTICAL: every picture in one dump occurs in the other'
                    if not unmatched else '%d picture(s) unmatched' % unmatched))


if __name__ == '__main__':
    main(*sys.argv[1:3])
