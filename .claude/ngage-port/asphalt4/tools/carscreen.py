#!/usr/bin/env python3
# carscreen.py <EKA2L1_POSTDUMP file> [skip]: round 41, the player car as SHOWN. The bench car is
# the blue Mini: its pixels (blue well over red and green) in the car's area (x 60-260, y 135-236)
# of each distinct post give the car's place on screen (the centroid x) and its turn against
# the camera (the roof's centroid x less the bumper's: the car's side shows as it yaws).
# A held turn moves both one way for many frames; "back and forth" is their frame-to-frame change
# reversing sign. Counts reversals of each per 100 frames where the car is moving on screen, and
# the share of frames whose change is off its 9 neighbours' median by more than half of it --
# measured on the picture, so it holds whatever the renderer drew the car and camera from.
import sys, struct, statistics as st
import numpy as np
fn = sys.argv[1]; skip = int(sys.argv[2]) if len(sys.argv) > 2 else 0
F = []; last = None
with open(fn, 'rb') as f:
    while True:
        h = f.read(32)
        if len(h) < 32: break
        mg, post, ms, w, hh, pitch, bpp, _ = struct.unpack('<8I', h)
        d = f.read(pitch * hh)
        if len(d) < pitch * hh: break
        a = np.frombuffer(d, '<u4').reshape(hh, pitch // 4)[135:236, 60:260]
        r = ((a >> 16) & 255).astype(np.int32); g = ((a >> 8) & 255).astype(np.int32); b = (a & 255).astype(np.int32)
        m = (b > r + 50) & (b > g + 15) & (b > 90)
        if m.sum() < 300: F.append((post, ms, None, None, int(m.sum()))); continue
        ys, xs = np.nonzero(m)
        y0, y1 = ys.min(), ys.max()
        cx = xs.mean() + 60
        top = xs[ys < y0 + (y1 - y0) * 0.3]; bot = xs[ys > y0 + (y1 - y0) * 0.7]
        tilt = (top.mean() - bot.mean()) if len(top) and len(bot) else 0.0
        key = (round(cx, 2), round(tilt, 2), int(m.sum()))
        if key == last: continue           # the same picture posted again
        last = key
        F.append((post, ms, cx, tilt, int(m.sum())))
F = [x for x in F[skip:]]
ok = [x for x in F if x[2] is not None]
print('%d distinct posts, %d with the car found' % (len(F), len(ok)))
dt = [(b[1] - a[1]) & 0xFFFFFFFF for a, b in zip(ok, ok[1:])]
print('ms between shown frames: median %d, 10-90%% %d..%d' % (st.median(dt), sorted(dt)[len(dt) // 10], sorted(dt)[9 * len(dt) // 10]))
for name, idx, floor in (('place x', 2, 0.6), ('turn (roof - bumper)', 3, 0.3)):
    dv = [(b[idx] - a[idx]) for a, b in zip(ok, ok[1:])]
    rev = mov = 0; off = n = 0; big = []
    for i in range(1, len(dv)):
        if abs(dv[i - 1]) > floor and abs(dv[i]) > floor:
            mov += 1
            if dv[i] * dv[i - 1] < 0: rev += 1; big.append((min(abs(dv[i]), abs(dv[i - 1])), ok[i][0]))
    for i in range(4, len(dv) - 4):
        nb = dv[i - 4:i + 5]; md = st.median(nb)
        if abs(md) > floor and all(x * md > 0 for x in nb[3:6] if x != dv[i]):
            n += 1
            if abs(dv[i] - md) > 0.5 * abs(md): off += 1
    big.sort(reverse=True)
    print('%s: change per frame median |%.2f| px; reversals %d in %d moving pairs (%.1f per 100); off >50%% %d of %d; biggest reversals (px, post): %s'
          % (name, st.median([abs(x) for x in dv]), rev, mov, 100.0 * rev / max(mov, 1), off, n,
             ', '.join('%.1f@%d' % b for b in big[:6])))
