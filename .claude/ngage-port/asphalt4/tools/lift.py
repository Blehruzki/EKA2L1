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
FUNCS = [(int(a.split(':')[0], 16), a.endswith(':i')) for a in args[1:]]
LIFTED = set(f for f, _ in FUNCS)   # a call to one of these from lifted code is a direct C call (no veneer)
# indirect calls whose likely targets are lifted: compared, then called directly (the face's depth key)
DEVIRT = {0x349BC: [0x3121E]}
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
# call-site hooks a lifted function makes as plain C calls (r0-r3 and r12 clobbered as by the BLX they replace)
CHOOKS = {}
if '-DA4_MESH' in FL:       # injector.py MESH_HOOKS: the mesh routine's ordering-table insert
    CHOOKS[0x34ADC] = 'a4_mesh_insert((uint32_t *)(uintptr_t)r0, (uint32_t *)(uintptr_t)r1); /* the injector\'s mesh hook */'
EDITS, used_edits = {}, set()
CALLMAP = {}            # calls the 3x engine routes through its own C (renderer/a4_eng.h), every site in the lifted code
if '-DA4_ENG3' in FL: CALLMAP = {0x1B2D6: 'eng_c_1b2d6'}
if '-DA4_FASTXF' in FL: CALLMAP[0x345E4] = 'eng_h_345e4'   # the transform by hand (a4_eng.h), from the lifted mesh routine
_ed = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'renderer', 'eng', 'edits.py')
if os.path.exists(_ed) and '-DA4_ENG3' in FL:          # the edits are the 3x engine's; without A4_ENG3 the game's code exactly
    _g = {}; exec(open(_ed).read(), _g); EDITS = _g['EDITS']
if os.path.exists(_ed):                                  # ALWAYS: the port's own hooks a lifted function must keep
    _g = {}; exec(open(_ed).read(), _g)                  # (they replace detours at its code), in every build
    for _a, _e in _g.get('ALWAYS', {}).items():
        EDITS.setdefault(_a, {}).update(_e)

# every function the game calls (Thumb BL targets): the only places a branch leaves a function by
BLT = set()
for o in range(0, len(code) - 3, 2):
    h1, h2 = struct.unpack_from('<HH', code, o)
    if (h1 & 0xF800) == 0xF000 and (h2 & 0xF800) == 0xF800:
        off = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
        if off & (1 << 22): off -= 1 << 23
        BLT.add(o + 4 + off)

