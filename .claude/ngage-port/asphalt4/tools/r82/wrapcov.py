# wrapcov.py <dirs...>: round 82 thoroughness check. Every 'CUT1' record (a bench A4_WRAPREC build keeps only triangles
# with a wrapped corner undone) rasterised at 320x240, pixel centres, twice: from the positions r82g uses (the true ones)
# and from the int16 words r82e and before used. Prints, per distinct triangle, the screen pixels each covers that the
# other does not, the rows involved, and whether r82e's cut to the band was empty.
import sys, glob, struct, os
W, H, G = 320, 240, 1024
s32 = lambda u: u - (1 << 32) if u & 0x80000000 else u
s16 = lambda u: u - 65536 if u & 0x8000 else u
def cover(P):
    (ax, ay), (bx, by), (cx, cy) = P
    det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
    if det == 0: return set()
    xs = [ax, bx, cx]; ys = [ay, by, cy]
    x0, x1 = max(0, int(min(xs))), min(W - 1, int(max(xs)) + 1)
    y0, y1 = max(0, int(min(ys))), min(H - 1, int(max(ys)) + 1)
    out = set()
    for y in range(y0, y1 + 1):
        py = y + 0.5
        for x in range(x0, x1 + 1):
            px = x + 0.5
            w0 = (bx - ax) * (py - ay) - (px - ax) * (by - ay)
            w1 = (cx - bx) * (py - by) - (px - bx) * (cy - by)
            w2 = (ax - cx) * (py - cy) - (px - cx) * (ay - cy)
            if (w0 >= 0 and w1 >= 0 and w2 >= 0) or (w0 <= 0 and w1 <= 0 and w2 <= 0): out.add((x, y))
    return out
def band_empty(P):   # Sutherland-Hodgman to the guard band, in floats
    poly = list(P)
    for axis, c, ge in ((0, -G, 1), (0, W + G, 0), (1, -G, 1), (1, H + G, 0)):
        out = []
        for i in range(len(poly)):
            p, q = poly[i], poly[(i + 1) % len(poly)]
            pin = (p[axis] >= c) if ge else (p[axis] <= c); qin = (q[axis] >= c) if ge else (q[axis] <= c)
            if pin: out.append(p)
            if pin != qin:
                t = (c - p[axis]) / (q[axis] - p[axis]); out.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
        poly = out
        if len(poly) < 3: return True
    return False
def main():
    recs = {}
    for d in sys.argv[1:]:
        for f in sorted(glob.glob(d + '/*.dat') + glob.glob(d + '/tel/*.dat')):
            b = open(f, 'rb').read(); i = b.rfind(b'CUT1')
            if i < 0 or len(b) < i + 8 + 16 * 96: continue
            tot = struct.unpack_from('<I', b, i + 4)[0]
            for k in range(min(tot, 16)):
                w = struct.unpack_from('<24I', b, i + 8 + k * 96)
                if w[0] == 0 and w[1] == 0: continue
                recs[w] = os.path.basename(f)
    print('%d distinct records' % len(recs))
    tot_true = tot_old = tot_lost = tot_wrong = 0; nempty = 0; strips = 0
    for w, fn in sorted(recs.items(), key=lambda kv: kv[0][0] & 0xFFFFF):
        fr, F = w[0] & 0xFFFFF, w[0] >> 20
        wrp = (w[1] >> 24) & 15
        true = [(s32(w[17 + 2 * v]) / 16, s32(w[18 + 2 * v]) / 16) for v in range(3)]
        old = [(s16(w[2 + 5 * v] & 0xFFFF), s16(w[2 + 5 * v] >> 16)) for v in range(3)]
        ct, co = cover(true), cover(old)
        lost, wrong = ct - co, co - ct
        e = band_empty(old)
        nempty += e
        tot_true += len(ct); tot_old += len(co); tot_lost += len(lost); tot_wrong += len(wrong)
        rows = sorted({y for _, y in lost})
        if lost:
            xs = [x for x, _ in lost]
            if len(rows) <= 8 and max(xs) - min(xs) >= 60: strips += 1
        print('frame %5d F %d wrapped %d | true covers %5d px, r82e %5d px; r82e missed %5d, drew wrongly %5d; r82e cut empty: %s%s' % (
            fr, F, wrp, len(ct), len(co), len(lost), len(wrong), 'yes' if e else 'no',
            ('; missed rows %d-%d (%d rows), x %d-%d' % (rows[0], rows[-1], len(rows), min(x for x, _ in lost), max(x for x, _ in lost))) if lost else ''))
        print('      true  ' + ' '.join('(%.1f,%.1f)' % p for p in true) + '   words ' + ' '.join('(%d,%d)' % p for p in old)
              + '   zw ' + ' '.join(str(s32(w[3 + 5 * v])) for v in range(3)))
    print('totals: true %d px, r82e %d px, missed %d, drawn wrongly %d; r82e cuts empty %d of %d; strip-shaped misses (<= 8 rows, >= 60 px wide) %d'
          % (tot_true, tot_old, tot_lost, tot_wrong, nempty, len(recs), strips))

if __name__ == "__main__": main()
