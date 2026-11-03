# faceid.py <dir>: across A4_MESHDUMP captures of consecutive renders, does a face pointer
# name the same polygon? Matches by pointer and reports texture agreement and corner motion.
import sys, struct, glob
O = 256 + 560 + 2 * 153600 + 500 + 512
caps = []
for f in glob.glob(sys.argv[1] + '/a4tel*.dat'):
    d = open(f, 'rb').read()
    n, F, cx, cy, frame, magic = struct.unpack_from('<6I', d, O)
    if magic != 0x504D444D: continue
    faces = {}
    dup = 0
    for i in range(n):
        r = d[O + 64 + i * 64: O + 128 + i * 64]
        c = struct.unpack_from('<15h', r, 0)
        tw, fl, wh, pix, tq, b0, b1, face = struct.unpack_from('<8I', r, 32)
        if face in faces: dup += 1
        faces[face] = (tw & ~3, fl, c)
    caps.append((frame, n, dup, faces, f.split('/')[-1]))
caps.sort()
for fr, n, dup, faces, nm in caps: print('%s frame %d: %d listed, %d distinct pointers (%d repeats)' % (nm, fr, n, len(faces), dup))
for (f0, n0, _, a, _), (f1, n1, _, b, _) in zip(caps, caps[1:]):
    common = set(a) & set(b)
    sametex = [p for p in common if a[p][0] == b[p][0]]
    moves = []
    for p in sametex:
        ca, cb = a[p][2], b[p][2]
        moves.append(max(abs(ca[j * 5 + k] - cb[j * 5 + k]) for j in range(3) for k in range(3)))
    moves.sort()
    q = lambda x: moves[int(x * (len(moves) - 1))] if moves else -1
    uvsame = sum(1 for p in sametex if all(a[p][2][j * 5 + k] == b[p][2][j * 5 + k] for j in range(3) for k in (3, 4)))
    print('frames %d->%d: common ptrs %d of %d/%d, same texture %d, same uv %d, corner move (max |dxyz|) p10 %d p50 %d p90 %d max %d'
          % (f0, f1, len(common), len(a), len(b), len(sametex), uvsame, q(.1), q(.5), q(.9), q(1)))
# three in a row: is a matched polygon's motion steady (what extrapolation needs)?
def key(e): return (e[0], tuple(e[2][j * 5 + k] for j in range(3) for k in (3, 4)))
for A, B, C in zip(caps, caps[1:], caps[2:]):
    a, b, c = A[3], B[3], C[3]
    good = bad = 0; errs = []; zs = []
    for p in set(a) & set(b) & set(c):
        if not (key(a[p]) == key(b[p]) == key(c[p])): continue
        for j in range(3):
            for k in range(3):
                d1 = b[p][2][j * 5 + k] - a[p][2][j * 5 + k]
                d2 = c[p][2][j * 5 + k] - b[p][2][j * 5 + k]
                e = abs(d2 - d1); errs.append((e, abs(d1)))
        zs.append(-b[p][2][2])
        ok = all(abs((c[p][2][j*5+k]-b[p][2][j*5+k]) - (b[p][2][j*5+k]-a[p][2][j*5+k])) <= 0.35 * abs(b[p][2][j*5+k]-a[p][2][j*5+k]) + 12 for j in range(3) for k in range(3))
        good += ok; bad += not ok
    zs.sort()
    print('frames %d,%d,%d: matched in all three %d; steady motion %d, not %d; |z| p50 %s'
          % (A[0], B[0], C[0], good + bad, good, bad, zs[len(zs)//2] if zs else '-'))
# renders can carry 1-5 logic steps each (more under the capture's own load), so fit one
# ratio r = steps(b->c)/steps(a->b) per triple and test d2 ~ r*d1 instead
print('-- with a fitted step ratio')
for A, B, C in zip(caps, caps[1:], caps[2:]):
    a, b, c = A[3], B[3], C[3]
    rows = []
    for p in set(a) & set(b) & set(c):
        if not (key(a[p]) == key(b[p]) == key(c[p])): continue
        d1 = [b[p][2][j*5+k] - a[p][2][j*5+k] for j in range(3) for k in range(3)]
        d2 = [c[p][2][j*5+k] - b[p][2][j*5+k] for j in range(3) for k in range(3)]
        rows.append((p, d1, d2))
    rs = sorted(y / x for _, d1, d2 in rows for x, y in zip(d1, d2) if abs(x) >= 40)
    if not rs: print('frames %d,%d,%d: nothing to fit' % (A[0], B[0], C[0])); continue
    r = rs[len(rs) // 2]
    ok = sum(1 for _, d1, d2 in rows if all(abs(y - r * x) <= 0.3 * abs(r * x) + 16 for x, y in zip(d1, d2)))
    print('frames %d,%d,%d: r %.2f, %d of %d polygons move as r says' % (A[0], B[0], C[0], r, ok, len(rows)))
