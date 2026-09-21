"""Read EKA2L1's bundled EPOC export databases as ordinal tables.

`src/emu/bridge/include/bridge/epoc6.def` lists, for 556 libraries, the exports
of an EPOC6-era (Symbian 6/7, GCC98r2 ARM) build in file order.  That order is
the ordinal order: checked against the authoritative `7.0-euseru.def` from the
Symbian source release, 1574 of euser's 1646 positions agree -- 95.6%, and 155
of the 164 ordinals the N-Gage game actually imports.  The disagreements are
mostly renames of the same function (`memclr` / `Mem::FillZ`, `User::Allocator`
/ `User::Heap`); a handful are genuine differences between builds.

So this is a strong lead, not proof.  Anything resolved through here should be
confirmed against the call site before it is relied on.
"""
import re

LIB = re.compile(r'\s*LIB\(([^)]*)\)')
EXPORT = re.compile(r'\s*EXPORT\("(.*)",\s*\d+\)')
ENDLIB = re.compile(r'\s*ENDLIB\(')


def load(path):
    """-> {library name: [export names, ordinal 1 first]}"""
    out, cur = {}, None
    for line in open(path, errors='replace'):
        m = LIB.match(line)
        if m:
            cur = m.group(1).lower()
            out.setdefault(cur, [])
            continue
        if ENDLIB.match(line):
            cur = None
            continue
        m = EXPORT.match(line)
        if m and cur is not None:
            out[cur].append(m.group(1))
    return out


def table(exports):
    """A list of exports -> the {ordinal: (symbol, None)} shape symdef.load returns."""
    return {i: (name, None) for i, name in enumerate(exports, start=1)}
