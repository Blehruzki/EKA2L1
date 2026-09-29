#!/usr/bin/env python3
"""ordcheck.py -- how far can the ordinal directory be trusted?

The port redials: the game asks for an old ordinal, the shim answers with a
9.x one. For a single game that map is 462 entries and every one of them was
looked at. For a **generic loader** the map has to cover every library the
platform has, and nobody will look at those one by one -- so the question is
what the error rate of the generated map is, and whether the errors are in
functions games actually call.

There are two halves and they have different ground truth.

**The new side.** `gen_shim.py` resolves 9.x ordinals out of the `.def` files
in the Symbian source release. The phone does not run the source release; it
runs its own ROM. If the two disagree about how many exports a library has,
they disagree about what lives at an ordinal, and the shim is dialling a
number from the wrong directory. The RM-409 ROM on the emulator's z: drive is
the real thing, and a ROM image header carries `iExportDirCount` -- so this
is directly measurable, per library, for the whole platform.

**The old side.** EKA2L1's `epoc6.def` lists 555 libraries' exports in what
`epocdb.py` reads as ordinal order. The only authoritative old-side list we
have is `7.0-euseru.def`, so euser is the one library where the reading can
be scored. What matters is not the overall score but the score **on the
ordinals games actually import** -- a wrong name on an export nothing calls
costs nothing.

    ordcheck.py [image ...]     # the images say which ordinals are 'used'
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import e32imports
import epocdb
import gnuv2
import romimg
import symdef

ROM = '/root/.local/share/EKA2L1/data/drives/z/rm-409/sys/bin'
EPOC6 = '/home/user/EKA2L1/src/emu/bridge/include/bridge/epoc6.def'
SRC = '/home/user/symbiansource'
EUSER7 = SRC + '/oss.fcl.sf.os.kernelhwsrv/kernel/eka/bmarm/7.0-euseru.def'


def rom_export_counts(d=ROM):
    """{library stem: export count} out of the device's own ROM images."""
    out = {}
    for f in sorted(os.listdir(d)):
        p = os.path.join(d, f)
        if not os.path.isfile(p):
            continue
        # The whole file: header() validates itself by reading the export
        # directory back, so a 128-byte slice fails every image. The first
        # cut of this did exactly that and reported nought libraries.
        try:
            with open(p, 'rb') as fh:
                h = romimg.header(fh.read())
        except Exception:
            continue
        if isinstance(h, tuple):
            h = h[0]
        if not h or h['uid1'] not in (romimg.UID1_DLL, romimg.UID1_EXE):
            continue
        n = h['export_dir_count']
        if 0 <= n < 100000:
            out[os.path.splitext(f)[0].lower()] = n
    return out


def source_export_counts(root=SRC):
    """{library stem: highest ordinal} out of every eabi .def in the release.

    The highest ordinal rather than the line count: a .def may skip numbers,
    and what has to line up with the ROM is the size of the ordinal space.
    """
    out = {}
    for base, dirs, files in os.walk(root):
        if os.path.basename(base).lower() != 'eabi':
            continue
        for f in files:
            if not f.lower().endswith('.def'):
                continue
            try:
                t = symdef.load(os.path.join(base, f))
            except Exception:
                continue
            if not t:
                continue
            # `WS322U.DEF` is ws32: a version digit and the 'u' of a
            # unicode build both hang off the name, and symdef.find knows
            # that when it is looking one up. Building a table instead means
            # registering every spelling, or euser, cone and bitgdi all read
            # as "no .def in the release" -- which is what the first cut of
            # this said, and it was wrong.
            for stem in _stems(symdef.base_name(f)):
                # A release carries several builds of some libraries; keep
                # the largest, which a late feature pack would ship.
                if out.get(stem, (0,))[0] < max(t):
                    out[stem] = (max(t), os.path.join(base, f))
    return out


def _stems(name):
    """Every name one .def might be known by: ws322u -> ws322u, ws322, ws32."""
    out = {name}
    if name.endswith('u'):
        out.add(name[:-1])
    for n in list(out):
        out.add(n.rstrip('0123456789'))
    return {n for n in out if n}


