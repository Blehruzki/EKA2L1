#!/usr/bin/env python3
"""sischeck.py <package.sis>: read a package back with nothing of mksis's.

Round 168. A parser of its own (not sisrw), so a mistake in the writer is not
repeated in the check: the two CRCs recomputed against their stored values,
the controller's field list, and -- for a signed package -- the signature
verified with the openssl command line against the certificate's own key,
over the controller's contents up to the SISSignatureCertificateChain.
Exit status 0 only if everything that is present checks out.
"""
import hashlib, os, struct, subprocess, sys, tempfile, zlib


def hdr(buf, o):
    t, l = struct.unpack_from('<II', buf, o); o += 8
    if l == 0xFFFFFFFF:
        l, = struct.unpack_from('<Q', buf, o); o += 8
    return t, l, o


def fields(buf, o, end):
    out = []
    while o + 8 <= end:
        s = o
        t, l, d = hdr(buf, o)
        out.append((t, s, d, l))
        o = d + l + (-l % 4)
    return out


def crc16(data):
    v = 0
    for c in data:
        v ^= c << 8
        for _ in range(8):
            v = ((v << 1) ^ 0x1021) if v & 0x8000 else v << 1
            v &= 0xFFFF
    return v


def main(path):
    b = open(path, 'rb').read()
    ok = True
    t, l, d = hdr(b, 16)
    top = {x[0]: x for x in fields(b, d, d + l)}
    for csum, field in ((34, 3), (35, 30)):
        if csum in top:
            _, s, fd, fl = top[field]
            stored = struct.unpack_from('<H', b, top[csum][2])[0]
            got = crc16(b[s:fd + fl + (-fl % 4)])
            print('%s checksum: stored %04x, computed %04x' %
                  ('controller' if field == 3 else 'data', stored, got))
            ok &= stored == got
    _, s, cd, cl = top[3]
    ctrl = zlib.decompress(b[cd + 12:cd + cl])
    t, l, d = hdr(ctrl, 0)
    kids = fields(ctrl, d, d + l)
    print('controller fields:', [k[0] for k in kids])
    chains = [k for k in kids if k[0] == 39]
    if not chains:
        print('unsigned')
    for chain in chains:
        signed = ctrl[d:chain[1]]
        ch = fields(ctrl, chain[2], chain[2] + chain[3])
        arr, certs = ch[0], ch[1]
        el, = struct.unpack_from('<I', ctrl, arr[2] + 4)
        sigf = fields(ctrl, arr[2] + 8, arr[2] + 8 + el)
        algf = fields(ctrl, sigf[0][2], sigf[0][2] + sigf[0][3])
        oid = ctrl[algf[0][2]:algf[0][2] + algf[0][3]].decode('utf-16-le')
        sig = ctrl[sigf[1][2]:sigf[1][2] + sigf[1][3]]
        cc = fields(ctrl, certs[2], certs[2] + certs[3])
        cert = ctrl[cc[0][2]:cc[0][2] + cc[0][3]]
        with tempfile.TemporaryDirectory() as tmp:
            p = lambda n: os.path.join(tmp, n)
            open(p('s'), 'wb').write(signed); open(p('g'), 'wb').write(sig)
            open(p('c'), 'wb').write(cert)
            pub = subprocess.run(['openssl', 'x509', '-inform', 'DER', '-in', p('c'),
                                  '-pubkey', '-noout'], capture_output=True, check=True).stdout
            open(p('k'), 'wb').write(pub)
            r = subprocess.run(['openssl', 'dgst', '-sha1', '-verify', p('k'), '-signature',
                                p('g'), p('s')], capture_output=True, text=True)
        print('signature %s over %d bytes, certificate %d bytes: %s' %
              (oid, len(signed), len(cert), r.stdout.strip() or r.stderr.strip().splitlines()[-1]))
        ok &= r.returncode == 0
    ok &= check_files(b, top, ctrl, kids)
    return 0 if ok else 1


