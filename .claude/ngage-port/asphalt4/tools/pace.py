# pace.py <a4tel dir> ...: the frame record's delays (render start -> present) and how evenly the picture moves per
# displayed frame: the drawn car's step between consecutive frames against the present interval, as a speed, each
# against the median of its 9 neighbours
import sys, struct, glob, os, statistics as st, math
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
for d in sys.argv[1:]:
    E = {}
    for fn in sorted(glob.glob(os.path.join(d, 'a4tel*.dat'))):
        b = open(fn, 'rb').read(); mg, n = struct.unpack_from('<2I', b, OFF)
        if mg != 0x31435246: continue
        for k in range(max(0, n - 256), n):
            e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32); E[e[0]] = e
    R = [dict(t=e[0], p=e[1], mode=(e[2] >> 20) & 0xFF, x=s32(e[3]), z=s32(e[4])) for e in (E[t] for t in sorted(E))]
    R = [r for r in R if r['mode'] == 2 and r['p']]
    L = [r['p'] - r['t'] for r in R]
    dL = [L[i] - L[i - 1] for i in range(1, len(L))]
    sp = []
    for i in range(1, len(R)):
        dp = R[i]['p'] - R[i - 1]['p']
        if 0 < dp < 300: sp.append(math.hypot(R[i]['x'] - R[i - 1]['x'], R[i]['z'] - R[i - 1]['z']) / dp)
    rel = []
    for i in range(len(sp)):
        nb = sorted(sp[max(0, i - 4):i + 5])
        m = nb[len(nb) // 2]
        if m > 0.5: rel.append(sp[i] / m)
    rel.sort(); q = lambda a, p: a[min(len(a) - 1, int(p * len(a)))] if a else 0
    print('%-8s %3d frames | delay mean %.1f sd %.1f, less the previous frame\'s sd %.1f | picture speed per displayed frame / neighbours: 10-90%% %.2f..%.2f, 1-99%% %.2f..%.2f, off >15%%: %.0f%%'
          % (os.path.basename(d.rstrip('/')), len(R), st.mean(L), st.pstdev(L), st.pstdev(dL), q(rel, .1), q(rel, .9), q(rel, .01), q(rel, .99),
             100.0 * sum(1 for v in rel if abs(v - 1) > 0.15) / max(1, len(rel))))
