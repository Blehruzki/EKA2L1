"""Find an image's absolute relocations by linking it twice.

Rather than parse ELF relocation sections, link the same objects at two bases a
known distance apart and compare the flat output word by word.  A word that is
identical is position-independent; a word that differs by exactly the base
delta is an absolute address and needs a relocation.  A word that differs by
anything else means the assumption broke, and `diff` says so instead of
quietly emitting a wrong table.

The E32 relocation section groups entries by 4 KB page:

    u32 size of the entries that follow, NOT counting these two words
    u32 relocation count
    per page:  u32 page base (from the start of the code section)
               u32 block size, including these 8 bytes, padded to 4
               u16 entries: (type << 12) | offset within the page

Type 3 is KInferredRelocType, which is what shipped images use: the loader
works out from the address whether a relocation lands in text or data.

The size word excluding its own header is the kind of detail that is cheap to
get wrong and expensive to debug -- a real device answers KErrCorrupt (-20) and
nothing else, while the emulator, which never reads that field, runs the image
happily.  `selftest` therefore rebuilds a shipped image's relocation section
from the relocations parsed out of it and requires a byte-for-byte match.
"""
import struct

TYPE_TEXT = 3
PAGE = 0x1000


class Mismatch(Exception):
    pass


def diff(low, high, delta):
    """Two flat builds -> the code offsets holding an absolute address."""
    if len(low) != len(high):
        raise Mismatch('the two links differ in size: %d vs %d' % (len(low), len(high)))
    out = []
    for off in range(0, len(low) & ~3, 4):
        a, = struct.unpack_from('<I', low, off)
        b, = struct.unpack_from('<I', high, off)
        if a == b:
            continue
        if (b - a) & 0xFFFFFFFF == delta:
            out.append(off)
        else:
            raise Mismatch('word at 0x%x moved by 0x%x, not the base delta 0x%x '
                           '(0x%08x -> 0x%08x)' % (off, (b - a) & 0xFFFFFFFF, delta, a, b))
    tail = len(low) & 3
    if tail and low[-tail:] != high[-tail:]:
        raise Mismatch('the trailing %d bytes differ but are not a whole word' % tail)
    return out


def section(offsets):
    """-> the serialised relocation section for those code offsets."""
    pages = {}
    for off in sorted(offsets):
        pages.setdefault(off & ~(PAGE - 1), []).append(off & (PAGE - 1))

    body = b''
    for base, entries in sorted(pages.items()):
        words = b''.join(struct.pack('<H', (TYPE_TEXT << 12) | e) for e in entries)
        if len(entries) & 1:
            words += b'\0\0'                 # pad the block to a whole word
        body += struct.pack('<II', base, 8 + len(words)) + words

    return struct.pack('<II', len(body), len(offsets)) + body


def selftest(image):
    """Rebuild a shipped image's relocation section and compare it byte for byte."""
    import e32imports, vtables
    d = open(image, 'rb').read()
    h = e32imports.header(d)
    mine = section(vtables.relocations(d, h))
    theirs = d[h['code_reloc_offset']:]
    ok = mine == theirs
    print('relocs selftest: %s (%d bytes)' % ('exact match' if ok else 'FAILED', len(theirs)))
    if not ok:
        n = min(len(mine), len(theirs))
        i = next((k for k in range(n) if mine[k] != theirs[k]), n)
        print('  first difference at byte %d\n  ours   %s\n  theirs %s'
              % (i, mine[max(0, i - 4):i + 12].hex(), theirs[max(0, i - 4):i + 12].hex()))
    return ok


if __name__ == '__main__':
    import sys
    sys.exit(0 if selftest(sys.argv[1]) else 1)