# Does the game's Thumb function +t read a stack word of its caller's? Its flow walked with the stack
# pointer's depth: a load or store at [sp, #imm] at or past its entry depth, a tail call (the callee gets the
# caller's words), the stack's address taken (add rX, sp / mov rX, sp: where it goes is not followed) or an
# sp the walk cannot follow -- each says yes; so does a call to a function that reads its caller's words
# (from a thin frame it could reach this one's caller's: conservative, a cycle counts as yes). No means a
# call may pass none (a4_eng_call0).
_NOARGS = {}
def noargs(t):
    if t in _NOARGS: return _NOARGS[t]
    _NOARGS[t] = False                                    # while it is walked (a cycle: yes, it reads)
    res = True; seen = {}; work = [(t, 0)]; calls = set()
    while work and res:
        a, dl = work.pop()
        while res:
            if a in seen:
                if seen[a] != dl: res = False
                break
            if not (0 <= a < len(code) - 2) or len(seen) > 4000: res = False; break
            seen[a] = dl
            try: i = next(md.disasm(code[a:a + 4], a, count=1))
            except StopIteration: res = False; break
            m, op = i.mnemonic, i.op_str
            if m == 'push': dl += 4 * len(i.operands)
            elif m == 'pop':
                dl -= 4 * len(i.operands)
                if 'pc' in op: break
            elif m in ('add', 'sub') and op.startswith('sp, ') and op.count(',') == 1 and '#' in op:
                dl += (-1 if m == 'add' else 1) * i.operands[-1].imm
            elif m in ('ldr', 'str') and '[sp' in op:
                mo = i.operands[1].mem
                if mo.disp - dl >= 0: res = False
            elif 'sp' in op.replace('[sp', ''): res = False      # sp as a value or written otherwise
            elif m in ('add',) and op.startswith('pc'): res = False
            elif m == 'blx': res = False                          # an ARM or indirect callee: not followed
            elif m == 'bl': calls.add(i.operands[0].imm)
            if m == 'bx' or (m == 'mov' and op.startswith('pc')): break
            if m == 'b' or (m.startswith('b') and len(m) == 3 and m not in ('bic', 'blx')):
                tt = i.operands[0].imm
                if tt in BLT and tt != t: res = False; break     # a tail call
                work.append((tt, dl))
                if m == 'b': break
            a += i.size
    if res: res = all(noargs(u) for u in calls if u != t)
    _NOARGS[t] = res
    return res

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
    for a_, e_ in EDITS.items():                # a goto in an edit makes its target a label
        if a_ in reach:
            for v_ in e_.values():
                if isinstance(v_, str):
                    for t_ in re.findall(r'goto L_([0-9A-F]{5});', v_):
                        if int(t_, 16) not in reach: raise SystemExit('lift.py: edit at %05X goes to %s, outside +%05X' % (a_, t_, start))
                        labels.add(int(t_, 16))
    ins = [allins[a] for a in sorted(reach)]
    L = []
    def E(s, i): L.append('    %-60s /* %06x %s %s */' % (s, i.address, i.mnemonic, i.op_str))
    HI = {'sb': 'r9', 'sl': 'r10', 'fp': 'r11', 'ip': 'r12'}
    def rn(op, i): n = i.reg_name(op.reg); return HI.get(n, n)
    def opv(op, i):
        if op.type == A.ARM_OP_REG: return rn(op, i)
        return '0x%Xu' % (op.imm & 0xFFFFFFFF)
    def nz(d): return 'fN = %s >> 31; fZ = %s == 0;' % (d, d)
    W = {'ldr': 'L32', 'str': 'L32', 'ldrb': 'L8', 'strb': 'L8', 'ldrh': 'L16', 'strh': 'L16', 'ldrsb': 'LS8', 'ldrsh': 'LS16'}
    def stkargs(): return ', '.join('S32(%d)' % (4 * k) for k in range(8))
    def callfn(t, thumb, tail=False, ra=None):  # a call to the game's +t: direct when lifted (and enabled), else through a4_eng_call
        slow = 'a4_eng_call(%s(0x%X), r0, r1, r2, r3, sp, r23)' % ('GAMEFN' if thumb else 'GAMEARM', t)
        if thumb and not tail and noargs(t):    # it reads no stack word of its caller's: none copied
            slow = 'a4_eng_call0(GAMEFN(0x%X), r0, r1, r2, r3, r23)' % t
        if thumb and t in LIFTED:
            fast = 'engi_%05X(r0, r1, r2, r3, %s, r23, %s)' % (t, stkargs(), 'lr' if tail else 'GAMEFN(0x%X)' % ra)
            ex = '(ENGD_%05X ? %s : %s)' % (t, fast, slow)
        else: ex = slow
        if tail: return '{ uint64_t rr = %s; o23[0] = r23[0]; o23[1] = r23[1]; return rr; }' % ex
        return '{ uint64_t rr = %s; r0 = (uint32_t)rr; r1 = (uint32_t)(rr >> 32); r2 = r23[0]; r3 = r23[1]; }' % ex
    def callreg(x, tail=False, site=None, ra=None):   # a call through a register
        ex = 'a4_eng_call(%s, r0, r1, r2, r3, sp, r23)' % x
        for t in DEVIRT.get(site, []):
            if t in LIFTED: ex = '((ENGD_%05X && %s == GAMEFN(0x%X)) ? engi_%05X(r0, r1, r2, r3, %s, r23, %s) : %s)' % (t, x, t, t, stkargs(), 'lr' if tail else 'GAMEFN(0x%X)' % ra, ex)
        if tail: return '{ uint64_t rr = %s; o23[0] = r23[0]; o23[1] = r23[1]; return rr; }' % ex
        return '{ uint64_t rr = %s; r0 = (uint32_t)rr; r1 = (uint32_t)(rr >> 32); r2 = r23[0]; r3 = r23[1]; }' % ex
    CMPC = {A.ARM_CC_EQ: '%s == %s', A.ARM_CC_NE: '%s != %s', A.ARM_CC_HS: '%s >= %s', A.ARM_CC_LO: '%s < %s',
            A.ARM_CC_HI: '%s > %s', A.ARM_CC_LS: '%s <= %s', A.ARM_CC_GE: '(int32_t)%s >= (int32_t)%s',
            A.ARM_CC_LT: '(int32_t)%s < (int32_t)%s', A.ARM_CC_GT: '(int32_t)%s > (int32_t)%s',
            A.ARM_CC_LE: '(int32_t)%s <= (int32_t)%s'}
    lastcmp = None          # (address after, a, b): a cmp the next instruction may branch on
    for i in ins:
        if i.address in labels: L.append('L_%05X:' % i.address)
        m = i.mnemonic; ops = i.operands
        flg = m.endswith('s') and m not in ('bics_',) and m[:-1] in ('mov', 'mvn', 'add', 'sub', 'lsl', 'lsr', 'asr', 'ror', 'and', 'orr', 'eor', 'bic', 'mul', 'neg', 'rsb', 'adc', 'sbc')
        base = m[:-1] if flg else m
        if m == 'push':
            regs = [rn(o, i) for o in ops]
            E('spp -= %d; ' % (4 * len(regs)) + ' '.join('S32(%d) = %s;' % (4 * k, r) for k, r in enumerate(regs)), i)
        elif m == 'pop':
            regs = [rn(o, i) for o in ops]
            s = ' '.join('%s = S32(%d);' % (r if r != 'pc' else 't', 4 * k) for k, r in enumerate(regs)) + ' spp += %d;' % (4 * len(regs))
            E(s + (' RET(t);' if 'pc' in regs else ''), i)
        elif m in ('ldm', 'stm'):
            b = rn(ops[0], i); regs = [rn(o, i) for o in ops[1:]]
            if m == 'ldm':
                E('t = %s; ' % b + ' '.join('%s = L32(t + %d);' % (r, 4 * k) for k, r in enumerate(regs)) + ('' if b in regs else ' %s = t + %d;' % (b, 4 * len(regs))), i)
            elif EDITS.get(i.address, {}).get('third'):     # a vector rate: each word keeps a third of its change
                c_ = EDITS[i.address]['third']; c_ = '' if c_ is True else 'if (%s) ' % c_
                E('t = %s; ' % b + ' '.join('%s%s = eng_third(t + %d, L32(t + %d), %s); L32(t + %d) = %s;' % (c_, r, 4 * k, 4 * k, r, 4 * k, r) for k, r in enumerate(regs)) + ' %s = t + %d;' % (b, 4 * len(regs)), i)
            else:
                E('t = %s; ' % b + ' '.join('L32(t + %d) = %s;' % (4 * k, r) for k, r in enumerate(regs)) + ' %s = t + %d;' % (b, 4 * len(regs)), i)
        elif m in W and ops[1].type == A.ARM_OP_MEM:
            mo = ops[1].mem; b = HI.get(i.reg_name(mo.base), i.reg_name(mo.base)); rt = rn(ops[0], i)
            if b == 'pc':
                la = ((i.address + 4) & ~3) + mo.disp
                E('%s = GAMEW(0x%X);' % (rt, la), i); continue
            ea = b + (' + ' + HI.get(i.reg_name(mo.index), i.reg_name(mo.index)) if mo.index else '') + (' + 0x%Xu' % (mo.disp & 0xFFFFFFFF) if mo.disp else '')
            Wm = W[m]
            if b == 'sp' and not mo.index:      # the frame: through the pointer-typed stack (clang keeps it in registers)
                Wm = W[m].replace('L', 'S', 1); ea = '0x%Xu' % (mo.disp & 0xFFFFFFFF)
            if m.startswith('ld'):
                E('%s = %s%s(%s);' % (rt, '(uint32_t)(int32_t)' if m in ('ldrsb', 'ldrsh') else '', Wm, ea), i)
            elif EDITS.get(i.address, {}).get('gate') == 'end':   # a countdown substeps read: stored on the step's last substep
                E('if (ENG_SUB_END(%s)) %s(%s) = %s; else %s = %s(%s);' % (EDITS[i.address]['car'], Wm, ea, rt, rt, Wm, ea), i)
            elif EDITS.get(i.address, {}).get('gate'):      # a counter: stored on the whole step; a substep keeps memory's
                E('if (!ENG_SUB) %s(%s) = %s; else %s = %s(%s);' % (Wm, ea, rt, rt, Wm, ea), i)
            elif EDITS.get(i.address, {}).get('third'):     # a rate: the store keeps a third of its change
                if m != 'str': raise SystemExit('lift.py: a third at %06x on %s' % (i.address, m))
                if b == 'sp': raise SystemExit('lift.py: a third at %06x on the frame' % i.address)
                c_ = EDITS[i.address]['third']; c_ = '' if c_ is True else 'if (%s) ' % c_   # a condition: the third only when it holds
                E('{ uint32_t ea_ = %s; %s%s = eng_third(ea_, L32(ea_), %s); L32(ea_) = %s; }' % (ea, c_, rt, rt, rt), i)
            else: E('%s(%s) = %s;' % (Wm, ea, rt), i)
        elif base in ('mov', 'mvn'):
            d = rn(ops[0], i); s = opv(ops[1], i)
            if d == 'pc': E('RET(%s); /* jump through a register */' % s if s == 'lr' else callreg(s, True, i.address) + ' /* jump through a register */', i); continue
            if d == 'sp': E('spp = (uint8_t *)(uintptr_t)(%s);' % s, i); continue
            E('%s = %s%s;%s' % (d, '~' if base == 'mvn' else '', s, (' ' + nz(d)) if flg else ''), i)
        elif i.address in jt:
            r = rn(ops[1], i)
            cases = ' '.join('case 0x%Xu: goto L_%05X;' % (2 * k_, t_) for k_, t_ in sorted(set(((t_ - i.address - 4) // 2, t_) for t_ in jt[i.address])))
            E('switch (%s) { %s default: %s }' % (r, cases, callreg('GAMEADDR(0x%X) + %s' % (i.address + 4, r), True)), i)
        elif base in ('add', 'sub', 'adc', 'sbc', 'rsb', 'neg', 'cmp', 'cmn') or m in ('cmp', 'cmn'):
            if m in ('cmp', 'cmn'): d, a, b = None, opv(ops[0], i), opv(ops[1], i)
            elif len(ops) == 3: d, a, b = rn(ops[0], i), opv(ops[1], i), opv(ops[2], i)
            elif base in ('neg', 'rsb'): d, a, b = rn(ops[0], i), opv(ops[1], i), '0'
            else: d, a, b = rn(ops[0], i), rn(ops[0], i), opv(ops[1], i)
            if a == 'pc': a = 'GAMEADDR(0x%X)' % ((i.address + 4) & ~3)
            if d == 'sp' and m in ('add', 'sub'):    # the frame moved: the stack pointer is a pointer
                if a == 'sp' and ops[-1].type == A.ARM_OP_IMM: E('spp %s= 0x%Xu;' % ('+' if m == 'add' else '-', ops[-1].imm), i)
                else: E('spp = (uint8_t *)(uintptr_t)(%s %s %s);' % (a, '+' if m == 'add' else '-', b), i)
                continue
            if m == 'cmp': lastcmp = (i.address + i.size, a, b)
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
            elif ops[0].type == A.ARM_OP_IMM and ops[0].imm in CALLMAP:
                t = ops[0].imm; callees.add((t, m == 'bl'))
                E('{ uint64_t rr = %s(r0, r1, r2, r3, sp, r23); r0 = (uint32_t)rr; r1 = (uint32_t)(rr >> 32); r2 = r23[0]; r3 = r23[1]; } /* the engine\'s own +%X */' % (CALLMAP[t], t), i)
            elif i.address in CHOOKS:            # the injector's call-site hook, called from C directly
                E(CHOOKS[i.address], i)
            elif ops[0].type == A.ARM_OP_IMM:
                t = ops[0].imm; callees.add((t, m == 'bl'))
                E(callfn(t, m == 'bl', ra=i.address + i.size), i)
            else:
                E(callreg(rn(ops[0], i), False, i.address, ra=i.address + i.size), i)
            E('r12 = 0xDEAD0000u; /* clobbered by the call */', i)
        elif m == 'bx':
            E('RET(lr);' if rn(ops[0], i) == 'lr' else callreg(rn(ops[0], i), True, i.address), i)
        elif isbr(i):
            t = ops[0].imm
            if t in allins and (t not in BLT or t == start or local(t)): tgt = 'goto L_%05X;' % t
            else:
                callees.add((t, True))
                tgt = callfn(t, True, True) + ' /* tail */'
            if m == 'b': E(tgt, i)
            elif lastcmp and lastcmp[0] == i.address and i.address not in labels and i.cc in CMPC:
                E('if (%s) %s' % (CMPC[i.cc] % lastcmp[1:], tgt), i)   # the cmp before it, as a C comparison
            else: E('if (%s) %s' % (CC[i.cc], tgt), i)
        elif m == 'adr':
            E('%s = GAMEADDR(0x%X);' % (rn(ops[0], i), ((i.address + 4) & ~3) + ops[1].imm), i)
        elif m in ('add', 'sub') and rn(ops[0], i) == 'sp':
            E('spp %s= %s;' % ('+' if m == 'add' else '-', opv(ops[-1], i)), i)
        else:
            E('#error "lift.py: %s %s"' % (m, i.op_str), i)
    frame = 0
    for i in ins[:8]:
        if i.mnemonic == 'push': frame += 4 * len(i.operands)
        elif i.mnemonic == 'sub' and i.op_str.startswith('sp'): frame += i.operands[-1].imm
    stkw = (frame + 64 + 32 + 7) // 8 * 2      # words: the frame, a margin, the eight argument words
    hdr = ('/* +%05X, %d instructions (tools/lift.py) */\n' % (start, len(ins)) +
           'static uint64_t engi_%05X(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3,\n'
           '                  uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7, uint32_t *o23, uint32_t lr0)\n{\n' % start +
           '    uint32_t r4 = 0, r5 = 0, r6 = 0, r7 = 0, r8 = 0, r9 = 0, r10 = 0, r11 = 0, r12 = 0, lr = lr0, t = 0;\n'
           '    uint32_t fN = 0, fZ = 0, fC = 0, fV = 0, r23[2];\n'
           '    uint32_t stk[%d]; uint8_t *spp = (uint8_t *)&stk[%d - 8];\n' % (stkw, stkw) +
           '#ifdef A4_STKPOISON\n    a4_stkpoison(stk, %du);\n#endif\n' % (stkw - 8) +
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
        if e.get('gate') and 'ENG_SUB' not in body: # another instruction gated: the whole step only
            mm2 = re.match(r'(\s*)(.*?)(\s*/\*.*\*/)$', body)
            body = '%sif (!ENG_SUB) { %s }%s' % mm2.groups()
        L[k] = '\n'.join(x for x in (why, ('    ' + e['before']) if 'before' in e else None, body, ('    ' + e['after']) if 'after' in e else None) if x)
    disp = ' '.join('case 0x%Xu: goto L_%05X;' % (k, v) for k, v in sorted(rets.items()))
    ret = '#undef RET\n#define RET(v) do { switch (v) { %s default: o23[0] = r2; o23[1] = r3; return ((uint64_t)r1 << 32) | r0; } } while (0)\n' % disp
    # round 51: the global entry only where something may call it -- the 3x engine, or this function
    # selected; else nothing keeps the (static) function, and a build of the render lifts alone drops the rest
    wrap = ('#if defined(A4_ENG3) || defined(A4_ENG_ALL) || defined(A4_ENG_%05X)\n'
            'uint64_t eng_%05X(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3,\n'
            '                  uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)\n'
            '{ uint32_t d_[2]; return engi_%05X(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7, d_, 0xFFFFFFFFu); }\n'
            '#endif\n'
            '/* from the veneer: lr the game\'s caller left (the function pushes it, and the port reads it: a4_gl_bb) */\n'
            'static inline __attribute__((always_inline)) uint64_t engr_%05X(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0,\n'
            '    uint32_t s1, uint32_t s2, uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7, uint32_t ra)\n'
            '{ uint32_t d_[2]; return engi_%05X(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7, d_, ra); }\n' % (start, start, start, start, start))
    return ret + hdr + '\n'.join(L) + '\n}\n' + wrap

# Small leaves inline (round 50): a game function a lifted one calls, with no calls of its own, no jump
# through a register but its return, at most 64 instructions, no edit and no injector patch inside, is lifted
# too, inline only -- a call to a 3-instruction getter through a4_eng_call cost ~35 (liftfuzz.py: +22C60's
# five getter calls made it six times the game's length). A4_NOLEAF turns it off.
PATCHED = {0x300B2, 0x2973A, 0x30252, 0x305A2, 0x2AB0C, 0x1EE, 0x34ADC, 0x1E6, 0x16796, 0x167B4, 0x16966,
           0x1696C, 0x33412, 0x3357A, 0x33B16, 0x14974, 0x1495E, 0x14C94, 0x14CA2, 0x14C68, 0x146FC, 0x146D6,
           0x14EEC, 0x1306C, 0x14574, 0x3A15C, 0x2ABAA, 0x51D96, 0x51DB8}   # injector.py's sites, all builds
def leaf(t):
    seen, work = set(), [t]
    while work:
        a = work.pop()
        while a not in seen:
            if not (0 <= a < len(code) - 2) or len(seen) > 64: return False
            seen.add(a)
            try: i = next(md.disasm(code[a:a + 4], a, count=1))
            except StopIteration: return False
            m = i.mnemonic
            if m in ('bl', 'blx', 'svc', 'bkpt', 'udf') or (m == 'add' and i.op_str.startswith('pc')): return False
            if m == 'bx': 
                if i.op_str != 'lr': return False
                break
            if m == 'mov' and i.op_str.startswith('pc'):
                if not i.op_str.endswith('lr'): return False
                break
            if m == 'pop' and 'pc' in i.op_str: break
            if m == 'b' or (m.startswith('b') and len(m) == 3 and m not in ('bic',)):
                tt = int(i.op_str.lstrip('#'), 16)
                if tt in BLT and tt != t: return False      # a tail call
                work.append(tt)
                if m == 'b': break
            if m in ('ldm', 'stm', 'push', 'pop') and ('sp' in i.op_str.split('{')[0] and m in ('ldm', 'stm')): return False
            a += i.size
    if any(min(seen) <= h < max(seen) + 4 for h in PATCHED): return False
    if any(min(seen) <= e < max(seen) + 4 for e in EDITS): return False
    return True
if '-DA4_NOLEAF' not in FL:
    for f, _ in list(FUNCS): lift(f)                    # a first pass for the callees
    used_hooks.clear(); used_edits.clear()
    for t, th in sorted(callees):
        if th and t not in LIFTED and t not in CALLMAP and t not in HOOKS and leaf(t):
            FUNCS.append((t, True)); LIFTED.add(t)
    callees.clear()
    sys.stderr.write('leaves inline: %s\n' % ' '.join('%05X' % f for f, i in FUNCS if i and '%05X' % f not in ' '.join(args)))
out = ['/* generated by tools/lift.py -- the game\'s own functions as C; edit only through the engine notes */']
# a lifted function calls another one directly when that one is in the build (A4_ENG_ALL or its own
# A4_ENG_XXXXX), else through the game's entry as before
for f, _ in FUNCS:
    out.append('#if (defined(A4_ENG_%05X) || defined(A4_ENG_ALL)) && !defined(A4_NODIRECT_%05X)\n#define ENGD_%05X 1\n#else\n#define ENGD_%05X 0\n#endif\n'
               'static uint64_t engi_%05X(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,\n'
               '    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t);' % (f, f, f, f, f))
for f, _ in FUNCS:
    out.append(lift(f))
out.insert(1, ''.join('extern void %s(void);\n' % h for h in sorted(used_hooks)))
# the entry each replaces (a4_engv_XXXXX: tools/injector.py puts a veneer at +XXXXX), per function or all;
# an inline-only one (XXXXX:i) has none: only lifted callers reach it, the game's own callers keep the Thumb
for f, inl in FUNCS:
    if inl: continue
    x = '%05X' % f
    out.append('#if defined(A4_ENG_%s) || defined(A4_ENG_ALL)\nENGV(%s)\n#endif' % (x, x))
txt = '\n'.join(out)
if outp: open(outp, 'w').write(txt)
else: print(txt)
missing = set(EDITS) - used_edits
if missing: raise SystemExit('lift.py: edits at %s matched no lifted instruction' % ' '.join('%05X' % a for a in sorted(missing)))
sys.stderr.write('callees: %s\n' % ' '.join('%s%05X' % ('T' if th else 'A', t) for t, th in sorted(callees)))
