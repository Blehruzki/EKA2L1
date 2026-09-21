#!/usr/bin/env python3
"""Name every import of an E32 image, using the Symbian source .def files.

    resolve_imports.py <image> <def dir> [...] [dll=path/to/exact.def ...]

An explicit `dll=path` pins one DLL's .def, which is how you pick the Symbian
7.0 ARM build of a library out of a directory that also holds the 9.x one.
`--epoc6 <file>` adds EKA2L1's EPOC6 export database as a fallback for the
libraries no .def covers; those lines are marked, because that source is about
95% right rather than authoritative.

Prints, per DLL, each imported ordinal with the function it resolves to, and
says plainly which DLLs have no .def available rather than quietly dropping
them.
"""
import sys
import e32imports, epocdb, gnuv2, symdef


def main(image, def_dirs, pinned=None, epoc6=None):
    pinned = pinned or {}
    db = epocdb.load(epoc6) if epoc6 else {}
    d = open(image, 'rb').read()
    h = e32imports.header(d)
    print('%s\n  %s / %s, %d DLLs' % (image, 'EKA1' if h['eka1'] else 'EKA2',
                                      h['abi'], h['dll_ref_count']))
    blocks = e32imports.imports(d, h)
    named = unnamed = 0
    missing = []
    for name, ords in sorted(blocks, key=lambda b: -len(b[1])):
        path = pinned.get(symdef.base_name(name)) or symdef.find(name, *def_dirs)
        uniq = sorted(set(ords))
        stem = symdef.base_name(name)
        if path:
            table, source = symdef.load(path), path.split('/')[-1]
        elif stem in db and db[stem]:
            table, source = epocdb.table(db[stem]), 'epoc6 database (~95% confidence)'
        else:
            missing.append((stem, len(uniq)))
            unnamed += len(uniq)
            continue
        print('\n%s  (%d imports)  <- %s' % (name, len(uniq), source))
        for n in uniq:
            sym, cmt = table.get(n, (None, None))
            named += sym is not None
            unnamed += sym is None
            print('  %5d  %s' % (n, cmt or (gnuv2.demangle(sym) if sym else 'not listed in this source')))
    if missing:
        print('\nno .def available for: ' + ', '.join('%s (%d)' % m for m in missing))
    print('\nresolved %d ordinals, %d still unnamed' % (named, unnamed))


if __name__ == '__main__':
    args = sys.argv[2:]
    epoc6 = None
    if '--epoc6' in args:
        i = args.index('--epoc6')
        epoc6 = args[i + 1]
        del args[i:i + 2]
    pins = dict(a.split('=', 1) for a in args if '=' in a)
    main(sys.argv[1], [a for a in args if '=' not in a],
         {symdef.base_name(k): v for k, v in pins.items()}, epoc6)
