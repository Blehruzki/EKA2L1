# cvtfuzz.py <lifted.exe> <syms.txt> <bb_code.bin> [-n N]: our cvt_fast against ngibackbuffer's +CB70 (run from
# its own code, its TRect::Size import emulated) on unicorn's ARM1136, on random converter/back-buffer objects
import sys, struct, random, re
from unicorn import *
from unicorn.arm_const import *
exe, symf, bbf = sys.argv[1:4]; N = int(sys.argv[sys.argv.index('-n') + 1]) if '-n' in sys.argv else 3000
SY = {m.group(2): int(m.group(1), 16) for m in (re.match(r'\+([0-9A-Fa-f]+)\s+(\S+)', l) for l in open(symf)) if m}
d = open(exe, 'rb').read(); u = lambda o: struct.unpack_from('<I', d, o)[0]
CODE, DATA = d[u(0x64):u(0x64) + u(0x30)], d[u(0x68):u(0x68) + u(0x34)]
BBC = open(bbf, 'rb').read()
CB, DB, DTOP, BB = 0x8000, 0x400000, 0x480000, 0x01000000
SRC, DST, OBJ, STK, MAGIC, DELEG = 0x00500000, 0x00600000, 0x00700000, 0x00800000, 0x00900000, 0x00901000
def mk():
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    try:
        from unicorn import UC_CPU_ARM_1136
        uc.ctl_set_cpu_model(UC_CPU_ARM_1136)
    except Exception: pass
    uc.mem_map(CB, (len(CODE) + 0xFFF) & ~0xFFF); uc.mem_write(CB, CODE)
    uc.mem_map(DB, DTOP - DB); uc.mem_write(DB, DATA)
    uc.mem_map(BB, (len(BBC) + 0xFFF) & ~0xFFF); uc.mem_write(BB, BBC)
    for a, n in ((SRC, 0x40000), (DST, 0x100000), (OBJ, 0x1000), (STK, 0x10000), (MAGIC, 0x2000)): uc.mem_map(a, n)
    def size(uc_, addr, sz, ud):     # RWindowBase::Size() (WS32 387): r0 = &TSize, r1 = the window; here its first two words
        r0, r1 = uc_.reg_read(UC_ARM_REG_R0), uc_.reg_read(UC_ARM_REG_R1)
        CALLS.append(r1)
        uc_.mem_write(r0, bytes(uc_.mem_read(r1, 8))); uc_.reg_write(UC_ARM_REG_PC, uc_.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE, size, begin=BB + 0x5EC8, end=BB + 0x5EC8)
    uc.hook_add(UC_HOOK_CODE, lambda uc_, a, sz, ud: uc_.emu_stop(), begin=DELEG, end=DELEG)
    return uc
def run(case, entry):
    src, objs, regs = case
    uc = mk()
    uc.mem_write(SRC, src); uc.mem_write(DST, b'\x5A' * 0x100000)
    for a, b in objs: uc.mem_write(a, b)
    uc.mem_write(DB + 0x12100 + 12, struct.pack('<I', DELEG))       # CVT[3]: where cvt_fast hands back
    uc.mem_write(DB + 0x12100 + 44, struct.pack('<I', BB + 0x5EC8)) # CVT[11]: the Size() stub
    del CALLS[:]
    for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2), regs): uc.reg_write(r, v)
    uc.reg_write(UC_ARM_REG_SP, STK + 0x8000); uc.reg_write(UC_ARM_REG_LR, MAGIC)
    for r in range(UC_ARM_REG_R4, UC_ARM_REG_R4 + 0): pass
    try:
        uc.emu_start(entry, MAGIC, count=4000000)
    except UcError as e:
        if uc.reg_read(UC_ARM_REG_PC) == DELEG: return ('deleg', None)
        return ("fault", "%s pc %08x" % (e, uc.reg_read(UC_ARM_REG_PC)))
    pc = uc.reg_read(UC_ARM_REG_PC)
    if pc == DELEG: return ('deleg', None)
    if pc != MAGIC: return ('limit', None)
    return ('ok', repr(CALLS).encode() + bytes(uc.mem_read(DST, 0x100000)) + bytes(uc.mem_read(SRC, 0x40000)) + bytes(uc.mem_read(OBJ, 0x1000)))
