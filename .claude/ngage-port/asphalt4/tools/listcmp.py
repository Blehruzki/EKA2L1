# listcmp.py ldump.bin [frames]: round 58 -- the list's run grouping replayed offline under three rules, and the
# pictures compared pixel by pixel. From A4_LISTDUMP (a4_gl.c a4_list_emit; EKA2L1_PCMEM ats at it: r0 0x20, r1
# 0xFFFC, r2 0x8000, r2 0x800 at +0xC000, r3 0x2000): each dumped frame's list as mesh_draw gets it -- the faces'
# view corners (MESH_V), texture word (texz = bit 1), screen box, table order, key slot, ZW range.
#   A  the build's rule: a face joins an earlier run of its key only past runs whose box misses its own
#   B  A4_MESHZ (round 46): also past a run when the depth test makes the order irrelevant with the ranges apart
#   C  relaxed: also past any depth-tested run when the face is depth-tested itself (only exact depth ties differ)
#   D  every face depth-tested (GEQUAL), as C: the painted faces' table order no longer kept -- not the game's picture
# Per rule: the draw calls (runs) and, rasterised as the list draws -- texz GEQUAL, the rest ALWAYS, all writing a
# 24-bit depth of the GPU projection -- which face shows at each pixel. A face shown is its colour, so pixels
# whose face differs from rule A's are the pixels that would change.
import sys, struct, numpy as np
MAXT, REO_SCAN, MESH_NR, VP_W, VP_H, ZK = 2048, 32, 512, 320, 240, 128

def load(fn):
    d = open(fn, 'rb').read(); o = 0; recs = []
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; recs.append(d[o:o + ln]); o += ln
    out = []; j = 0
    while j + 4 < len(recs):
        h = recs[j]
        if len(h) < 0x20 or struct.unpack_from('<I', h, 0)[0] != 0x5453494C: j += 1; continue
        V, T, K, Z = recs[j + 1:j + 5]
        O = recs[j + 5] if j + 5 < len(recs) and len(recs[j + 5]) == 0x1000 else None   # round 60: each face's mesh call
        out.append((struct.unpack_from('<8i', h), V, T, K, Z, h[0x20:], O))   # h[0x20:]: log2 widths, heights (0x120 headers)
        j += 6 if O is not None else 5
    return out

def far(a, b): return a - b > (b >> 8) + 1          # MESHZ_FARTHER: a's ZW past b's by 1/256

def group(n, rule, K, T, B, ORD, Z, zok):
    runs = []; batches = []
    for t in range(n):
        i = ORD[t]; k = K[i]
        if k == 0xFF: continue
        k |= ((T[i] & 2) << 7) if rule != 'D' else 256    # D: every face depth-tested
        b = B[i]; tz = (k >> 8) & 1; fl, fh = Z[i]
        while True:
            r = len(runs) - 1; found = -1; steps = 0
            while r >= 0 and steps <= REO_SCAN:
                R = runs[r]
                if R['k'] == k: found = r; break
                rb = R['box']
                if b[2] < rb[0] or rb[2] < b[0] or b[3] < rb[1] or rb[3] < b[1]: r -= 1; steps += 1; continue
                if rule in 'BCD' and zok:
                    rz = R['k'] >> 8
                    if tz and rz and (rule in 'CD' or far(fl, R['zhi']) or far(R['zlo'], fh)): r -= 1; steps += 1; continue
                    if tz and not rz and far(fl, R['zhi']): r -= 1; steps += 1; continue
                    if not tz and rz and far(R['zlo'], fh): r -= 1; steps += 1; continue
                break
            if found < 0 and len(runs) == MESH_NR:
                batches.append(runs); runs = []; continue      # draw what is held, then retake this one
            break
        if found < 0:
            runs.append(dict(k=k, box=list(b), zlo=fl, zhi=fh, faces=[i]))
        else:
            R = runs[found]; R['faces'].append(i)
            R['zlo'] = min(R['zlo'], fl); R['zhi'] = max(R['zhi'], fh)
            R['box'] = [min(R['box'][0], b[0]), min(R['box'][1], b[1]), max(R['box'][2], b[2]), max(R['box'][3], b[3])]
    batches.append(runs)
    return batches

