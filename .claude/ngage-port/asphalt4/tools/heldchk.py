# heldchk.py crec.bin: frames whose every car is drawn exactly at the step (x, y, z, heading == the step's), against q
import sys, struct, collections
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
d = open(sys.argv[1], 'rb').read(); o = 0; cur = None; recs = []
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: cur = [b]; recs.append(cur)
    elif cur is not None: cur.append(b)
fr = collections.defaultdict(list)
for r in recs:
    h = struct.unpack_from('<8I', r[0])
    if h[0] != 0x444B5246 or len(r) < 2: continue
    w = lambda off: s32(struct.unpack_from('<I', r[1], off)[0])
    key = (h[1], h[2] & 0xFFFFFF)
    fr[key].append((w(4), w(8), w(0xC), w(0x678) & 2047) == (s32(h[5]), s32(h[3]), s32(h[6]), s32(h[7]) & 2047))
held = [k for k, v in fr.items() if all(v) and len(v) >= 4]
late = [k for k in held if k[1] / 65536 > 0.3]
print('frames %d; every car exactly at the step %d (of them at q > 0.3: %d, the hold)' % (len(fr), len(held), len(late)))
