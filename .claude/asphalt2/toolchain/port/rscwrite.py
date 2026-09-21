"""Write Symbian .rsc resource files.

Everything here was read out of shipped Nokia-built files and is checked by
selftests that reproduce those files byte for byte.

Container (uid1 0x101F4A6B, not dictionary-compressed):
    0x00  3 x u32 UID, then the UID checksum
    0x10  flags byte (unused on this path)
    0x11  u16 size of the largest resource once expanded
    0x13  bit array: one bit per resource, set when it is unicode-compressed
    0x14  resource data
    ...   index of u16 resource offsets, then a final u16 holding its own offset

A compressed resource is a sequence of alternating runs, the first one
unicode-compressed: a length byte (or a 0x80-flagged big-endian pair) followed
by that many bytes.  Compressed runs hold plain ASCII, which the expander
widens to UTF-16 after padding to an even offset -- which is why the reader
sees aligned 16-bit strings in a file that contains none.
"""
import struct

UID1_RSC = 0x101F4A6B
UID2_APP_REG = 0x101F8021
RES_DATA_OFFSET = 20


def _runlen(n):
    return bytes([n]) if n < 0x80 else bytes([0x80 | (n >> 8), n & 0xFF])


def encode_runs(runs):
    """runs: [('u'|'c', bytes)], starting with a 'u' run (possibly empty)."""
    out = bytearray()
    for _kind, payload in runs:
        out += _runlen(len(payload)) + payload
    return bytes(out)


def expanded_size(runs):
    written = 0
    for kind, payload in runs:
        if not payload:
            continue
        if kind == 'u':
            written += (written & 1) + 2 * len(payload)
        else:
            written += len(payload)
    return written


def string_runs(prefix, text):
    """A copy run of `prefix` plus the string's length, then the text itself."""
    raw = text.encode('ascii')
    return [('c', prefix + bytes([len(raw)])), ('u', raw)]


def container(uid2, uid3, resources, unicode_bits, largest):
    """resources: list of already-encoded resource bodies, resource 1 first."""
    import mke32
    out = bytearray()
    out += struct.pack('<III', UID1_RSC, uid2, uid3)
    out += struct.pack('<I', mke32.uid_checksum(UID1_RSC, uid2, uid3))
    out += bytes([0])
    out += struct.pack('<H', largest)
    out += bytes([unicode_bits])
    assert len(out) == RES_DATA_OFFSET

    offsets = []
    for body in resources:
        offsets.append(len(out))
        out += body

    index_off = len(out)
    for off in offsets:
        out += struct.pack('<H', off)
    out += struct.pack('<H', index_off)
    return bytes(out)
