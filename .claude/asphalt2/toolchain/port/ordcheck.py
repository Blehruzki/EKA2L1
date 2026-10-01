#!/usr/bin/env python3
"""How far the ordinal tables go, for a title that has not been ported yet.

    ordcheck.py <game dir or image> [...]       one report per title
    ordcheck.py --libs                          the table sources, per library

A title's imports are read out of its .app and of every DLL shipped beside
it (those are EKA1 images too, and a loader would carry them the same way).
Every import is then followed along the only chain there is:

    old ordinal --(old name)--> signature --(9.x def)--> new ordinal --> RM-409 ROM

and classified by the weakest link it crossed:

    def       old name from the authoritative 7.0 euser def, 9.x ordinal from
              the release def, present in the ROM and not a stub
    epoc6=    old name from EKA2L1's epoc6.def, for a library whose export
              count in epoc6 equals the N-Gage ROM's own (rh-29): the list is
              the same build, so a position is very likely the right name
    epoc6~    the same, but the counts differ: epoc6 lists another build of
              that library and a position past the first insertion names the
              wrong function. Confirm at the call site before relying on it
    beyond    the chain gives a 9.x ordinal past the ROM's export count, or
              an ordinal whose body is `bx lr` (def drift; see avkon 4021)
    no-9.x    the old name is known and 9.x has no export of that signature:
              the function is gone and the shim has to supply one
    unnamed   nothing names the old ordinal at all
    absent    the library does not exist on S60v3 (N-Gage-only, or removed):
              every import of it is the shim's to answer
    shipped   the library comes with the game, so it is translated as part
              of the game rather than looked up

`shared` counts the (library, old ordinal) pairs that the two Asphalt ports
already resolve the same way, which is as close to "proven on a phone" as a
table gets. Everything else is the new title's own bill.
"""
import glob, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e32imports, epocdb, gen_shim, gnuv2, romimg, shimtable, symdef

Z = '/root/.local/share/EKA2L1/data/drives/z/'
ROM9 = '/root/.local/share/EKA2L1/data/roms/rm-409/SYM.ROM'
OLD_DEFS = {'euser': gen_shim.KERNEL + '/kernel/eka/bmarm/7.0-euseru.def'}
ASPHALT = [os.path.join(os.path.dirname(os.path.abspath(__file__)), p)
           for p in ('../../../../.claude/asphalt2/toolchain/port/6rbc_orig.app',)]
BX_LR = ('4770', 'e12fff1e')


def libname(n):
    return n.split('[')[0].split('{')[0].lower().replace('.dll', '')


class Tables:
    def __init__(self):
        self.db6 = epocdb.load(gen_shim.EPOC6)
        self.db9 = epocdb.load(gen_shim.EPOC9)
        self.defs = gen_shim.find_defs()
        self.old = {lib: symdef.load(p) for lib, p in OLD_DEFS.items()}
        self.new_index, self.new_mangled, self.rom9, self.rom7 = {}, {}, {}, {}

    def new_tables(self, lib):
        if lib in self.new_index:
            return
        if lib in self.defs:
            t = symdef.load(self.defs[lib])
            self.new_index[lib] = shimtable.index(t)
            self.new_mangled[lib] = shimtable.mangled_index(t)
        elif lib in self.db9:
            t = {}
            for o, sig in enumerate(self.db9[lib], 1):
                t.setdefault(shimtable.norm(sig), o)
            self.new_index[lib] = t
            self.new_mangled[lib] = {}
        else:
            self.new_index[lib] = None
            self.new_mangled[lib] = {}

    def rom(self, lib, which):
        cache = self.rom9 if which == 9 else self.rom7
        if lib in cache:
            return cache[lib]
        path = Z + ('rm-409/sys/bin/%s.dll' if which == 9 else 'rh-29/system/libs/%s.dll') % lib
        out = None
        if os.path.exists(path):
            try:
                d, h = romimg.load(path, rom=ROM9 if which == 9 else None)
                out = (d, h, romimg.exports(d, h) if which == 9 else None)
            except Exception as e:          # an extract the reader cannot place
                out = (None, {'export_dir_count': -1, 'err': str(e)}, None)
        cache[lib] = out
        return out

    def rom9_count(self, lib):
        r = self.rom(lib, 9)
        return r[1]['export_dir_count'] if r else None

    def rom7_count(self, lib):
        r = self.rom(lib, 7)
        return r[1]['export_dir_count'] if r else None

    def is_stub(self, lib, ordinal):
        r = self.rom(lib, 9)
        if not r or not r[2] or ordinal not in r[2]:
            return None
        d, h, ex = r
        a = ex[ordinal]
        off = romimg.offset(h, a & ~1)
        if off is None or off + 4 > len(d):
            return None                 # a data export, or outside the image
        if a & 1:
            return d[off:off + 2].hex() == BX_LR[0]
        return d[off:off + 4].hex() == BX_LR[1]

    def old_name(self, lib, o):
        """-> (signature, source) or (None, None)"""
        if lib in self.old:
            t = self.old[lib]
            if o in t:
                return t[o][1] or gnuv2.demangle(t[o][0]), 'def'
            return None, None
        if lib in self.db6 and 0 < o <= len(self.db6[lib]):
            raw = self.db6[lib][o - 1]
            return raw, 'epoc6=' if self.rom7_count(lib) == len(self.db6[lib]) else 'epoc6~'
        return None, None

    def new_ordinal(self, lib, sig, source):
        self.new_tables(lib)
        idx = self.new_index.get(lib)
        if idx is None:
            return None
        if source != 'def':
            n = self.new_mangled[lib].get(sig)
            if n is not None:
                return n
            sig = gnuv2.demangle(sig)
        return idx.get(shimtable.norm(sig))


