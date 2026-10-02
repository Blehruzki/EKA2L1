"""Match an old binary's imports to their Symbian 9.x equivalents.

A shim's job is to answer 462 imports. Most of them are the same function under
a different ordinal, so the work is to pair them up -- and the pairing is done
on the demangled signature, since the ordinal numbering has nothing in common
between the two ABIs.

For euser both sides are authoritative: the Symbian source ships a Symbian 7.0
ARM def and a 9.x EABI def, each carrying the demangled signature in a comment.
For the other libraries the old side comes from EKA2L1's EPOC6 database, which
records only mangled names, so those are demangled here first and matched at
roughly 95% confidence -- `match` says which source each pairing came from and
never presents the two as equal.

Signatures are normalised before comparison because the two files spell the
same type differently: `const TRect &` against `TRect const&`.
"""
import re

import subprocess

import epocdb, gnuv2, symdef

CONST = re.compile(r'\bconst\b')


def split_args(sig):
    inner = sig[sig.index('(') + 1:sig.rindex(')')] if '(' in sig else ''
    out, depth, cur = [], 0, ''
    for c in inner:
        if c == '(' or c == '<':
            depth += 1
        elif c == ')' or c == '>':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur); cur = ''
        else:
            cur += c
    if cur.strip():
        out.append(cur)
    return out


def norm_type(t):
    t = ' '.join(t.split())
    isconst = bool(CONST.search(t))
    t = CONST.sub('', t)
    t = t.replace(' ', '')
    if isconst:
        base = t.rstrip('*&')
        suffix = t[len(base):]
        t = base + 'const' + suffix
    return t


def norm(sig):
    if not sig or '(' not in sig:
        return (sig or '').replace(' ', '')
    name = sig[:sig.index('(')].replace(' ', '')
    tail = sig[sig.rindex(')') + 1:].replace(' ', '')
    args = [norm_type(a) for a in split_args(sig)]
    args = [a for a in args if a not in ('void', '')]
    return '%s(%s)%s' % (name, ','.join(args), tail)


def signatures(table):
    """A 9.x def table -> {ordinal: demangled signature}.

    Some of these files carry the signature in a comment and some are bare
    Itanium symbols, so the mangled names go through llvm-cxxfilt; a plain C
    name comes back unchanged, which is what we want.
    """
    ords = sorted(table)
    raw = [table[o][0] for o in ords]
    out = subprocess.run(['llvm-cxxfilt'], input='\n'.join(raw),
                         capture_output=True, text=True).stdout.splitlines()
    got = {}
    for o, sym, dem in zip(ords, raw, out):
        got[o] = table[o][1] or dem or sym
    return got


def index(table, demangle=False):
    """A def table -> {normalised signature: ordinal}."""
    sigs = signatures(table)
    out = {}
    for o in table:
        out.setdefault(norm(sigs[o]), o)
    return out


def mangled_index(table):
    """A 9.x def table -> {GCC 2.x mangled name: ordinal}, both spellings.

    Matching this way round rather than demangling the old side is exact: a .def
    comment carries a full signature and there is only one mangling of it, while
    the EPOC6 database's names are lossy -- avkon's carry no length prefixes at
    all, so they cannot be demangled even in principle.
    """
    sigs = signatures(table)
    out = {}
    for o in table:
        for digits in (True, False):
            m = gnuv2.mangle(sigs[o], digits)
            if m:
                out.setdefault(m, o)
    return out


def match(image_imports, old_sources, new_sources, epoc6=None):
    """-> [(index, lib, old ordinal, signature, new ordinal or None, source)]"""
    db = epocdb.load(epoc6) if epoc6 else {}
    old_tables, new_index, new_mangled = {}, {}, {}
    for lib, path in old_sources.items():
        old_tables[lib] = (symdef.load(path), 'def')
    for lib, path in new_sources.items():
        table = symdef.load(path)
        new_index[lib] = index(table)
        new_mangled[lib] = mangled_index(table)

    out = []
    for i, (lib, o) in enumerate(image_imports):
        sig, source, new = None, None, None
        if lib in old_tables:
            table, source = old_tables[lib]
            if o in table:
                sig = table[o][1] or gnuv2.demangle(table[o][0])
            new = new_index.get(lib, {}).get(norm(sig)) if sig else None
        elif lib in db and 0 < o <= len(db[lib]):
            raw, source = db[lib][o - 1], 'epoc6'
            sig = gnuv2.demangle(raw)
            # Mangling the 9.x side is exact; falling back to comparing
            # demangled signatures catches the plain C libraries.
            new = new_mangled.get(lib, {}).get(raw)
            if new is None:
                new = new_index.get(lib, {}).get(norm(sig))
        out.append((i, lib, o, sig, new, source))
    return out
