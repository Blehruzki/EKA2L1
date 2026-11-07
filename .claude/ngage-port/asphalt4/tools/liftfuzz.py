# liftfuzz.py <base.exe> <lifted.exe> <syms.txt> <func hex> ... [-n cases] [-seed s] [-v]: each lifted game
# function against the game's own, offline, on unicorn's ARM1136 (round 50). Both images run at their link
# address (code 0x8000, data 0x400000: no relocation needed), on the same random memory: a 1 MB arena of
# words that are pointers into the arena, small numbers, 16-bit values or noise, the registers and eight
# stack words likewise. The game's function runs from the untouched image (Thumb, its whole call tree the
# game's); the lifted one from the injected image (eng_XXXXX, ARM, calling the lifted or the game's
# functions as the build does). Compared: r0, r1 and every byte of the arena, the data and bss (the
# port's scratch with it) and the eight stack words. A case where the game's run faults or runs past the
# instruction limit is not a case (garbage pointers); one where only the lifted one does is a failure.
# Also counted: instructions run (unicorn's blocks, Thumb by halfword pairs, ARM by words), the lift's
# gain that holds on any CPI split. syms.txt: the injector's symbol file (A4_SYMFILE: "+offset name").
import sys, struct, random, re
import numpy as np
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK, UC_PROT_ALL
from unicorn.arm_const import *
args = sys.argv[1:]
def opt(name, dflt):
    if name in args:
        k = args.index(name); v = args[k + 1]; del args[k:k + 2]; return v
    return dflt
NC = int(opt('-n', '300')); SEED = int(opt('-seed', '1')); VERB = '-v' in args
if VERB: args.remove('-v')
base_exe, lift_exe, symf = args[0], args[1], args[2]
funcs = [(int(a.split('=')[0].split(':')[0], 16), a.split('=')[1] if '=' in a else None) for a in args[3:]]  # XXXXX[=symbol]
SYMS = {}
for l in open(symf):
    m = re.match(r'\+([0-9A-Fa-f]+)\s+(\S+)', l)
    if m: SYMS[m.group(2)] = int(m.group(1), 16)
def image(path):
    d = open(path, 'rb').read(); u = lambda o: struct.unpack_from('<I', d, o)[0]
    co, cs, do, ds = u(0x64), u(0x30), u(0x68), u(0x34)
    return d[co:co + cs], d[do:do + ds], u(0x4C), u(0x50)
BC, BD, CB, DB = image(base_exe)
LC, LD, CB2, DB2 = image(lift_exe)
assert CB == CB2 == 0x8000 and DB == DB2 == 0x400000
ARENA, ASZ = 0x00500000, 0x40000
STK, SSZ = 0x00700000, 0x10000
SP0 = STK + 0xC000
MAGIC = 0x00F00000
DTOP = 0x00480000
def page(n): return (n + 0xFFF) & ~0xFFF
def mk(code, data):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    try:
        from unicorn import UC_CPU_ARM_1136
        uc.ctl_set_cpu_model(UC_CPU_ARM_1136)
    except Exception: pass
    uc.mem_map(CB, page(len(code)), UC_PROT_ALL); uc.mem_write(CB, code)
    uc.mem_map(DB, DTOP - DB, UC_PROT_ALL); uc.mem_write(DB, data)
    uc.mem_map(ARENA, ASZ, UC_PROT_ALL); uc.mem_map(STK, SSZ, UC_PROT_ALL); uc.mem_map(MAGIC, 0x1000, UC_PROT_ALL)
    return uc
