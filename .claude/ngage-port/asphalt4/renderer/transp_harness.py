"""transp_harness.py -- run Asphalt 4's ORIGINAL keyed/transparent span inner
loop (Thumb at code +33B16..+33B42) under unicorn with controlled inputs, and
compare its output to transp_model.py. Proves the decode is correct. Needs
unicorn and A4_CODE (the exe's extracted code section; see span_harness.py).

Register/stack map at the loop: r1=u, r2=v, r6=ustep, r7=vstep, lr=umask,
ip=tex, r4=dst; [sp+0x10]=vshift, [sp+0x14]=vmask, [sp+0x18]=dst_end.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import transp_model
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R4,
                               UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R12,
                               UC_ARM_REG_LR, UC_ARM_REG_SP)

CODEBASE = 0x8000
LOOP = 0x33B16
STOP = 0x33B42
CODE = open(os.environ.get('A4_CODE', 'a4.code'), 'rb').read()

STACK, TEX, DST = 0x70000000, 0x60000000, 0x61000000
SP = STACK + 0x8000
M32 = 0xFFFFFFFF


def run_original(u, v, ustep, vstep, umask, vmask, vshift, tex, dst_in, n):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0, 0x200000)
    uc.mem_write(CODEBASE, CODE)
    for base in (STACK, TEX, DST):
        uc.mem_map(base, 0x100000)
    uc.mem_write(TEX, b''.join(struct.pack('<H', t) for t in tex))
    uc.mem_write(DST, b''.join(struct.pack('<H', d) for d in dst_in))

    def w(off, val):
        uc.mem_write(SP + off, struct.pack('<I', val & M32))
    w(0x10, vshift); w(0x14, vmask); w(0x18, DST + 2 * n)
    uc.reg_write(UC_ARM_REG_SP, SP)
    uc.reg_write(UC_ARM_REG_R1, u & M32)
    uc.reg_write(UC_ARM_REG_R2, v & M32)
    uc.reg_write(UC_ARM_REG_R6, ustep & M32)
    uc.reg_write(UC_ARM_REG_R7, vstep & M32)
    uc.reg_write(UC_ARM_REG_LR, umask & M32)
    uc.reg_write(UC_ARM_REG_R12, TEX)
    uc.reg_write(UC_ARM_REG_R4, DST)
    uc.emu_start(CODEBASE + LOOP + 1, CODEBASE + STOP, count=n * 100 + 1000)
    return list(struct.unpack('<%dH' % n, uc.mem_read(DST, 2 * n)))


def main(trials=2000):
    rnd = random.Random(7)
    bad = 0
    for t in range(trials):
        shift = rnd.choice([4, 5, 6])
        tw, th = 1 << shift, 1 << rnd.choice([4, 5, 6])
        # ~25% transparent (zero) texels so the skip branch is exercised
        tex = [0 if rnd.random() < 0.25 else rnd.getrandbits(16) for _ in range(tw * th)]
        n = rnd.randint(1, 40)
        dst_in = [rnd.getrandbits(16) for _ in range(n)]
        args = dict(u=rnd.getrandbits(32), v=rnd.getrandbits(32),
                    ustep=rnd.getrandbits(20), vstep=rnd.getrandbits(20),
                    umask=tw - 1, vmask=th - 1, vshift=shift,
                    tex=tex, dst_in=dst_in, n=n)
        if run_original(**args) != transp_model.span(**args):
            bad += 1
            if bad <= 3:
                print('MISMATCH trial', t)
    print('%d/%d trials match' % (trials - bad, trials))
    return bad == 0


if __name__ == '__main__':
    sys.exit(0 if main() else 1)
