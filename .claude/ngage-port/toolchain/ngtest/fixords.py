#!/usr/bin/env python3
"""fixords.py <app> -- carry ngtest's imports from the S60 6.1 SDK's ordinals
to the N-Gage firmware's where the two differ.

The SDK's mediaclientaudiostream.lib exports CMdaAudioOutputStream::NewL at
ordinal 1; every N-Gage title the port has met imports it at 2 (gen_shim
HOOKS, IMPORT_MDA_NEWL), and the port hooks the call by that pair. EKA1 keeps
each ordinal twice -- in the import section and in the import address table
the loader fills -- so both are rewritten."""
import struct, sys
sys.path.insert(0, __file__.rsplit('/', 2)[0] + '/port')
import e32imports
REMAP = {('mediaclientaudiostream', 1): 2}
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
h = e32imports.header(bytes(d))
off = h['import_offset']; o = off + 4
iat = h['code_offset'] + h['text_size']
k = 0
for _ in range(h['dll_ref_count']):
    name_off, count = struct.unpack_from('<II', d, o); o += 8
    end = d.index(b'\0', off + name_off)
    dll = d[off + name_off:end].decode('latin1').split('[')[0].lower()
    for i in range(count):
        v = struct.unpack_from('<I', d, o + 4 * i)[0]
        new = REMAP.get((dll, v))
        if new:
            struct.pack_into('<I', d, o + 4 * i, new)
            if struct.unpack_from('<I', d, iat + 4 * (k + i))[0] == v:
                struct.pack_into('<I', d, iat + 4 * (k + i), new)
            print('%s ordinal %d -> %d' % (dll, v, new))
    o += 4 * count; k += count
open(p, 'wb').write(d)
