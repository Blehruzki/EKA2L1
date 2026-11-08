# pxfuzz.py <px.bin> <bb_code.bin>: a4_comp_row against the C fast loop's rule; a4_cvt565_run against the
# runtime's own inner loop (ngibackbuffer +CCB8..+CD04, run from its code), on unicorn's ARM1136
import sys, struct, random
from unicorn import *
from unicorn.arm_const import *
px = open(sys.argv[1], 'rb').read(); bb = open(sys.argv[2], 'rb').read()
# optional: the two routines' offsets in px (default a4_comp_row at 0, a4_cvt565_run at 0xA8)
OFF_COMP = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
OFF_CVT = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0xA8
CODE, BB, D, S, STK, MAGIC = 0x10000, 0x100000, 0x200000, 0x300000, 0x400000, 0x500000
def mk():
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    try:
        from unicorn import UC_CPU_ARM_1136
        uc.ctl_set_cpu_model(UC_CPU_ARM_1136)
    except Exception: pass
    for a, n in ((CODE, 0x2000), (BB, 0x30000), (D, 0x10000), (S, 0x10000), (STK, 0x10000), (MAGIC, 0x1000)):
        uc.mem_map(a, n)
    uc.mem_write(CODE, px); uc.mem_write(BB, bb)
    return uc
cnt = [0]
def run(uc, pc, regs, until=MAGIC):
    for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), regs): uc.reg_write(r, v)
    uc.reg_write(UC_ARM_REG_SP, STK + 0x8000); uc.reg_write(UC_ARM_REG_LR, MAGIC)
    uc.emu_start(pc, until, count=10000000)
rng = random.Random(7)
def pix():
    k = rng.random()
    if k < 0.7: return rng.getrandbits(24) | 0x80000000 | (rng.getrandbits(7) << 24)
    if k < 0.85: return rng.getrandbits(31)
    return rng.choice([0, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x80FFFFFF, 0x00F81F00])
bad = 0
for case in range(400):
    n = 4 * rng.randint(1, 100)
    src = [pix() for _ in range(n)]
    if rng.random() < 0.5: src = [p | 0x80000000 for p in src]
    dst0 = [rng.getrandbits(16) for _ in range(n)]
    exp = list(dst0)
    for i, p in enumerate(src):
        if p >> 31: exp[i] = ((p >> 8) & 0xF800) | ((p >> 5) & 0x7E0) | ((p >> 3) & 0x1F)
    uc = mk()
    uc.mem_write(S, struct.pack('<%dI' % n, *src)); uc.mem_write(D, struct.pack('<%dH' % n, *dst0))
    uc.mem_write(D + 2 * n, b'\xAA' * 16)
    run(uc, CODE + OFF_COMP, (D, S, S + 4 * n))
    got = list(struct.unpack('<%dH' % n, uc.mem_read(D, 2 * n)))
    if got != exp or uc.mem_read(D + 2 * n, 16) != b'\xAA' * 16:
        bad += 1
        if bad < 4: print('comp case %d n %d: first diff %s' % (case, n, next(i for i in range(n) if got[i] != exp[i]) if got != exp else 'past end'))
print('a4_comp_row: 400 cases, %d bad' % bad)
# the runtime's loop: +CCA8 enters a row with r8 = width, r4 = dst, r5 = src; we enter at +CCB8 with
# r1 = count, r2 = dst, r0 = src, and stop at +CD08 (its row end)
bad = 0
for case in range(400):
    n = 4 * rng.randint(1, 100)
    src = [rng.getrandbits(32) for _ in range(n // 2)]
    uc = mk()
    uc.mem_write(S, struct.pack('<%dI' % (n // 2), *src))
    uc.mem_write(D, b'\x55' * (4 * n + 16))
    uc.reg_write(UC_ARM_REG_R1, n); uc.reg_write(UC_ARM_REG_R2, D); uc.reg_write(UC_ARM_REG_R0, S)
    uc.reg_write(UC_ARM_REG_SP, STK + 0x8000)
    uc.emu_start(BB + 0xCCB8, BB + 0xCD08, count=10000000)
    ref = bytes(uc.mem_read(D, 4 * n + 16))
    uc2 = mk()
    uc2.mem_write(S, struct.pack('<%dI' % (n // 2), *src)); uc2.mem_write(D, b'\x55' * (4 * n + 16))
    run(uc2, CODE + OFF_CVT, (D, S, S + 2 * n))
    got = bytes(uc2.mem_read(D, 4 * n + 16))
    if got != ref:
        bad += 1
        if bad < 4: print('cvt case %d n %d differs' % (case, n))
print('a4_cvt565_run: 400 cases against the runtime\'s loop, %d bad' % bad)
