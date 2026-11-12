import sys, struct, collections, numpy as np
__file__ = '/home/user/EKA2L1/.claude/ngage-port/asphalt4/tools/objorder.py'
src = open(__file__).read().split("fs = load(sys.argv_saved[1])")[0]
sys.argv = ['x', 'ldump60.bin']; exec(src)
fs = load('ldump60.bin'); stats = collections.Counter(); gaps = []; angs = []
def plane(c):
    c = c.astype(np.float64); n = np.cross(c[1] - c[0], c[2] - c[0]); l = np.linalg.norm(n)
    return (n / l, np.dot(n / l, c[0])) if l > 0 else (None, None)
for h, V, T, Kb, Zb, ext, O in fs[:30]:
    _, n, F, cx, cy, zbits, frame, nk = h
    if n * 36 > len(V) or O is None: continue
    F = max(1, min(16383, F))
    Pv = np.frombuffer(V[:n * 36], dtype='<i2').reshape(n, 18); P = np.stack([Pv[:, 0:3], Pv[:, 6:9], Pv[:, 12:15]], axis=1)
    Tw = struct.unpack_from('<%dI' % n, T, 0); Bx = [struct.unpack_from('<4h', T, 0x2000 + 8 * i) for i in range(n)]
    ORD = struct.unpack_from('<%dH' % n, T, 0x7000); Zz = [struct.unpack_from('<2H', Zb, 4 * i) for i in range(n)]
    ob = struct.unpack_from('<%dH' % n, O, 0)
    bt = group(n, 'A', Kb[:n], Tw, Bx, ORD, Zz, zbits >= 24)
    a = raster2([(i, R['k'] >> 8, 0) for runs in bt for R in runs for i in R['faces']], P, F, cx, cy)
    d = raster2([(i, 1, 0) for i in range(n)], P, F, cx, cy)
    m = (a != d) & (a >= 0) & (d >= 0)
    seen = set()
    for wa, wd in zip(a[m].tolist(), d[m].tolist()):
        if ob[wa] != ob[wd] or (wa, wd) in seen: continue
        seen.add((wa, wd))
        na, da = plane(P[wa]); nd, dd = plane(P[wd])
        if na is None or nd is None: stats['degenerate'] += 1; continue
        ang = np.degrees(np.arccos(min(1.0, abs(float(np.dot(na, nd))))))
        angs.append(ang)
        if ang < 5: gaps.append(abs(float(np.dot(na, P[wa].astype(np.float64).mean(0)) - da) + 0.0) if False else abs(float(np.dot(nd, P[wa].astype(np.float64).mean(0)) - dd)))
        stats['parallel (<5 deg)' if ang < 5 else 'angled'] += 1
print('same-mesh face pairs that change:', dict(stats))
g = sorted(gaps); q = lambda p: g[min(len(g) - 1, int(p * len(g)))] if g else 0
print('parallel pairs: distance between the two planes (view units) 50%% %.1f 90%% %.1f max %.1f' % (q(.5), q(.9), g[-1] if g else 0))
a_ = sorted(angs); print('angles between the pair: 50%% %.0f 90%% %.0f' % (a_[len(a_)//2], a_[int(.9*len(a_))]))
