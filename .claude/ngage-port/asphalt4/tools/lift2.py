# lift2.py <code.bin> <func hex> [r0=car r1=keys ...]: a Thumb function of the game as readable
# goto-C for the engine map (round 50): per basic block, register and stack-slot values folded into
# expressions (constants, base + offset: car[0x7D8]); values that every predecessor agrees on are
# carried into a block (the base pointers a function sets up once), the rest start as the register's
# name. Stores, calls and branches are printed; registers changed in a block and read later are
# assigned at its end. For reading -- tools/lift.py is the exact, compilable one.
import sys, struct, capstone
from capstone import arm_const as A
code = open(sys.argv[1], 'rb').read(); start = int(sys.argv[2], 16)
names = dict(s.split('=') for s in sys.argv[3:] if '=' in s)
end = start + 2
while end < len(code) and (struct.unpack_from('<H', code, end)[0] & 0xFF00) != 0xB500: end += 2
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB); md.detail = True
allins = {i.address: i for i in md.disasm(code[start:end], start)}
CCN = {A.ARM_CC_EQ: 'eq', A.ARM_CC_NE: 'ne', A.ARM_CC_HS: 'hs', A.ARM_CC_LO: 'lo', A.ARM_CC_MI: 'mi', A.ARM_CC_PL: 'pl', A.ARM_CC_HI: 'hi',
       A.ARM_CC_LS: 'ls', A.ARM_CC_GE: 'ge', A.ARM_CC_LT: 'lt', A.ARM_CC_GT: 'gt', A.ARM_CC_LE: 'le'}
def isbr(i): return i.mnemonic in ('b',) or (i.mnemonic.startswith('b') and len(i.mnemonic) == 3 and i.mnemonic not in ('bic', 'blx'))
# blocks
leaders, reach, work = {start}, set(), [start]
while work:
    a = work.pop()
    while a in allins and a not in reach:
        i = allins[a]; reach.add(a); a2 = a + i.size
        if isbr(i):
            t = i.operands[0].imm
            if start <= t < end and t in allins: leaders.add(t); work.append(t)
            if i.mnemonic == 'b': break
            leaders.add(a2)
        if i.mnemonic == 'bx' or (i.mnemonic == 'pop' and 'pc' in i.op_str): break
        a = a2
blocks = {}
for L in sorted(l for l in leaders if l in reach):
    a, body = L, []
    while a in reach:
        i = allins[a]; body.append(i); a += i.size
        if isbr(i) or i.mnemonic == 'bx' or (i.mnemonic == 'pop' and 'pc' in i.op_str) or a in leaders: break
    blocks[L] = body
succ = {}
for L, body in blocks.items():
    i = body[-1]; s = []
    if isbr(i):
        t = i.operands[0].imm
        if t in blocks: s.append(t)
        if i.mnemonic != 'b': s.append(i.address + i.size)
    elif not (i.mnemonic == 'bx' or (i.mnemonic == 'pop' and 'pc' in i.op_str)):
        s.append(i.address + i.size)
    succ[L] = [x for x in s if x in blocks]
pred = {L: [] for L in blocks}
for L, ss in succ.items():
    for x in ss: pred[x].append(L)
# linear values: (dict atom->coef, const); atoms are strings
def C(v): return ({}, v & 0xFFFFFFFF)
def AT(s): return ({s: 1}, 0)
def lin_add(x, y, k=1):
    d = dict(x[0])
    for a, c in y[0].items(): d[a] = d.get(a, 0) + k * c
    return ({a: c for a, c in d.items() if c}, (x[1] + k * y[1]) & 0xFFFFFFFF)
def lin_mul(x, c): return ({a: v * c for a, v in x[0].items()}, (x[1] * c) & 0xFFFFFFFF)
def isconst(x): return not x[0]
def sval(v):
    if v >= 0x80000000: v -= 1 << 32
    return v
def show(x):
    if x is None: return '?'
    d, c = x
    if not d: v = sval(c); return ('0x%X' % v) if abs(v) > 9 else str(v)
    parts = []
    for a, k in d.items():
        parts.append(a if k == 1 else ('-' + a if k == -1 else '%s*%d' % (a, k) if k > 0 else '-%s*%d' % (a, -k)))
    s = ' + '.join(parts).replace('+ -', '- ')
    v = sval(c)
    if v: s += ' %s 0x%X' % ('-' if v < 0 else '+', abs(v))
    return s if len(d) == 1 and not v and list(d.values())[0] == 1 else '(' + s + ')'
