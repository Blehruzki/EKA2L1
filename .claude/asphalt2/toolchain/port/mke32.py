"""Wrap a flat ARM code blob into a V-format E32 executable.

Nothing here is copied from a stock image: the 156-byte header is built field by
field, so every value is one we can explain.  The two self-describing fields --
the UID checksum at 0x0c and the header CRC at 0x14 -- are computed with the
same routines that reproduce the stored values in the shipped game binaries.
"""
import datetime, struct, sys

HDR_LEN = 156                  # V-format: 0x7c base + 0x20 extended
CODE_OFFSET = HDR_LEN

UID1_EXE = 0x1000007A
UID2_APP = 0x100039CE          # not used for a bare exe, kept for reference
CPU_ARMV5 = 0x2001

# Import format ELF (1<<28) | header format 2 (V) (2<<24) | entry point type
# EKA2 (1<<5) | ABI EABI (1<<3) | no call entry point (1<<1).
#
# The import format matters even with no imports: an EABI image declaring PE
# imports is a combination Symbian's loader rejects outright, and it is exactly
# what the emulator never notices, because it only reads the import format when
# the DLL reference count is non-zero.  Both shipped S60v3 executables checked
# against here -- one SDK-built, one that installs and runs on the target phone
# -- carry 0x1200002a, so that is what we write.
FLAGS = (1 << 28) | (2 << 24) | (1 << 5) | (1 << 3) | (1 << 1)

MODULE_VERSION = 0x000A0000
DATA_BASE = 0x00400000
PRIORITY = 0x015E                  # EPriorityForeground, as both shipped exes use


def symbian_time(when=None):
    """TTime: microseconds since the start of year 0.

    Checked against the shipped binaries: the game exe's stamp decodes to
    2007-07-03 and the SDK sample's to 2018-04-21, both plausible build dates.
    """
    when = when or datetime.datetime.utcnow()
    days = when.date().toordinal() + 365
    secs = when.hour * 3600 + when.minute * 60 + when.second
    return days * 86400 * 10 ** 6 + secs * 10 ** 6 + when.microsecond

_TAB = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ 0xEDB88320 if _c & 1 else _c >> 1
    _TAB.append(_c)


def _crc32(buf, crc=0):
    for b in buf:
        crc = (crc >> 8) ^ _TAB[(crc ^ b) & 0xFF]
    return crc & 0xFFFFFFFF


def _crc16(buf):
    """CRC-16-CCITT, poly 0x1021, init 0 -- Symbian's Mem::Crc."""
    crc = 0
    for b in buf:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def uid_checksum(uid1, uid2, uid3):
    raw = struct.pack('<III', uid1, uid2, uid3)
    return (_crc16(raw[1::2]) << 16) | _crc16(raw[0::2])


def build(code, uid3, entry=0, stack=0x2000, heap_min=0x1000, heap_max=0x100000,
          uid2=0, code_base=0x8000, when=None, reloc_offsets=()):
    import relocs
    code = bytes(code)
    code += b'\0' * (-len(code) % 4)
    reloc_section = relocs.section(reloc_offsets) if reloc_offsets else b''

    h = bytearray(HDR_LEN)
    def u32(off, v): struct.pack_into('<I', h, off, v & 0xFFFFFFFF)
    def u16(off, v): struct.pack_into('<H', h, off, v & 0xFFFF)

    u32(0x00, UID1_EXE)
    u32(0x04, uid2)
    u32(0x08, uid3)
    u32(0x0c, uid_checksum(UID1_EXE, uid2, uid3))
    h[0x10:0x14] = b'EPOC'
    u32(0x14, 0)               # header CRC, filled in below
    u32(0x18, MODULE_VERSION)
    u32(0x1c, 0)               # compression: none
    h[0x20] = 2; h[0x21] = 0; u16(0x22, 0x0200)     # tool version
    stamp = symbian_time(when)
    u32(0x24, stamp & 0xFFFFFFFF); u32(0x28, stamp >> 32)
    u32(0x2c, FLAGS)
    u32(0x30, len(code))       # code size
    u32(0x34, 0)               # data size
    u32(0x38, heap_min)
    u32(0x3c, heap_max)
    u32(0x40, stack)
    u32(0x44, 0)               # bss
    u32(0x48, entry)           # entry point, an offset from the code base
    u32(0x4c, code_base)
    u32(0x50, DATA_BASE)
    u32(0x54, 0)               # dll ref table count
    u32(0x58, 0); u32(0x5c, 0)                       # export dir
    u32(0x60, len(code))       # text size == code size, so the IAT walk is empty
    u32(0x64, CODE_OFFSET)
    u32(0x68, 0)               # data offset
    u32(0x6c, 0)               # import offset
    u32(0x70, (CODE_OFFSET + len(code)) if reloc_section else 0)   # code relocations
    u32(0x74, 0)                                                    # data relocations
    u16(0x78, PRIORITY)
    u16(0x7a, CPU_ARMV5)
    u32(0x7c, len(code) + len(reloc_section))   # everything past the header
    u32(0x80, uid3)            # secure id
    u32(0x84, 0)               # vendor id
    u32(0x88, 0); u32(0x8c, 0)                       # capabilities
    u32(0x90, 0)               # exception descriptor
    u32(0x94, 0)               # spare
    u16(0x98, 0); h[0x9a] = 0; h[0x9b] = 0           # export description

    img = bytearray(h) + code + reloc_section

    crc_hdr = bytearray(img[:HDR_LEN])
    struct.pack_into('<I', crc_hdr, 0x14, 0xC90FDAA2)
    struct.pack_into('<I', img, 0x14, _crc32(bytes(crc_hdr)))
    return bytes(img)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    uid3 = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0xE0001001
    with open(src, 'rb') as f:
        code = f.read()
    out = build(code, uid3)
    with open(dst, 'wb') as f:
        f.write(out)
    print('%s: %d bytes code, %d bytes image' % (dst, len(code), len(out)))
