"""transp_verify.py <rewrite.bin> -- run an ARM rewrite of the keyed/transparent
span filler under unicorn and check it bit-exact against the ORIGINAL loop on the
same random inputs, including the destination's prior contents (the colour-key
skip leaves pixels untouched). The rewrite is the AAPCS function

  a4_span_transparent(dst, n, u, v, ustep, vstep, umask, vmask, vshift, tex)

Build the .bin first (see a4_span_transparent.c) and run with A4_CODE pointing at
the exe's extracted code section (see span_harness.py)."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import transp_harness as H
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR)

FN, STK, TEX, DST, RET = 0x400000, 0x1000000, 0x2000000, 0x3000000, 0x5000000
BIN = open(sys.argv[1], 'rb').read()


def run_rewrite(u, v, ustep, vstep, umask, vmask, vshift, tex, dst_in, n):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for base in (FN, STK, TEX, DST):
        uc.mem_map(base, 0x100000)
    uc.mem_map(RET & ~0xFFF, 0x1000)
    uc.mem_write(FN, BIN)
    uc.mem_write(TEX, b''.join(struct.pack('<H', t) for t in tex))
    uc.mem_write(DST, b''.join(struct.pack('<H', d) for d in dst_in))
    sp = STK + 0x8000
    stackargs = [ustep, vstep, umask, vmask, vshift, TEX]
    for i, a in enumerate(stackargs):
        uc.mem_write(sp + 4 * i, struct.pack('<I', a & 0xFFFFFFFF))
    uc.reg_write(UC_ARM_REG_R0, DST)
    uc.reg_write(UC_ARM_REG_R1, n)
    uc.reg_write(UC_ARM_REG_R2, u & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R3, v & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_SP, sp)
    uc.reg_write(UC_ARM_REG_LR, RET)
    uc.emu_start(FN, RET, count=n * 100 + 2000)
    return list(struct.unpack('<%dH' % n, uc.mem_read(DST, 2 * n)))


def main(trials=2000):
    rnd = random.Random(7)
    bad = 0
    for t in range(trials):
        shift = rnd.choice([4, 5, 6])
        tw, th = 1 << shift, 1 << rnd.choice([4, 5, 6])
        tex = [0 if rnd.random() < 0.25 else rnd.getrandbits(16) for _ in range(tw * th)]
        n = rnd.randint(1, 40)
        dst_in = [rnd.getrandbits(16) for _ in range(n)]
        args = dict(u=rnd.getrandbits(32), v=rnd.getrandbits(32),
                    ustep=rnd.getrandbits(20), vstep=rnd.getrandbits(20),
                    umask=tw - 1, vmask=th - 1, vshift=shift,
                    tex=tex, dst_in=dst_in, n=n)
        if H.run_original(**args) != run_rewrite(**args):
            bad += 1
            if bad <= 3:
                print('MISMATCH trial', t)
    print('%d/%d trials match' % (trials - bad, trials))
    return bad == 0


if __name__ == '__main__':
    sys.exit(0 if main() else 1)
