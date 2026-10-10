"""Build a small SIS package from a template SIS.

The template is EKA2L1's own EKA2L1HW sample (a 472-byte controller holding a
single install file), so nothing about the container has to be invented: this
only rewrites the package UID, the names, and the install block's file list.

Field types used here, from the SIS spec and confirmed against that template:
  1 String  2 Array  4 Version  8 Date/Time  9 Uid  13 Controller  14 Info
  24 FileDescription  28 InstallBlock  31 DataUnit  32 FileData  3 Compressed
"""
import hashlib, os, struct, sys, zlib
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


# **Self-signing (round 168).** A phone installs an unsigned package only
# while nothing in it asks for a capability; Colin's build 010 asked for
# LocalServices and the N95 said "Required application access not granted".
# A self-signed package may carry the user-grantable ones (LocalServices,
# NetworkServices, ReadUserData, WriteUserData, Location, UserEnvironment).
# The layout is Ensymble's (sisfile.py, which phones of the time accepted):
# the signature is RSA/SHA-1 (PKCS#1 v1.5) over the SISController's
# **contents** -- every field before the signatures, headers and padding
# included, the controller's own header not -- and goes in a
# SISSignatureCertificateChain {SISArray<SISSignature>, SISCertificateChain},
# between the install block and the SISDataIndex. The certificate is the
# port's own (selfsign/: RSA-1024, sha1WithRSA, 2006-2046), with no trust
# value at all: it signs, it does not vouch.
SIG_CHAIN, SIGNATURE, BLOB, SIG_ALGORITHM, CERT_CHAIN, DATA_INDEX = 39, 36, 37, 38, 22, 40
CTRL_CHECKSUM, DATA_CHECKSUM = 34, 35
SHA1_RSA = '1.2.840.113549.1.1.5'
SELFSIGN = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'selfsign')


def crc16_ccitt(data):
    """SISControllerChecksum / SISDataChecksum: CRC-16/CCITT, initial 0, over
    the whole SISCompressed controller field and the whole SISData field,
    headers included (checked against EKA2L1's template, whose two stored
    values this reproduces)."""
    v = 0
    for c in data:
        v ^= c << 8
        for _ in range(8):
            v = ((v << 1) ^ 0x1021) if v & 0x8000 else v << 1
            v &= 0xFFFF
    return v


def sign_controller(ctrl, keydir=SELFSIGN):
    """Insert a self-signed SISSignatureCertificateChain into `ctrl`."""
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding
    ctrl.kids = [k for k in ctrl.kids if k.t != SIG_CHAIN]
    before = [k for k in ctrl.kids if k.t != DATA_INDEX]
    after = [k for k in ctrl.kids if k.t == DATA_INDEX]
    signed = b''.join(k.ser() for k in before)
    key = serialization.load_pem_private_key(
        open(os.path.join(keydir, 'selfsign.key'), 'rb').read(), password=None)
    sig = key.sign(signed, padding.PKCS1v15(), hashes.SHA1())
    cert = open(os.path.join(keydir, 'selfsign.cer'), 'rb').read()
    chain = sisrw.F(SIG_CHAIN, kids=[
        sisrw.F(ARRAY, elem=SIGNATURE, kids=[sisrw.F(SIGNATURE, kids=[
            sisrw.F(SIG_ALGORITHM, kids=[sisrw.F(STRING, raw=_u16(SHA1_RSA))]),
            sisrw.F(BLOB, raw=sig)])]),
        sisrw.F(CERT_CHAIN, kids=[sisrw.F(BLOB, raw=cert)])])
    ctrl.kids = before + [chain] + after
    return signed, sig


def build(dst, uid, name, vendor, files, template=TEMPLATE, sign=False):
    """files: [(local path, install target)], the target like '!:\\sys\\bin\\x.exe'.

    A target of `None` makes the entry a **display text** instead of an
    install: the installer shows the file's contents with a Continue button
    and puts nothing on disk. The file still travels in the package, so it is
    written UTF-16LE with a BOM, which is what the installer reads it as.
    """
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
        op, op_op = ((sisadd.OP_TEXT, sisadd.FT_LET_CONTINUE) if target is None
                     else (sisadd.OP_INSTALL, 0))
        desc_arr.kids.append(sisrw.F(FILEDESC, raw=sisadd.make_filedesc(
            target or '', hashlib.sha1(payload).digest(), len(packed), len(payload),
            index, op, op_op)))

    if sign:
        sign_controller(ctrl)
    cbuf = bytearray(ctrl.ser())
    ctrl_node.raw = struct.pack('<IQ', 1, len(cbuf)) + zlib.compress(bytes(cbuf), 9)
    # The two checksums, which until round 168 kept the template's values:
    # the N95 installed every such package, so it does not check them, but
    # a stale checksum is a wrong field all the same.
    data_node = [k for k in contents.kids if k.t == sisrw.DATA][0]
    for k in contents.kids:
        if k.t == CTRL_CHECKSUM:
            k.raw = struct.pack('<H', crc16_ccitt(ctrl_node.ser()))
        elif k.t == DATA_CHECKSUM:
            k.raw = struct.pack('<H', crc16_ccitt(data_node.ser()))

    import mke32
    head = bytearray(head)
    struct.pack_into('<I', head, 8, uid)
    struct.pack_into('<I', head, 12, mke32.uid_checksum(
        *struct.unpack_from('<III', head, 0)))

    sisrw.save(dst, bytes(head), contents)
    return dst
