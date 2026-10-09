#!/usr/bin/env python3
"""lxce.py -- read an AirPlay engine: Colin McRae Rally 2005's `6r66.nax`.

    lxce.py <file.nax> [--out engine.lxe] [--bill]

Ideaworks3D's AirPlay titles do not keep the game in their `.app`. The game
is a small EKA1 EXE (`6r66.nax`; its first 5 KB are byte for byte the
loader in `\\system\\programs\\airplayserver.exe`) with a gzip stream
appended. The loader (E32Main at 0xe98 of its code):

  1. opens its own file (`RProcess::FileName`) and inflates the stream;
  2. lays the sections out one after another in an `RChunk::CreateLocal`;
  3. applies the PE base relocations, type 3 (HIGHLOW) only, rebasing each
     address by the section it falls in (0xc88);
  4. for every library in a PE import directory named the E32 way
     (`EUSER[100039e5].DLL`), `RLibrary::Load`s it with the UIDs in the
     brackets and fills each slot with `RLibrary::Lookup(ordinal)` --
     ordinals only, a name import is error 0x22 (0xb14);
  5. flushes the instruction cache over the chunk and calls the entry with
     no arguments (`bx r5`, 0xf00): the engine is the program from there.

The inflated image, magic `LXCE`:

  word 0     'LXCE'           word 1   version (1)
  word 2     image base       word 3   entry
  words 4-13 five (address, size) pairs: text, bss (no raw data), rdata,
             idata (the import directory), reloc
  0x38       raw text, rdata, idata, reloc, in that order

Addresses in the import directory are relative to the image base.
"""
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

MAGIC = 0x4543584C
SECTIONS = ('text', 'bss', 'rdata', 'idata', 'reloc')
RAW = ('text', 'rdata', 'idata', 'reloc')        # in file order; bss has none


def unpack(nax):
    """The inflated engine out of a `.nax` (or the bytes of one)."""
    d = open(nax, 'rb').read() if isinstance(nax, str) else nax
    m = re.search(rb'\x1f\x8b\x08', d[0x7c:])
    if not m:
        raise ValueError('no gzip stream after the loader')
    z = zlib.decompressobj(16 + 15)
    return z.decompress(d[0x7c + m.start():])


def parse(raw):
    w = struct.unpack_from('<14I', raw, 0)
    if w[0] != MAGIC:
        raise ValueError('not LXCE: 0x%08x' % w[0])
    base, entry = w[2], w[3]
    sec = {SECTIONS[i]: (w[4 + 2 * i], w[5 + 2 * i]) for i in range(5)}
    off, at = 0x38, {}
    for n in RAW:
        at[n] = off
        off += sec[n][1]
    if off != len(raw):
        raise ValueError('raw sections end at 0x%x, file is 0x%x' % (off, len(raw)))

    def file_of(va):
        for n in RAW:
            a, sz = sec[n]
            if a <= va < a + sz:
                return at[n] + va - a
        raise ValueError('0x%x is in no raw section' % va)

    imports, o = [], file_of(sec['idata'][0])
    while True:
        oft, _ts, _fc, name, iat = struct.unpack_from('<5I', raw, o)
        o += 20
        if not name:
            break
        n = file_of(base + name)
        lib = raw[n:raw.index(b'\0', n)].decode()
        t, ords = file_of(base + (oft or iat)), []
        while True:
            v = struct.unpack_from('<I', raw, t)[0]
            t += 4
            if not v:
                break
            if not v & 0x80000000:
                raise ValueError('%s imports by name: the loader refuses that' % lib)
            ords.append(v & 0xFFFF)
        imports.append((lib, base + iat, ords))

    relocs, o, end = [], at['reloc'], at['reloc'] + sec['reloc'][1]
    while o + 8 <= end:
        page, size = struct.unpack_from('<II', raw, o)
        if not page or size < 8:
            break
        for k in range((size - 8) // 2):
            e = struct.unpack_from('<H', raw, o + 8 + 2 * k)[0]
            if e >> 12 == 3:
                relocs.append(base + page + (e & 0xFFF))
            elif e >> 12:
                raise ValueError('relocation type %d: the loader refuses it' % (e >> 12))
        o += size
    return {'base': base, 'entry': entry, 'sections': sec, 'raw_at': at,
            'imports': imports, 'relocs': relocs}


def import_list(info):
    """(library, ordinal) in import-directory order, as gen_shim takes them."""
    return [(lib.split('[')[0].split('.')[0].lower(), o)
            for lib, _iat, ords in info['imports'] for o in ords]


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    raw = unpack(args[0])
    info = parse(raw)
    print('LXCE image: base 0x%x, entry 0x%x, %d bytes' % (info['base'], info['entry'], len(raw)))
    for n in SECTIONS:
        a, sz = info['sections'][n]
        print('  %-6s 0x%08x  0x%07x%s' % (n, a, sz, '' if n in RAW else '  (no raw data)'))
    span = max(a + sz for a, sz in info['sections'].values()) - info['base']
    print('  the image spans 0x%x bytes once laid out' % span)
    print('%d base relocations (HIGHLOW)' % len(info['relocs']))
    print('%d imports from %d libraries:' % (sum(len(o) for _l, _i, o in info['imports']),
                                             len(info['imports'])))
    for lib, iat, ords in info['imports']:
        print('  %-40s %3d  slots at 0x%x' % (lib, len(ords), iat))
    if '--out' in args:
        p = args[args.index('--out') + 1]
        open(p, 'wb').write(raw)
        print('written: %s' % p)
    if '--bill' in args:
        import gen_shim
        out, _dlls = gen_shim.build_imports(import_list(info))
        kinds = {}
        missing = []
        for (_i, _lib, _o, sig, kind), (lib, o) in zip(out, import_list(info)):
            k = ('unanswered' if kind == gen_shim.KIND_NONE else
                 'local' if kind == gen_shim.KIND_LOCAL else 'forwarded')
            kinds[k] = kinds.get(k, 0) + 1
            if kind == gen_shim.KIND_NONE:
                missing.append((lib, o, sig))
        print('shim: %s' % ', '.join('%d %s' % (v, k) for k, v in sorted(kinds.items())))
        for lib, o, sig in missing:
            print('  unanswered  %-24s %4d  %s' % (lib, o, sig or '(no name)'))


if __name__ == '__main__':
    main()
