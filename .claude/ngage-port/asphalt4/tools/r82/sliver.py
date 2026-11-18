# sliver.py <dirs...>: of the CUT1 records, how many are full-width slivers on screen -- covering 300+ px of some row and
# at most 8 rows -- drawn from the positions used (true) and from the int16 words (as r82e drew).
import sys, glob, struct, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wrapcov import cover   # noqa
s32 = lambda u: u - (1 << 32) if u & 0x80000000 else u
s16 = lambda u: u - 65536 if u & 0x8000 else u
def sliver(c):
    if not c: return False
    rows = {}
    for x, y in c: rows[y] = rows.get(y, 0) + 1
    return len(rows) <= 8 and max(rows.values()) >= 300
recs = {}
for d in sys.argv[1:]:
    for f in glob.glob(d + '/*.dat') + glob.glob(d + '/tel/*.dat'):
        b = open(f, 'rb').read(); i = b.rfind(b'CUT1')
        if i < 0 or len(b) < i + 8 + 16 * 96: continue
        tot = struct.unpack_from('<I', b, i + 4)[0]
        for k in range(min(tot, 16)):
            w = struct.unpack_from('<24I', b, i + 8 + k * 96)
            if w[0] or w[1]: recs[w] = 1
nw = nt = sw_true = sw_old = sn_true = 0
for w in recs:
    wrp = (w[1] >> 24) & 15
    true = [(s32(w[17 + 2 * v]) / 16, s32(w[18 + 2 * v]) / 16) for v in range(3)]
    old = [(s16(w[2 + 5 * v] & 0xFFFF), s16(w[2 + 5 * v] >> 16)) for v in range(3)]
    if wrp:
        nw += 1; sw_true += sliver(cover(true)); sw_old += sliver(cover(old))
    else:
        nt += 1; sn_true += sliver(cover(true))
print('not wrapped: %d triangles, full-width slivers %d | wrapped: %d triangles, slivers as r82g draws %d, as r82e drew %d' % (nt, sn_true, nw, sw_true, sw_old))
