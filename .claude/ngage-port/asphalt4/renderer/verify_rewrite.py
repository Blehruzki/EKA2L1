"""verify_rewrite.py <rewrite.bin> -- run an ARM rewrite of the textured span
filler under unicorn and check it bit-exact against the ORIGINAL loop on the
same random inputs. Works for both the C-compiled (a4_span_textured.c) and the
hand-written (a4_span_textured.s) rewrites: both expose the AAPCS function

  a4_span_textured(dst, cov, n, u, v, ustep, vstep, umask, vmask, vshift,
                   tex, light, thresh, lightmask)

Build the .bin first, e.g.
  clang --target=armv5te-none-eabi -marm -O2 -ffreestanding -fno-builtin -fropi \
        -c a4_span_textured.c -o o && llvm-objcopy -O binary --only-section=.text o r.bin
and run with A4_CODE pointing at the exe's extracted code section (see
span_harness.py)."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import span_harness as H
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR)

# bases spaced far apart so the 1 MiB regions never overlap
FN, STK, TEX, DST, COV, RET = 0x400000, 0x1000000, 0x2000000, 0x3000000, 0x4000000, 0x5000000
BIN = open(sys.argv[1], 'rb').read()


def run_rewrite(light, u, v, ustep, vstep, umask, vmask, vshift, thresh, lightmask, tex, n):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for base in (FN, STK, TEX, DST, COV):
        uc.mem_map(base, 0x100000)
    uc.mem_map(RET & ~0xFFF, 0x1000)
    uc.mem_write(FN, BIN)
    uc.mem_write(TEX, b''.join(struct.pack('<H', t) for t in tex))
    sp = STK + 0x8000
    stackargs = [v, ustep, vstep, umask, vmask, vshift, TEX, light, thresh, lightmask]
    for i, a in enumerate(stackargs):
        uc.mem_write(sp + 4 * i, struct.pack('<I', a & 0xFFFFFFFF))
    uc.reg_write(UC_ARM_REG_R0, DST)
    uc.reg_write(UC_ARM_REG_R1, COV)
    uc.reg_write(UC_ARM_REG_R2, n)
    uc.reg_write(UC_ARM_REG_R3, u & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_SP, sp)
    uc.reg_write(UC_ARM_REG_LR, RET)
    uc.emu_start(FN, RET, count=n * 200 + 2000)
    return list(struct.unpack('<%dH' % n, uc.mem_read(DST, 2 * n)))


def main(trials=2000):
    rnd = random.Random(99)
    bad = 0
    for t in range(trials):
        shift = rnd.choice([4, 5, 6]); tw = 1 << shift; th = 1 << rnd.choice([4, 5, 6])
        tex = [rnd.getrandbits(16) for _ in range(tw * th)]
        a = dict(light=rnd.getrandbits(32), u=rnd.getrandbits(32), v=rnd.getrandbits(32),
                 ustep=rnd.getrandbits(20), vstep=rnd.getrandbits(20),
                 umask=tw - 1, vmask=th - 1, vshift=shift,
                 thresh=rnd.randint(1, 64), lightmask=rnd.choice([0x1f, 0x3f, 0x7f]),
                 tex=tex, n=rnd.randint(1, 40))
        if H.run_original(**a) != run_rewrite(**a):
            bad += 1
            if bad <= 3:
                print('MISMATCH trial', t)
    print('%d/%d trials match' % (trials - bad, trials))
    return bad == 0


if __name__ == '__main__':
    sys.exit(0 if main() else 1)
