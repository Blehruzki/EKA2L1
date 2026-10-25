"""palconv_harness.py -- run Asphalt 4's ORIGINAL per-pixel palette->RGB565
conversion block (Thumb at code +298D8..+29920, inside the sprite blitter) under
unicorn with controlled inputs, and compare it to palconv_model.py. Proves the
conversion is a pure function of (palette, index) -- the premise of the LUT
hoist. Needs unicorn and A4_CODE (see span_harness.py).

The block reads a small struct graph: r7 (context) +0xc byte, +0x24 short,
+0x14 -> palette-bank ptr; r4 (sprite) +0xb byte; the bank selects a palette by
(sprite[+0xb]*ctx[+0xc]+ctx[+0x24]); palette +0xc short = colorkey index, +8 ->
colour table (3 bytes/entry). We pin the selector to palette 0 and feed random
colour tables + indices. r12 (ip) = palette index in; r2 (low 16) = rgb565 out;
r5 = coverage (0 iff colorkey==index, else its entry value).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import palconv_model
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_R2, UC_ARM_REG_R4, UC_ARM_REG_R5,
                               UC_ARM_REG_R7, UC_ARM_REG_R12, UC_ARM_REG_SP)

CODEBASE, START, STOP = 0x8000, 0x298D8, 0x29920
SPR, CTX, BANK, PAL, COLORTAB, STK = (0x10000000, 0x11000000, 0x12000000,
                                      0x13000000, 0x14000000, 0x70000000)
SP = STK + 0x8000
CODE = open(os.environ.get('A4_CODE', 'a4.code'), 'rb').read()


def run_original(colortab, i, colorkey, entry_r5):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0, 0x200000)
    uc.mem_write(CODEBASE, CODE)
    for b in (SPR, CTX, BANK, PAL, COLORTAB, STK):
        uc.mem_map(b, 0x100000)
    uc.mem_write(CTX + 0xc, b'\x00')                       # ctx[+0xc]  = 0
    uc.mem_write(CTX + 0x24, struct.pack('<h', 0))         # ctx[+0x24] = 0
    uc.mem_write(CTX + 0x14, struct.pack('<I', BANK))      # ctx[+0x14] -> bank
    uc.mem_write(SPR + 0xb, b'\x01')                       # sprite[+0xb] = 1
    uc.mem_write(BANK, struct.pack('<I', PAL))             # bank[0] -> palette
    uc.mem_write(PAL + 0xc, struct.pack('<h', colorkey))   # palette[+0xc] colorkey
    uc.mem_write(PAL + 8, struct.pack('<I', COLORTAB))     # palette[+8] -> colortab
    uc.mem_write(COLORTAB, bytes(colortab))
    uc.reg_write(UC_ARM_REG_R4, SPR)
    uc.reg_write(UC_ARM_REG_R7, CTX)
    uc.reg_write(UC_ARM_REG_R12, i)
    uc.reg_write(UC_ARM_REG_R5, entry_r5)
    uc.reg_write(UC_ARM_REG_SP, SP)
    uc.emu_start(CODEBASE + START + 1, CODEBASE + STOP, count=200)
    return (uc.reg_read(UC_ARM_REG_R2) & 0xFFFF, uc.reg_read(UC_ARM_REG_R5))


def main(trials=3000):
    rnd = random.Random(5)
    bad = 0
    for t in range(trials):
        colortab = [rnd.getrandbits(8) for _ in range(256 * 3)]
        i = rnd.randint(0, 255)
        colorkey = rnd.choice([i, rnd.randint(0, 255), -1])
        entry_r5 = rnd.choice([0xf, 5, 0])
        got = run_original(colortab, i, colorkey, entry_r5)
        exp = (palconv_model.convert(colortab, i), 0 if colorkey == i else entry_r5)
        if got != exp:
            bad += 1
            if bad <= 5:
                print('MISMATCH i=%d key=%d r5=%d got=%s exp=%s' % (i, colorkey, entry_r5, got, exp))
    print('%d/%d trials match' % (trials - bad, trials))
    return bad == 0


if __name__ == '__main__':
    sys.exit(0 if main() else 1)
