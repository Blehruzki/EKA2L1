"""Minimal SIS(X) reader/writer: descends only the path we need, everything else stays raw."""
import struct, zlib

CONTENTS, COMPRESSED, DATA, ARRAY, DATAUNIT, FILEDATA, FILEDESC = 12, 3, 30, 2, 31, 32, 24

def rd_hdr(buf, off):
    t, l = struct.unpack_from('<II', buf, off); off += 8; wide = False
    if l == 0xFFFFFFFF:
        l, = struct.unpack_from('<Q', buf, off); off += 8; wide = True
    return t, l, off, wide

class F:
    """type, wide-length flag, and either raw bytes or (elem_type, children).

    `tail` keeps whatever sits inside a container past its last field: this
    package's SISInfo declares two bytes more than its fields account for, and
    dropping them would change every length above it."""
    def __init__(s, t, wide=False, raw=None, elem=None, kids=None, tail=b''):
        s.t, s.wide, s.raw, s.elem, s.kids, s.tail = t, wide, raw, elem, kids, tail
    def body(s):
        if s.raw is not None: return s.raw
        out = struct.pack('<I', s.elem) if s.elem is not None else b''
        for k in s.kids:
            b = k.body()
            out += struct.pack('<I', len(b)) if s.elem is not None else b''
            if s.elem is None: out += hdr(k.t, len(b), k.wide)
            out += b + b'\0' * (-len(b) % 4)
        return out + s.tail
    def ser(s):
        b = s.body(); return hdr(s.t, len(b), s.wide) + b + b'\0' * (-len(b) % 4)

def hdr(t, l, wide):
    return struct.pack('<IIQ', t, 0xFFFFFFFF, l) if wide else struct.pack('<II', t, l)

def parse(buf, off, end, descend):
    return parse_body(buf, off, end, descend)[0]


def parse_body(buf, off, end, descend):
    """descend(type) -> True to recurse; arrays always recurse.

    Returns (fields, trailing bytes)."""
    out = []
    while off + 8 <= end:
        t, l, doff, wide = rd_hdr(buf, off)
        if l > end - doff: break
        if t == ARRAY:
            elem, = struct.unpack_from('<I', buf, doff)
            kids, o = [], doff + 4
            while o + 4 <= doff + l:
                el, = struct.unpack_from('<I', buf, o); o += 4
                if el > doff + l - o: break
                kids.append(sub(buf, o, o + el, elem, descend))
                o += el + (-el % 4)
            out.append(F(t, wide, elem=elem, kids=kids))
        elif descend(t):
            kids, tail = parse_body(buf, doff, doff + l, descend)
            out.append(F(t, wide, kids=kids, tail=tail))
        else:
            out.append(F(t, wide, raw=buf[doff:doff + l]))
        off = doff + l + (-l % 4)
    return out, buf[off:end]

def sub(buf, doff, dend, t, descend):
    if t == ARRAY:
        elem, = struct.unpack_from('<I', buf, doff)
        kids, o = [], doff + 4
        while o + 4 <= dend:
            el, = struct.unpack_from('<I', buf, o); o += 4
            if el > dend - o: break
            kids.append(sub(buf, o, o + el, elem, descend))
            o += el + (-el % 4)
        return F(t, elem=elem, kids=kids)
    if descend(t):
        kids, tail = parse_body(buf, doff, dend, descend)
        return F(t, kids=kids, tail=tail)
    return F(t, raw=buf[doff:dend])

PATH = {CONTENTS, DATA, DATAUNIT, FILEDATA}

def load(path):
    buf = open(path, 'rb').read()
    return buf[:16], parse(buf, 16, len(buf), lambda t: t in PATH)[0], buf

def save(path, head, contents):
    open(path, 'wb').write(head + contents.ser())

def walk(f, t):
    if f.t == t: yield f
    for k in (f.kids or []):
        yield from walk(k, t)
