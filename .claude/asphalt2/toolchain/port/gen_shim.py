#!/usr/bin/env python3
"""Generate the forwarding table gate 4 links against.

    gen_shim.py <image> <output.cpp>

For every import the old binary makes, shimtable finds the Symbian 9.x ordinal
of the same function.  This writes that out as data: a list of DLLs to load and
one word per import holding which DLL and which ordinal, or zero where nothing
matched.

The forwards are resolved at run time with RLibrary::Lookup rather than through
our own import section, for two reasons.  An import section is checked when the
image loads, so one ordinal that does not exist on the device would stop the
whole program from starting, with no way to say which; a lookup that returns
null can be reported.  And the ordinals come from the Symbian source, which is
a later 9.x than any one phone, so some of them will be wrong.
"""
import os, re, sys

import e32imports, shimtable, symdef

ROOT = '/home/user/symbiansource'
KERNEL = ROOT + '/oss.fcl.sf.os.kernelhwsrv'
COMPSUPP = KERNEL + '/kernel/eka/compsupp/eabi'
EPOC6 = '/home/user/EKA2L1/src/emu/bridge/include/bridge/epoc6.def'

# Each table entry is  kind << 24 | dll index << 16 | ordinal.
KIND_NONE, KIND_CALL, KIND_REM, KIND_LOCAL, KIND_ARG3 = 0, 1, 2, 3, 4

# GCC98r2 put its compiler helpers in euser; EABI puts them in the runtime
# libraries the SDK links against, under their AEABI names. Same routines,
# same registers -- a double arrives in r0:r1 and r2:r3 either way -- so most
# are a straight forward.
HELPERS = {
    '__adddf3':      ('dfpaeabi', '__aeabi_dadd',     KIND_CALL),
    '__addsf3':      ('dfpaeabi', '__aeabi_fadd',     KIND_CALL),
    '__divsf3':      ('dfpaeabi', '__aeabi_fdiv',     KIND_CALL),
    '__muldf3':      ('dfpaeabi', '__aeabi_dmul',     KIND_CALL),
    '__mulsf3':      ('dfpaeabi', '__aeabi_fmul',     KIND_CALL),
    '__extendsfdf2': ('dfpaeabi', '__aeabi_f2d',      KIND_CALL),
    '__truncdfsf2':  ('dfpaeabi', '__aeabi_d2f',      KIND_CALL),
    '__fixsfsi':     ('dfpaeabi', '__aeabi_f2iz',     KIND_CALL),
    '__floatsidf':   ('dfpaeabi', '__aeabi_i2d',      KIND_CALL),
    '__floatsisf':   ('dfpaeabi', '__aeabi_i2f',      KIND_CALL),
    # __aeabi_idiv and __aeabi_uidiv sit at the end of the def and are absent
    # from shipped drtaeabi builds. divmod returns the quotient in r0, which is
    # exactly what these want, so use it and ignore the remainder in r1.
    '__divsi3':      ('drtaeabi', '__aeabi_idivmod',  KIND_CALL),
    '__udivsi3':     ('drtaeabi', '__aeabi_uidivmod', KIND_CALL),
    '__divdi3':      ('drtaeabi', '__aeabi_ldivmod',  KIND_CALL),
    # idivmod returns the quotient in r0 and the remainder in r1; __modsi3 has
    # to return the remainder, so these get a thunk that moves it across.
    '__modsi3':      ('drtaeabi', '__aeabi_idivmod',  KIND_REM),
    '__umodsi3':     ('drtaeabi', '__aeabi_uidivmod', KIND_REM),
    # operator new and delete, which EABI keeps in their own library.
    '__builtin_new':        ('scppnwdl', '_Znwj', KIND_CALL),
    '__builtin_delete':     ('scppnwdl', '_ZdlPv', KIND_CALL),
    '__builtin_vec_delete': ('scppnwdl', '_ZdaPv', KIND_CALL),
}

