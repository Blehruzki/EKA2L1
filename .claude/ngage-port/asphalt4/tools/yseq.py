import sys, struct, collections
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
d = open(sys.argv[1], 'rb').read(); slot = int(sys.argv[2]); lo, hi = int(sys.argv[3]), int(sys.argv[4])
o = 0; cur = None; recs = []
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: cur = [b]; recs.append(cur)
    elif cur is not None: cur.append(b)
for r in recs:
    h = struct.unpack_from('<8I', r[0])
    if h[0] != 0x444B5246 or ((h[2] >> 24) & 0x7F) != slot or not (lo <= h[1] <= hi): continue
    w = lambda off: s32(struct.unpack_from('<I', r[1], off)[0])
    wa = lambda off: s32(struct.unpack_from('<I', r[3], off - 0xA00)[0])
    print('step %d q %.2f %s | step y %5d +0D0 %5d | drawn y %5d gnd %5d | +014 %4d corr %+5.0f | +7D0 %3d +0E0 %3d +A18 %4d' % (
        h[1], (h[2] & 0xFFFFFF) / 65536.0, 'F' if h[2] >> 31 else ' ', s32(h[3]), s32(h[4]), w(8), w(0xD0), w(0x14), s32(h[5]) / 256.0, w(0x7D0), w(0xE0), wa(0xA18)))