def new_side(used_libs):
    rom = rom_export_counts()
    src = source_export_counts()
    both = sorted(set(rom) & set(src))
    print('=' * 72)
    print('NEW SIDE -- the Symbian source release against the phone that runs it')
    print('=' * 72)
    print('%d libraries in the RM-409 ROM, %d with an eabi .def in the source '
          'release, %d in both' % (len(rom), len(src), len(both)))
    # **Direction is everything, and the first cut of this missed it.**
    # Symbian froze .def files: a later release appends exports and never
    # renumbers, which is the whole reason a 9.1 binary runs on 9.3. So a
    # release with MORE exports than the phone agrees with it on every
    # ordinal the phone has -- the extra ones are simply newer functions,
    # and asking for one returns null, which the shim already reports. A
    # release with FEWER is the dangerous direction: it is older than the
    # phone, and nothing then vouches for the numbering.
    same = [l for l in both if rom[l] == src[l][0]]
    newer = [l for l in both if src[l][0] > rom[l]]
    older = [(l, rom[l], src[l][0]) for l in both if src[l][0] < rom[l]]
    print('  release == ROM : %4d  (%.1f%%)  identical numbering'
          % (len(same), 100.0 * len(same) / max(1, len(both))))
    print('  release >  ROM : %4d  (%.1f%%)  release is newer -- safe under '
          'append-only; a missing ordinal reads back null'
          % (len(newer), 100.0 * len(newer) / max(1, len(both))))
    print('  release <  ROM : %4d  (%.1f%%)  release is OLDER than the phone '
          '-- the direction that can mislead'
          % (len(older), 100.0 * len(older) / max(1, len(both))))
    agree = same
    off = [(l, rom[l], src[l][0]) for l in both if rom[l] != src[l][0]]
    off.sort(key=lambda t: -abs(t[1] - t[2]))
    print('\nthe libraries the port actually binds to:')
    for l in sorted(used_libs):
        r, s = rom.get(l), src.get(l, (None,))[0]
        if r is None and s is None:
            print('  %-12s not in the ROM, not in the release' % l)
        elif r is None:
            print('  %-12s not in the ROM; release says %s' % (l, s))
        elif s is None:
            print('  %-12s ROM %4d; no .def in the release' % (l, r))
        else:
            mark = ('same' if r == s else
                    'release newer by %d -- safe' % (s - r) if s > r else
                    'RELEASE OLDER BY %d -- check' % (r - s))
            print('  %-12s ROM %4d   release %4d   %s' % (l, r, s, mark))
    if older:
        older.sort(key=lambda t: t[1] - t[2], reverse=True)
        print('\nevery library where the release is older than the phone (%d):'
              % len(older))
        for l, r, s in older[:20]:
            print('  %-24s ROM %5d   release %5d   -%d' % (l, r, s, r - s))
    else:
        print('\nno library in the release is older than the phone.')
    return rom, src


def old_side(used_ordinals):
    print()
    print('=' * 72)
    print("OLD SIDE -- EKA2L1's epoc6 database against the authoritative euser")
    print('=' * 72)
    db = epocdb.load(EPOC6)
    auth = symdef.load(EUSER7)
    guess = db.get('euser') or db.get('euseru') or []
    top = max(auth)
    print('epoc6 lists %d euser exports; 7.0-euseru.def goes to ordinal %d'
          % (len(guess), top))
    same = diff = missing = 0
    wrong = []
    for o in sorted(auth):
        want = auth[o][1] or auth[o][0]
        if o - 1 >= len(guess):
            missing += 1
            continue
        got = guess[o - 1]
        if _same(got, want):
            same += 1
        else:
            diff += 1
            wrong.append((o, want, got))
    scored = same + diff
    print('scored %d positions: %d agree, %d disagree, %d past the end of the guess'
          % (scored, same, diff, missing))
    print('  -> %.1f%% of the whole library' % (100.0 * same / max(1, scored)))
    if used_ordinals:
        us = ud = 0
        for o in sorted(used_ordinals):
            if o not in auth or o - 1 >= len(guess):
                continue
            want = auth[o][1] or auth[o][0]
            if _same(guess[o - 1], want):
                us += 1
            else:
                ud += 1
        print('  -> %.1f%% of the %d euser ordinals a real game imports (%d wrong)'
              % (100.0 * us / max(1, us + ud), us + ud, ud))
    # Not every disagreement is a wrong number. 9.x moved methods up the
    # hierarchy -- `RHeap::AllocL` became `RAllocator::AllocL` and exports
    # at the same ordinal -- and the release's own .def suffixes a duplicate
    # class name with a digit (`RHeap1`). Both are the same code reached the
    # same way. What matters is a disagreement where the **method** differs.
    renamed = [t for t in wrong if _method(t[2]) == _method_of(t[1])]
    real = [t for t in wrong if t not in renamed]
    print('  of the %d disagreements: %d are the same method on a renamed or '
          'reparented class, %d name a different method'
          % (len(wrong), len(renamed), len(real)))
    print('  -> %.1f%% of the library reaches the right function'
          % (100.0 * (same + len(renamed)) / max(1, scored)))
    print('\nthe first ten that name a different method -- these are the real ones:')
    for o, want, got in real[:10]:
        print('  %4d  release: %-42s  epoc6: %s' % (o, _short(want), _short(got)))
    return wrong


