# lift.py <code.bin> <func hex> [<func hex> ...] [-o out.c]: game Thumb functions translated to C, one
# statement per instruction, exact: registers are variables, the flags N Z C V are computed by every
# instruction that sets them and read by every one that uses them, the function's own stack frame is an
# array (incoming stack arguments copied to its top), pc-relative literals are read from the game's own
# code at run time (so the loader's relocations hold), and calls go through a4_eng_call (renderer/
# a4_eng.s), which passes eight stack words and returns r0:r1. Round 50: the first draft of each engine
# function rewritten in C -- checked against the game's own function in lockstep (the whole race,
# every word of every car) before a line of it is changed. Compiled by renderer/a4_eng.c.
import sys, os, re, struct, capstone
from capstone import arm_const as A
args = [a for a in sys.argv[1:]]
outp = None
if '-o' in args: k = args.index('-o'); outp = args[k + 1]; del args[k:k + 2]
code = open(args[0], 'rb').read()
CC = {A.ARM_CC_EQ: 'fZ', A.ARM_CC_NE: '!fZ', A.ARM_CC_HS: 'fC', A.ARM_CC_LO: '!fC', A.ARM_CC_MI: 'fN', A.ARM_CC_PL: '!fN',
      A.ARM_CC_VS: 'fV', A.ARM_CC_VC: '!fV', A.ARM_CC_HI: '(fC && !fZ)', A.ARM_CC_LS: '(!fC || fZ)', A.ARM_CC_GE: '(fN == fV)',
      A.ARM_CC_LT: '(fN != fV)', A.ARM_CC_GT: '(!fZ && fN == fV)', A.ARM_CC_LE: '(fZ || fN != fV)'}
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB); md.detail = True
callees = set()
# the injector's own call-site hooks (tools/injector.py, the same flags): a lifted function calls the
# hook where the injector put one, or the bench's frame code (and its records) would lose those calls
FL = os.environ.get('A4_EXTRA_CFLAGS', '').split()
HOOKS = {}
if '-DA4_HALFMOTION' in FL:
    HOOKS.update({0x14974: 'a4_h_1de78', 0x1495E: 'a4_h_1f1f8', 0x14C94: 'a4_h_1ea64', 0x14CA2: 'a4_h_1ea64',
                  0x14C68: 'a4_h_1fd5c', 0x146FC: 'a4_h_1f1f8', 0x146D6: 'a4_h_1fd0e'})
if '-DA4_HALFPHYS' in FL: HOOKS.update({0x14EEC: 'a4_h_12dec', 0x1306C: 'a4_h_12dec'})
if '-DA4_SPECTRAFFIC' in FL: HOOKS[0x14574] = 'a4_h_fb12'
if '-DA4_SPECCAMSAFE' in FL:
    HOOKS.update({0x556E: 'a4_cs_5205a_tramp', 0x5912: 'a4_cs_5205a_tramp', 0x5578: 'a4_cs_3d2a_tramp', 0x5926: 'a4_cs_3d2a_tramp'})
    for t in (0x3CE8, 0x3CF8, 0x3D08, 0x5662, 0x566E, 0x5682, 0x5696, 0x56A2, 0x56B6, 0x56C2, 0x56CE, 0x5762, 0x576E, 0x5786, 0x579A):
        HOOKS[t] = 'a4_cs_51fec_tramp'
    for t in (0x3CCC, 0x3F14, 0x5618, 0x5620, 0x5211C): HOOKS[t] = 'a4_cs_5202c_tramp'
used_hooks = set()
EDITS, used_edits = {}, set()
_ed = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'renderer', 'eng', 'edits.py')
if os.path.exists(_ed) and '-DA4_ENG3' in FL:          # the edits are the 3x engine's; without A4_ENG3 the game's code exactly
    _g = {}; exec(open(_ed).read(), _g); EDITS = _g['EDITS']

# every function the game calls (Thumb BL targets): the only places a branch leaves a function by
BLT = set()
for o in range(0, len(code) - 3, 2):
    h1, h2 = struct.unpack_from('<HH', code, o)
    if (h1 & 0xF800) == 0xF000 and (h2 & 0xF800) == 0xF800:
        off = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
        if off & (1 << 22): off -= 1 << 23
        BLT.add(o + 4 + off)

