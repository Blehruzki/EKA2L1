"""A small demangler for GCC 2.x (GCC98r2) symbol names.

Symbian's pre-EABI ARM builds use this mangling, and modern binutils no longer
demangles it, so the names in the EPOC6 export database would otherwise stay
unreadable.  This handles the shapes that actually occur in those tables and
returns the original string for anything it does not recognise, so a name is
never silently mangled into something wrong.

    Foo__5CBaseRC7TDesC16i  ->  CBase::Foo(TDesC16 const &, int)
    _._5CBase               ->  CBase::~CBase()
    __5CBasei               ->  CBase::CBase(int)
"""
import re

BUILTIN = {'v': 'void', 'c': 'char', 's': 'short', 'i': 'int', 'l': 'long',
           'x': 'long long', 'f': 'float', 'd': 'double', 'b': 'bool',
           'r': 'long double', 'w': 'wchar_t', 'e': '...'}


class _Fail(Exception):
    pass


def _type(s, i, seen):
    pre, post = '', ''
    while i < len(s):
        c = s[i]
        if c == 'P':
            post = ' *' + post; i += 1
        elif c == 'R':
            post = ' &' + post; i += 1
        elif c == 'C':
            pre = 'const ' if post else ''; i += 1
            if not pre:
                post = ' const' + post
        elif c == 'U':
            pre += 'unsigned '; i += 1
        elif c == 'S':
            pre += 'signed '; i += 1
        else:
            break
    if i >= len(s):
        raise _Fail()
    c = s[i]
    if c == 'F':                         # function type: F<args>_<return>
        depth, j = 0, i + 1
        while j < len(s):
            if s[j] == 'F':
                depth += 1
            elif s[j] == '_' and depth == 0:
                break
            elif s[j] == '_':
                depth -= 1
            j += 1
        if j >= len(s):
            raise _Fail()
        inner = _args(s[i + 1:j])
        ret, k = _type(s, j + 1, [])
        return '%s (%s)(%s)' % (ret, post.strip() or '*', ', '.join(inner)), k
    if c == 'Q':                         # qualified name: Q<count><len><name>...
        m = re.match(r'Q(\d)', s[i:])
        if not m:
            raise _Fail()
        parts, j = [], i + m.end()
        for _ in range(int(m.group(1))):
            mm = re.match(r'(\d+)', s[j:])
            if not mm:
                raise _Fail()
            n = int(mm.group(1)); j += mm.end()
            parts.append(s[j:j + n]); j += n
        return (pre + '::'.join(parts) + post).strip(), j
    if c.isdigit():
        m = re.match(r'\d+', s[i:])
        n = int(m.group())
        i += m.end()
        base = s[i:i + n]
        i += n
    elif c in BUILTIN:
        base = BUILTIN[c]; i += 1
    elif c == 'T':                       # back-reference to an earlier argument
        m = re.match(r'T(\d+)', s[i:])
        if not m:
            raise _Fail()
        idx = int(m.group(1)) - 1
        if idx >= len(seen):
            raise _Fail()
        return seen[idx], i + m.end()
    else:
        raise _Fail()
    return (pre + base + post).strip(), i


def _args(s):
    if s in ('', 'v'):
        return []
    out, i = [], 0
    while i < len(s):
        m = re.match(r'N(\d)(\d+)', s[i:])          # N<count><arg> repeats an argument
        if m:
            idx = int(m.group(2)) - 1
            if idx >= len(out):
                raise _Fail()
            out += [out[idx]] * int(m.group(1))
            i += m.end()
            continue
        t, i = _type(s, i, out)
        out.append(t)
    return out