# Classes the game derives from, whose constructors and destructors must not be
# forwarded.
#
# Forwarding a leaf function is sound: allocation, descriptors, arithmetic and
# file I/O have the same layout either side. Forwarding a base-class
# constructor is not. The game allocates its application object at 556 bytes --
# the size the 7.0s compiler computed -- and then calls the base constructor;
# sending that to the 9.x one runs 9.x code writing 9.x field offsets into an
# object that was never laid out that way. Nothing makes iCoeEnv and
# iResourceFileOffset sit where the old code expects them.
#
# Until each is reimplemented against the old layout, an empty body on zeroed
# memory is the closer approximation: it leaves the old fields at zero rather
# than filling them with values meant for a different object.
FRAMEWORK_BASES = ('CCoeControl', 'CCoeAppUi', 'CEikApplication', 'CEikDocument',
                   'CEikAppUi', 'CEikDialog', 'CEikBorderedControl',
                   'CAknApplication', 'CAknDocument', 'CAknAppUi')

# No EABI routine matches these, so gate 4 generates them itself.
LOCAL_NEGSF2, LOCAL_PURE_VIRTUAL, LOCAL_NOOP, LOCAL_MEM_COMPARE = 0, 1, 2, 3
LOCAL_TRAP_ENTER = 4
LOCAL = {'__negsf2': LOCAL_NEGSF2, '__pure_virtual': LOCAL_PURE_VIRTUAL}

