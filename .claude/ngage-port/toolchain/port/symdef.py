"""Read Symbian .def files: ordinal -> exported symbol.

A .def line looks like

    _ZN10CCirBuffer3GetEv @ 2 NONAME ; CCirBuffer::Get()

i.e. the linker symbol, its ordinal, attributes, and -- for the ARM/GCC98r2
files -- a comment carrying the already-demangled signature.  Some lines use
`name=alias @ n`, so the symbol is taken up to the first '='.

These files come from the Symbian Foundation source release.  Which one to read
depends on the target's ABI, not just its name: `eabi/` is Symbian 9.x (what
S60v3 links against) and `bmarm/7.0-*` is the Symbian 7.0 ARM build the N-Gage
and other S60 1st Edition titles link against.
"""
import os, re

LINE = re.compile(r'^\s*(?P<sym>[^\s;=]+)(?:=[^\s]+)?\s+@\s*(?P<ord>\d+)\s*(?P<attr>[^;]*)(?:;\s*(?P<cmt>.*))?$')


def load(path):
    out = {}
    for line in open(path, 'r', errors='replace'):
        if line.strip().upper() in ('EXPORTS', '') or line.lstrip().startswith(';'):
            continue
        m = LINE.match(line.rstrip())
        if not m:
            continue
        cmt = (m.group('cmt') or '').strip()
        if cmt in ('(null)', ''):
            cmt = None
        out[int(m.group('ord'))] = (m.group('sym'), cmt)
    return out


VERSIONED = re.compile(r'^\d+\.\d+-')


def base_name(dll):
    """'CONE{000a0000}[10003a41].DLL' -> 'cone'; '7.0-euseru.def' -> 'euseru'."""
    name = dll.split('{')[0].split('[')[0]
    return VERSIONED.sub('', os.path.splitext(name)[0].lower())


def find(dll, *dirs):
    """Locate the .def for a DLL in the given directories, honouring the 'u' suffix."""
    want = base_name(dll)
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith('.def'):
                continue
            stem = base_name(f)
            if stem == want or stem == want + 'u' or stem.replace('-', '') == want:
                return os.path.join(d, f)
    return None