def memname(x, w):
    d, c = x
    if len(d) == 1 and list(d.values())[0] == 1:
        a = list(d)[0]
        if a.startswith('sp0'): return 'S[%s]' % hex(sval(c))
        if all(ch.isalnum() or ch in '_' for ch in a):
            return '%s%s[0x%X]' % (a, {'32': '', '16': '.h', 'S16': '.sh', '8': '.b', 'S8': '.sb'}[w], sval(c))
    return 'M%s[%s]' % (w, show(x))
SDIV = {}; SIGN = {}
regs0 = {'r%d' % k: AT(names.get('r%d' % k, 'r%d' % k)) for k in range(13)}
regs0['sp'] = AT('sp0'); regs0['lr'] = AT('lr0')
# dataflow: block entry states (regs + stack slots), meet = keep only the agreeing entries
def run(L, st, emit=None):
    R = dict(st[0]); S = dict(st[1]); flags = [None]; out = []
    def get(r):
        return R.get(r) or AT(r)
    def src(op, i):
        if op.type == A.ARM_OP_REG: return get(i.reg_name(op.reg))
        return C(op.imm)
    def atom(s, x=None): return AT(s)
    def setr(r, v): R[r] = v
    for i in blocks[L]:
        m = i.mnemonic; ops = i.operands
        base = m[:-1] if m.endswith('s') and m[:-1] in ('mov', 'mvn', 'add', 'sub', 'lsl', 'lsr', 'asr', 'ror', 'and', 'orr', 'eor', 'bic', 'mul', 'neg', 'rsb', 'adc', 'sbc') else m
        if m == 'push':
            n = len(ops); R['sp'] = lin_add(get('sp'), C(-4 * n))
        elif m == 'pop':
            n = len(ops); R['sp'] = lin_add(get('sp'), C(4 * n))
            for o in ops:
                r = i.reg_name(o.reg)
                if r != 'pc': R[r] = AT(r)
            if 'pc' in i.op_str and emit is not None: out.append('return r0 = %s;' % show(get('r0')))
        elif m in ('ldr', 'ldrb', 'ldrh', 'ldrsb', 'ldrsh', 'str', 'strb', 'strh') and ops[1].type == A.ARM_OP_MEM:
            mo = ops[1].mem; b = i.reg_name(mo.base); rt = i.reg_name(ops[0].reg)
            if b == 'pc':
                la = ((i.address + 4) & ~3) + mo.disp; R[rt] = C(struct.unpack_from('<I', code, la)[0]); continue
            ea = get(b)
            if mo.index: ea = lin_add(ea, get(i.reg_name(mo.index)))
            ea = lin_add(ea, C(mo.disp))
            w = {'ldr': '32', 'str': '32', 'ldrb': '8', 'strb': '8', 'ldrh': '16', 'strh': '16', 'ldrsb': 'S8', 'ldrsh': 'S16'}[m]
            key = None
            if len(ea[0]) == 1 and list(ea[0]) == ['sp0'] and ea[0]['sp0'] == 1: key = sval(ea[1])
            if m.startswith('ld'):
                if key is not None and w == '32' and key in S: R[rt] = S[key]
                else: R[rt] = AT(memname(ea, w))
            else:
                if key is not None and w == '32': S[key] = get(rt)
                if emit is not None and key is None: out.append('%s = %s;' % (memname(ea, w), show(get(rt))))
                elif emit is not None and key is not None and w != '32': out.append('%s = %s;' % (memname(ea, w), show(get(rt))))
        elif m in ('ldm', 'stm', 'ldmia', 'stmia'):
            b = i.reg_name(ops[0].reg)
            if m.startswith('ld'):
                for k, o in enumerate(ops[1:]): R[i.reg_name(o.reg)] = AT(memname(lin_add(get(b), C(4 * k)), '32'))
            elif emit is not None:
                for k, o in enumerate(ops[1:]): out.append('%s = %s;' % (memname(lin_add(get(b), C(4 * k)), '32'), show(get(i.reg_name(o.reg)))))
            R[b] = lin_add(get(b), C(4 * (len(ops) - 1)))
        elif base in ('mov', 'mvn'):
            d = i.reg_name(ops[0].reg); v = src(ops[1], i)
            if base == 'mvn': v = C(~v[1]) if isconst(v) else AT('~' + show(v))
            if d != 'pc': setr(d, v)
            if m.endswith('s'): flags[0] = ('res', d, v)
        elif base in ('add', 'sub', 'mul', 'and', 'orr', 'eor', 'bic', 'neg', 'rsb', 'adc', 'sbc'):
            d = i.reg_name(ops[0].reg)
            if len(ops) == 3: a, b = src(ops[1], i), src(ops[2], i)
            elif base in ('neg', 'rsb'): a, b = src(ops[1], i), C(0)
            else: a, b = get(d), src(ops[1], i)
            if ops[1].type == A.ARM_OP_REG and i.reg_name(ops[1].reg) == 'pc': a = C(((i.address + 4) & ~3) + 0x70000000)
            if base == 'add': v = lin_add(a, b)
            elif base == 'sub': v = lin_add(a, b, -1)
            elif base in ('neg', 'rsb'): v = lin_mul(a, -1)
            elif base == 'mul': v = lin_mul(a, b[1]) if isconst(b) else lin_mul(b, a[1]) if isconst(a) else AT('(%s * %s)' % (show(a), show(b)))
            elif isconst(a) and isconst(b):
                v = C({'and': a[1] & b[1], 'orr': a[1] | b[1], 'eor': a[1] ^ b[1], 'bic': a[1] & ~b[1]}.get(base, 0))
            else:
                v = AT('(%s %s %s)' % (show(a), {'and': '&', 'orr': '|', 'eor': '^', 'bic': '& ~', 'adc': '+c+', 'sbc': '-c-'}[base], show(b)))
            setr(d, v)
            if m.endswith('s'): flags[0] = ('cmp', a, b) if base == 'sub' else ('res', d, v)
        elif base in ('lsl', 'lsr', 'asr', 'ror'):
            d = i.reg_name(ops[0].reg)
            if len(ops) == 3: a, b = src(ops[1], i), src(ops[2], i)
            else: a, b = get(d), src(ops[1], i)
            sdiv = None
            if base == 'asr' and isconst(b) and not isconst(a):
                for at in a[0]:
                    if at in SDIV and a[0][at] == 1 and SDIV[at][1] == 32 - b[1]:
                        rest = (dict((k, c) for k, c in a[0].items() if k != at), a[1])
                        if show(rest) == SDIV[at][0]: sdiv = AT('(%s / %d)' % (SDIV[at][0], 1 << b[1]))
            if sdiv is not None: v = sdiv
            elif base == 'lsr' and isconst(b) and not isconst(a) and len(a[0]) == 1 and a[1] == 0 and list(a[0].values())[0] == 1 and list(a[0])[0] in SIGN:
                v = AT('(%s >> 0x1F >>> 0x%X)' % (SIGN[list(a[0])[0]], b[1])); SDIV[show(v)] = (SIGN[list(a[0])[0]], b[1])
            elif base == 'asr' and isconst(b) and b[1] == 31 and not isconst(a):
                v = AT('(%s >> 0x1F)' % show(a)); SIGN[show(v)] = show(a)
            elif isconst(b) and base == 'lsl': v = lin_mul(a, 1 << b[1]) if not isconst(a) else C(a[1] << b[1])
            elif isconst(a) and isconst(b): v = C((a[1] >> b[1]) if base == 'lsr' else (sval(a[1]) >> b[1]) if base == 'asr' else 0)
            else: v = AT('(%s %s %s)' % (show(a), {'lsl': '<<', 'lsr': '>>>', 'asr': '>>', 'ror': 'ror'}[base], show(b)))
            setr(d, v); flags[0] = ('res', d, v, base, a, b)
        elif m in ('cmp', 'cmn', 'tst'):
            flags[0] = (m, src(ops[0], i), src(ops[1], i))
        elif m in ('bl', 'blx'):
            t = ops[0].imm if ops[0].type == A.ARM_OP_IMM else None
            args = ', '.join(show(get('r%d' % k)) for k in range(4))
            nm = ('f%05X' % t) if t is not None else 'call(%s)' % show(get(i.reg_name(ops[0].reg)))
            tmp = 'v%05X' % (i.address & 0xFFFFF)
            if emit is not None: out.append('%s = %s(%s);' % (tmp, nm, args))
            for k in ('r0', 'r1', 'r2', 'r3', 'r12', 'lr'): R[k] = AT(k)
            R['r0'] = AT(tmp); flags[0] = None
        elif isbr(i):
            t = i.operands[0].imm; cc = CCN.get(i.cc)
            if emit is not None:
                fin = assigns(R, L)
                out.extend(fin)
                if m == 'b':
                    out.append('goto L_%05X;' % t if t in blocks else 'return f%05X(...); /* tail call */' % t)
                else:
                    out.append('if (%s) goto L_%05X;' % (condstr(flags[0], cc), t))
        elif m == 'bx':
            if emit is not None: out.append('return %s;' % show(get('r0')))
        else:
            if ops and ops[0].type == A.ARM_OP_REG: R[i.reg_name(ops[0].reg)] = AT(i.reg_name(ops[0].reg))
            if emit is not None: out.append('/* ?? %s %s */' % (m, i.op_str))
    if emit is not None and not isbr(blocks[L][-1]): out.extend(assigns(R, L))
    return (R, S), out
