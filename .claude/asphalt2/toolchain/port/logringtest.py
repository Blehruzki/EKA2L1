#!/usr/bin/env python3
"""logringtest.py -- the log reader, against every state the file can be in.

The log is written by gate6.cpp and read by readlog.py, and the two halves
have to agree about a format with four states: the head still filling, the
head full and the ring partly written, the ring wrapped once, and a file
from before any of this existed. Build 007 shipped a reader that was right
in two of those and silently dropped the ring in a third -- the state a
short run ends in, which is exactly the run a crash produces.

Nothing here touches a phone or an emulator. It builds each state as bytes
and checks the reader gives back the records that went in, in order.

    logringtest.py
"""
import struct
import sys

import readlog

HDR = readlog.LOG_HDR_BYTES // 8
RING = readlog.LOG_HEAD_BYTES // 8
WRAP = readlog.NOTE_LOG_WRAP


def build(records, total_recs, head_end, write, wrapped, ring_at=None):
    """A log file as bytes: header, head, ring. `records` is what a reader
    should get back; they are laid out the way gate6.cpp lays them out."""
    buf = [(0, 0)] * total_recs
    for i in range(HDR):
        buf[i] = (WRAP, 0)
    buf[0] = (WRAP, write * 8)
    buf[1] = (WRAP, head_end * 8)
    buf[2] = (WRAP, 1 if wrapped else 0)
    return buf


def case(name, place, expect):
    """`place` fills a buffer and returns it; `expect` is the record list."""
    buf = place()
    d = b''.join(struct.pack('<II', a, b) for a, b in buf)
    path = '/tmp/logringtest.bin'
    open(path, 'wb').write(d)
    got = readlog.read(path)
    ok = got == expect
    print('%-46s %s' % (name, 'ok' if ok else 'FAILED'))
    if not ok:
        print('   expected %d records %s' % (len(expect), expect[:4]))
        print('   got      %d records %s' % (len(got), got[:4]))
    return ok


def main():
    total = RING + 64              # a small ring, so the cases are readable
    ok = True

    # 1. No header at all: a log from before the ring existed.
    plain = [(100 + i, i) for i in range(50)]
    ok &= case('a log from before the ring',
               lambda: list(plain), list(plain))

    # 2. The head is still filling. Nothing in the ring yet.
    n = 40
    head = [(200 + i, i) for i in range(n)]

    def place2():
        buf = [(0, 0)] * total
        buf[0] = (WRAP, (HDR + n) * 8)
        buf[1] = (WRAP, (HDR + n) * 8)
        buf[2] = (WRAP, 0)
        buf[HDR:HDR + n] = head
        return buf
    ok &= case('the head still filling', place2, list(head))

    # 3. The head is full and the ring is partly written, not yet wrapped.
    #    This is the case build 007 got wrong.
    #
    #    The head end must be a little *short* of the ring's start, because
    #    that is what the writer produces: a block is written whole, so the
    #    head stops at the last block that fitted. Round 93's own log has
    #    261,472 against a boundary of 262,144. The first version of this
    #    test used an exact fit and so did not reproduce the state at all --
    #    a case that cannot fail is not a case.
    SHORT = SHORT6 = 12                     # records of the head block that did not fit
    hn, rn = RING - HDR - SHORT, 30
    head3 = [(300 + (i % 90), i) for i in range(hn)]
    ring3 = [(400 + i, i) for i in range(rn)]

    def place3():
        buf = [(0, 0)] * total
        buf[0] = (WRAP, (RING + rn) * 8)
        buf[1] = (WRAP, (HDR + hn) * 8)
        buf[2] = (WRAP, 0)
        buf[HDR:HDR + hn] = head3
        buf[RING:RING + rn] = ring3
        return buf
    ok &= case('the head full, the ring not yet round', place3, head3 + ring3)

    # 4. The ring has wrapped. The write pointer is the oldest record.
    older = [(500 + i, i) for i in range(40)]      # written first, further on
    newer = [(600 + i, i) for i in range(24)]      # written after the wrap

    def place4():
        buf = [(0, 0)] * total
        buf[0] = (WRAP, (RING + len(newer)) * 8)
        buf[1] = (WRAP, (HDR + len(head3)) * 8)
        buf[2] = (WRAP, 1)
        buf[HDR:HDR + len(head3)] = head3
        buf[RING:RING + len(newer)] = newer
        buf[RING + len(newer):RING + len(newer) + len(older)] = older
        return buf
    ok &= case('the ring wrapped once', place4, head3 + older + newer)

    # 5. A header that makes no sense -- a file caught mid-write, say --
    #    must not throw and must not invent records.
    def place5():
        buf = [(0, 0)] * 40
        buf[0] = (WRAP, 0xFFFFFF * 8)
        buf[1] = (WRAP, 0xFFFFFF * 8)
        buf[2] = (WRAP, 1)
        return buf
    ok &= case('a header pointing off the end', place5, [(0, 0)] * (40 - HDR))

    # 6 and 7. Builds 007 and 008 write a two-record header and are on a
    #    phone as this is written, so their logs still have to open. The
    #    ring's write pointer is not in the file; it is recovered from
    #    where the trailing zeroes begin.
    hn6, rn6 = RING - 2 - SHORT6, 30
    head6 = [(700 + (i % 90), i) for i in range(hn6)]
    ring6 = [(800 + i, i) for i in range(rn6)]

    def place6():
        buf = [(0, 0)] * total
        buf[0] = (WRAP, 0)                       # 0 meant "not wrapped"
        buf[1] = (WRAP, (2 + hn6) * 8)
        buf[2:2 + hn6] = head6
        buf[RING:RING + rn6] = ring6
        return buf
    ok &= case('build 007/008, ring not yet round', place6, head6 + ring6)

    old7, new7 = [(900 + i, i) for i in range(20)], [(950 + i, i) for i in range(16)]

    def place7():
        buf = [(0, 0)] * total
        buf[0] = (WRAP, (RING + len(new7)) * 8)
        buf[1] = (WRAP, (2 + hn6) * 8)
        buf[2:2 + hn6] = head6
        buf[RING:RING + len(new7)] = new7
        buf[RING + len(new7):RING + len(new7) + len(old7)] = old7
        return buf
    ok &= case('build 007/008, ring wrapped', place7, head6 + old7 + new7)

    # 8. Round twice: the ring is full, there are no zeroes to trim, and
    #    the rotation has to be exact or every record is in the wrong place.
    full = total - RING
    cut = 37
    older8 = [(1000 + i, i) for i in range(full - cut)]
    newer8 = [(1100 + i, i) for i in range(cut)]

    def place8():
        buf = [(0, 0)] * total
        buf[0] = (WRAP, (RING + cut) * 8)
        buf[1] = (WRAP, (HDR + len(head3)) * 8)
        buf[2] = (WRAP, 1)
        buf[HDR:HDR + len(head3)] = head3
        buf[RING:RING + cut] = newer8
        buf[RING + cut:] = older8
        return buf
    ok &= case('the ring round twice, nothing to trim', place8,
               head3 + older8 + newer8)

    print('\n%s' % ('all cases pass' if ok else '** SOME CASES FAILED **'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