def word(rng):                                   # a register or stack word: mostly a pointer
    k = rng.random() * 0.6 if rng.random() < 0.75 else rng.random()
    if k < 0.40: return ARENA + 4 * rng.randrange(ASZ // 4 - 64)
    if k < 0.70: return rng.randrange(0, 65)
    if k < 0.85: return rng.randrange(-512, 512) & 0xFFFFFFFF
    return rng.getrandbits(32)
def case(rng):
    n = ASZ // 4; g = np.random.default_rng(rng.getrandbits(32))
    k = g.random(n)
    w = np.where(k < 0.55, ARENA + 4 * g.integers(0, n - 64, n),
        np.where(k < 0.80, g.integers(0, 65, n),
        np.where(k < 0.92, g.integers(-512, 512, n) & 0xFFFFFFFF, g.integers(0, 1 << 32, n))))
    arena = w.astype('<u4').tobytes()
    regs = [word(rng) for _ in range(4)]
    stk = [word(rng) for _ in range(8)]
    return arena, regs, stk
def run(code, data, entry, thumb, arena, regs, stk, limit=3000000):
    uc = mk(code, data)
    uc.mem_write(ARENA, bytes(arena)); uc.mem_write(SP0, struct.pack('<8I', *stk))
    for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), regs): uc.reg_write(r, v)
    for r in (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12):
        uc.reg_write(r, 0x11111111)
    uc.reg_write(UC_ARM_REG_SP, SP0); uc.reg_write(UC_ARM_REG_LR, MAGIC)
    cnt = [0]
    def blk(uc_, addr, size, ud):
        cnt[0] += size // (2 if uc_.reg_read(UC_ARM_REG_CPSR) & 0x20 else 4)
    uc.hook_add(UC_HOOK_BLOCK, blk)
    try:
        uc.emu_start(entry | (1 if thumb else 0), MAGIC, count=limit)
    except UcError as e:
        return ('fault', str(e), cnt[0])
    if uc.reg_read(UC_ARM_REG_PC) != MAGIC: return ('limit', None, cnt[0])
    out = (uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1), bytes(uc.mem_read(ARENA, ASZ)),
           bytes(uc.mem_read(DB, DTOP - DB)), bytes(uc.mem_read(SP0, 32)))
    return ('ok', out, cnt[0])
def diff(a, b):
    n = 0; first = None
    for i in range(0, len(a), 4096):
        if a[i:i + 4096] != b[i:i + 4096]:
            for j in range(i, min(i + 4096, len(a))):
                if a[j] != b[j]:
                    n += 1
                    if first is None: first = j
    return n, first
rng = random.Random(SEED)
for f, sym in funcs:
    sym = sym or 'eng_%05X' % f
    if sym not in SYMS: print('+%05X: no %s in %s' % (f, sym, symf)); continue
    ok = bad = skip = lfault = 0; ig = il = 0; first_bad = None
    for c in range(NC):
        arena, regs, stk = case(rng)
        g = run(BC, BD, CB + f, True, arena, regs, stk)
        if g[0] != 'ok': skip += 1; continue
        l = run(LC, LD, CB + SYMS[sym], False, arena, regs, stk)
        if l[0] != 'ok':
            lfault += 1; bad += 1
            if first_bad is None: first_bad = 'case %d: lifted %s %s (game ran %d instructions)' % (c, l[0], l[1], g[2])
            continue
        G, Lr = g[1], l[1]
        d = []
        if G[0] != Lr[0]: d.append('r0 %08X/%08X' % (G[0], Lr[0]))
        if G[1] != Lr[1]: d.append('r1 %08X/%08X' % (G[1], Lr[1]))
        for name, k, base in (('arena', 2, ARENA), ('data', 3, DB), ('args', 4, SP0)):
            n, fst = diff(G[k], Lr[k])
            if n: d.append('%s %d bytes from %08X' % (name, n, base + fst))
        if d:
            bad += 1
            if first_bad is None: first_bad = 'case %d: %s' % (c, ', '.join(d))
        else:
            ok += 1; ig += g[2]; il += l[2]
        if VERB: print('  case %d: %s game %d lifted %d %s' % (c, 'ok' if not d else 'BAD', g[2], l[2], ' '.join(d)))
    print('+%05X: %d cases agree, %d differ (%d the lifted faults alone), %d not cases; instructions game %d lifted %d (%.2fx)%s'
          % (f, ok, bad, lfault, skip, ig, il, ig / il if il else 0, ('\n    first: ' + first_bad) if first_bad else ''))