def _method(sym):
    return _norm((sym or '').split('__')[0]).rstrip('0123456789')


def _method_of(sig):
    head = (sig or '').split('(')[0]
    # `User1::ReAlloc1L` is the release .def's way of disambiguating a
    # duplicate name, not a function called ReAlloc1L. Strip the digits.
    return _norm(head.split('::')[-1]).rstrip('0123456789').replace('1l', 'l')


def _norm(s):
    return ''.join(c for c in (s or '').lower() if c.isalnum())


def _same(got, want):
    """Same export?

    `got` is an epoc6 entry, which is a **GCC98r2 mangled symbol**, and
    `want` is the release's demangled signature. The first cut of this
    compared the two as text and scored 3% -- with `ASin__4MathRdRCd`
    against `Math::ASin(double &, double const &)` in the disagreement list,
    which is the same function written two ways. The **instrument was the
    finding**, for the seventh time on this project, and it was caught by
    reading the sample rather than the percentage.

    So demangle first, and compare signatures.
    """
    na, nb = _norm(got), _norm(want)
    if na and na == nb:
        return True
    try:
        sig = gnuv2.demangle(got)
    except Exception:
        sig = None
    if sig and _norm(sig) == nb:
        return True
    # Argument spellings differ between the two sources (`int` vs `TInt`,
    # spacing round `&`), so compare class and method -- which is what
    # decides whether a redial reaches the right code. And take that
    # straight off the mangled symbol rather than through the demangler:
    # gnuv2 does not handle `G`, the GCC98r2 marker for a class argument
    # passed by value, so `After__4UserG27TTimeIntervalMicroSeconds32`
    # came back as a disagreement with `User::After(...)`. Second time the
    # comparison, not the data, was the finding in this one script.
    if sig and _head(sig) == _head(want):
        return True
    return _head_mangled(got) == _head(want)


def _head(sig):
    """'Math::ASin(double &, ...)' -> 'mathasin' -- who and what, no arguments."""
    return _norm((sig or '').split('(')[0])


def _head_mangled(sym):
    """'AppendFill__5TDes8G5TChari' -> 'tdes8appendfill'.

    GCC98r2 writes `method__[C|V]*<len><Class><arguments>` for a member and
    `func__F<arguments>` for a free function. Only the front is needed here.
    """
    sym = sym or ''
    if '__' not in sym:
        return _norm(sym)
    method, rest = sym.split('__', 1)
    i = 0
    while i < len(rest) and rest[i] in 'CV':
        i += 1
    if i < len(rest) and rest[i] == 'F':        # a free function, no class
        return _norm(method)
    j = i
    while j < len(rest) and rest[j].isdigit():
        j += 1
    if j == i:
        return _norm(method)
    n = int(rest[i:j])
    return _norm(rest[j:j + n] + method)


def _short(s, n=42):
    s = (s or '').strip()
    return s if len(s) <= n else s[:n - 3] + '...'


def used_from(images):
    """{library stem: {ordinals}} the given old images actually import."""
    libs, ords = set(), {}
    for p in images:
        try:
            d = open(p, 'rb').read()
            imp = e32imports.imports(d)
        except Exception as e:
            print('%s: %s' % (p, e))
            continue
        for dll, olist in imp:
            stem = symdef.base_name(dll)
            libs.add(stem)
            ords.setdefault(stem, set()).update(olist)
    return libs, ords


if __name__ == '__main__':
    images = sys.argv[1:]
    libs, ords = used_from(images) if images else (set(), {})
    if not libs:
        libs = {'apparc', 'avkon', 'bitgdi', 'cone', 'efsrv', 'eikcore',
                'eikcoctl', 'eikdlg', 'estlib', 'euser', 'fbscli', 'hal',
                'ws32', 'etel', 'esock'}
    new_side(libs)
    old_side(ords.get('euser', set()))
