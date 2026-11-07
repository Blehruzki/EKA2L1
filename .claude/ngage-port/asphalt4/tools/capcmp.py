# capcmp.py <dir A> <dir B>: the telemetry captures (A4_TEL, C:\a4telN.dat) two runs took at the same
# capture index, compared pixel for pixel (round 50). In lockstep the game's state is the same step for
# step, so a build that changes how a frame is drawn but not what -- a lifted function, a reordered draw
# -- must give the same pictures: the count of 565 pixels that differ, and their bounding box.
import sys, struct, glob, os
def caps(d):
    out = {}
    for f in glob.glob(os.path.join(d, 'a4tel*.dat')):
        b = open(f, 'rb').read()
        ver = struct.unpack_from('<I', b, 4)[0]
        hb = 256 if ver >= 4 else 128
        h = struct.unpack_from('<%dI' % (hb // 4), b, 0)
        cap, w, hgt, stride, fbb = h[2], h[4], h[5], h[6], h[7]
        fb = b[hb + 560: hb + 560 + fbb]
        out[cap] = (w, hgt, stride, fb, h[10])
    return out
A, B = caps(sys.argv[1]), caps(sys.argv[2])
common = sorted(set(A) & set(B))
print('captures: A %s, B %s, both %s' % (sorted(A), sorted(B), common))
bad = 0
for c in common:
    (w, hgt, st, fa, na), (_, _, _, fb, nb) = A[c], B[c]
    n = 0; x0 = y0 = 10 ** 9; x1 = y1 = -1
    for y in range(hgt):
        ra, rb = fa[y * st * 2:(y * st + w) * 2], fb[y * st * 2:(y * st + w) * 2]
        if ra == rb: continue
        for x in range(w):
            if ra[2 * x:2 * x + 2] != rb[2 * x:2 * x + 2]:
                n += 1; x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y)
    bad += n > 0
    print('  capture %4d (frames %d / %d): %6d pixels differ%s' % (c, na, nb, n, ' in x %d-%d y %d-%d' % (x0, x1, y0, y1) if n else ''))
print('%d of %d common captures differ' % (bad, len(common)))