# Functions 9.x kept but moved, renamed or gave another argument. Each was
# checked against the 9.x def rather than assumed; the rest of what does not
# match is genuinely gone (CServer, CSession, TTrap, TInt64) and needs writing.
MANUAL = {
    # The exception handler moved from RThread to User.
    'RThread::SetExceptionHandler(void (*)(TExcType), unsigned long)':
        ('euser', 'User::SetExceptionHandler(void (*)(TExcType), unsigned long)', KIND_CALL),
    # RFsBase went away; closing a session handle is RHandleBase::Close.
    'RFsBase::Close()': ('euser', 'RHandleBase::Close()', KIND_CALL),
    # 9.x ReAllocL takes a mode as a third argument. Zero is the old behaviour.
    'User1::ReAlloc1L(void *, int)': ('euser', 'User::ReAllocL(void*, int, int)', KIND_ARG3),
    # CBase's constructor and destructor are empty and 9.x stopped exporting them.
    'CBase1::CBase1(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    'CBase1::~CBase1(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
    # Only the 16-bit Mem::Compare survives as an export, so do the 8-bit one here.
    'Mem::Compare(unsigned char const *, int, unsigned char const *, int)':
        ('local', LOCAL_MEM_COMPARE, KIND_LOCAL),
    # EKA1's trap harness has no 9.x counterpart -- TRAP became a thread trap
    # handler. Entering a trap reports the first pass with no error and leaving
    # it does nothing, so a leave inside the body propagates to the framework's
    # own TRAP instead of being caught here. Only code that leaves can tell.
    'TTrap::Trap(int &)': ('local', LOCAL_TRAP_ENTER, KIND_LOCAL),
    'TTrap::UnTrap(void)': ('local', LOCAL_NOOP, KIND_LOCAL),
}


def _is_framework_ctor(sig):
    """Class::Class(...) or Class::~Class(...) for a class the game derives from."""
    m = re.match(r'^(C\w+)::(~?)\1\s*\(', sig)
    return bool(m) and m.group(1) in FRAMEWORK_BASES


def find_defs():
    out = {}
    for dirpath, _dirs, files in os.walk(ROOT):
        if 'eabi' not in dirpath.lower():
            continue
        for f in files:
            if not f.lower().endswith('.def'):
                continue
            b = symdef.base_name(f)
            for key in {b, b.rstrip('u'), re.sub(r'\d+u?$', '', b)}:
                out.setdefault(key, os.path.join(dirpath, f))
    return out


def build(image):
    d = open(image, 'rb').read()
    imports = [(n.split('[')[0].split('{')[0].lower(), o)
               for n, ords in e32imports.imports(d) for o in ords]
    defs = find_defs()
    new = {lib: defs[lib] for lib in {l for l, _o in imports} if lib in defs}
    rows = shimtable.match(imports, {'euser': KERNEL + '/kernel/eka/bmarm/7.0-euseru.def'},
                           new, epoc6=EPOC6)

    new_index = {lib: shimtable.index(symdef.load(path)) for lib, path in new.items()}

    helper_ords = {}
    for lib in ('dfpaeabi', 'drtaeabi', 'scppnwdl'):
        table = symdef.load('%s/%su.def' % (COMPSUPP, lib))
        helper_ords[lib] = {sym: o for o, (sym, _c) in table.items()}

    # (index, library, ordinal, signature, kind)
    out = []
    for i, lib, _o, sig, ordinal, _src in rows:
        # Checked before the ordinal, deliberately: these do have a 9.x
        # equivalent, and forwarding to it is exactly what must not happen.
        if sig and _is_framework_ctor(sig):
            out.append((i, None, LOCAL_NOOP, sig, KIND_LOCAL))
        elif ordinal:
            out.append((i, lib, ordinal, sig, KIND_CALL))
        elif sig and 'Reserved' in sig:
            # Vtable padding. Symbian's _Reserved members have empty bodies and
            # exist only to hold a slot, so an empty body is the whole shim.
            out.append((i, None, LOCAL_NOOP, sig, KIND_LOCAL))
        elif sig in MANUAL:
            target, what, kind = MANUAL[sig]
            if target == 'local':
                out.append((i, None, what, sig, KIND_LOCAL))
            else:
                o = new_index.get(target, {}).get(shimtable.norm(what))
                out.append((i, target, o, sig, kind if o else KIND_NONE))
        elif sig in HELPERS:
            hlib, sym, kind = HELPERS[sig]
            o = helper_ords[hlib].get(sym)
            out.append((i, hlib, o, sig, kind if o else KIND_NONE))
        elif sig in LOCAL:
            out.append((i, None, LOCAL[sig], sig, KIND_LOCAL))
        else:
            out.append((i, None, 0, sig, KIND_NONE))

    dlls = sorted({lib for _i, lib, _o, _s, kind in out if lib and kind != KIND_NONE})
    return out, dlls


def emit(rows, dlls, path):
    lines = ['// Generated by gen_shim.py -- do not edit.',
             '//',
             '// One word per import: (DLL index + 1) << 16 | 9.x ordinal, or 0 when',
             '// nothing matched and the import keeps its reporting stub.',
             '',
             'extern "C" {',
             '']
    for i, dll in enumerate(dlls):
        name = dll if dll.endswith('.dll') else dll + '.dll'
        chars = ', '.join("'%s'" % c for c in name)
        lines.append('extern const unsigned short kShimDll%d[];' % i)
        lines.append('extern const unsigned short kShimDll%d[] = { %s };' % (i, chars))
    lines.append('')
    lines.append('extern const unsigned short *const kShimDllName[] = { %s };'
                 % ', '.join('kShimDll%d' % i for i in range(len(dlls))))
    lines.append('extern const unsigned char kShimDllLen[] = { %s };'
                 % ', '.join(str(len(d if d.endswith('.dll') else d + '.dll')) for d in dlls))
    lines.append('extern const unsigned int kShimDllCount = %d;' % len(dlls))
    lines.append('')

    index = {d: i for i, d in enumerate(dlls)}
    words = []
    for _i, lib, ordinal, _sig, kind in rows:
        if kind == KIND_LOCAL:
            words.append(KIND_LOCAL << 24 | ordinal)
        elif kind and lib:
            words.append(kind << 24 | index[lib] << 16 | ordinal)
        else:
            words.append(0)
    lines.append('extern const unsigned int kShimTable[] = {')
    for i in range(0, len(words), 8):
        lines.append('    ' + ' '.join('0x%08X,' % w for w in words[i:i + 8]))
    lines.append('};')
    lines.append('extern const unsigned int kShimCount = %d;' % len(words))
    lines.append('')
    lines.append('}')
    open(path, 'w').write('\n'.join(lines) + '\n')
    return sum(1 for w in words if w)


if __name__ == '__main__':
    rows, dlls = build(sys.argv[1])
    n = emit(rows, dlls, sys.argv[2])
    kinds = {}
    for _i, _l, _o, _s, k in rows:
        kinds[k] = kinds.get(k, 0) + 1
    print('%s: %d of %d imports answered across %d DLLs (%s)'
          % (sys.argv[2], n, len(rows), len(dlls), ', '.join(dlls)))
    print('   direct %d, remainder thunk %d, generated locally %d, unanswered %d'
          % (kinds.get(KIND_CALL, 0), kinds.get(KIND_REM, 0),
             kinds.get(KIND_LOCAL, 0), kinds.get(KIND_NONE, 0)))