def image_imports(path):
    d = open(path, 'rb').read()
    return [(libname(n), o) for n, ords in e32imports.imports(d) for o in ords]


def title_images(arg):
    if os.path.isdir(arg):
        files = sorted(glob.glob(os.path.join(arg, '*')))
        apps = [f for f in files if f.lower().endswith('.app')]
        dlls = [f for f in files if f.lower().endswith('.dll')]
        return apps, dlls
    return [arg], []


def answered(lib, o, sig):
    """True if gen_shim.py already carries a hand answer for this import."""
    if (lib, o) in gen_shim.BY_ORDINAL or lib in gen_shim.NGAGE_ONLY:
        return True
    if not sig:
        return False
    dem = gnuv2.demangle(sig)
    if dem in gen_shim.HELPERS or dem in gen_shim.LOCAL or dem in gen_shim.MANUAL:
        return True
    return gen_shim._is_framework_ctor(dem)


def classify(T, lib, o, shipped):
    if lib in shipped:
        return 'shipped', None, None
    count9 = T.rom9_count(lib)
    sig, src = T.old_name(lib, o)
    if count9 is None:
        return 'absent', sig, None
    if sig is None:
        return 'unnamed', None, None
    n = T.new_ordinal(lib, sig, src)
    if n is None:
        return 'no-9.x', sig, None
    if count9 >= 0 and (n > count9 or T.is_stub(lib, n)):
        return 'beyond', sig, n
    return src, sig, n


ORDER = ['def', 'epoc6=', 'epoc6~', 'answered', 'beyond', 'no-9.x', 'unnamed', 'absent', 'shipped']
BILL = ('beyond', 'no-9.x', 'unnamed', 'absent')


