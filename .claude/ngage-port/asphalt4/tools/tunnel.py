# tunnel.py crec.bin [...]: after round 49 on the phone ("traffic passed through, no hit") -- the game's
# traffic collision is a sphere overlap at its steps (+1FF56 -> +239B6 -> +23728: |d| <= rA + rB, the
# radii +078; 8 calls a step, no sub-steps). For each meeting with a live traffic car (its +0x128 bit 0
# clear: the collision's own skip), from the 'FRKT' and 'CREC' records: did the two overlap at a step,
# only between steps (the straight line between the step positions), or not at all -- and did the game
# hit (the traffic car's move jumps, or the player's falls by over 15%). Prints each meeting and the
# table, with the closing speed (units a step).
import sys, struct, collections, math
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
def load(fn):
    d = open(fn, 'rb').read(); o = 0; recs = []; cur = None
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20: cur = [b]; recs.append(cur)
        elif cur is not None: cur.append(b)
    return recs
for fn in sys.argv[1:]:
    PL = {}; T = collections.defaultdict(dict)
    for r in load(fn):
        h = struct.unpack_from('<8I', r[0])
        if len(r) < 2: continue
        w = lambda o: s32(struct.unpack_from('<I', r[1], o)[0])
        if h[0] == 0x43455243 and h[1] not in PL: PL[h[1]] = (w(4), w(0xC), w(0x78))
        elif h[0] == 0x544B5246:
            slot = (h[2] >> 24) & 0x7F
            if h[1] not in T[slot]: T[slot][h[1]] = (s32(h[5]), s32(h[6]), w(0x78), w(0x128) & 1)
    tab = collections.Counter(); rows = []
    for s, tr in sorted(T.items()):
        ks = sorted(k for k in tr if k in PL)
        # meetings: runs of consecutive steps within 1500 of the player
        run = []
        for k in ks + [None]:
            if k is not None and (not run or k == run[-1] + 1) and math.hypot(tr[k][0] - PL[k][0], tr[k][1] - PL[k][1]) < 1500: run.append(k); continue
            if len(run) >= 3:
                live = all(not tr[j][3] for j in run)
                R = (tr[run[0]][2] or 80) + (PL[run[0]][2] or 77)
                dstep = min(math.hypot(tr[j][0] - PL[j][0], tr[j][1] - PL[j][1]) for j in run)
                dline = dstep; close = 0
                for a, b in zip(run, run[1:]):
                    ta, tb, pa, pb = tr[a], tr[b], PL[a], PL[b]
                    for i in range(41):
                        u = i / 40
                        dline = min(dline, math.hypot(ta[0] + (tb[0] - ta[0]) * u - (pa[0] + (pb[0] - pa[0]) * u), ta[1] + (tb[1] - ta[1]) * u - (pa[1] + (pb[1] - pa[1]) * u)))
                    close = max(close, math.hypot((pb[0] - pa[0]) - (tb[0] - ta[0]), (pb[1] - pa[1]) - (tb[1] - ta[1])))
                hit = False
                for a, b, c in zip(run, run[1:], run[2:]):
                    t0, t1, t2 = tr[a], tr[b], tr[c]
                    if math.hypot((t2[0] - t1[0]) - (t1[0] - t0[0]), (t2[1] - t1[1]) - (t1[1] - t0[1])) > 60: hit = True
                    if c in PL and b in PL and a in PL:
                        m0 = math.hypot(PL[b][0] - PL[a][0], PL[b][1] - PL[a][1]); m1 = math.hypot(PL[c][0] - PL[b][0], PL[c][1] - PL[b][1])
                        if m0 > 60 and m1 < 0.85 * m0 and math.hypot(t1[0] - PL[b][0], t1[1] - PL[b][1]) < 2 * R: hit = True
                kind = 'at a step' if dstep <= R else 'between steps only' if dline <= R else 'apart'
                if live and dline <= 2 * R:
                    tab[(kind, hit)] += 1
                    rows.append('  slot %2d steps %d-%d: R %d, nearest at a step %4.0f, between %4.0f, closing %3.0f/step -> %-18s %s'
                                % (s, run[0], run[-1], R, dstep, dline, close, kind, 'HIT' if hit else 'no hit'))
            run = [k] if k is not None and math.hypot(tr[k][0] - PL[k][0], tr[k][1] - PL[k][1]) < 1500 else []
    print(fn.split('/')[-1]); print('\n'.join(rows))
    for k in sorted(tab): print('  %-20s %-6s %d' % (k[0], 'hit' if k[1] else 'no hit', tab[k]))
