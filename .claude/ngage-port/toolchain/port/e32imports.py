"""Read an E32 image's import surface, for either kernel generation.

EKA1 blocks store the ordinals themselves.  EKA2 images built for EABI store,
in their place, offsets into the code section; the word sitting at each offset
holds `ordinal | (adjustment << 16)`, and the loader overwrites it with the
resolved address.
"""
import struct

E32_SIG = 0x434F5045


def _u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def header(d):
    if _u32(d, 0x10) != E32_SIG:
        raise ValueError('not an E32 image')
    eka1 = _u32(d, 0x14) in (0x1000, 0x2000)
    keys = ('flags code_size data_size heap_min heap_max stack_size bss_size entry_point '
            'code_base data_base dll_ref_count export_dir_offset export_dir_count text_size '
            'code_offset data_offset import_offset code_reloc_offset data_reloc_offset').split()
    h = dict(zip(keys, struct.unpack_from('<19I', d, 0x2c)))
    h.update(uid1=_u32(d, 0), uid2=_u32(d, 4), uid3=_u32(d, 8),
             compression=_u32(d, 0x1c), eka1=eka1)
    h['abi'] = 'EABI' if ((h['flags'] >> 3) & 3) == 1 else 'GCC98r2'
    return h


def imports(d, h=None):
    """-> [(dll name, [ordinals])]"""
    h = h or header(d)
    off = h['import_offset']
    if not off or off >= len(d):
        return []
    if h['compression']:
        raise ValueError('image payload is compressed; decompress it first')

    o = off + 4                      # the section's own size word
    out = []
    for _ in range(h['dll_ref_count']):
        name_off, count = struct.unpack_from('<II', d, o)
        o += 8
        words = list(struct.unpack_from('<%dI' % count, d, o)) if count else []
        o += 4 * count
        end = d.index(b'\0', off + name_off)
        name = d[off + name_off:end].decode('latin1')
        if h['eka1']:
            ords = words
        else:
            ords = [_u32(d, h['code_offset'] + w) & 0xFFFF for w in words]
        out.append((name, ords))
    return out
