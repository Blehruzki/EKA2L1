"""Locate the SHA-1 in each SISFileDescription of a SIS controller.

The installer on a real device re-hashes every file it extracts and compares it
with the hash stored here.  EKA2L1 skips that check, so a stale hash installs
fine in the emulator and panics on hardware.

Field numbering is EKA2L1's sis_field_type: SISFileDes=24, SISHash=25,
SISCapabilities=41, SISBlob=37.
"""
import struct

FILEDES, HASH, CAPS, BLOB, ARRAY = 24, 25, 41, 37, 2
CONTAINERS = {2,12,13,14,15,16,17,18,19,20,21,22,23,24,26,27,28,29,30,31,32,33,36,38,39,40}

def read_field(buf, off):
    """-> (type, body_off, body_len, next_off); 4-byte padded, bit31 = 64-bit length"""
    t, l = struct.unpack_from('<II', buf, off); off += 8
    if (l >> 31) & 1:
        hi, = struct.unpack_from('<I', buf, off); off += 4
        l = (l & 0x7fffffff) | (hi << 32)
    return t, off, l, off + l + (-l % 4)

def find_filedes(buf):
    """every SISFileDescription body, in file order"""
    out = []
    def walk(off, end):
        while off + 8 <= end:
            t, l = struct.unpack_from('<II', buf, off); off += 8
            if l == 0xFFFFFFFF:
                l, = struct.unpack_from('<Q', buf, off); off += 8
            if l > end - off:
                return
            if t == FILEDES:
                out.append((off, off + l))
            if t == ARRAY:
                e, = struct.unpack_from('<I', buf, off); o = off + 4
                while o + 4 <= off + l:
                    el, = struct.unpack_from('<I', buf, o); o += 4
                    if el > off + l - o:
                        break
                    if e == FILEDES:
                        out.append((o, o + el))
                    elif e in CONTAINERS:
                        walk(o, o + el)
                    o += el + (-el % 4)
            elif t in CONTAINERS:
                walk(off, off + l)
            off += l + (-l % 4)
    walk(0, len(buf))
    return out

def hash_slice(buf, body):
    """(offset, length) of the raw SHA-1 bytes inside one SISFileDescription"""
    o = body
    _, _, _, o = read_field(buf, o)                 # target
    _, _, _, o = read_field(buf, o)                 # mimeType
    t, = struct.unpack_from('<I', buf, o)
    if t == CAPS:
        _, _, _, o = read_field(buf, o)             # capabilities (optional)
    t, hs, _, _ = read_field(buf, o)
    assert t == HASH, 'expected SISHash, got %d' % t
    bt, bs, bl, _ = read_field(buf, hs + 4)         # after the u32 algorithm id
    assert bt == BLOB, 'expected SISBlob, got %d' % bt
    return bs, bl

SIGCERTCHAIN, CONTROLLER = 39, 13

def strip_signature(cbuf):
    """Drop every SISSignatureCertChain from an uncompressed controller.

    The signature covers the controller content that precedes it, so patching the
    install block invalidates it.  A package that claims to be signed but whose
    signature does not verify is a worse thing to hand an installer than one that
    never claimed to be signed -- SISController takes zero or more chains, so
    removing them is structurally legal and yields a plain unsigned package.
    """
    t, body, blen, end = read_field(cbuf, 0)
    assert t == CONTROLLER, 'expected SISController, got %d' % t
    kept, removed, o = [], 0, body
    while o < body + blen:
        ft, fs, fl, nxt = read_field(cbuf, o)
        if ft == SIGCERTCHAIN:
            removed += nxt - o
        else:
            kept.append(cbuf[o:nxt])
        o = nxt
    if not removed:
        return cbuf, 0
    new_body = b''.join(kept)
    out = bytearray(cbuf[:body])              # SISController header
    struct.pack_into('<I', out, 4, len(new_body))
    out += new_body
    out += cbuf[body + blen:]                 # anything trailing the controller
    return bytearray(out), removed
