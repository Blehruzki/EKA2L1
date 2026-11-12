# camrate.py crec.bin: the chase camera's yaw as drawn (A4_CAMYAWRATE carries the game camera's offset from the car on
# by its last change while the camera falls behind) against the same frames without it. A recording of a build without
# A4_CAMYAWRATE: add 'nocarry' -- then 'without' is the yaw as drawn and 'with' the carry replayed on it against the same frames without the carry, both measured against the line between the
# game's camera yaws at the steps around each frame; and one-frame yaw reversals of each
import sys, struct, collections
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
d = open(sys.argv[1], 'rb').read(); o = 0; cur = None; recs = []
nocarry = len(sys.argv) > 2 and sys.argv[2] == 'nocarry'
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: cur = [b]; recs.append(cur)
    elif cur is not None: cur.append(b)
CAM = {}; PL = {}; FR = []
for r in recs:
    h = struct.unpack_from('<8I', r[0])
    if len(r) < 3: continue
    w = lambda b, off: s32(struct.unpack_from('<I', b, off)[0])
    if h[0] == 0x43455243: CAM[h[1]] = (w(r[2], 0x68) & 2047, w(r[2], 0x94)); PL[h[1]] = w(r[1], 0x678) & 2047
    elif h[0] == 0x444B5246 and ((h[2] >> 24) & 0x7F) == 0:
        FR.append(dict(step=h[1], q=(h[2] & 0xFFFFFF) / 65536.0, yaw=w(r[2], 0x68) & 2047, mode=w(r[2], 0x94)))
def carry(k):
    if not all(j in CAM and j in PL and CAM[j][1] == 2 for j in (k, k - 1, k - 2)): return 0
    o0, o1, o2 = (wrap(CAM[j][0] - PL[j]) for j in (k, k - 1, k - 2))
    g1, g2 = wrap(o0 - o1), wrap(o1 - o2)
    return g1 if (abs(o0) > abs(o1) > abs(o2) and g1 * o0 > 0 and g2 * o1 > 0 and 4 <= abs(g1) <= 64
                  and abs(g1 - g2) <= abs(g1) // 2 + 2) else 0     # round 59: while the camera falls behind (a4_gl.c)
err = {'with': [], 'without': []}; seq = {'with': [], 'without': []}; carried = {'with': [], 'without': []}
for f in FR:
    if f['mode'] != 2: continue
    a = CAM.get(f['step']); b = CAM.get(f['step'] + 1)
    g = carry(f['step']); q = min(f['q'], 1.0)
    yw = (f['yaw'] + int(g * q)) & 2047 if nocarry else f['yaw']
    alt = f['yaw'] if nocarry else (f['yaw'] - int(g * q)) & 2047
    seq['with'].append(yw); seq['without'].append(alt)
    if not a or not b or a[1] != 2 or b[1] != 2: continue
    line = a[0] + wrap(b[0] - a[0]) * q
    err['with'].append(abs(wrap(int(round(yw - line))))); err['without'].append(abs(wrap(int(round(alt - line)))))
    if int(g * q): carried['with'].append(err['with'][-1]); carried['without'].append(err['without'][-1])
pq = lambda a, p: sorted(a)[min(len(a) - 1, int(p * len(a)))] if a else 0
for k in ('without', 'with'):
    s = seq[k]; rev = sum(1 for i in range(1, len(s) - 1) if abs(wrap(s[i] - s[i - 1])) >= 4 and abs(wrap(s[i + 1] - s[i])) >= 4 and wrap(s[i] - s[i - 1]) * wrap(s[i + 1] - s[i]) < 0)
    e = err[k]
    print('%-8s chase frames %d: yaw off the game camera\'s path 50%% %d 90%% %d 99%% %d max %d | one-frame reversals (>=4) %d' % (k, len(s), pq(e, .5), pq(e, .9), pq(e, .99), max(e or [0]), rev))
if carried['with']:
    print('frames the carry moved (%d): yaw off the game camera\'s path, mean with %.1f, without %.1f; max with %d, without %d'
          % (len(carried['with']), sum(carried['with']) / len(carried['with']), sum(carried['without']) / len(carried['without']),
             max(carried['with']), max(carried['without'])))
print('frames with a carry:', sum(1 for f in FR if f['mode'] == 2 and carry(f['step']) != 0), 'of', sum(1 for f in FR if f['mode'] == 2))
