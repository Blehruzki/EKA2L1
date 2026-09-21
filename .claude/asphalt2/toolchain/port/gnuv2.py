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
        m = re.match(r'(\d+)(.*)$', rest)
        if not m:
            return sym
        n = int(m.group(1)); tail = m.group(2)
        cls, tail = tail[:n], tail[n:]
        konst = tail.endswith('C') or tail.startswith('C')
        if tail.startswith('C'):
            tail = tail[1:]
        return '%s::%s(%s)%s' % (cls, name, ', '.join(_args(tail)),
                                 ' const' if konst else '')
    except Exception:
        return sym