CALLS = []
rng = random.Random(int(sys.argv[sys.argv.index('-seed') + 1]) if '-seed' in sys.argv else 3)
def pick(common, *rare):
    return common() if rng.random() < 0.8 else rng.choice(rare)()
def gen():
    w = pick(lambda: rng.choice([320, 4 * rng.randint(1, 40)]), lambda: rng.randint(-8, 330), lambda: 0)
    h = pick(lambda: rng.choice([240, rng.randint(1, 12)]), lambda: rng.randint(-3, 3), lambda: 0)
    if w * max(h, 1) > 320 * 240: h = 240
    sstr = pick(lambda: (max(w, 0) + 1) // 2 + rng.choice([0, 0, 0, 1, 5]), lambda: rng.randint(0, 200))
    dstr = pick(lambda: max(w, 0) + rng.choice([0, 0, 0, 3, 16]), lambda: rng.randint(0, 400))
    x = pick(lambda: 0, lambda: rng.randint(-7, 7)); y = pick(lambda: 0, lambda: rng.randint(-3, 3))
    tw = pick(lambda: w + rng.choice([0, 0, 4, 40]), lambda: rng.randint(-4, 330))
    th = pick(lambda: h + rng.choice([0, 0, 2]), lambda: rng.randint(-2, 250))
    tl = (rng.randint(-50, 50), rng.randint(-50, 50))
    srcb = bytes(rng.getrandbits(8) for _ in range(0x40000)) if rng.random() < 0.1 else None
    sbase = SRC + 0x20000 + 4 * rng.randint(0, 64) + (rng.choice([0, 0, 0, 0, 2]))
    dbase = DST + 0x40000 + 4 * rng.randint(0, 64) + (rng.choice([0, 0, 0, 0, 0, 0, 2]))
    if rng.random() < 0.03: dbase = sbase          # overlap
    conv, X, X10 = OBJ, OBJ + 0x100, OBJ + 0x400
    cv = struct.pack('<4I', 0x11111111, X, x & 0xFFFFFFFF, y & 0xFFFFFFFF)
    xo = bytearray(0x100); struct.pack_into('<I', xo, 0x10, X10); struct.pack_into('<i', xo, 0x24, w); struct.pack_into('<i', xo, 0x28, h)
    struct.pack_into('<I', xo, 0x40, sstr & 0xFFFFFFFF)
    xt = bytearray(0x100); struct.pack_into('<2i', xt, 0x28, tw, th); struct.pack_into('<I', xt, 0xB4, dstr & 0xFFFFFFFF)
    return (srcb, [(conv, cv), (X, bytes(xo)), (X10, bytes(xt))], (conv, dbase, sbase))
SRCB = bytes(random.Random(9).getrandbits(8) for _ in range(0x40000))
ok = bad = deleg = skip = 0; first = None
for c in range(N):
    srcb, objs, regs = gen()
    case = (srcb or SRCB, objs, regs)
    g = run(case, BB + 0xCB70)
    if g[0] != 'ok': skip += 1; continue
    l = run(case, CB + SY['cvt_fast'])
    if l[0] == 'deleg': deleg += 1; continue
    if l[0] != 'ok' or l[1] != g[1]:
        bad += 1
        if first is None:
            o = next((i for i in range(len(g[1])) if l[1] is None or g[1][i] != l[1][i]), -1)
            first = 'case %d: %s %s, first byte differing %d (w %d h %d)' % (c, l[0], l[1] if l[0] != 'ok' else '', o, struct.unpack_from('<i', objs[1][1], 0x24)[0], struct.unpack_from('<i', objs[1][1], 0x28)[0])
        continue
    ok += 1
print('cvt_fast against +CB70: %d agree, %d differ, %d handed back, %d not cases (+CB70 faulted or ran away)%s'
      % (ok, bad, deleg, skip, ('\n  first: ' + first) if first else ''))
