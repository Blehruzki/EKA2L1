"""Build a small SIS package from a template SIS.

The template is EKA2L1's own EKA2L1HW sample (a 472-byte controller holding a
single install file), so nothing about the container has to be invented: this
only rewrites the package UID, the names, and the install block's file list.

Field types used here, from the SIS spec and confirmed against that template:
  1 String  2 Array  4 Version  8 Date/Time  9 Uid  13 Controller  14 Info
  24 FileDescription  28 InstallBlock  31 DataUnit  32 FileData  3 Compressed
"""
import hashlib, os, struct, sys, zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir))
import sisrw, sisadd

STRING, ARRAY, UID, INFO, FILEDESC, INSTALLBLOCK = 1, 2, 9, 14, 24, 28
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        *(['..'] * 4 + ['native', 'EKA2L1HWD', 'sis', 'EKA21LHW.sis']))


def _u16(s):
    return s.encode('utf-16-le')


def _find(node, t):
    if node.t == t:
        return node
    for k in (node.kids or []):
        got = _find(k, t)
        if got is not None:
            return got
    return None


def build(dst, uid, name, vendor, files, template=TEMPLATE):
    """files: [(local path, install target)], the target like '!:\\sys\\bin\\x.exe'."""
    head, contents, _ = sisrw.load(template)
    ctrl_node = [k for k in contents.kids if k.t == sisrw.COMPRESSED][0]
    cbuf = bytearray(zlib.decompress(ctrl_node.raw[12:]))

    ctrl = sisadd.parse_controller(cbuf)
    assert ctrl.ser() == bytes(cbuf), 'template controller does not round-trip'

    info = _find(ctrl, INFO)
    info.kids[0].raw = struct.pack('<I', uid)
    info.kids[1].raw = _u16(vendor)                 # unique vendor name
    info.kids[2].kids = [sisrw.F(STRING, raw=_u16(name))]
    info.kids[3].kids = [sisrw.F(STRING, raw=_u16(vendor))]

    block = _find(ctrl, INSTALLBLOCK)
    desc_arr = [k for k in block.kids if k.elem == FILEDESC][0]
    desc_arr.kids = []

    unit = list(sisrw.walk(contents, sisrw.DATAUNIT))[-1]
    data_arr = [k for k in unit.kids if k.elem == sisrw.FILEDATA][0]
    data_arr.kids = []

    for index, (src, target) in enumerate(files):
        payload = open(src, 'rb').read()
        packed = zlib.compress(payload, 9)
        data_arr.kids.append(sisrw.F(sisrw.FILEDATA,
            kids=[sisrw.F(sisrw.COMPRESSED, raw=struct.pack('<IQ', 1, len(payload)) + packed)]))
        desc_arr.kids.append(sisrw.F(FILEDESC, raw=sisadd.make_filedesc(
            target, hashlib.sha1(payload).digest(), len(packed), len(payload), index)))

    cbuf = bytearray(ctrl.ser())
    ctrl_node.raw = struct.pack('<IQ', 1, len(cbuf)) + zlib.compress(bytes(cbuf), 9)

    import mke32
    head = bytearray(head)
    struct.pack_into('<I', head, 8, uid)
    struct.pack_into('<I', head, 12, mke32.uid_checksum(
        *struct.unpack_from('<III', head, 0)))

    sisrw.save(dst, bytes(head), contents)
    return dst
