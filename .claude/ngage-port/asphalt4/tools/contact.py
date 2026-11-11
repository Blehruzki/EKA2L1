# contact.py crec.bin slot lo hi: per game step (FRKD 'first' frames) the car's heading, its change,
# the contact clock +9FC, the contact forces +0F0/+0F8 and flags +B28 (a4 car record, 0xA00 bytes).
import sys, struct
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
fn, SL, LO, HI = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
d = open(fn, 'rb').read(); o = 0; cur = None; recs = []
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: cur = [b]; recs.append(cur)
    elif cur is not None: cur.append(b)
prev = None
for r in recs:
    h = struct.unpack_from('<8I', r[0])
    if len(r) < 3 or h[0] != 0x444B5246 or not (h[2] >> 31) or ((h[2] >> 24) & 0x7F) != SL: continue
    if not (LO <= h[1] <= HI): continue
    w = lambda off: s32(struct.unpack_from('<I', r[1], off)[0])
    hd = s32(h[7]) & 2047
    print('step %d hW %4d d %4s  9FC %8d  0F0 %6d 0F8 %6d' % (h[1], hd, '' if prev is None else wrap(hd - prev),
          w(0x9FC), w(0xF0), w(0xF8)))
    prev = hd
