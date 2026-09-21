"""Write an application registration resource (<app>_reg.rsc) for Symbian 9.x.

The single resource expands to the layout apparc reads:
    8 reserved bytes, the exe basename as a 16-bit string, a u32 of capability
    flags, the localised resource's path and its resource id, then the
    non-localisable fields (hidden, embeddability, group name, screen number)
    and the mime/ownership counts, all zero here.

selftest() rebuilds a shipped Nokia file from its own inputs and compares.
"""
import struct, sys
import rscwrite as R


def build(app_name, uid3, localised_rsc_id, app_path=None, caps_flags=0, tail_pad=16):
    if app_path is None:
        app_path = '\\resource\\apps\\' + app_name

    runs = [('u', b'')]
    runs += R.string_runs(bytes(8), app_name)
    runs += R.string_runs(struct.pack('<I', caps_flags), app_path)
    runs += [('c', struct.pack('<I', localised_rsc_id) + bytes(tail_pad))]

    return R.container(R.UID2_APP_REG, uid3, [R.encode_runs(runs)],
                       unicode_bits=0x01, largest=R.expanded_size(runs))


def selftest(reference):
    ref = open(reference, 'rb').read()
    mine = build('asphalt2_full', 0x20008629, 0x083FA006,
                 app_path='\\resource\\apps\\Asphalt2_full')
    ok = mine == ref
    print('mkreg selftest: %s (%d bytes)' % ('exact match' if ok else 'FAILED', len(ref)))
    if not ok:
        print(' ref ', ref.hex()); print(' mine', mine.hex())
    return ok


if __name__ == '__main__':
    sys.exit(0 if selftest(sys.argv[1]) else 1)
