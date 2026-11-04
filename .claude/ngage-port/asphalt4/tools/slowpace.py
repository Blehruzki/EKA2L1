#!/usr/bin/env python3
# slowpace.py <trace.regs> <render-mark2-pc> [skip]: round 41, with A4_BENCH_SLOWMO (slow motion
# forced 3 s of every 10 by the host ms clock, a4_ntick: seconds 7-9 of each 10). The player car
# as drawn (render mark 2: x, z, heading, ms), split into slow and normal stretches (300 ms after
# each switch left out): how often the car is drawn moving backwards (its displacement against
# the one before), and its drawn speed against its 9 neighbours' median.
import sys, struct, math, statistics as st
d = open(sys.argv[1], 'rb').read(); m2 = int(sys.argv[2], 16)
skip = int(sys.argv[3]) if len(sys.argv) > 3 else 100
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
R = [(s32(e[1]), s32(e[2]), e[4]) for e in (struct.unpack_from('<17I', d, i * 68) for i in range(len(d) // 68)) if e[0] == m2][skip:]
def win(t):
    s = (t // 1000) % 10; ms = t % 1000
    if s == 7 and ms < 300 or s == 0 and ms < 300: return None
    return 'slow' if s >= 7 else 'normal'
for w in ('normal', 'slow'):
    mv = back = 0; sp = []
    for a, b, c in zip(R, R[1:], R[2:]):
        if win(a[2]) != w or win(c[2]) != w: continue
        d1 = (b[0] - a[0], b[1] - a[1]); d2 = (c[0] - b[0], c[1] - b[1])
        if math.hypot(*d1) < 8: continue
        mv += 1
        if d1[0] * d2[0] + d1[1] * d2[1] < 0: back += 1
        dt = (c[2] - b[2]) or 1
        sp.append(math.hypot(*d2) / dt)
    rat = []
    for i in range(4, len(sp) - 4):
        m = st.median(sp[i - 4:i + 5])
        if m > 0.05: rat.append(sp[i] / m)
    rat.sort(); k = len(rat)
    print('%-6s: %d moving frames, drawn backwards %d (%.1f%%); speed / neighbours: quartiles %.2f..%.2f, 10-90%% %.2f..%.2f, off by >25%%: %.0f%%'
          % (w, mv, back, 100.0 * back / max(mv, 1), rat[k // 4] if k else 0, rat[3 * k // 4] if k else 0, rat[k // 10] if k else 0, rat[9 * k // 10] if k else 0,
             100.0 * sum(1 for x in rat if abs(x - 1) > .25) / max(k, 1)))
