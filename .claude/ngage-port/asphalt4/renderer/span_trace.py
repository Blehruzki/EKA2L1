"""span_trace.py -- single-step the ORIGINAL textured span inner loop for one
pixel under unicorn, logging registers at each instruction, to find where the
reimplementation diverges. No running exe needed."""
import os, struct, sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                               UC_ARM_REG_R4, UC_ARM_REG_R7, UC_ARM_REG_R12, UC_ARM_REG_LR,
                               UC_ARM_REG_SP)

CODEBASE, LOOP, STOP = 0x8000, 0x3357A, 0x3361C
CODE = open(os.environ.get('A4_CODE', 'a4.code'), 'rb').read()
STACK, TEX, DST, COV = 0x70000000, 0x60000000, 0x61000000, 0x62000000
SP = STACK + 0x8000

# deterministic single-pixel inputs
shift = 5; tw = 1 << shift; th = 32
tex = [((i * 2654435761) >> 13) & 0xFFFF for i in range(tw * th)]
args = dict(light=0x12345678, u=0x00123456, v=0x00654321, ustep=0, vstep=0,
            umask=tw - 1, vmask=th - 1, vshift=shift, thresh=20, lightmask=0x3f)

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
uc.mem_map(0, 0x200000); uc.mem_write(CODEBASE, CODE)
for b in (STACK, TEX, DST, COV): uc.mem_map(b, 0x100000)
uc.mem_write(TEX, b''.join(struct.pack('<H', t) for t in tex))
def w(o, v): uc.mem_write(SP + o, struct.pack('<I', v & 0xFFFFFFFF))
w(0x14, DST + 2); w(0x18, args['vstep']); w(0x1c, args['ustep'])
w(0x20, args['lightmask']); w(0x24, args['thresh'])
w(0x28, args['vshift']); w(0x2c, args['vmask']); w(0x30, args['umask']); w(0x44, TEX)
uc.reg_write(UC_ARM_REG_SP, SP)
uc.reg_write(UC_ARM_REG_R7, args['light'])
uc.reg_write(UC_ARM_REG_R12, args['u']); uc.reg_write(UC_ARM_REG_LR, args['v'])
uc.reg_write(UC_ARM_REG_R4, DST); uc.reg_write(UC_ARM_REG_R0, COV)

RN = {'r0': UC_ARM_REG_R0, 'r1': UC_ARM_REG_R1, 'r2': UC_ARM_REG_R2, 'r3': UC_ARM_REG_R3,
      'ip': UC_ARM_REG_R12, 'lr': UC_ARM_REG_LR, 'r7': UC_ARM_REG_R7}

def hook(uc, address, size, _):
    off = address - CODEBASE
    if LOOP <= off <= STOP:
        regs = ' '.join('%s=%08X' % (k, uc.reg_read(v)) for k, v in RN.items())
        print('%05X  %s' % (off, regs))

uc.hook_add(UC_HOOK_CODE, hook)
uc.emu_start(CODEBASE + LOOP + 1, CODEBASE + STOP, count=400)
print('OUT =', struct.unpack('<H', uc.mem_read(DST, 2))[0])
