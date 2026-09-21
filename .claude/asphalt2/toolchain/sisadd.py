"""Add files to an existing SIS package.

`sisrw` only ever replaced payloads in place. Shipping a soundtrack needs new
entries: one SISFileDescription in the controller's install block and one
SISFileData beside the existing ones, per track.

Round-tripping the controller is the safety net -- parse and re-serialise it
untouched and it must come back byte-identical before anything is appended.
"""
import struct, zlib, hashlib, sisrw

STRING, ARRAY, HASH, FILEDESC, BLOB, INSTALLBLOCK = 1, 2, 25, 24, 37, 28

# Every field type whose body is itself a list of fields. FILEDESC is deliberately
# absent: its body ends in bare integers, not fields.
CONTAINERS = {2, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29,
              30, 31, 32, 33, 36, 38, 39, 40}


def _field(t, data):
    return struct.pack('<II', t, len(data)) + data + b'\0' * (-len(data) % 4)


def make_filedesc(target, sha1, clen, ulen, index):
    """target is the install path, e.g. '!:\\private\\20008629\\A2\\bgm_1.wav'."""
    body = _field(STRING, target.encode('utf-16-le'))
    body += _field(STRING, b'')
    body += _field(HASH, struct.pack('<I', 1) + _field(BLOB, sha1))
    body += struct.pack('<IIQQI', 1, 0, clen, ulen, index)
    return body


def parse_controller(cbuf):
    return sisrw.parse(cbuf, 0, len(cbuf), lambda t: t in CONTAINERS)[0]


def find_filedesc_array(node):
    if node.elem == FILEDESC:
        return node
    for k in (node.kids or []):
        found = find_filedesc_array(k)
        if found is not None:
            return found
    return None


def add_files(cbuf, contents, payloads, targets, level=9):
    """payloads: list of uncompressed bytes; targets: matching install paths.

    Returns the new controller buffer. `contents` gains one SISFileData each."""
    ctrl = parse_controller(cbuf)
    assert ctrl.ser() == bytes(cbuf), 'controller does not round-trip; refusing to edit it'

    arr = find_filedesc_array(ctrl)
    assert arr is not None, 'no SISFileDescription array in the controller'

    unit = None
    for du in sisrw.walk(contents, sisrw.DATAUNIT):
        unit = du
    assert unit is not None, 'no SISDataUnit to append to'
    data_arr = [k for k in unit.kids if k.elem == sisrw.FILEDATA][0]

    next_index = len(data_arr.kids)
    for payload, target in zip(payloads, targets):
        packed = zlib.compress(payload, level)
        blob = struct.pack('<IQ', 1, len(payload)) + packed
        data_arr.kids.append(sisrw.F(sisrw.FILEDATA, kids=[sisrw.F(sisrw.COMPRESSED, raw=blob)]))
        arr.kids.append(sisrw.F(FILEDESC, raw=make_filedesc(
            target, hashlib.sha1(payload).digest(), len(packed), len(payload), next_index)))
        next_index += 1

    return bytearray(ctrl.ser())