def report(T, arg, shared):
    apps, dlls = title_images(arg)
    shipped = {libname(os.path.basename(f)) for f in dlls}
    # A shipped DLL that is one of the N-Gage-only services is stubbed whole
    # by the shim (NGAGE_ONLY), so its own imports are nobody's bill. Every
    # other shipped DLL is game code and is carried, imports and all.
    stubbed = [f for f in dlls if libname(os.path.basename(f)) in gen_shim.NGAGE_ONLY]
    carried = [f for f in dlls if f not in stubbed]
    rows, seen = [], set()
    for f in apps + carried:
        for lib, o in image_imports(f):
            if (lib, o) in seen:
                continue
            seen.add((lib, o))
            cat, sig, n = classify(T, lib, o, shipped)
            if cat != 'shipped' and answered(lib, o, sig):
                cat = 'answered'
            rows.append((os.path.basename(f), lib, o, cat, sig, n))
    name = os.path.basename(arg.rstrip('/'))
    total = len(rows)
    by = {c: sum(1 for r in rows if r[3] == c) for c in ORDER}
    platform = total - by['shipped']
    inshared = sum(1 for r in rows if r[3] not in ('shipped',) and (r[1], r[2]) in shared)
    print('== %s: %d images (%d app, %d shipped dll of which %d stubbed as N-Gage-only), %d distinct imports, %d of them platform'
          % (name, len(apps) + len(dlls), len(apps), len(dlls), len(stubbed), total, platform))
    appset = {(r[1], r[2]) for f in apps for r in [(None,) + x for x in image_imports(f)]}
    appbill = sum(1 for r in rows if r[3] in BILL and (r[1], r[2]) in appset)
    print('   the .app alone: %d platform imports, %d of them on the bill; the %d carried DLLs add the rest'
          % (sum(1 for r in rows if (r[1], r[2]) in appset and r[3] != 'shipped'), appbill, len(carried)))
    print('   ' + '  '.join('%s %d' % (c, by[c]) for c in ORDER if by[c]))
    print('   shared with the Asphalt ports: %d of %d platform imports; new to this title: %d'
          % (inshared, platform, platform - inshared))
    newby = {c: sum(1 for r in rows if r[3] == c and (r[1], r[2]) not in shared) for c in ORDER}
    print('   of the new ones: ' + '  '.join('%s %d' % (c, newby[c]) for c in ORDER if newby[c] and c != 'shipped'))
    bill = sum(by[c] for c in BILL); newbill = sum(newby[c] for c in BILL)
    print('   THE BILL (no table answers it, the shim generator has none yet): %d imports, %d of them not in the Asphalt ports'
          % (bill, newbill))
    libs = sorted({r[1] for r in rows if r[3] != 'shipped'})
    print('   %-26s %5s %5s %6s %6s %5s %6s %7s %6s  old side' % ('library', 'imps', 'def', 'epoc6=', 'epoc6~', 'answ', 'beyond', 'no-9.x', 'absent'))
    for lib in libs:
        rl = [r for r in rows if r[1] == lib]
        c = {k: sum(1 for r in rl if r[3] == k) for k in ORDER}
        side = 'rh-29 %s / epoc6 %s / rm-409 %s' % (T.rom7_count(lib), len(T.db6.get(lib, [])) or '--', T.rom9_count(lib))
        print('   %-26s %5d %5d %6d %6d %5d %6d %7d %6d  %s' % (lib, len(rl), c['def'], c['epoc6='], c['epoc6~'], c['answered'], c['beyond'], c['no-9.x'], c['absent'], side))
    drift = [r for r in rows if r[3] == 'beyond']
    if drift:
        print('   def drift (the chain names a 9.x ordinal the ROM does not have, or a bx lr stub):')
        for f, lib, o, cat, sig, n in drift:
            print('     %-14s %5d  %s  -> 9.x %d' % (lib, o, (gnuv2.demangle(sig) if sig else '?')[:70], n))
    bad = [r for r in rows if r[3] in ('no-9.x', 'unnamed') and (r[1], r[2]) not in shared]
    if bad:
        print('   new to this title and answered by nothing (%d):' % len(bad))
        for f, lib, o, cat, sig, n in bad[:60]:
            print('     %-8s %-14s %5d  %s' % (cat, lib, o, (gnuv2.demangle(sig) if sig else '?')[:80]))
        if len(bad) > 60:
            print('     ... and %d more' % (len(bad) - 60))
    return rows


def main(args):
    T = Tables()
    if args == ['--libs']:
        for lib in sorted(T.db6):
            if T.rom7_count(lib) is not None:
                print('%-28s rh-29 %5s  epoc6 %5s  %s' % (lib, T.rom7_count(lib), len(T.db6[lib]), '=' if T.rom7_count(lib) == len(T.db6[lib]) else '~'))
        return 0
    shared = set()
    here = os.path.dirname(os.path.abspath(__file__))
    for img in (os.path.join(here, 'games/asphalt2/6rbc_orig.app'), os.path.join(here, 'games/asphalt1/6r67.app'),
                '/root/.local/share/EKA2L1/data/drives/e/system/apps/6r67/6r67.app',
                '/root/.local/share/EKA2L1/data/drives/e.ngage/system/apps/6rbc/6rbc.app'):
        if os.path.exists(img):
            shared |= set(image_imports(img))
    print('reference: %d (library, ordinal) pairs in the two Asphalt ports' % len(shared))
    for a in args:
        report(T, a, shared)
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
