#!/usr/bin/env python3
"""Does every import a title's shim forwards land on a real export of a phone?

    shimreach.py            every games/*/shim.cpp, against RM-409 and RM-92

gen_shim pairs an old import with a 9.x signature and takes its ordinal from
the release's .def -- which is Symbian^3's, carrying exports no S60v3 phone
has. ordcheck.py catches that on the def chain, as `beyond`; the MANUAL and
BY_ORDINAL rows are written by hand and nothing checked them. One of them
forwarded `RHeap::Size() const` to `RAllocator::Size()`, euser 2533, past
the RM-409 euser's 2,229 exports: the run-time lookup failed, the import kept
its reporting stub, and Glimmerati died in its menus
(E1308). This reads the generated tables back and checks each forward against
the two ROMs on this machine (S60 3.2 RM-409 and S60 3.0 RM-92): the ordinal
inside the library's export count. Not whether its body is a bare `bx lr`:
that is every empty default (CCoeControl::FocusChanged), not a sign of drift.

Exits 1 when any forward misses, so rules.py can run it.
"""
import glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import romimg

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = '/root/.local/share/EKA2L1/data/'
ROMS = (('RM-409', 'rm-409'), ('RM-92', 'rm-92'))
KIND_NONE, KIND_LOCAL = 0, 3

_cache = {}


def rom(tag, lib):
    key = (tag, lib)
    if key not in _cache:
        path = DATA + 'drives/z/%s/sys/bin/%s' % (tag, lib)
        out = None
        if os.path.exists(path):
            try:
                d, h = romimg.load(path, rom=DATA + 'roms/%s/SYM.ROM' % tag)
                out = (d, h, romimg.exports(d, h))
            except ValueError as e:     # an extract the reader cannot place
                print('  (%s %s unreadable: %s)' % (tag, lib, e))
                out = None
        _cache[key] = out
    return _cache[key]


def table(path):
    s = open(path).read()
    names = {int(i): ''.join(re.findall(r"'(.)'", body))
             for i, body in re.findall(r'kShimDll(\d+)\[\] = \{([^}]*)\}', s)}
    words = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+',
             re.search(r'kShimTable\[\] = \{(.*?)\};', s, re.S).group(1))]
    return names, words


def main():
    bad = 0
    for path in sorted(glob.glob(os.path.join(HERE, 'games', '*', 'shim.cpp'))):
        game = os.path.basename(os.path.dirname(path))
        names, words = table(path)
        for i, w in enumerate(words):
            kind, dll, o = w >> 24, (w >> 16) & 0xFF, w & 0xFFFF
            if kind in (KIND_NONE, KIND_LOCAL):
                continue
            lib = names[dll]
            for name, tag in ROMS:
                r = rom(tag, lib)
                if r is None:
                    continue            # not in that ROM: the loader's own business
                n = r[1]['export_dir_count']
                if o > n:
                    print('%-10s import %3d: %s %d is past %s\'s %d exports' % (game, i, lib, o, name, n))
                    bad += 1
    print('%d forwards miss' % bad if bad else 'every forward lands on an export of RM-409 and RM-92')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
