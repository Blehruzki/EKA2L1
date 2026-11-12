# objorder.py ldump.bin [frames]: round 60 -- the scenery and cars drawn object by object (a GPU-native renderer: a mesh,
# a matrix, a draw), replayed against the list as drawn now (tools/listcmp.py's rule A: the game's table order, texz
# GEQUAL, the rest ALWAYS), the face shown at every pixel compared. From A4_LISTDUMP with each face's mesh call (a4_gl.c,
# round 60: one call of the mesh routine = one object or one car part). Variants:
#   D     every face depth-tested (no order: round 58's rule D)
#   S:k   objects far to near by key k, each in its own depth slice (a later object always in front of an earlier one),
#         depth-tested inside the object -- glDepthRange per object on the GPU
#   P:k   objects far to near by key k, each object's faces painted in their own order (no depth test)
# keys: far = the object's deepest point (ZW), near = its nearest, mid = the mean of its faces' ranges
import sys, struct, collections, numpy as np
sys.argv_saved = list(sys.argv)
src = open(__file__.replace('objorder.py', 'listcmp.py')).read().replace('\nmain()\n', '\n')
exec(src)
VP_W, VP_H = 320, 240
def raster2(order, P, F, cx, cy):
    """order: [(face, mode, rank)]: mode 0 painted (always), 1 depth-tested; rank the object's slice (0 = none)."""
    dep = np.full((VP_H, VP_W), -1, dtype=np.int64); who = np.full((VP_H, VP_W), -1, dtype=np.int32)
    ys, xs = np.mgrid[0:VP_H, 0:VP_W]; px = xs + 0.5; py = ys + 0.5; kz = ZK * (1 - 1 / 64.0)
    for it in order:
        i, tested, rank = it[0], it[1], it[2]; sc = it[3] if len(it) > 3 else 1.0
        c = P[i].astype(np.float64); w = -c[:, 2]
        sx = cx + F * c[:, 0] / w; sy = cy - F * c[:, 1] / w; iw = 1.0 / w
        x0, x1 = int(max(0, np.floor(sx.min()))), int(min(VP_W - 1, np.ceil(sx.max())))
        y0, y1 = int(max(0, np.floor(sy.min()))), int(min(VP_H - 1, np.ceil(sy.max())))
        if x0 > x1 or y0 > y1: continue
        X = px[y0:y1 + 1, x0:x1 + 1]; Y = py[y0:y1 + 1, x0:x1 + 1]
        area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0])
        if area == 0: continue
        e0 = ((sx[2] - sx[1]) * (Y - sy[1]) - (sy[2] - sy[1]) * (X - sx[1])) / area
        e1 = ((sx[0] - sx[2]) * (Y - sy[2]) - (sy[0] - sy[2]) * (X - sx[2])) / area
        e2 = 1.0 - e0 - e1
        m = (e0 > 0) & (e1 > 0) & (e2 > 0)
        if not m.any(): continue
        inv = (e0 * iw[0] + e1 * iw[1] + e2 * iw[2]) * sc     # sc > 1: drawn as if nearer (a depth bias)
        dq = np.floor((0.5 + 0.5 * kz * inv) * 16777215.0).astype(np.int64) + (np.int64(rank) << 26)
        D = dep[y0:y1 + 1, x0:x1 + 1]; Wh = who[y0:y1 + 1, x0:x1 + 1]
        if tested: m &= dq >= D
        D[m] = dq[m]; Wh[m] = i
    return who
fs = load(sys.argv_saved[1]); lim = int(sys.argv_saved[2]) if len(sys.argv_saved) > 2 else 40
tot = collections.Counter(); shown = 0; nf = 0; objs = []
for h, V, T, Kb, Zb, ext, O in fs[:lim]:
    _, n, F, cx, cy, zbits, frame, nk = h
    if n * 36 > len(V) or O is None: continue
    F = max(1, min(16383, F))
    Pv = np.frombuffer(V[:n * 36], dtype='<i2').reshape(n, 18); P = np.stack([Pv[:, 0:3], Pv[:, 6:9], Pv[:, 12:15]], axis=1)
    Tw = struct.unpack_from('<%dI' % n, T, 0); Bx = [struct.unpack_from('<4h', T, 0x2000 + 8 * i) for i in range(n)]
    ORD = struct.unpack_from('<%dH' % n, T, 0x7000); Zz = [struct.unpack_from('<2H', Zb, 4 * i) for i in range(n)]
    ob = struct.unpack_from('<%dH' % n, O, 0)
    bt = group(n, 'A', Kb[:n], Tw, Bx, ORD, Zz, zbits >= 24)
    a = raster2([(i, R['k'] >> 8, 0) for runs in bt for R in runs for i in R['faces']], P, F, cx, cy)
    vis = a >= 0; shown += int(vis.sum()); nf += 1
    faces = collections.defaultdict(list)
    for i in range(n): faces[ob[i]].append(i)
    objs.append(len(faces))
    key = {'far': lambda fl: max(Zz[i][1] for i in fl), 'near': lambda fl: min(Zz[i][0] for i in fl),
           'mid': lambda fl: sum(Zz[i][0] + Zz[i][1] for i in fl) / (2.0 * len(fl))}
    res = {'D': raster2([(i, 1, 0) for i in range(n)], P, F, cx, cy)}
    for N_, e_ in ((8, 1 / 64.0), (16, 1 / 64.0), (16, 1 / 32.0), (32, 1 / 32.0), (16, 1 / 16.0)):
        small = {o for o in faces if len(faces[o]) <= N_}
        res['D+%d/%d' % (N_, round(1 / e_))] = raster2([(i, 1, 0, 1.0 + e_ if ob[i] in small else 1.0) for i in range(n)], P, F, cx, cy)
    for kn, kf in ([] if 'noobj' in sys.argv_saved else key.items()):
        order = sorted(faces, key=lambda o: -kf(faces[o]))          # far (large ZW) first
        res['S:' + kn] = raster2([(i, 1, r + 1) for r, o in enumerate(order) for i in faces[o]], P, F, cx, cy)
        res['P:' + kn] = raster2([(i, 0, 0) for o in order for i in faces[o]], P, F, cx, cy)
    line = []
    for k, w in res.items():
        d = int((w != a).sum()); tot[k] += d; line.append('%s %d' % (k, d))
    print('frame %5d: %4d faces in %3d meshes, %5d px | changed: %s' % (frame, n, len(faces), int(vis.sum()), ', '.join(line)))
if nf:
    print('%d frames, %d list pixels, meshes a frame %.0f; pixels changed against the list as drawn now:' % (nf, shown, sum(objs) / nf))
    for k in tot: print('  %-7s %6d  %.2f%%' % (k, tot[k], 100.0 * tot[k] / max(1, shown)))
