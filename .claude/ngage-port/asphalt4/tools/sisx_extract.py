#!/usr/bin/env python3
"""sisx_extract.py <pkg.sis> <outdir> -- list a SISX (S60 3rd) package's files and write them out."""
import os, struct, sys, zlib

def hdr(b, o):
    t, l = struct.unpack_from('<II', b, o); o += 8
    if l & 0x80000000:                       # 63-bit length
        l2, = struct.unpack_from('<I', b, o); o += 4
        l = (l & 0x7FFFFFFF) | (l2 << 31)
    return t, l, o

def fields(b, o, end):
    while o + 8 <= end:
        t, l, d = hdr(b, o)
        yield t, d, d + l
        o = d + l + (-l % 4)

def decomp(b, d, e):
    alg, = struct.unpack_from('<I', b, d); size, = struct.unpack_from('<Q', b, d + 4)
    raw = b[d + 12:e]
    return zlib.decompress(raw) if alg == 1 else raw

def sisstring(b, d, e):
    return b[d:e].decode('utf-16le', 'replace')

def array(b, d, e):
    et, = struct.unpack_from('<I', b, d); o = d + 4; out = []
    while o + 4 <= e:
        l, = struct.unpack_from('<I', b, o); o += 4
        out.append((et, o, o + l)); o += l + (-l % 4)
    return out

def main(path, outdir):
    b = open(path, 'rb').read()
    uids = struct.unpack_from('<4I', b, 0)
    print('uids', ' '.join('%08x' % u for u in uids))
    t, l, d = hdr(b, 16)
    assert t == 12, t
    ctrl = data = None
    for ft, fd, fe in fields(b, d, d + l):
        if ft == 3 and ctrl is None:
            ctrl = decomp(b, fd, fe)
        elif ft == 30:
            data = (fd, fe)
    descs = []
    def walk(buf, o, end, depth=0):
        for ft, fd, fe in fields(buf, o, end):
            if ft == 24:
                tt, tl, td = hdr(buf, fd); target = sisstring(buf, td, td + tl)
                o2 = td + tl + (-tl % 4)
                mt, ml, md = hdr(buf, o2); o2 = md + ml + (-ml % 4)
                ct, cl, cd = hdr(buf, o2)
                if ct == 41: o2 = cd + cl + (-cl % 4); ct, cl, cd = hdr(buf, o2)
                o2 = cd + cl + (-cl % 4)              # past the hash
                op, opts = struct.unpack_from('<II', buf, o2)
                ln, uln = struct.unpack_from('<QQ', buf, o2 + 8)
                idx, = struct.unpack_from('<I', buf, o2 + 24)
                descs.append((idx, target, op, uln))
            elif ft == 2:
                for et, ad, ae in array(buf, fd, fe):
                    if et in (24, 26, 27, 28, 13, 14):
                        walk(buf, ad - 8, ae, depth + 1) if False else None
                        # array elements carry no header: rebuild one
                        fake = struct.pack('<II', et, ae - ad) + buf[ad:ae] + b'\0' * (-(ae - ad) % 4)
                        walk(fake, 0, len(fake), depth + 1)
            elif ft in (13, 14, 26, 27, 28):
                walk(buf, fd, fe, depth + 1)
            elif ft == 1 and depth <= 3:
                s = sisstring(buf, fd, fe)
                if s.strip(): print('  string:', s)
    walk(ctrl, 0, len(ctrl))
    files = []
    if data:
        for ft, fd, fe in fields(b, *data):
            if ft == 2:
                for et, ad, ae in array(b, fd, fe):          # data units
                    for ft2, fd2, fe2 in fields(b, ad, ae):
                        if ft2 == 2:
                            for et3, ad3, ae3 in array(b, fd2, fe2):   # file data
                                for ft4, fd4, fe4 in fields(b, ad3, ae3):
                                    if ft4 == 3:
                                        files.append(decomp(b, fd4, fe4))
    os.makedirs(outdir, exist_ok=True)
    for idx, target, op, uln in sorted(descs):
        name = target.replace('\\', '/').split('/')[-1] or 'file%d' % idx
        blob = files[idx] if idx < len(files) else b''
        print('%3d op=%d %7d %-50s' % (idx, op, len(blob), target))
        if blob:
            open(os.path.join(outdir, name), 'wb').write(blob)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
