"""Check a built package the way a device would: every SISFileDescription's SHA-1
and lengths must match the SISFileData actually shipped beside it."""
import struct, zlib, hashlib, sys, sisrw, sishash

def check(path):
    head, contents, orig = sisrw.load(path)
    ctrl = [k for k in contents.kids if k.t == sisrw.COMPRESSED][0]
    cbuf = bytearray(zlib.decompress(ctrl.raw[12:]))
    # Descriptions with op != 1 (install) carry no data unit -- the package ends
    # with a FILENULL entry that only names a file to remove on uninstall.
    #
    # ...except the install-text entry, which `buildapp` puts **first** and
    # which does carry one: its text is shipped as a data unit like any other
    # file. Filtering the descriptions by op and then indexing the data units
    # in parallel therefore pairs every file with the unit belonging to the
    # one before it, and the count comes out one short. This asserted on the
    # shipping Asphalt 2 package as readily as on a new one, which is how it
    # was caught: a checker that fails on a build three phones install is
    # reporting on itself.
    #
    # So pair by position over the descriptions that have a unit -- install
    # and text -- and let the trailing FILENULL drop out.
    all_des = list(sishash.find_filedes(cbuf))
    ops = [struct.unpack_from('<I', cbuf, d[1] - 28)[0] for d in all_des]
    fds = list(sisrw.walk(contents, sisrw.FILEDATA))
    des = [d for d, op in zip(all_des, ops) if op != 3][:len(fds)]
    assert len(des) == len(fds), ('descriptions with data %d vs data units %d '
                                  '(ops %s)' % (len(des), len(fds), ops))
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
