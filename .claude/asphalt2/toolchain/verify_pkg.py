"""Check a built package the way a device would: every SISFileDescription's SHA-1
and lengths must match the SISFileData actually shipped beside it."""
import struct, zlib, hashlib, sys, sisrw, sishash

def check(path):
    head, contents, orig = sisrw.load(path)
    ctrl = [k for k in contents.kids if k.t == sisrw.COMPRESSED][0]
    cbuf = bytearray(zlib.decompress(ctrl.raw[12:]))
    # Descriptions with op != 1 (install) carry no data unit -- the package ends with a
    # FILENULL entry that only names a file to remove on uninstall.
    des = [d for d in sishash.find_filedes(cbuf)
           if struct.unpack_from('<I', cbuf, d[1] - 28)[0] == 1]
    fds = list(sisrw.walk(contents, sisrw.FILEDATA))
    assert len(des) == len(fds), 'install descriptions %d vs data units %d' % (len(des), len(fds))
    ok = True
    for i, (dstart, dend) in enumerate(des):
        comp = fds[i].kids[0]
        alg, ulen = struct.unpack_from('<IQ', comp.raw, 0)
        payload = zlib.decompress(comp.raw[12:]) if alg == 1 else comp.raw[12:]
        c_len, u_len = struct.unpack_from('<QQ', cbuf, dend - 28 + 8)
        stored_hash = bytes(cbuf[sishash.hash_slice(cbuf, dstart)[0]:][:20])
        real = hashlib.sha1(payload).digest()
        good = (stored_hash == real and u_len == len(payload) and c_len == len(comp.raw) - 12)
        ok &= good
        print('  %2d %s len=%8d/%-8d sha1=%s' % (
            i, 'ok  ' if good else 'BAD ', u_len, len(payload),
            'match' if stored_hash == real else stored_hash.hex()[:16] + '!=' + real.hex()[:16]))
    print('%s: %s' % (path, 'all descriptions consistent' if ok else 'MISMATCH'))
    return ok

if __name__ == '__main__':
    sys.exit(0 if all(check(p) for p in sys.argv[1:]) else 1)
