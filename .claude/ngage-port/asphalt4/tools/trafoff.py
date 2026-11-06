# trafoff.py crec.bin [...]: after round 49 on the phone ("traffic passed through, no hit") -- how far
# each frame draws the traffic from the player, against where the game has them at the frame's time.
# From 'FRKT' (a4_gl.c A4_YREC: every traffic car within 4000 of the player, drawn x/z and the step's
# x/z) and the player's 'FRKD' (drawn x/z and the step's). The game's place at a frame is the straight
# line between the step before and the step after at its fraction q, known after the fact. Prints the
# traffic's own error, the player's, and the error of the drawn gap between them (what the eye uses
# to aim), for traffic close to the player; and the steps where a close traffic car's move jumps
# (the game's hits).
import sys, struct, collections, math
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
pq = lambda a, p: a[min(len(a) - 1, int(p * len(a)))] if a else 0
def load(fn):
    d = open(fn, 'rb').read(); o = 0; recs = []; cur = None
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20: cur = [b]; recs.append(cur)
        elif cur is not None: cur.append(b)
    return recs
for fn in sys.argv[1:]:
    D = collections.defaultdict(dict); TR = collections.defaultdict(dict); PL = {}
    for r in load(fn):
        h = struct.unpack_from('<8I', r[0])
        if h[0] == 0x444B5246:
            slot = (h[2] >> 24) & 0x7F; key = (h[1], h[2] & 0xFFFFFF)
            x = s32(struct.unpack_from('<I', r[1], 4)[0]) if len(r) > 1 else 0
            z = s32(struct.unpack_from('<I', r[1], 0xC)[0]) if len(r) > 1 else 0
            D[slot][key] = (x, z, s32(h[5]), s32(h[6]), h[1], (h[2] & 0xFFFFFF) / 65536.0)
        elif h[0] == 0x43455243 and len(r) > 1:
            PL[h[1]] = (s32(struct.unpack_from('<I', r[1], 4)[0]), s32(struct.unpack_from('<I', r[1], 0xC)[0]))
        elif h[0] == 0x544B5246:
            slot = (h[2] >> 24) & 0x7F; key = (h[1], h[2] & 0xFFFFFF)
            TR[slot][key] = (s32(h[3]), s32(h[4]), s32(h[5]), s32(h[6]), h[1], (h[2] & 0xFFFFFF) / 65536.0)
    # the player: the slot whose step values are the CREC player's
    ps = None
    for s in D:
        if sum(1 for k, v in D[s].items() if k[0] in PL and (v[2], v[3]) == PL[k[0]]) > 20: ps = s; break
    print(fn.split('/')[-1], 'player slot', ps, '| traffic records', sum(len(v) for v in TR.values()))
    if ps is None: continue
    P = D[ps]; Pstep = {}
    for k, v in P.items(): Pstep[k[0]] = (v[2], v[3])
    err = collections.defaultdict(list); worst = []
    for s, fr in TR.items():
        Tstep = {}
        for k, v in fr.items(): Tstep[k[0]] = (v[2], v[3])
        for k, v in fr.items():
            st, q = k[0], min(v[5], 1.0)
            if k not in P or st + 1 not in Tstep or st + 1 not in Pstep: continue
            a, b = Tstep[st], Tstep[st + 1]; pa, pb = Pstep[st], Pstep[st + 1]
            if math.hypot(b[0] - a[0], b[1] - a[1]) > 1500 or math.hypot(pb[0] - pa[0], pb[1] - pa[1]) > 1500: continue
            tt = (a[0] + (b[0] - a[0]) * q, a[1] + (b[1] - a[1]) * q); pt = (pa[0] + (pb[0] - pa[0]) * q, pa[1] + (pb[1] - pa[1]) * q)
            td = (v[0], v[1]); pd = (P[k][0], P[k][1])
            gap_t = math.hypot(tt[0] - pt[0], tt[1] - pt[1])
            e_rel = math.hypot((td[0] - pd[0]) - (tt[0] - pt[0]), (td[1] - pd[1]) - (tt[1] - pt[1]))
            e_t = math.hypot(td[0] - tt[0], td[1] - tt[1]); e_p = math.hypot(pd[0] - pt[0], pd[1] - pt[1])
            band = 'near' if gap_t < 800 else 'far'
            err[band + ' gap'].append(e_rel); err[band + ' traffic'].append(e_t); err[band + ' player'].append(e_p)
            if band == 'near': worst.append((e_rel, st, round(q, 2), s, round(gap_t), round(e_t), round(e_p), v[0] - v[2], v[1] - v[3], b[0] - a[0], b[1] - a[1]))
    for kk in sorted(err):
        e = sorted(err[kk])
        print('  %-13s %5d frames: 50%% %4.0f 90%% %4.0f 99%% %4.0f max %4.0f' % (kk, len(e), pq(e, .5), pq(e, .9), pq(e, .99), e[-1]))
    worst.sort(reverse=True)
    for w in worst[:12]: print('   err %4.0f step %d q %.2f slot %d gap %d | traffic off %d player off %d | drawn-step %+d %+d | step move %+d %+d' % w)
    # the game's hits: a close traffic car whose step move changes by > 60 units in a step
    hits = []
    for s, fr in TR.items():
        Ts = sorted(set((k[0], v[2], v[3]) for k, v in fr.items()))
        for i in range(2, len(Ts)):
            (s0, x0, z0), (s1, x1, z1), (s2, x2, z2) = Ts[i - 2], Ts[i - 1], Ts[i]
            if s1 != s0 + 1 or s2 != s1 + 1 or s2 not in Pstep: continue
            dv = math.hypot((x2 - x1) - (x1 - x0), (z2 - z1) - (z1 - z0))
            gp = math.hypot(x2 - Pstep[s2][0], z2 - Pstep[s2][1])
            if dv > 60 and dv < 1500 and gp < 600: hits.append((s2, s, round(dv), round(gp)))
    print('  traffic moves jumping near the player (the game\'s hits?):', len(hits), hits[:12])
