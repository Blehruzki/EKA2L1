"""Write the import section of an EABI (Symbian 9.x) E32 image.

    u32 size, counting this word, not padded
    per DLL: u32 name offset from the section start, u32 import count,
             u32 * count: offsets into the CODE section
    then the DLL names, NUL-terminated, back to back

Each of those code offsets points at a word holding the ordinal, which the
loader overwrites with the resolved address.  The linker puts that word behind
an eight-byte stub:

    ldr pc, [pc, #-4]
    .word <ordinal>          <- patched

`ldr pc,[pc,#-4]` reads pc as its own address plus 8, so it jumps through the
word directly after it.  Calling an import is then a plain `bl` to the stub.

`selftest` rebuilds a shipped image's import section from the section parsed
back out of it and requires a byte-for-byte match -- the check whose absence
cost a device round-trip on the relocation section.
"""
import struct, sys

STUB = struct.pack('<I', 0xE51FF004)      # ldr pc, [pc, #-4]


def section(dlls):
    """dlls: [(name, [code offsets of the ordinal words])] -> bytes"""
    head = 4 + sum(8 + 4 * len(offs) for _n, offs in dlls)
    body, names, cursor = b'', b'', head
    for name, offs in dlls:
        body += struct.pack('<II', cursor, len(offs))
        body += b''.join(struct.pack('<I', o) for o in offs)
        raw = name.encode('latin1') + b'\0'
        names += raw
        cursor += len(raw)
    out = struct.pack('<I', 4 + len(body) + len(names)) + body + names
    return out + b'\0' * (-len(out) % 4)


def parse(d, header):
    """-> [(name, [code offsets])], as `section` takes them."""
    imp = header['import_offset']
    o, out = imp + 4, []
    for _ in range(header['dll_ref_count']):
        name_off, n = struct.unpack_from('<II', d, o)
        o += 8
        offs = list(struct.unpack_from('<%dI' % n, d, o)) if n else []
        o += 4 * n
        end = d.index(b'\0', imp + name_off)
        out.append((d[imp + name_off:end].decode('latin1'), offs))
    return out


def selftest(image):
    import e32imports
    d = open(image, 'rb').read()
    h = e32imports.header(d)
    mine = section(parse(d, h))
    theirs = d[h['import_offset']:h['code_reloc_offset']]
    ok = mine == theirs
    print('importsec selftest: %s (%d bytes, %d DLLs)'
          % ('exact match' if ok else 'FAILED', len(theirs), h['dll_ref_count']))
    if not ok:
        n = min(len(mine), len(theirs))
        i = next((k for k in range(n) if mine[k] != theirs[k]), n)
        print('  sizes %d vs %d; first difference at byte %d' % (len(mine), len(theirs), i))
        print('  ours   %s\n  theirs %s' % (mine[max(0, i - 4):i + 12].hex(),
                                            theirs[max(0, i - 4):i + 12].hex()))
    return ok


if __name__ == '__main__':
    sys.exit(0 if selftest(sys.argv[1]) else 1)