def condstr(f, cc):
    if f is None: return '/*?flags*/'
    k = f[0]
    if k == 'cmp' or k == 'cmn':
        a, b = show(f[1]), show(f[2])
        if k == 'cmn': b = '-' + b
        op = {'eq': '==', 'ne': '!=', 'lt': '<', 'ge': '>=', 'gt': '>', 'le': '<=', 'hi': '>u', 'ls': '<=u', 'hs': '>=u', 'lo': '<u', 'mi': '-<0', 'pl': '->=0'}.get(cc, '?' + str(cc))
        if op == '-<0': return '(%s - %s) < 0' % (a, b)
        if op == '->=0': return '(%s - %s) >= 0' % (a, b)
        return '%s %s %s' % (a, op, b)
    if k == 'tst':
        return '(%s & %s) %s 0' % (show(f[1]), show(f[2]), {'eq': '==', 'ne': '!=', 'mi': '<', 'pl': '>='}.get(cc, '?'))
    if k == 'res':
        v = show(f[2])
        if len(f) > 3 and f[3] == 'lsl' and isconst(f[5]) and cc in ('mi', 'pl', 'eq', 'ne'):
            n = f[5][1]
            if cc in ('mi', 'pl'): return '(%s bit %d) %s' % (show(f[4]), 31 - n, 'set' if cc == 'mi' else 'clear')
            return '(%s & 0x%X) %s 0' % (show(f[4]), (0xFFFFFFFF >> n), '==' if cc == 'eq' else '!=')
        if len(f) > 3 and f[3] == 'lsl' and isconst(f[5]) and cc in ('hs', 'lo'):
            return '(%s bit %d) %s' % (show(f[4]), 32 - f[5][1], 'set' if cc == 'hs' else 'clear')
        return '%s %s' % (v, {'eq': '== 0', 'ne': '!= 0', 'mi': '< 0', 'pl': '>= 0', 'gt': '> 0 /*?*/', 'le': '<= 0 /*?*/', 'lt': '< 0 /*?*/', 'ge': '>= 0 /*?*/'}.get(cc, '?' + str(cc)))
    return '?'