def lift(start):
    end = start + 0x2000
    for t in sorted(BLT):
        if t > start: end = min(end, t + 0x1000); break
    end = min(end, len(code))
    class Lazy(dict):       # decode where the flow goes (a linear sweep would misread literal pools)
        def __missing__(self, a):
            for i in md.disasm(code[a:a + 4], a, count=1): self[a] = i; return i
            raise KeyError(a)
        def __contains__(self, a):
            if dict.__contains__(self, a): return True
            if not (0 <= a < len(code) - 1): return False
            try: self.__missing__(a); return True
            except KeyError: return False
    allins = Lazy()
    def isbr(i): return i.mnemonic == 'b' or (i.mnemonic.startswith('b') and len(i.mnemonic) == 3 and i.mnemonic not in ('bic', 'blx'))
    nextpush = start + 2
    while nextpush < len(code) - 1 and (struct.unpack_from('<H', code, nextpush)[0] & 0xFF00) != 0xB500: nextpush += 2
    def local(t):           # a BL target inside this function's body, using its frame (no prologue of its own)
        return start < t < nextpush and (struct.unpack_from('<H', code, t)[0] & 0xFF00) != 0xB500
    rets = {}               # local subroutine return points: marker (the BL's return address | 1) -> label
    reach, work, labels = set(), [start], set()
    jt = {}                 # 'add pc, rX' address -> its targets (a byte table: cmp rI, #n; adr rT; ldrb; adds)
    def jumptable(i):
        back = []; a = i.address
        for k in range(1, 12):
            if (a - 2 * k) in allins: back.append(allins[a - 2 * k])
        n = next((b.operands[1].imm for b in back if b.mnemonic == 'cmp' and b.operands[1].type == A.ARM_OP_IMM), None)
        tb = next((((b.address + 4) & ~3) + b.operands[1].imm for b in back if b.mnemonic == 'adr'), None)
        if n is None or tb is None: return None
        return [i.address + 4 + 2 * code[tb + k] for k in range(n)]
    while work:
        a = work.pop()
        while a in allins and a not in reach:
            i = allins[a]; reach.add(a); m = i.mnemonic
            if m == 'add' and i.op_str.startswith('pc,'):
                ts = jumptable(i)
                if ts is None: raise SystemExit('lift.py: a jump through pc at %06x without its table' % a)
                jt[a] = ts
                for t in set(ts): labels.add(t); work.append(t)
                break
            if m == 'bl' and i.operands[0].type == A.ARM_OP_IMM and local(i.operands[0].imm):
                t = i.operands[0].imm; labels.add(t); work.append(t)
                rets[(i.address + 4) | 1] = i.address + 4; labels.add(i.address + 4)
            if isbr(i):
                t = i.operands[0].imm
                if t in allins and (t not in BLT or local(t)) or t == start: labels.add(t); work.append(t)
                if m == 'b': break
            if m == 'bx' or (m == 'pop' and 'pc' in i.op_str) or (m == 'mov' and i.op_str.startswith('pc')): break
            a += i.size
    ins = [allins[a] for a in sorted(reach)]
    L = []
    def E(s, i): L.append('    %-60s /* %06x %s %s */' % (s, i.address, i.mnemonic, i.op_str))
    HI = {'sb': 'r9', 'sl': 'r10', 'fp': 'r11', 'ip': 'r12'}
    def rn(op, i): n = i.reg_name(op.reg); return HI.get(n, n)
    def opv(op, i):
        if op.type == A.ARM_OP_REG: return rn(op, i)
        return '0x%Xu' % (op.imm & 0xFFFFFFFF)
    def nz(d): return 'fN = %s >> 31; fZ = %s == 0;' % (d, d)
    W = {'ldr': 'MEM32', 'str': 'MEM32', 'ldrb': 'MEM8', 'strb': 'MEM8', 'ldrh': 'MEM16', 'strh': 'MEM16', 'ldrsb': 'MEMS8', 'ldrsh': 'MEMS16'}
    for i in ins:
        if i.address in labels: L.append('L_%05X:' % i.address)
        m = i.mnemonic; ops = i.operands
        flg = m.endswith('s') and m not in ('bics_',) and m[:-1] in ('mov', 'mvn', 'add', 'sub', 'lsl', 'lsr', 'asr', 'ror', 'and', 'orr', 'eor', 'bic', 'mul', 'neg', 'rsb', 'adc', 'sbc')
        base = m[:-1] if flg else m
        if m == 'push':
            regs = [rn(o, i) for o in ops]
            E('sp -= %d; ' % (4 * len(regs)) + ' '.join('MEM32(sp + %d) = %s;' % (4 * k, r) for k, r in enumerate(regs)), i)
        elif m == 'pop':
            regs = [rn(o, i) for o in ops]
            s = ' '.join('%s = MEM32(sp + %d);' % (r if r != 'pc' else 't', 4 * k) for k, r in enumerate(regs)) + ' sp += %d;' % (4 * len(regs))
            E(s + (' RET(t);' if 'pc' in regs else ''), i)
        elif m in ('ldm', 'stm'):
            b = rn(ops[0], i); regs = [rn(o, i) for o in ops[1:]]
            if m == 'ldm':
                E('t = %s; ' % b + ' '.join('%s = MEM32(t + %d);' % (r, 4 * k) for k, r in enumerate(regs)) + ('' if b in regs else ' %s = t + %d;' % (b, 4 * len(regs))), i)
            else:
                E('t = %s; ' % b + ' '.join('MEM32(t + %d) = %s;' % (4 * k, r) for k, r in enumerate(regs)) + ' %s = t + %d;' % (b, 4 * len(regs)), i)
        elif m in W and ops[1].type == A.ARM_OP_MEM:
            mo = ops[1].mem; b = HI.get(i.reg_name(mo.base), i.reg_name(mo.base)); rt = rn(ops[0], i)
            if b == 'pc':
                la = ((i.address + 4) & ~3) + mo.disp
                E('%s = GAME32(0x%X);' % (rt, la), i); continue
            ea = b + (' + ' + HI.get(i.reg_name(mo.index), i.reg_name(mo.index)) if mo.index else '') + (' + 0x%Xu' % (mo.disp & 0xFFFFFFFF) if mo.disp else '')
            if m.startswith('ld'):
                E('%s = %s%s(%s);' % (rt, '(uint32_t)(int32_t)' if 'MEMS' in W[m] else '', W[m], ea), i)
            else: E('%s(%s) = %s;' % (W[m], ea, rt), i)
        elif base in ('mov', 'mvn'):
            d = rn(ops[0], i); s = opv(ops[1], i)
            if d == 'pc': E('RET(%s); /* jump through a register */' % s if s == 'lr' else 'return CALLR(%s); /* jump through a register */' % s, i); continue
            E('%s = %s%s;%s' % (d, '~' if base == 'mvn' else '', s, (' ' + nz(d)) if flg else ''), i)
        elif i.address in jt:
            r = rn(ops[1], i)
            cases = ' '.join('case 0x%Xu: goto L_%05X;' % (2 * k_, t_) for k_, t_ in sorted(set(((t_ - i.address - 4) // 2, t_) for t_ in jt[i.address])))
            E('switch (%s) { %s default: return CALLR(GAMEADDR(0x%X) + %s); }' % (r, cases, i.address + 4, r), i)
        elif base in ('add', 'sub', 'adc', 'sbc', 'rsb', 'neg', 'cmp', 'cmn') or m in ('cmp', 'cmn'):
            if m in ('cmp', 'cmn'): d, a, b = None, opv(ops[0], i), opv(ops[1], i)
            elif len(ops) == 3: d, a, b = rn(ops[0], i), opv(ops[1], i), opv(ops[2], i)
            elif base in ('neg', 'rsb'): d, a, b = rn(ops[0], i), opv(ops[1], i), '0'
            else: d, a, b = rn(ops[0], i), rn(ops[0], i), opv(ops[1], i)
            if a == 'pc': a = 'GAMEADDR(0x%X)' % ((i.address + 4) & ~3)
            if a == 'sp' or d == 'sp':
                pass
            op = {'add': '+', 'cmn': '+', 'adc': '+', 'sub': '-', 'cmp': '-', 'sbc': '-', 'rsb': '-', 'neg': '-'}[base if m not in ('cmp', 'cmn') else m]
            if base in ('rsb', 'neg'): a, b = '0u' if b == '0' else b, a     # rsb d, a, #0 = 0 - a
            if base == 'adc': expr = 'ta + tb + fC_old'
            elif base == 'sbc': expr = 'ta - tb - !fC_old'
            else: expr = 'ta %s tb' % op
            setf = flg or m in ('cmp', 'cmn')
            s = '{ uint32_t ta = %s, tb = %s; t = %s;' % (a, b, expr)
            if setf:
                if op == '+':
                    s += ' fC = (t < ta) || (fC_old && t == ta);' if base == 'adc' else ' fC = t < ta;'
                    s += ' fV = ((~(ta ^ tb)) & (ta ^ t)) >> 31;'
                else:
                    s += ' fC = %s;' % ('ta > tb || (ta == tb && fC_old)' if base == 'sbc' else 'ta >= tb')
                    s += ' fV = ((ta ^ tb) & (ta ^ t)) >> 31;'
                s += ' fN = t >> 31; fZ = t == 0;'
            if base in ('adc', 'sbc'): s = s.replace('{ uint32_t ta', '{ uint32_t fC_old = fC, ta')
            if d: s += ' %s = t;' % d
            E(s + ' }', i)
        elif base in ('and', 'orr', 'eor', 'bic', 'mul', 'tst') or m == 'tst':
            if m == 'tst': d, a, b = None, opv(ops[0], i), opv(ops[1], i)
            elif len(ops) == 3: d, a, b = rn(ops[0], i), opv(ops[1], i), opv(ops[2], i)
            else: d, a, b = rn(ops[0], i), rn(ops[0], i), opv(ops[1], i)
            op = {'and': '&', 'tst': '&', 'orr': '|', 'eor': '^', 'bic': '& ~', 'mul': '*'}[base if m != 'tst' else 'tst']
            s = 't = %s %s %s;' % (a, op, b)
            if flg or m == 'tst': s += ' fN = t >> 31; fZ = t == 0;'
            if d: s += ' %s = t;' % d
            E(s, i)
        elif base in ('lsl', 'lsr', 'asr', 'ror'):
            d = rn(ops[0], i)
            if len(ops) == 3: a, b = opv(ops[1], i), opv(ops[2], i)
            else: a, b = d, opv(ops[1], i)
            imm = ops[-1].type == A.ARM_OP_IMM
            fn = {'lsl': 'SHL', 'lsr': 'SHR', 'asr': 'SAR', 'ror': 'ROR'}[base]
            if imm and base in ('lsr', 'asr') and (ops[-1].imm == 0): b = '32u'
            E('t = %s_C(%s, %s, &fC); %s = t; fN = t >> 31; fZ = t == 0;' % (fn, a, b, d), i)
        elif m in ('bl', 'blx'):
            if m == 'bl' and ops[0].type == A.ARM_OP_IMM and local(ops[0].imm):
                E('lr = 0x%Xu; goto L_%05X; /* a local subroutine of this function */' % ((i.address + 4) | 1, ops[0].imm), i)
                continue
            if i.address in HOOKS:
                h = HOOKS[i.address]; used_hooks.add(h)
                E('{ uint64_t rr = a4_eng_call((uint32_t)(uintptr_t)&%s, r0, r1, r2, r3, sp, r23); r0 = (uint32_t)rr; r1 = (uint32_t)(rr >> 32); r2 = r23[0]; r3 = r23[1]; } /* the injector\'s hook */' % h, i)
            elif ops[0].type == A.ARM_OP_IMM:
                t = ops[0].imm; callees.add((t, m == 'bl'))
                E('{ uint64_t rr = a4_eng_call(%s(0x%X), r0, r1, r2, r3, sp, r23); r0 = (uint32_t)rr; r1 = (uint32_t)(rr >> 32); r2 = r23[0]; r3 = r23[1]; }' % ('GAMEFN' if m == 'bl' else 'GAMEARM', t), i)
            else:
                E('{ uint64_t rr = a4_eng_call(%s, r0, r1, r2, r3, sp, r23); r0 = (uint32_t)rr; r1 = (uint32_t)(rr >> 32); r2 = r23[0]; r3 = r23[1]; }' % rn(ops[0], i), i)
            E('r12 = 0xDEAD0000u; /* clobbered by the call */', i)
        elif m == 'bx':
            E('RET(lr);' if rn(ops[0], i) == 'lr' else 'return CALLR(%s);' % rn(ops[0], i), i)
        elif isbr(i):
            t = ops[0].imm
            if t in allins and (t not in BLT or t == start or local(t)): tgt = 'goto L_%05X;' % t
            else:
                callees.add((t, True))
                tgt = '{ return a4_eng_call(GAMEFN(0x%X), r0, r1, r2, r3, sp, r23); } /* tail */' % t
            if m == 'b': E(tgt, i)
            else: E('if (%s) %s' % (CC[i.cc], tgt), i)
        elif m == 'adr':
            E('%s = GAMEADDR(0x%X);' % (rn(ops[0], i), ((i.address + 4) & ~3) + ops[1].imm), i)
        elif m in ('add', 'sub') and rn(ops[0], i) == 'sp':
            E('sp %s= %s;' % ('+' if m == 'add' else '-', opv(ops[-1], i)), i)
        else:
            E('#error "lift.py: %s %s"' % (m, i.op_str), i)
    frame = 0
    for i in ins[:8]:
        if i.mnemonic == 'push': frame += 4 * len(i.operands)
        elif i.mnemonic == 'sub' and i.op_str.startswith('sp'): frame += i.operands[-1].imm
    stkw = (frame + 64 + 32 + 7) // 8 * 2      # words: the frame, a margin, the eight argument words
    hdr = ('/* +%05X, %d instructions (tools/lift.py) */\n' % (start, len(ins)) +
           'uint64_t eng_%05X(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3,\n'
           '                  uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)\n{\n' % start +
           '    uint32_t r4 = 0, r5 = 0, r6 = 0, r7 = 0, r8 = 0, r9 = 0, r10 = 0, r11 = 0, r12 = 0, lr = 0xFFFFFFFFu, t = 0;\n'
           '    uint32_t fN = 0, fZ = 0, fC = 0, fV = 0, r23[2];\n'
           '    uint32_t stk[%d]; uint32_t sp = (uint32_t)(uintptr_t)&stk[%d - 8];\n' % (stkw, stkw) +
           '    stk[%d] = s0; stk[%d] = s1; stk[%d] = s2; stk[%d] = s3;\n' % (stkw - 8, stkw - 7, stkw - 6, stkw - 5) +
           '    stk[%d] = s4; stk[%d] = s5; stk[%d] = s6; stk[%d] = s7;\n' % (stkw - 4, stkw - 3, stkw - 2, stkw - 1) +
           '    (void)fN; (void)fZ; (void)fC; (void)fV; (void)t; (void)lr; (void)r8; (void)r9; (void)r10; (void)r11; (void)r12;\n')
    # the engine's own changes (renderer/eng/edits.py): per instruction, C before it, after it, or in
    # its place -- the rate map in code, each with its reason; the rest stays the game's, exactly
    done = set()
    for k, line in enumerate(L):
        mm = re.search(r'/\* ([0-9a-f]{6}) ', line)
        if not mm: continue
        e = EDITS.get(int(mm.group(1), 16))
        if not e: continue
        if int(mm.group(1), 16) in done:            # an instruction lifted to two lines: the edit once
            if 'replace' in e: L[k] = '    /* (replaced above) */'
            continue
        done.add(int(mm.group(1), 16))
        used_edits.add(int(mm.group(1), 16))
        why = '    /* engine: %s */' % e.get('why', '')
        body = ('    ' + e['replace'] + '  /* was: ' + line.strip().replace('/*', '(').replace('*/', ')') + ' */') if 'replace' in e else line
        L[k] = '\n'.join(x for x in (why, ('    ' + e['before']) if 'before' in e else None, body, ('    ' + e['after']) if 'after' in e else None) if x)
    disp = ' '.join('case 0x%Xu: goto L_%05X;' % (k, v) for k, v in sorted(rets.items()))
    ret = '#undef RET\n#define RET(v) do { switch (v) { %s default: return ((uint64_t)r1 << 32) | r0; } } while (0)\n' % disp
    return ret + hdr + '\n'.join(L) + '\n}\n'

out = ['/* generated by tools/lift.py -- the game\'s own functions as C; edit only through the engine notes */']
for a in args[1:]:
    out.append(lift(int(a, 16)))
out.insert(1, ''.join('extern void %s(void);\n' % h for h in sorted(used_hooks)))
# the entry each replaces (a4_engv_XXXXX: tools/injector.py puts a veneer at +XXXXX), per function or all
for a in args[1:]:
    x = '%05X' % int(a, 16)
    out.append('#if defined(A4_ENG_%s) || defined(A4_ENG_ALL)\nENGV(%s)\n#endif' % (x, x))
txt = '\n'.join(out)
if outp: open(outp, 'w').write(txt)
else: print(txt)
missing = set(EDITS) - used_edits
if missing: raise SystemExit('lift.py: edits at %s matched no lifted instruction' % ' '.join('%05X' % a for a in sorted(missing)))
sys.stderr.write('callees: %s\n' % ' '.join('%s%05X' % ('T' if th else 'A', t) for t, th in sorted(callees)))
