"""equiv.py <original.exe> <patched.exe> -- run a patched function and the original on the same inputs.

Both images' code sections are mapped at their link address (0x8000), where
their absolute words are already right, so no relocation is applied. A call
enters at the ORIGINAL function's address in Thumb state, so the patched image
is tested through its entry veneer. Each case gets fresh identical memory; the
buffers the function writes must come out byte for byte the same.
Run with the venv that has unicorn: scratchpad venv/bin/python.
"""
import random, struct, sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UcError
from unicorn.arm_const import *

CODE, DATA, STACK, HEAP, RET = 0x8000, 0x400000, 0x7F000000, 0x10000000, 0x7E000000

def load(path):
    d = open(path, 'rb').read()
    u = lambda o: struct.unpack_from('<I', d, o)[0]
    return d[u(0x64):u(0x64) + u(0x30)], d[u(0x68):u(0x68) + u(0x34)]

class Machine:
    def __init__(self, image):
        code, data = image
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.uc.mem_map(CODE, (len(code) + 0xFFFF) & ~0xFFFF)
        self.uc.mem_write(CODE, code)
        self.uc.mem_map(DATA, 0x100000)
        self.uc.mem_write(DATA, data)
        self.uc.mem_map(STACK - 0x100000, 0x100000)
        self.uc.mem_map(HEAP, 0x1000000)
        self.uc.mem_map(RET, 0x1000)
        self.uc.mem_write(RET, b'\x70\x47' * 8)          # bx lr, never reached
        self.count = 0
    def call(self, off, args, limit=200_000_000):
        regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
        sp = STACK - 0x1000
        extra = args[4:]
        sp -= 4 * len(extra)
        for i, a in enumerate(extra):
            self.uc.mem_write(sp + 4 * i, struct.pack('<I', a & 0xFFFFFFFF))
        for r, a in zip(regs, args):
            self.uc.reg_write(r, a & 0xFFFFFFFF)
        self.uc.reg_write(UC_ARM_REG_SP, sp)
        self.uc.reg_write(UC_ARM_REG_LR, RET | 1)
        self.uc.emu_start((CODE + off) | 1, RET, count=limit)
        return self.uc.reg_read(UC_ARM_REG_R0)

def surface(m, w, h, pitch, rnd):
    """the fields +2ABAA reads: pixels at +4, pitch (u16) at +8, *width at +0x4C, *height at +0x50"""
    s, wp, hp, buf = HEAP, HEAP + 0x100, HEAP + 0x104, HEAP + 0x1000
    blob = bytearray(0x60)
    struct.pack_into('<I', blob, 4, buf)
    struct.pack_into('<H', blob, 8, pitch)
    struct.pack_into('<I', blob, 0x4C, wp)
    struct.pack_into('<I', blob, 0x50, hp)
    m.uc.mem_write(s, bytes(blob))
    m.uc.mem_write(wp, struct.pack('<ii', w, h))
    pixels = bytes(rnd.getrandbits(8) for _ in range(pitch * h * 2))
    m.uc.mem_write(buf, pixels)
    return s, buf

def test_fill(orig, new, cases):
    rnd = random.Random(1)
    for k in range(cases):
        w, h = rnd.choice([(320, 240), (240, 320), (17, 9), (1, 1)])
        pitch = w + rnd.choice([0, 0, 8])
        x, y = rnd.randint(-w // 2, w + 4), rnd.randint(-h // 2, h + 4)
        rw, rh = rnd.randint(-3, w + 8), rnd.randint(-3, h + 8)
        colour = rnd.getrandbits(32) if rnd.random() < 0.2 else rnd.getrandbits(16)
        alpha = rnd.choice(list(range(17)) + [rnd.getrandbits(32)])
        out = []
        seed = rnd.getrandbits(32)
        for img in (orig, new):
            m = Machine(img)
            s, buf = surface(m, w, h, pitch, random.Random(seed))
            m.call(0x2ABAA, [s, x, y, rw, rh, colour, alpha])
            out.append(bytes(m.uc.mem_read(buf, pitch * h * 2)))
        if out[0] != out[1]:
            print('MISMATCH case %d: surf %dx%d pitch %d rect (%d,%d %dx%d) colour %x alpha %x' % (k, w, h, pitch, x, y, rw, rh, colour, alpha))
            return False
    print('fill +2ABAA: %d cases identical' % cases)
    return True

if __name__ == '__main__':
    orig, new = load(sys.argv[1]), load(sys.argv[2])
    ok = test_fill(orig, new, int(sys.argv[3]) if len(sys.argv) > 3 else 200)
    sys.exit(0 if ok else 1)
