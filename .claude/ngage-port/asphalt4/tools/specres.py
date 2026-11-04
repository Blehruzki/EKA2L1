#!/usr/bin/env python3
# specres.py <trace.regs> <render-mark-pc> <render-mark2-pc> [skip]: round 41. Each speculative
# frame against the true path: between two whole-step renders W_a and W_b one step apart, the
# frame drawn at fraction q of the step should show W_a + q (W_b - W_a). The residual (drawn less
# that) of the car's place and heading, the camera's place and yaw, and what the screen shows of
# them -- the car's turn against the line of sight (heading less the bearing from the camera to
# the car) and its bearing against the camera's yaw -- as a share of the step's own change. A
# held turn that the speculation predicts well has residuals near 0; "back and forth" is a
# residual of the order of the step's change. Marks: render mark (camera x, y, z,
# yaw | q16>>4 << 12 | h5 << 24 | computed << 31), render mark 2 (car x, z, heading, ms).
import sys, struct, statistics as st, math
d = open(sys.argv[1], 'rb').read(); m1, m2 = int(sys.argv[2], 16), int(sys.argv[3], 16)
skip = int(sys.argv[4]) if len(sys.argv) > 4 else 100
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
R = []; cam = None
for i in range(len(d) // 68):
    e = struct.unpack_from('<17I', d, i * 68)
    if e[0] == m1: cam = e[1:5]
    elif e[0] == m2 and cam:
        w = cam[3]
        R.append(dict(cx=s32(cam[0]), cz=s32(cam[2]), yaw=w & 0xFFF, q=((w >> 12) & 0xFFF) / 4096.0,
                      spec=w >> 31, x=s32(e[1]), z=s32(e[2]), hd=e[3] & 0xFFF, t=e[4]))
        cam = None
R = R[skip:]
wrap = lambda a: ((a + 1024) % 2048) - 1024
B = 2048 / (2 * math.pi)
def view(r):   # the bearing from the camera to the car on the 2048 circle (sign and origin fitted below)
    return math.atan2(r['x'] - r['cx'], r['z'] - r['cz']) * B
# fit the bearing convention against the camera's yaw on the whole frames (a chase camera looks at the car)
W = [r for r in R if not r['spec']]
best = None
for sg in (1, -1):
    for off in range(0, 2048, 8):
        e = st.median([abs(wrap(sg * view(r) + off - r['yaw'])) for r in W[::5]])
        if best is None or e < best[0]: best = (e, sg, off)
print('%d renders (%d speculative); bearing fit: sign %d, offset %d, median miss %.1f /2048'
      % (len(R), sum(r['spec'] for r in R), best[1], best[2], best[0]))
sg, off = best[1], best[2]
for r in R:
    r['look'] = sg * view(r) + off             # bearing on the camera's circle
    r['app'] = wrap(r['hd'] - r['look'])       # the car's turn as seen
    r['scr'] = wrap(r['look'] - r['yaw'])      # where on screen (left/right of centre)
def res(f, ang):
    out = []
    for i in range(1, len(R) - 1):
        a, s, b = R[i - 1], R[i], R[i + 1]
        if not s['spec'] or a['spec'] or b['spec']: continue
        if f == 'pos':
            step = math.hypot(b['x'] - a['x'], b['z'] - a['z'])
            if step < 20: continue
            ix, iz = a['x'] + s['q'] * (b['x'] - a['x']), a['z'] + s['q'] * (b['z'] - a['z'])
            # along the step and across it
            ux, uz = (b['x'] - a['x']) / step, (b['z'] - a['z']) / step
            dx, dz = s['x'] - ix, s['z'] - iz
            out.append(((dx * ux + dz * uz) / step, (dz * ux - dx * uz) / step, s['q'], step))
        else:
            dv = wrap(b[f] - a[f]) if ang else b[f] - a[f]
            ideal = a[f] + s['q'] * dv
            r_ = wrap(s[f] - ideal) if ang else s[f] - ideal
            out.append((r_, dv, s['q']))
    return out
P = res('pos', 0)
if P:
    al = sorted(p[0] for p in P); ac = sorted(abs(p[1]) for p in P)
    print('car place, %d frames: along the step (share of the step) median %+.3f, 10-90%% %+.3f..%+.3f; across |.| median %.3f, 90%% %.3f'
          % (len(P), al[len(al) // 2], al[len(al) // 10], al[9 * len(al) // 10], ac[len(ac) // 2], ac[9 * len(ac) // 10]))
    qs = sorted(p[2] for p in P); print('  q: median %.2f, 10-90%% %.2f..%.2f' % (qs[len(qs) // 2], qs[len(qs) // 10], qs[9 * len(qs) // 10]))
for f, label in (('hd', 'car heading'), ('yaw', 'camera yaw'), ('app', 'car turn as seen'), ('scr', 'car on screen (bearing - yaw)')):
    X = res(f, 1)
    big = [x for x in X if abs(x[1]) >= 4]
    if not big: print(label + ': no turning steps'); continue
    sh = sorted(x[0] / x[1] for x in big); ab = sorted(abs(x[0]) for x in X)
    print('%s: residual |.| median %.1f, 90%% %.1f, 99%% %.1f /2048; on turning steps (|step| >= 4, %d) as a share of the step: median %+.2f, 10-90%% %+.2f..%+.2f, beyond +-0.5: %.0f%%'
          % (label, ab[len(ab) // 2], ab[9 * len(ab) // 10], ab[99 * len(ab) // 100], len(big), sh[len(sh) // 2], sh[len(sh) // 10], sh[9 * len(sh) // 10],
             100.0 * sum(1 for v in sh if abs(v) > 0.5) / len(sh)))
