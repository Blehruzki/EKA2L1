#!/usr/bin/env python3
# trafpace.py <trace.regs> <traf-mark-pc> [skip]: round 41, A4_SPECTRAFFIC. A traffic car as drawn
# at each render (a4_traf_mark: slot, x, z, ms; the first slot moved by the last step): over runs
# of renders on one slot, its drawn speed per real ms against its 9 neighbours' median, and how
# often it is drawn going backwards -- traffic standing at the whole step while the scene moves
# on is uneven; carried on with the step, even.
import sys, struct, math, statistics as st
d = open(sys.argv[1], 'rb').read(); pc = int(sys.argv[2], 16); skip = int(sys.argv[3]) if len(sys.argv) > 3 else 50
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
R = [(e[1], s32(e[2]), s32(e[3]), e[4]) for e in (struct.unpack_from('<17I', d, i * 68) for i in range(len(d) // 68)) if e[0] == pc][skip:]
rat = []; back = mv = 0; n = 0
for k in range(len(R) - 10):
    w = R[k:k + 11]
    if any(x[0] != w[0][0] for x in w): continue
    sp = []
    for a, b in zip(w, w[1:]):
        dt = (b[3] - a[3]) or 1
        sp.append(math.hypot(b[1] - a[1], b[2] - a[2]) / dt)
    m = st.median(sp[:9])
    if m > 0.05: rat.append(sp[4] / m); n += 1
    d1 = (w[5][1] - w[4][1], w[5][2] - w[4][2]); d2 = (w[6][1] - w[5][1], w[6][2] - w[5][2])
    if math.hypot(*d1) > 4:
        mv += 1
        if d1[0] * d2[0] + d1[1] * d2[1] < 0: back += 1
rat.sort(); k = len(rat)
print('%d traffic marks; %d windows on one slot' % (len(R), k))
if k: print('drawn speed / neighbours: quartiles %.2f..%.2f, 10-90%% %.2f..%.2f, off by >25%%: %.0f%%; drawn backwards %d of %d'
            % (rat[k // 4], rat[3 * k // 4], rat[k // 10], rat[9 * k // 10], 100.0 * sum(1 for x in rat if abs(x - 1) > .25) / k, back, mv))
