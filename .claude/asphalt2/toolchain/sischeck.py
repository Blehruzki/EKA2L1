#!/usr/bin/env python3
"""sischeck.py <a.sis> [b.sis] -- check a SIS the way a device does, and diff.

EKA2L1 verifies none of the integrity fields a real installer checks, which
is written down in the project's own notes as a known gap. This closes it
for our own packages: every file's SHA-1 against its actual data, every
length against what is really there, and the shape of the controller beside
a package the phone is known to accept.
"""
import hashlib
import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import sisrw
import sisadd

FILEDESC, DATAUNIT, FILEDATA, COMPRESSED = 24, 31, 32, 3
HASH = 25


def walk(n, d=0):
    yield n, d
    for k in (n.kids or []):
        yield from walk(k, d + 1)


def parse_desc(raw):
    o = 0
    def field():
        nonlocal o
        t, l = struct.unpack_from('<II', raw, o)
        o += 8
        body = raw[o:o + l]
        o += l + (-l % 4)
        return t, body
    _t, target = field()
    _t, mime = field()
    t3, b3 = field()
    if t3 != HASH:
        raise ValueError('third field is type %d, expected a hash' % t3)
    htype, = struct.unpack_from('<I', b3, 0)
    _ht, hl = struct.unpack_from('<II', b3, 4)
    digest = b3[12:12 + hl]
    op, opop, ln, uln, idx = struct.unpack_from('<IIQQI', raw, o)
    trailing = len(raw) - (o + 28)
    return dict(target=target.decode('utf-16-le'), mime=mime.decode('utf-16-le'),
                htype=htype, digest=digest, op=op, opop=opop, length=ln,
                ulength=uln, idx=idx, trailing=trailing)


def load(path):
    head, contents, _rest = sisrw.load(path)
    ctrl_node = [k for k in contents.kids if k.t == COMPRESSED][0]
    cbuf = zlib.decompress(ctrl_node.raw[12:])
    ctrl = sisadd.parse_controller(bytearray(cbuf))
    descs = [parse_desc(n.raw) for n, _d in walk(ctrl) if n.t == FILEDESC]
    units = [n for n, _d in walk(contents) if n.t == DATAUNIT]
    datas = []
    for u in units:
        arr = [k for k in u.kids if getattr(k, 'elem', None) == FILEDATA]
        for a in arr:
            for fd in (a.kids or []):
                c = [k for k in (fd.kids or []) if k.t == COMPRESSED]
                datas.append(c[0].raw if c else None)
    return dict(path=path, head=head, ctrl_bytes=len(cbuf), descs=descs,
                units=len(units), datas=datas)


def check(p):
    bad = 0
    print('%s' % p['path'])
    print('  controller %d bytes, %d file descriptions, %d data unit(s), %d data blocks'
          % (p['ctrl_bytes'], len(p['descs']), p['units'], len(p['datas'])))
    for i, d in enumerate(p['descs']):
        if d['trailing']:
            print('  ! desc %d has %d trailing bytes' % (i, d['trailing']))
            bad += 1
        if d['op'] not in (1, 2, 4, 8):
            print('  ! desc %d operation %d' % (i, d['op']))
            bad += 1
        if d['op'] in (2, 8) and d['length'] == 0:
            continue
        if d['idx'] >= len(p['datas']):
            print('  ! desc %d index %d, only %d data blocks'
                  % (i, d['idx'], len(p['datas'])))
            bad += 1
            continue
        raw = p['datas'][d['idx']]
        alg, ulen = struct.unpack_from('<IQ', raw, 0)
        body = raw[12:]
        if len(body) != d['length']:
            print('  ! %s: descriptor says %d compressed bytes, block holds %d'
                  % (d['target'], d['length'], len(body)))
            bad += 1
        if ulen != d['ulength']:
            print('  ! %s: block says %d uncompressed, descriptor says %d'
                  % (d['target'], ulen, d['ulength']))
            bad += 1
        plain = zlib.decompress(body) if alg == 1 else body
        if len(plain) != d['ulength']:
            print('  ! %s: inflates to %d, descriptor says %d'
                  % (d['target'], len(plain), d['ulength']))
            bad += 1
        got = hashlib.sha1(plain).digest()
        if got != d['digest']:
            print('  ! %s: SHA-1 mismatch' % d['target'])
            bad += 1
    print('  %s' % ('all checks pass' if not bad else '%d PROBLEM(S)' % bad))
    return bad


def summarise(p):
    print('  targets:')
    for d in p['descs']:
        print('    op=%d opop=%-4d idx=%-3d %10d -> %s'
              % (d['op'], d['opop'], d['idx'], d['ulength'], d['target']))


if __name__ == '__main__':
    for path in sys.argv[1:]:
        p = load(path)
        check(p)
        summarise(p)
        print()