def raster(order, P, F, cx, cy):
    """order: [(face, tested)], P: per face 3x3 view corners. Returns the face shown per pixel (-1 none)."""
    dep = np.full((VP_H, VP_W), -1, dtype=np.int64); who = np.full((VP_H, VP_W), -1, dtype=np.int32)
    ys, xs = np.mgrid[0:VP_H, 0:VP_W]; px = xs + 0.5; py = ys + 0.5
    kz = ZK * (1 - 1 / 64.0)
    for i, tested in order:
        c = P[i].astype(np.float64); w = -c[:, 2]          # (int16 corners: float before the products)
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
        inv = e0 * iw[0] + e1 * iw[1] + e2 * iw[2]             # 1/w is affine in screen space
        dq = np.floor((0.5 + 0.5 * kz * inv) * 16777215.0).astype(np.int64)   # 24-bit window depth
        D = dep[y0:y1 + 1, x0:x1 + 1]; Wh = who[y0:y1 + 1, x0:x1 + 1]
        if tested: m &= dq >= D
        D[m] = dq[m]; Wh[m] = i
    return who, dep

def main():
    fs = load(sys.argv[1]); lim = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    gaps = {'tie': 0, 'close': 0, 'nearer': 0}
    tot = {r: [0, 0] for r in 'ABCD'}; px = {r: 0 for r in 'BCD'}; shown = 0; nf = 0
    for h, V, T, Kb, Zb, _, _o in fs[:lim]:
        _, n, F, cx, cy, zbits, frame, nk = h
        if n <= 0 or n * 36 > len(V): continue
        Pv = np.frombuffer(V[:n * 36], dtype='<i2').reshape(n, 18)
        P = np.stack([Pv[:, 0:3], Pv[:, 6:9], Pv[:, 12:15]], axis=1)
        Tw = struct.unpack_from('<%dI' % n, T, 0)
        Bx = [struct.unpack_from('<4h', T, 0x2000 + 8 * i) for i in range(n)]
        ORD = struct.unpack_from('<%dH' % n, T, 0x7000)
        Kk = Kb[:n]; Zz = [struct.unpack_from('<2H', Zb, 4 * i) for i in range(n)]
        zok = zbits >= 24
        res = {}; dr = {}; deps = {}
        for rule in 'ABCD':
            bt = group(n, rule, Kk, Tw, Bx, ORD, Zz, zok)
            order = [(i, R['k'] >> 8) for runs in bt for R in runs for i in R['faces']]
            res[rule], deps[rule] = raster(order, P, max(1, min(16383, F)), cx, cy)
            draws = sum(len(runs) for runs in bt); dr[rule] = draws
            tot[rule][0] += draws; tot[rule][1] += sum(1 for _ in order)
        a = res['A']; vis = a >= 0; shown += int(vis.sum()); nf += 1
        dm = res['D'] != a                                   # D's pixels that change: by how much nearer D's face is
        if dm.any():
            gap = (deps['D'][dm] - deps['A'][dm]) / 16777215.0
            for lim, key in ((1e-6, 'tie'), (1e-4, 'close'), (1e9, 'nearer')):
                hit = int(((gap <= lim) & (gap > (0 if key != 'tie' else -1))).sum()) if key != 'tie' else int((gap <= lim).sum())
                gaps[key] += hit if key == 'tie' else int(((gap <= lim) & (gap > (1e-6 if key == 'close' else 1e-4))).sum())
        d = {r: int((res[r] != a).sum()) for r in 'BCD'}
        for r in 'BCD': px[r] += d[r]
        print('frame %6d: %4d faces (%3d tested), %3d keys | draws A %3d B %3d C %3d D %3d | pixels differing from A: B %d, C %d, D %d (of %d drawn)'
              % (frame, n, sum(1 for w in Tw if w & 2), nk, *(dr[r] for r in 'ABCD'), d['B'], d['C'], d['D'], int(vis.sum())))
    if nf:
        print('%d frames: draws a frame A %.1f B %.1f C %.1f D %.1f; pixels differing from A: B %d, C %d, D %d of %d drawn (%.4f%%, %.4f%%, %.4f%%)'
              % (nf, tot['A'][0] / nf, tot['B'][0] / nf, tot['C'][0] / nf, tot['D'][0] / nf, px['B'], px['C'], px['D'], shown,
                 100.0 * px['B'] / max(1, shown), 100.0 * px['C'] / max(1, shown), 100.0 * px['D'] / max(1, shown)))
        print("D's changed pixels by how much nearer its face is than A's (window depth): a tie (<= 1e-6, coplanar) %d, "
              "close (<= 1e-4) %d, nearer %d" % (gaps['tie'], gaps['close'], gaps['nearer']))

main()
