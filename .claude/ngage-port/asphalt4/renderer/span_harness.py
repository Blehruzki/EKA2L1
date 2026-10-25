"""span_harness.py -- run Asphalt 4's ORIGINAL textured span inner loop (Thumb at
code +3357A..+3361C) under unicorn with controlled inputs, and compare its output
to span_model.py. Proves the decode is correct. venv/bin/python (needs unicorn).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import span_model
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R4, UC_ARM_REG_R7,
                               UC_ARM_REG_R12, UC_ARM_REG_LR, UC_ARM_REG_SP)

CODEBASE = 0x8000
LOOP = 0x3357A
STOP = 0x3361C
# a4.code is the exe's extracted code section (not checked in -- it is game
# data). Regenerate it from the user's asphalt4_ngi.exe and point A4_CODE at it.
CODE = open(os.environ.get('A4_CODE', 'a4.code'), 'rb').read()

STACK, TEX, DST, COV = 0x70000000, 0x60000000, 0x61000000, 0x62000000
SP = STACK + 0x8000


def run_original(light, u, v, ustep, vstep, umask, vmask, vshift, thresh, lightmask, tex, n):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0, 0x200000)                                  # code
    uc.mem_write(CODEBASE, CODE)
    for base in (STACK, TEX, DST, COV):
        uc.mem_map(base, 0x100000)
    uc.mem_write(TEX, b''.join(struct.pack('<H', t) for t in tex))
    # stack slots the loop reads (offsets from SP)
    def w(off, val): uc.mem_write(SP + off, struct.pack('<I', val & 0xFFFFFFFF))
    w(0x14, DST + 2 * n)        # end color ptr
    w(0x18, vstep); w(0x1c, ustep)
    w(0x20, lightmask); w(0x24, thresh)
    w(0x28, vshift); w(0x2c, vmask); w(0x30, umask)
    w(0x44, TEX)
    uc.reg_write(UC_ARM_REG_SP, SP)
    uc.reg_write(UC_ARM_REG_R7, light & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R12, u & 0xFFFFFFFF)             # ip = u accum
    uc.reg_write(UC_ARM_REG_LR, v & 0xFFFFFFFF)              # lr = v accum
    uc.reg_write(UC_ARM_REG_R4, DST)
    uc.reg_write(UC_ARM_REG_R0, COV)
    uc.emu_start(CODEBASE + LOOP + 1, CODEBASE + STOP, count=n * 200 + 1000)
    raw = uc.mem_read(DST, 2 * n)
    return list(struct.unpack('<%dH' % n, raw))


def main(trials=300):
    rnd = random.Random(7)
    bad = 0
    for t in range(trials):
        shift = rnd.choice([4, 5, 6])          # texwidth log2
        tw, th = 1 << shift, 1 << rnd.choice([4, 5, 6])
        tex = [rnd.getrandbits(16) for _ in range(tw * th)]
        umask, vmask = tw - 1, th - 1
        n = rnd.randint(1, 40)
        args = dict(light=rnd.getrandbits(32), u=rnd.getrandbits(32), v=rnd.getrandbits(32),
                    ustep=rnd.getrandbits(20), vstep=rnd.getrandbits(20),
                    umask=umask, vmask=vmask, vshift=shift,
                    thresh=rnd.randint(1, 64), lightmask=rnd.choice([0x1f, 0x3f, 0x7f]),
                    tex=tex, n=n)
        got = run_original(**args)
        exp = span_model.span(**args)
        if got != exp:
            bad += 1
            if bad <= 3:
                print('MISMATCH trial %d n=%d\n  orig %s\n  model%s' % (t, n, got[:8], exp[:8]))
    print('%d/%d trials match' % (trials - bad, trials))
    return bad == 0


if __name__ == '__main__':
    sys.exit(0 if main() else 1)
