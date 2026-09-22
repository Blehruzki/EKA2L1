#!/usr/bin/env python3
"""Read an EKA1 ROM image, as EKA2L1 extracts one into its z drive.

    romimg.py <file> [ordinal ...]

A ROM image is not an E32 image. There is no 'EPOC' signature, no import
section and no relocations -- the code is linked for the address it is executed
from, in place, so every pointer in it is already final. What there is: a
100-byte TRomImageHeader, the code, and an export directory inside the code
holding one absolute address per ordinal.

That last part is what makes these worth reading. The N-Gage ROM has the 7.0s
framework the game was built against, so the old class layouts and the
N-Gage-only libraries can be read out of it rather than guessed at.

Some of the extracted files are a little shorter than their code section, so
anything past the end of the file reads as missing rather than as zeroes.
"""
import struct
import sys

HEADER_LENS = (100, 120)    # loader/romimage.h: 100 up to EKA2, 120 after

FIELDS = ('uid1', 'uid2', 'uid3', 'uid_checksum', 'entry_point', 'code_address',
          'data_address', 'code_size', 'text_size', 'data_size', 'bss_size',
          'heap_min', 'heap_max', 'stack_size', 'dll_ref_table', 'export_dir_count',
          'export_dir')

UID1_DLL = 0x10000079
UID1_EXE = 0x1000007A


def header(d, header_len=None):
    """The header, plus the length of it -- which says which era the ROM is.

    The fields are the same for both; only what follows them differs, so the
    length is settled by which one puts the export directory somewhere that
    reads back as addresses inside the code.
    """
    if len(d) < max(HEADER_LENS):
        raise ValueError('shorter than a ROM image header')
    h = dict(zip(FIELDS, struct.unpack_from('<17I', d, 0)))
    if h['uid1'] not in (UID1_DLL, UID1_EXE):
        raise ValueError('not a ROM image: uid1 %08x' % h['uid1'])
    if h['entry_point'] & ~1 != h['code_address']:
        raise ValueError('entry point is not the start of the code')
    for n in ((header_len,) if header_len else HEADER_LENS):
        h['header_len'] = n
        try:
            check(d, h)
            return h
        except ValueError:
            continue
    raise ValueError('no header length makes the export directory read back')


def offset(h, address):
    """A linked address -> an offset into the file, or None if it is not there."""
    o = address - h['code_address'] + h['header_len']
    return o if h['header_len'] <= o else None


def exports(d, h):
    """-> {ordinal: linked address}. Ordinals start at 1."""
    out, o = {}, offset(h, h['export_dir'])
    for i in range(h['export_dir_count']):
        if o + 4 * i + 4 > len(d):
            break                       # the file stops short of the last few
        out[i + 1] = struct.unpack_from('<I', d, o + 4 * i)[0]
    return out


def code(d, h):
    return d[h['header_len']:h['header_len'] + h['code_size']]


def check(d, h):
    """Everything the format lets us verify. Raises on anything inconsistent."""
    lo, hi = h['code_address'], h['code_address'] + h['code_size']
    if h['dll_ref_table'] and h['dll_ref_table'] != hi:
        raise ValueError('the DLL reference table does not follow the code')
    if not lo <= h['export_dir'] <= hi:
        raise ValueError('the export directory is outside the code')
    ex = exports(d, h)
    for ordinal, a in ex.items():
        # A zero is an ordinal the library does not fill in, not a bad read.
        if a and not lo <= (a & ~1) < hi:
            raise ValueError('ordinal %d points outside the code: %08x' % (ordinal, a))
    if not ex and offset(h, h['export_dir']) < len(d):
        raise ValueError('the export directory is there but reads back as nothing')
    return len(ex), h['export_dir_count']


def main(path, wanted):
    d = open(path, 'rb').read()
    h = header(d)
    got, want = check(d, h)
    print('%s: code %08x..%08x, %d exports%s' %
          (path, h['code_address'], h['code_address'] + h['code_size'],
           h['export_dir_count'], '' if got == want else ', %d readable' % got))
    ex = exports(d, h)
    for o in wanted:
        a = ex.get(int(o))
        print('  ordinal %-5s %s' % (o, '%08x' % a if a else 'not in the file'))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:])