def _array(buf, f):
    """The elements of a SISArray field f = (type, start, data, length)."""
    out, o, end = [], f[2] + 4, f[2] + f[3]
    elem, = struct.unpack_from('<I', buf, f[2])
    while o + 4 <= end:
        el, = struct.unpack_from('<I', buf, o)
        out.append((elem, o - 4, o + 4 - 4 + 4, el))
        o += 4 + el + (-el % 4)
    return out


def check_files(b, top, ctrl, kids):
    """Each installed file's SHA-1, and for an executable its declared
    capabilities (SISCapabilities, type 41) against its E32 header's: the
    installer refuses a mismatch in the middle of the install, "Required
    application access not granted" (SecurityCheckUtil::
    CompareHeaderAndExeCapabilities; rounds 169-170 were that)."""
    ok = True
    block = [k for k in kids if k[0] == 28][0]
    descs = fields(ctrl, block[2], block[2] + block[3])
    arr = [f for f in descs if f[0] == 2][0]
    elem, = struct.unpack_from('<I', ctrl, arr[2])
    entries, o, end = [], arr[2] + 4, arr[2] + arr[3]
    while o + 4 <= end:
        el, = struct.unpack_from('<I', ctrl, o)
        entries.append(ctrl[o + 4:o + 4 + el])
        o += 4 + el + (-el % 4)
    # the payloads, in index order
    _, s, dd, dl = top[30]
    payloads = []
    def walk(buf, o, end):
        for f in fields(buf, o, end):
            if f[0] == 3:
                alg, = struct.unpack_from('<I', buf, f[2])
                raw = buf[f[2] + 12:f[2] + f[3]]
                payloads.append(zlib.decompress(raw) if alg == 1 else raw)
            elif f[0] == 2:
                o2, e2 = f[2] + 4, f[2] + f[3]
                t, = struct.unpack_from('<I', buf, f[2])
                while o2 + 4 <= e2:
                    el, = struct.unpack_from('<I', buf, o2)
                    walk(struct.pack('<II', t, el) + buf[o2 + 4:o2 + 4 + el], 0, 8 + el)
                    o2 += 4 + el + (-el % 4)
            elif f[0] in (30, 31, 32):
                walk(buf, f[2], f[2] + f[3])
    walk(b, dd, dd + dl)
    for e in entries:
        fs = fields(e, 0, len(e))
        target = e[fs[0][2]:fs[0][2] + fs[0][3]].decode('utf-16-le')
        declared, k = 0, 2
        if fs[2][0] == 41:
            w = e[fs[2][2]:fs[2][2] + fs[2][3]]
            for i in range(0, len(w), 4):
                declared |= struct.unpack_from('<I', w, i)[0] << (8 * i)
            k = 3
        hashf = fs[k]
        hf = fields(e, hashf[2] + 4, hashf[2] + hashf[3])
        sha1 = e[hf[0][2]:hf[0][2] + hf[0][3]]
        tail = hashf[2] + hashf[3] + (-hashf[3] % 4)
        op, opt, clen, ulen, index = struct.unpack_from('<IIQQI', e, tail)
        data = payloads[index]
        good_hash = hashlib.sha1(data).digest() == sha1
        header = 0
        if len(data) >= 0x90 and data[0x10:0x14] == b'EPOC' and \
                struct.unpack_from('<I', data, 0)[0] in (0x1000007A, 0x10000079):
            lo, hi = struct.unpack_from('<II', data, 0x88)
            header = lo | (hi << 32)
        caps_ok = declared == header
        if header or declared or not good_hash:
            print('%s: sha1 %s, capabilities declared %#x, header %#x: %s' % (
                target or '(text)', 'ok' if good_hash else 'WRONG', declared, header,
                'ok' if caps_ok else 'MISMATCH -- a phone refuses this mid-install'))
        ok &= good_hash and caps_ok
    if ok:
        print('files: %d, every sha1 right, every executable\'s capabilities declared as its header asks' % len(entries))
    return ok


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
