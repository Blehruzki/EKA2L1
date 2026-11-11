# predrules2.py crec.bin ...: turn predictors scored per step (see predrules.py), with the car's steering +7FC
# (-4096..4096; the game's heading follows it) as the second input.
import sys, struct, collections
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
pq = lambda a, p: sorted(a)[min(len(a) - 1, int(p * len(a)))] if a else 0
def r58b(t, tp, s, sp):
    if abs(s) < abs(sp): return 0
    if t * tp < 0 and t * s <= 0: return 0
    if t * s < 0 and abs(s) >= 1024 and abs(s) > abs(sp) and s * sp > 0: return 0
    return t
R = dict(
    base=lambda t, tp, s, sp: t,
    revk_ease=lambda t, tp, s, sp: 0 if (t * tp < 0 and t * s <= 0) or abs(s) < abs(sp) else t,
    r58b=r58b,
)
for fn in sys.argv[1:]:
    d = open(fn, 'rb').read(); o = 0; cur = None; recs = []
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20: cur = [b]; recs.append(cur)
        elif cur is not None: cur.append(b)
    S = collections.defaultdict(dict)
    for r in recs:
        h = struct.unpack_from('<8I', r[0])
        if len(r) < 3 or h[0] != 0x444B5246 or not (h[2] >> 31): continue
        S[(h[2] >> 24) & 0x7F][h[1]] = (s32(h[7]) & 2047, s32(struct.unpack_from('<I', r[1], 0x7FC)[0]))
    print(fn)
    for name, f in R.items():
        line = []
        for grp in ('player', 'others'):
            err = []; over = 0; lag = 0
            for sl in S:
                if (grp == 'player') != (sl == 0): continue
                st = S[sl]
                for k in st:
                    if not all(j in st for j in (k - 2, k - 1, k + 1)): continue
                    t = wrap(st[k][0] - st[k - 1][0]); tp = wrap(st[k - 1][0] - st[k - 2][0]); tn = wrap(st[k + 1][0] - st[k][0])
                    if abs(t) >= 256 or abs(tp) >= 256 or abs(tn) >= 256: continue
                    p = f(t, tp, st[k][1], st[k - 1][1])
                    err.append(abs(p - tn))
                    if abs(p) >= 15 and p * tn <= 0: over += 1
                    if abs(tn) >= 15 and abs(p - tn) >= 15 and p * tn >= 0 and abs(p) < abs(tn): lag += 1
            line.append('%s mean %.2f 99%% %2d over %3d lag %3d' % (grp, sum(err) / max(1, len(err)), pq(err, .99), over, lag))
        print('  %-9s %s' % (name, ' | '.join(line)))
