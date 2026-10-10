#!/usr/bin/env python3
"""sischeck.py <package.sis>: read a package back with nothing of mksis's.

Round 168. A parser of its own (not sisrw), so a mistake in the writer is not
repeated in the check: the two CRCs recomputed against their stored values,
the controller's field list, and -- for a signed package -- the signature
verified with the openssl command line against the certificate's own key,
over the controller's contents up to the SISSignatureCertificateChain.
Exit status 0 only if everything that is present checks out.
"""
import os, struct, subprocess, sys, tempfile, zlib


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
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
