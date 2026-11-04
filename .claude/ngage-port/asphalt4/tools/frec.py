#!/usr/bin/env python3
# frec.py <a4tel dir or files> [mode]: round 41, the frame record (A4_FRAMEREC) of every capture,
# joined (entries are deduplicated by their render ms). Per frame: render start, present, q16,
# camera mode, the player car (x, z, heading) and the camera (yaw, x, z) as drawn. Prints the
# lag from render start to present, then by present time: how evenly the car's drawn speed and
# turn go (against the 9 neighbours' median), and the car's heading against the camera's yaw
# frame to frame -- whose reversals while it turns are "the car going back and forth".
# mode 'list' prints the frames.
import sys, struct, glob, os, math, statistics as st
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
args = sys.argv[1:]; mode = args.pop() if args and args[-1] == 'list' else ''
files = []
for a in args: files += sorted(glob.glob(os.path.join(a, 'a4tel*.dat'))) if os.path.isdir(a) else [a]
E = {}
for fn in files:
    d = open(fn, 'rb').read()
    if len(d) < OFF + 16 + 256 * 32: print(fn, 'no frame record'); continue
    mg, n = struct.unpack_from('<2I', d, OFF)
    if mg != 0x31435246: print(fn, 'frame record magic %08x' % mg); continue
    for k in range(max(0, n - 256), n):
        e = struct.unpack_from('<8I', d, OFF + 16 + (k % 256) * 32)
        E[e[0]] = e
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
F = [E[t] for t in sorted(E)]
print('%d files, %d frames recorded' % (len(files), len(F)))
if not F: sys.exit()
wrap = lambda a: ((a + 1024) % 2048) - 1024
R = [dict(t=e[0], p=e[1], q=(e[2] & 0xFFFFF) / 65536.0, comp=e[2] >> 31, mode=(e[2] >> 20) & 0xFF,
          x=s32(e[3]), z=s32(e[4]), hd=e[5] & 0x7FF, yaw=(e[5] >> 16) & 0x7FF, cx=s32(e[6]), cz=s32(e[7])) for e in F]
if mode == 'list':
    for r in R: print('%9d %9d q %.3f%s mode %3d car %7d %7d hd %4d cam yaw %4d %7d %7d' % (r['t'], r['p'], r['q'], '' if r['comp'] else '-', r['mode'], r['x'], r['z'], r['hd'], r['yaw'], r['cx'], r['cz']))
    sys.exit()
P = [r for r in R if r['p']]
lag = sorted((r['p'] - r['t']) & 0xFFFFFFFF for r in P)
print('presented %d; render start to present: median %d ms, 10-90%% %d..%d, max %d' % (len(P), lag[len(lag) // 2], lag[len(lag) // 10], lag[9 * len(lag) // 10], lag[-1]))
dts = sorted((b['p'] - a['p']) & 0xFFFFFFFF for a, b in zip(P, P[1:]))
print('ms between presents: median %d, 10-90%% %d..%d' % (dts[len(dts) // 2], dts[len(dts) // 10], dts[9 * len(dts) // 10]))
qs = sorted(r['q'] for r in R if r['comp'])
if qs: print('fraction q: median %.2f, 10-90%% %.2f..%.2f; frames with none %d' % (qs[len(qs) // 2], qs[len(qs) // 10], qs[9 * len(qs) // 10], sum(1 for r in R if not r['comp'])))
def even(v, label, floor):
    r = []
    for i in range(4, len(v) - 4):
        nb = v[i - 4:i + 5]
        if not (all(x > 0 for x in nb) or all(x < 0 for x in nb)): continue
        m = st.median(nb)
        if abs(m) > floor: r.append(v[i] / m)
    r.sort(); k = len(r)
    if k: print('  %s / neighbours: quartiles %.2f..%.2f, 10-90%% %.2f..%.2f, off by >25%%: %.0f%% (%d)' % (label, r[k // 4], r[3 * k // 4], r[k // 10], r[9 * k // 10], 100.0 * sum(1 for x in r if abs(x - 1) > .25) / k, k))
for key in ('t', 'p'):
    S = P if key == 'p' else R
    sp, tr, rel = [], [], []
    for a, b in zip(S, S[1:]):
        dt = (b[key] - a[key]) & 0xFFFFFFFF
        if not dt or dt > 1000: sp.append(0.0); tr.append(0.0); rel.append(0); continue
        sp.append(math.hypot(b['x'] - a['x'], b['z'] - a['z']) / dt)
        tr.append(wrap(b['hd'] - a['hd']) / dt)
        rel.append(wrap((b['hd'] - b['yaw']) - (a['hd'] - a['yaw'])))
    print('-- timed by %s:' % ('render start' if key == 't' else 'present'))
    even(sp, 'drawn speed', 0.5); even(tr, 'turn rate  ', 0.02)
    if key == 'p':
        rv = mv = 0
        for a, b in zip(rel, rel[1:]):
            if abs(a) >= 3 and abs(b) >= 3:
                mv += 1
                if a * b < 0: rv += 1
        ab = sorted(abs(x) for x in rel)
        print('  car heading less camera yaw, change a frame: median %d, 90%% %d, 99%% %d /2048; reversals %d of %d frames where it moves (>= 3 both)'
              % (ab[len(ab) // 2], ab[9 * len(ab) // 10], ab[99 * len(ab) // 100], rv, mv))
# one-frame reversals of the camera's yaw and the car's heading (|change| >= 4), with the q of
# the frame before and of the reversing frame: an over-predicted camera shows as a late-in-step
# frame (high q) swinging past and the next step's frame coming back
for key in ('yaw', 'hd'):
    dd = [wrap(b[key] - a[key]) for a, b in zip(P, P[1:])]
    bl = [(P[i + 1]['t'] % 100000, dd[i - 1], dd[i], dd[i + 1], round(P[i]['q'], 2), round(P[i + 1]['q'], 2))
          for i in range(1, len(dd) - 1) if dd[i] * dd[i - 1] < 0 and dd[i] * dd[i + 1] < 0 and abs(dd[i]) >= 4]
    print('%s one-frame reversals (|change| >= 4): %d in %d frames; first: %s' % ('camera yaw' if key == 'yaw' else 'car heading', len(bl), len(P), bl[:6]))

