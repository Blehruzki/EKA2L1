#!/usr/bin/env python3
# pacing2.py <trace.regs> <render-mark-pc hex> <render-mark2-pc hex> [skip-renders]: round 40.
# A4_FRAMEMARK's render marks (camera; car x, z, heading, real time in ms) -> how evenly the car
# is drawn against REAL time, render to render: speed on screen = distance drawn / real ms
# between renders, against the median of its 9 neighbours (1.00 = even); the share of renders
# off by more than 25%; how often the car is drawn going backwards (displacement against the
# previous one); and the camera's offset from the car, render to render. The game draws what
# the player sees; real time is what the player sees it in.
import sys, struct, statistics as st, math
d = open(sys.argv[1], 'rb').read(); m1, m2 = int(sys.argv[2], 16), int(sys.argv[3], 16)
skip = int(sys.argv[4]) if len(sys.argv) > 4 else 300
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
R = []; cam = None
for i in range(len(d) // 68):
    e = struct.unpack_from('<17I', d, i * 68)
    if e[0] == m1: cam = (s32(e[1]), s32(e[2]), s32(e[3]), e[4])
    elif e[0] == m2 and cam:
        R.append({'cam': cam, 'x': s32(e[1]), 'z': s32(e[2]), 'hd': e[3] & 0xFFF, 't': e[4]})
R = R[skip:]
print('%d renders' % len(R))
dt = [((b['t'] - a['t']) & 0xFFFFFFFF) for a, b in zip(R, R[1:])]
print('real ms between renders: median %d, 10-90%% %d..%d' % (st.median(dt), sorted(dt)[len(dt) // 10], sorted(dt)[9 * len(dt) // 10]))
v = []; back = 0; moving = 0
for i, (a, b) in enumerate(zip(R, R[1:])):
    dx, dz = b['x'] - a['x'], b['z'] - a['z']; t = dt[i]
    v.append(math.hypot(dx, dz) / t if t else None)
    if i:
        p = R[i - 1]; px, pz = a['x'] - p['x'], a['z'] - p['z']
        if math.hypot(px, pz) > 20:
            moving += 1
            if dx * px + dz * pz < 0: back += 1
rat = []
for i in range(4, len(v) - 4):
    if not v[i]: continue
    nb = [x for x in v[i - 4:i + 5] if x]
    m = st.median(nb)
    if m > 0.5: rat.append(v[i] / m)
rat.sort(); n = len(rat)
if n:
    print('speed on screen / neighbours: median %.2f, quartiles %.2f..%.2f, 10-90%% %.2f..%.2f, off by >25%%: %.0f%% (%d renders)'
          % (rat[n // 2], rat[n // 4], rat[3 * n // 4], rat[n // 10], rat[9 * n // 10], 100.0 * sum(1 for r in rat if abs(r - 1) > 0.25) / n, n))
print('drawn going backwards: %d of %d moving renders' % (back, moving))
co = [(r['cam'][0] - r['x'], r['cam'][2] - r['z']) for r in R]
dd = sorted(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(co, co[1:]))
print('camera offset change a render (ground): median %.0f, 90%% %.0f, 99%% %.0f' % (dd[len(dd) // 2], dd[9 * len(dd) // 10], dd[99 * len(dd) // 100]))
hd = [((b['hd'] - a['hd'] + 2048) % 4096) - 2048 for a, b in zip(R, R[1:])]
flip = sum(1 for a, b in zip(hd, hd[1:]) if a * b < 0 and abs(a) > 4 and abs(b) > 4)
print('heading: renders where the turn reverses (both over 4 of 4096): %d of %d' % (flip, len(hd)))
# single-render blips: a heading change whose sign disagrees with both neighbours' (which
# agree), each over 2 of 4096; and the car drawn off the line between its neighbours by more
# than a fifth of the distance between them (a sideways wobble), while moving
blip = sum(1 for a, b, c in zip(hd, hd[1:], hd[2:]) if a * c > 0 and b * a < 0 and min(abs(a), abs(b), abs(c)) > 2)
wob = 0; cnt = 0
for p, q, r in zip(R, R[1:], R[2:]):
    ax, az = r['x'] - p['x'], r['z'] - p['z']; L = math.hypot(ax, az)
    if L < 40: continue
    cnt += 1
    off = abs((q['x'] - p['x']) * az - (q['z'] - p['z']) * ax) / L
    if off > L / 5: wob += 1
print('heading blips (one render against both neighbours): %d of %d; sideways wobbles over a fifth: %d of %d' % (blip, len(hd), wob, cnt))
# the camera: its yaw (render mark r3 low 12 bits, on 2048... read as 4096 wrap-safe) blips,
# and its sideways offset from the car in the car's own frame (heading +0x678 on 4096),
# render to render: a camera swinging across the car reads as steering jitter
yaw = [r['cam'][3] & 0xFFF for r in R]
dy = [((b - a + 2048) % 4096) - 2048 for a, b in zip(yaw, yaw[1:])]
yb = sum(1 for a, b, c in zip(dy, dy[1:], dy[2:]) if a * c > 0 and b * a < 0 and min(abs(a), abs(b), abs(c)) > 2)
lat = []
for r in R:
    th = r['hd'] * 2 * math.pi / 4096
    ox, oz = r['cam'][0] - r['x'], r['cam'][2] - r['z']
    lat.append(ox * math.cos(th) - oz * math.sin(th))
dl = sorted(abs(b - a) for a, b in zip(lat, lat[1:]))
lb = sum(1 for a, b, c in zip(lat, lat[1:], lat[2:]) if (b - a) * (c - b) < 0 and min(abs(b - a), abs(c - b)) > 8)
print('camera yaw blips: %d of %d; sideways offset (car frame) change a render: median %.0f, 90%% %.0f, 99%% %.0f; its reversals over 8: %d'
      % (yb, len(dy), dl[len(dl) // 2], dl[9 * len(dl) // 10], dl[99 * len(dl) // 100], lb))
# round 41 (the owner: hold the key, the car goes "back n forth"): the turn rate (heading
# change per real ms) of each render against the median of its 9 neighbours', where the car
# is turning steadily (the neighbours' median over 0.02 units of 4096 a ms, all one sign)
rate = [h / t if t else 0.0 for h, t in zip(hd, dt)]
tr = []
for i in range(4, len(rate) - 4):
    nb = rate[i - 4:i + 5]
    if not (all(x > 0 for x in nb) or all(x < 0 for x in nb)): continue
    m = st.median(nb)
    if abs(m) > 0.02: tr.append(rate[i] / m)
tr.sort(); k = len(tr)
if k:
    print('turn rate / neighbours while turning: median %.2f, quartiles %.2f..%.2f, 10-90%% %.2f..%.2f, off by >25%%: %.0f%% (%d renders)'
          % (tr[k // 2], tr[k // 4], tr[3 * k // 4], tr[k // 10], tr[9 * k // 10], 100.0 * sum(1 for r in tr if abs(r - 1) > 0.25) / k, k))
else:
    print('turn rate: no steady turning found')
# the car's angle against the camera's yaw (both on the 2048 circle, +678 and +0x68): what
# the player sees of the car's turn on screen; its change a render, its single-render blips
# and how often it reverses while the car is turning (round 41: "the car is kind of going
# back n forth")
rel = [((r['hd'] - (r['cam'][3] & 0xFFF)) + 1024) % 2048 - 1024 for r in R]
dr = [((b - a + 1024) % 2048) - 1024 for a, b in zip(rel, rel[1:])]
ad = sorted(abs(x) for x in dr)
rb = sum(1 for a, b, c in zip(dr, dr[1:], dr[2:]) if a * c > 0 and b * a < 0 and min(abs(a), abs(b), abs(c)) > 2)
rv = sum(1 for a, b in zip(dr, dr[1:]) if a * b < 0 and abs(a) > 3 and abs(b) > 3)
print('car against camera: change a render median %d, 90%% %d, 99%% %d; blips %d; reversals over 3: %d of %d'
      % (ad[len(ad) // 2], ad[9 * len(ad) // 10], ad[99 * len(ad) // 100], rb, rv, len(dr)))
