"""Find an image's absolute relocations by linking it twice.

Rather than parse ELF relocation sections, link the same objects at two bases a
known distance apart and compare the flat output word by word.  A word that is
identical is position-independent; a word that differs by exactly the base
delta is an absolute address and needs a relocation.  A word that differs by
anything else means the assumption broke, and `diff` says so instead of
quietly emitting a wrong table.

The E32 relocation section groups entries by 4 KB page:

    u32 section size, u32 relocation count
    per page:  u32 page base (from the start of the code section)
               u32 block size, including these 8 bytes, padded to 4
               u16 entries: (type << 12) | offset within the page

Type 3 is a text relocation, which is all a code-only image needs.
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

    return struct.pack('<II', 8 + len(body), len(offsets)) + body