def demangle(sym):
    try:
        if sym.startswith('_._'):                       # destructor
            m = re.match(r'_\._(\d+)(.*)$', sym)
            cls = m.group(2)[:int(m.group(1))]
            return '%s::~%s()' % (cls, cls)
        if sym.startswith('__') and sym[2:3].isdigit():  # constructor
            m = re.match(r'__(\d+)(.*)$', sym)
            n = int(m.group(1)); rest = m.group(2)
            cls, rest = rest[:n], rest[n:]
            return '%s::%s(%s)' % (cls, cls, ', '.join(_args(rest)))
        if '__' not in sym:
            return sym
        name, rest = sym.split('__', 1)
        if rest.startswith('F'):                        # free function
            return '%s(%s)' % (name, ', '.join(_args(rest[1:])))
        konst = False
        if rest.startswith('C') and rest[1:2].isdigit():   # a const member function
            konst, rest = True, rest[1:]
        m = re.match(r'(\d+)(.*)$', rest)
        if not m:
            return sym
        n = int(m.group(1)); tail = m.group(2)
        cls, tail = tail[:n], tail[n:]
        return '%s::%s(%s)%s' % (cls, name, ', '.join(_args(tail)),
                                 ' const' if konst else '')
    except Exception:
        return sym


# --- the other direction ------------------------------------------------------
#
# EKA2L1's EPOC6 database stores GCC 2.x mangled names, and for some libraries
# (avkon among them) without the length prefixes, so demangling them is lossy or
# impossible.  Mangling the 9.x side instead is exact: a Symbian .def comment
# gives a full signature, and there is one mangling of it.  `mangle` returns
# both spellings so a digit-less table still matches.

_CODES = [('unsigned char', 'Uc'), ('unsigned short', 'Us'), ('unsigned int', 'Ui'),
          ('unsigned long long', 'Ux'), ('unsigned long', 'Ul'), ('signed char', 'Sc'),
          ('long long', 'x'), ('long double', 'r'), ('unsigned', 'Ui'),
          ('void', 'v'), ('bool', 'b'), ('char', 'c'), ('short', 's'), ('int', 'i'),
          ('long', 'l'), ('float', 'f'), ('double', 'd'), ('wchar_t', 'w')]


def _mangle_type(t, digits):
    t = ' '.join(t.replace('const', ' const ').split())
    suffix = ''
    while t.endswith('*') or t.endswith('&'):
        suffix = ('P' if t[-1] == '*' else 'R') + suffix
        t = t[:-1].strip()
    isconst = False
    if t.startswith('const '):
        isconst, t = True, t[6:].strip()
    elif t.endswith(' const'):
        isconst, t = True, t[:-6].strip()
    for name, code in _CODES:
        if t == name:
            return suffix + ('C' if isconst else '') + code
    return suffix + ('C' if isconst else '') + (('%d%s' % (len(t), t)) if digits else t)


def mangle(sig, digits=True):
    """'CCoeControl::SetRect(TRect const&)' -> 'SetRect__11CCoeControlRC5TRect'"""
    if '(' not in sig:
        return None
    head = sig[:sig.index('(')].strip()
    args = sig[sig.index('(') + 1:sig.rindex(')')]
    konst = sig[sig.rindex(')') + 1:].strip().startswith('const')
    if '::' not in head:
        return None
    cls, fn = head.rsplit('::', 1)
    if fn.startswith('~'):
        fn = '_._'
        return fn + (('%d%s' % (len(cls), cls)) if digits else cls)
    if fn == cls:                                    # a constructor
        parts = []
        for a in _split(args):
            parts.append(_mangle_type(a, digits))
        return '__' + (('%d%s' % (len(cls), cls)) if digits else cls) + ''.join(parts)
    parts = [_mangle_type(a, digits) for a in _split(args) if a.strip() not in ('', 'void')]
    return '%s__%s%s%s' % (fn, 'C' if konst else '',
                           ('%d%s' % (len(cls), cls)) if digits else cls, ''.join(parts))


def _split(args):
    out, depth, cur = [], 0, ''
    for c in args:
        if c in '(<':
            depth += 1
        elif c in ')>':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur); cur = ''
        else:
            cur += c
    if cur.strip():
        out.append(cur)
    return out
