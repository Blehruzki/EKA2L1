"""Diagnostic: find who stops the music, ignoring the start routine's own two stops.

The stop wrapper's `push {r4,lr}` leaves the caller's return address at [sp,#4].
The probe compares its low 16 bits against the two sites inside the start routine
and, for anything else, touches an unmappable address: the emulator then logs the
access violation with every register, and r3 is the call site.
"""
import struct
from thumb import *
import e32crc

CODE_OFF = 156
PATCH_AT = 0xdabe                  # movs r4,r0 / ldr r0,[r0,#0x14]
SKIP = (0x0893, 0x0825)            # the start routine's own stops, low 16 bits
MAGIC = 0xDEAD0000

src = open('asphalt2_full_patched_audio.exe', 'rb').read()
img = bytearray(src)
u = lambda o: struct.unpack_from('<I', img, o)[0]

cave = u(0x60)
extra = 48
text_end = CODE_OFF + cave
img = bytearray(bytes(img[:text_end]) + bytes(extra) + bytes(img[text_end:]))
for off in (0x30, 0x60, 0x7c):
    struct.pack_into('<I', img, off, u(off) + extra)
for off in (0x68, 0x6c, 0x70, 0x74):
    if u(off):
        struct.pack_into('<I', img, off, u(off) + extra)

ins, addr = [], cave
def emit(hw):
    global addr
    ins.append(hw); addr += 2

# layout first to know where the pool lands
n_ins = 14
pool = cave + n_ins * 2
pool += (4 - pool % 4) % 4
out_at = cave + 22

emit(ldr_sp(3, 4))                                  # r3 = caller return address
emit(lsls(2, 3, 16)); emit(lsrs2 := 0x0C12)         # r2 = r3 & 0xffff  (lsrs r2,r2,#16)
emit(ldr_pc(1, cave + 6, pool)); emit(cmp_r(2, 1)); emit(beq(cave + 10, out_at))
emit(ldr_pc(1, cave + 12, pool + 4)); emit(cmp_r(2, 1)); emit(beq(cave + 16, out_at))
emit(ldr_pc(1, cave + 18, pool + 8)); emit(ldr_r(1, 1, 0))      # fault
emit(mov_r(4, 0))                                   # movs r4,r0   (replaced)
emit(ldr_r(0, 0, 0x14))                             # ldr r0,[r0,#0x14] (replaced)
emit(bx_lr())

body = b''.join(struct.pack('<H', h) for h in ins)
body += b'\0' * (pool - (cave + len(body)))
body += struct.pack('<III', SKIP[0], SKIP[1], MAGIC)
img[CODE_OFF + cave:CODE_OFF + cave + len(body)] = body
img[CODE_OFF + PATCH_AT:CODE_OFF + PATCH_AT + 4] = bl(PATCH_AT, cave)

out = e32crc.fix(bytes(img))
assert e32crc.stored(out) == e32crc.compute(out)
open('asphalt2_probe.exe', 'wb').write(out)
print('probe cave at %#x, out at %#x, pool at %#x' % (cave, out_at, pool))
