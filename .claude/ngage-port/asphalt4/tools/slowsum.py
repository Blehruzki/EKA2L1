# slowsum.py <dir>: round 34's slowest frames and lossy frames (A4_SLOWREC), from the latest
# capture: each frame's ms split by phase (ticks are NTickCount, 1 ms on the N95).
import sys, struct, glob
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384
best = None
for f in glob.glob(sys.argv[1] + '/a4tel*.dat'):
    d = open(f, 'rb').read()
    if len(d) < OFF + 0x680: continue
    cap = struct.unpack_from('<I', d, 8)[0]
    if best is None or cap > best[0]: best = (cap, d, f)
if not best: sys.exit('no slow-frame block')
cap, d, f = best
S = struct.unpack_from('<416I', d, OFF)
print('%s capidx %d: magic %08x, 3D frames seen %d' % (f.split('/')[-1], cap, S[0], S[1]))
cols = 'ms 3D hook comp 2D pres logic spec rcall pre tris list lost mode nodraw stale frame npres'.split()
def show(rows, title):
    print(title); print('  ' + ' '.join('%6s' % c for c in cols))
    for e in rows:
        if e[0]:
            v = list(e[:13]) + [e[13] & 0xFFFF, (e[13] >> 16) & 0x3FF, e[13] >> 26] + list(e[14:16])
            print('  ' + ' '.join('%6d' % x for x in v))
R = sorted([S[32 + i * 16: 48 + i * 16] for i in range(16)], key=lambda e: -e[0])
show(R, 'the 16 slowest 3D frames (ms; 3D = first triangle to composite, comp = the flush/composite, rcall = the game\'s render call, pre = its part before the first triangle)')
L = [S[288 + i * 16: 304 + i * 16] for i in range(8)]
show(sorted(L, key=lambda e: e[14]), 'frames with list triangles lost (the last 8)')
