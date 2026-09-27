#!/usr/bin/env python3
"""mkmbm.py -- lift the icons out of an EPOC AIF and write them as an MBM.

An `.aif` from the N-Gage era is a direct file store whose UID2 says "AIF"
(0x10003a38) rather than "multi-bitmap" (0x10000042), and whose bitmaps are
laid out exactly as an MBM's are: a 40-byte `SEpocBitmapHeader` followed by
the pixel data, one after another from offset 20. What differs is only the
trailer -- an AIF's carries caption and icon-size information, an MBM's is a
count and a table of offsets.

So converting one to the other copies the bitmaps byte for byte and writes a
new container around them. Nothing is decoded and nothing is re-encoded, so
whatever compression the original used survives, and the icon that ends up on
the phone is the one Gameloft drew.

    mkmbm.py <in.aif> <out.mbm> [index ...]

With no indices, every bitmap is copied. Icons come in pairs -- a colour
bitmap then its 1bpp mask -- and S60 reads them in that order, so a subset
should name whole pairs.
"""
import struct
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'port'))
import mke32

STORE_UID = 0x10000037          # KDirectFileStoreLayoutUid
MBM_UID = 0x10000042            # KMultiBitmapFileImageUid
HEADER_LEN = 40                 # SEpocBitmapHeader
FIRST = 20                      # uid triple, checksum, trailer offset


def bitmaps(data):
    """-> [(offset, length, header fields)], by walking the chain from 20.

    The AIF's own trailer is not a bitmap table, so the bitmaps are found the
    way the format lets you find them: each header's first word is the length
    of the whole bitmap, so one leads to the next.
    """
    out, o = [], FIRST
    while o + HEADER_LEN <= len(data):
        f = struct.unpack_from('<10i', data, o)
        length, struct_len, w, h = f[0], f[1], f[2], f[3]
        if length <= 0 or struct_len != HEADER_LEN:
            break
        if w <= 0 or h <= 0 or w > 1024 or h > 1024 or o + length > len(data):
            break
        out.append((o, length, f))
        o += length
    return out


def opaque_mask(w, h):
    """A 1bpp bitmap of the given size with every visible bit set.

    The AIF's own mask is not reusable, and the reason is a convention that
    reversed between the two platforms. Its rows read `00 00 00 00 00 f0 ff
    ff` -- for a 44-pixel row that is **every visible bit clear and the
    padding set**, which is the old AIF meaning: a set bit is *transparent*,
    and the icon is a solid square. S60v3 reads an MBM icon/mask pair the
    other way round, a set bit being opaque, so copying that mask across
    would ask the shell for an icon that is entirely transparent.

    Inverting it comes to the same thing as this, because the original is
    uniformly opaque, and this says so without depending on having read the
    original correctly.
    """
    stride = ((w + 31) // 32) * 4
    rows = bytearray()
    for _y in range(h):
        row = bytearray(stride)
        for x in range(w):
            row[x >> 3] |= 1 << (x & 7)
        rows += row
    header = struct.pack('<10i', HEADER_LEN + len(rows), HEADER_LEN, w, h,
                         0, 0, 1, 0, 0, 0)
    return header + bytes(rows)


def build(src, dst, want=None, mask=True):
    data = open(src, 'rb').read()
    found = bitmaps(data)
    if not found:
        raise ValueError('no bitmaps in %s' % src)
    if want:
        found = [found[i] for i in want]

    body, offsets, shown = b'', [], []
    for off, length, f in found:
        offsets.append(FIRST + len(body))
        body += data[off:off + length]
        shown.append(f)
        if mask and f[6] > 1:               # a colour bitmap wants a mask after it
            m = opaque_mask(f[2], f[3])
            offsets.append(FIRST + len(body))
            body += m
            shown.append(struct.unpack_from('<10i', m, 0))
    found = [(0, 0, f) for f in shown]

    trailer_off = FIRST + len(body)
    uids = (STORE_UID, MBM_UID, 0)
    head = struct.pack('<4I', *uids, mke32.uid_checksum(*uids))
    head += struct.pack('<I', trailer_off)
    trailer = struct.pack('<I', len(offsets))
    trailer += b''.join(struct.pack('<I', o) for o in offsets)

    open(dst, 'wb').write(head + body + trailer)
    return [f for _o, _l, f in found]


def describe(fields):
    return ('%3dx%-3d px  %d bpp  %s  compression %d'
            % (fields[2], fields[3], fields[6],
               'colour' if fields[7] else 'grey/mask', fields[9]))


def check(path):
    """Read it back the way EKA2L1's mbm_file does, and say what is in it."""
    d = open(path, 'rb').read()
    u1, u2, _u3, chk = struct.unpack_from('<4I', d, 0)
    assert u1 == STORE_UID and u2 == MBM_UID, 'wrong uids %08x %08x' % (u1, u2)
    assert chk == mke32.uid_checksum(u1, u2, 0), 'uid checksum does not match'
    trailer, = struct.unpack_from('<I', d, 16)
    n, = struct.unpack_from('<I', d, trailer)
    offs = struct.unpack_from('<%dI' % n, d, trailer + 4)
    assert trailer + 4 + 4 * n == len(d), 'trailer is not the end of the file'
    out = []
    for o in offs:
        f = struct.unpack_from('<10i', d, o)
        assert f[1] == HEADER_LEN, 'bad header at 0x%x' % o
        assert o + f[0] <= trailer, 'bitmap at 0x%x runs into the trailer' % o
        out.append(f)
    return out


if __name__ == '__main__':
    idx = [int(a) for a in sys.argv[3:]] or None
    got = build(sys.argv[1], sys.argv[2], idx)
    print('%s -> %s' % (sys.argv[1], sys.argv[2]))
    for i, f in enumerate(got):
        print('  [%d] %s' % (i, describe(f)))
    back = check(sys.argv[2])
    print('reads back: %d bitmap(s), %d bytes' % (len(back), os.path.getsize(sys.argv[2])))
