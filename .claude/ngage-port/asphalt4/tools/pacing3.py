#!/usr/bin/env python3
# pacing3.py <trace.regs> <render-mark2-pc> <present-mark-pc> [skip]: round 41. The car as
# drawn (render mark 2: x, z, heading, render-start ms) against when its frame was SHOWN
# (the next made present's a4_present_mark ms): drawn speed and turn rate per shown interval
# against their 9 neighbours' median; the lag from render start to present.
import sys, struct, statistics as st, math
d = open(sys.argv[1], 'rb').read(); m2, pm = int(sys.argv[2], 16), int(sys.argv[3], 16)
skip = int(sys.argv[4]) if len(sys.argv) > 4 else 300
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
F = []; pend = None
for i in range(len(d) // 68):
    e = struct.unpack_from('<17I', d, i * 68)
    if e[0] == m2: pend = (s32(e[1]), s32(e[2]), e[3] & 0xFFF, e[4])
    elif e[0] == pm and pend:
        F.append((pend[0], pend[1], pend[2], pend[3], e[1])); pend = None
F = F[skip:]
lag = sorted(((f[4] - f[3]) & 0xFFFFFFFF) for f in F)
print('%d shown frames; render start to present: median %d ms, 10-90%% %d..%d' % (len(F), lag[len(lag) // 2], lag[len(lag) // 10], lag[9 * len(lag) // 10]))
def evenness(vals, label, floor):
    r = []
    for i in range(4, len(vals) - 4):
        nb = vals[i - 4:i + 5]
        if not (all(x > 0 for x in nb) or all(x < 0 for x in nb)): continue
        m = st.median(nb)
        if abs(m) > floor: r.append(vals[i] / m)
    r.sort(); k = len(r)
    if k: print('%s against shown time / neighbours: quartiles %.2f..%.2f, 10-90%% %.2f..%.2f, off by >25%%: %.0f%% (%d)'
                % (label, r[k // 4], r[3 * k // 4], r[k // 10], r[9 * k // 10], 100.0 * sum(1 for x in r if abs(x - 1) > 0.25) / k, k))
for key, idx in (('render start', 3), ('present', 4)):
    sp, tr = [], []
    for a, b in zip(F, F[1:]):
        t = (b[idx] - a[idx]) & 0xFFFFFFFF
        sp.append(math.hypot(b[0] - a[0], b[1] - a[1]) / t if t else 0.0)
        tr.append((((b[2] - a[2] + 1024) % 2048) - 1024) / t if t else 0.0)
    print('-- timed by %s:' % key)
    evenness(sp, '  drawn speed', 0.5); evenness(tr, '  turn rate', 0.02)
