"""E32 image header CRC.

The header carries a CRC32 of itself at 0x14.  Symbian computes it by seeding
that field with KImageCrcInitialiser and running Mem::Crc32 over the whole
header -- a raw reflected CRC-32 (poly 0xEDB88320) with no initial or final
inversion.  Changing any header field (compression_type, say) invalidates it.
Derived by matching the stored value in the untouched game executables.
"""
import struct

K_IMAGE_CRC_INITIALISER = 0xC90FDAA2
CRC_FIELD = 0x14
HEADER_LEN = 0x64          # code_offset lives here and equals the header size

_TAB = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ 0xEDB88320 if _c & 1 else _c >> 1
    _TAB.append(_c)

def mem_crc32(buf, crc=0):
    for b in buf:
        crc = (crc >> 8) ^ _TAB[(crc ^ b) & 0xFF]
    return crc & 0xFFFFFFFF

def header_size(img):
    return struct.unpack_from('<I', img, HEADER_LEN)[0]

def compute(img):
    n = header_size(img)
    hdr = bytearray(img[:n])
    struct.pack_into('<I', hdr, CRC_FIELD, K_IMAGE_CRC_INITIALISER)
    return mem_crc32(bytes(hdr))

def stored(img):
    return struct.unpack_from('<I', img, CRC_FIELD)[0]

def fix(img):
    """return img with a correct header CRC"""
    out = bytearray(img)
    struct.pack_into('<I', out, CRC_FIELD, compute(bytes(out)))
    return bytes(out)
