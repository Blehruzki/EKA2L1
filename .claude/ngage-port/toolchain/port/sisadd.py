"""Add files to an existing SIS package.

`sisrw` only ever replaced payloads in place. Shipping a soundtrack needs new
entries: one SISFileDescription in the controller's install block and one
SISFileData beside the existing ones, per track.

Round-tripping the controller is the safety net -- parse and re-serialise it
untouched and it must come back byte-identical before anything is appended.
"""
import struct, zlib, hashlib, sisrw

STRING, ARRAY, HASH, FILEDESC, BLOB, INSTALLBLOCK = 1, 2, 25, 24, 37, 28
CAPABILITIES = 41

# Every field type whose body is itself a list of fields. FILEDESC is deliberately
# absent: its body ends in bare integers, not fields.
CONTAINERS = {2, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29,
              30, 31, 32, 33, 36, 38, 39, 40}


def _field(t, data):
    return struct.pack('<II', t, len(data)) + data + b'\0' * (-len(data) % 4)


# SISFileDescription's operation word, and the options that go with it.
# `install` puts the file at its target; `text` displays it during the install
# and puts it nowhere, which is how a package says who made it. `let_continue`
# is the one-button form -- a Continue and nothing to decline. EKA2L1's own
# installer reads both (src/emu/loader/include/loader/sis_fields.h).
OP_INSTALL, OP_RUN, OP_TEXT, OP_NULL = 1, 2, 4, 8
FT_LET_CONTINUE = 1 << 9


# **An executable's capabilities are declared in its file description.** The
# installer grants capabilities from the declarations (CInstallMachine::
# CheckAndGrantCapabilitiesL: the user-grantable ones asked about, before the
# progress bar), and then, as each executable is copied, requires its E32
# header's capabilities to equal its declaration exactly (SecurityCheckUtil::
# CompareHeaderAndExeCapabilities -> EUiCapabilitiesCannotBeGranted, "Required
# application access not granted"). makesis writes the declaration from the
# header (sisxlibrary/siscapabilities.cpp), and leaves it out when there are
# none. Ours never wrote one, so an exe asking for LocalServices was refused in
# the middle of the install whether the package was unsigned (round 169) or
# self-signed (170) -- no capability question first, because nothing was
# declared to ask about. Source: Symbian's appinstall, swi/source/swis/server.
def exe_capabilities(payload):
    """The capability bits an E32 image's header asks for, or 0 for any other
    file: UID1 0x1000007A (exe) or 0x10000079 (dll), "EPOC" at 0x10, the
    capability set as two words at 0x88 (SSecurityInfo after SID and VID)."""
    if len(payload) < 0x90 or payload[0x10:0x14] != b'EPOC':
        return 0
    uid1, = struct.unpack_from('<I', payload, 0)
    if uid1 not in (0x1000007A, 0x10000079):
        return 0
    lo, hi = struct.unpack_from('<II', payload, 0x88)
    return lo | (hi << 32)


def make_filedesc(target, sha1, clen, ulen, index, op=OP_INSTALL, op_op=0, caps=0):
    """target is the install path, e.g. '!:\\private\\20008629\\A2\\bgm_1.wav'.

    For `OP_TEXT` the target is empty: the file is shown, not installed.
    `caps`, an executable's header capabilities (exe_capabilities), go in a
    SISCapabilities field between the MIME type and the hash, one word -- two if
    any bit above 31 is set -- and none at all for 0, as makesis does.
    """
    body = _field(STRING, target.encode('utf-16-le'))
    body += _field(STRING, b'')
    if caps:
        words = struct.pack('<I', caps & 0xFFFFFFFF)
        if caps >> 32:
            words += struct.pack('<I', caps >> 32)
        body += _field(CAPABILITIES, words)
    body += _field(HASH, struct.pack('<I', 1) + _field(BLOB, sha1))
    body += struct.pack('<IIQQI', op, op_op, clen, ulen, index)
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
            target, hashlib.sha1(payload).digest(), len(packed), len(payload), next_index,
            caps=exe_capabilities(payload))))
        next_index += 1

    return bytearray(ctrl.ser())