entry = {}
entry[start] = (dict(regs0), {})
changed = True; it = 0
order = sorted(blocks)
while changed and it < 50:
    changed = False; it += 1
    for L in order:
        if L not in entry: continue
        st, _ = run(L, entry[L])
        for s in succ[L]:
            if s not in entry: entry[s] = (dict(st[0]), dict(st[1])); changed = True
            else:
                R0, S0 = entry[s]
                R1 = {k: v for k, v in R0.items() if st[0].get(k) == v}
                S1 = {k: v for k, v in S0.items() if st[1].get(k) == v}
                if len(R1) != len(R0) or len(S1) != len(S0): entry[s] = (R1, S1); changed = True
def assigns(R, L):
    # registers whose value at some successor's entry is not carried (so the successor reads the name)
    out = []
    for s in succ.get(L, []):
        for k in ['r%d' % n for n in range(13)]:
            if k not in entry[s][0] and R.get(k) is not None and show(R.get(k)) != k:
                line = '%s = %s;' % (k, show(R[k]))
                if line not in out: out.append(line)
    return out
print('/* +%05X: %d blocks; %s */' % (start, len(blocks), ' '.join('%s=%s' % kv for kv in names.items())))
for L in order:
    if L not in entry: continue
    _, out = run(L, entry[L], emit=True)
    print('L_%05X:' % L)
    for s in out: print('    ' + s)
