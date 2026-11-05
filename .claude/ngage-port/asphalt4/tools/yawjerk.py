# yawjerk.py <dirs>: chase camera (mode 2) yaw turn rate per presented frame; its change at a
# step's first frame (q fell) against within a step -- a camera stepping at the game's rate
# shows as a larger change at the first frames
import sys, struct, glob, os, statistics as st
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
wrap = lambda a: ((a + 1024) % 2048) - 1024
for d in sys.argv[1:]:
    E = {}
    for fn in sorted(glob.glob(os.path.join(d, 'a4tel*.dat'))):
        b = open(fn, 'rb').read()
        if len(b) < OFF + 16 + 256 * 32: continue
        mg, n = struct.unpack_from('<2I', b, OFF)
        if mg != 0x31435246: continue
        for k in range(max(0, n - 256), n):
            e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32); E[e[0]] = e
    R = [E[t] for t in sorted(E) if E[t][1]]
    rows = [(e[1], (e[2] & 0xFFFFF) / 65536.0, (e[2] >> 20) & 0xFF, (e[5] >> 16) & 0x7FF, e[5] & 0x7FF) for e in R]
    first, inner, rates, hfirst, hinner = [], [], [], [], []
    prev = None
    for a, b, c in zip(rows, rows[1:], rows[2:]):
        if not (a[2] == b[2] == c[2] == 2): continue
        d1, d2 = (b[0] - a[0]) & 0xFFFFFFFF, (c[0] - b[0]) & 0xFFFFFFFF
        if not d1 or not d2 or d1 > 300 or d2 > 300: continue
        r1, r2 = wrap(b[3] - a[3]) * 85.33 / d1, wrap(c[3] - b[3]) * 85.33 / d2    # /2048 per game step
        h1, h2 = wrap(b[4] - a[4]) * 85.33 / d1, wrap(c[4] - b[4]) * 85.33 / d2
        if abs(r1) > 300 or abs(r2) > 300: continue
        rates.append(abs(r2))
        (first if c[1] < b[1] else inner).append(abs(r2 - r1))
        (hfirst if c[1] < b[1] else hinner).append(abs(h2 - h1))
    q = lambda a, p: sorted(a)[min(len(a) - 1, int(p * len(a)))] if a else 0
    print('%-8s yaw rate /step: median %.1f; its change a frame -- at a step\'s first frame (%d): 50%% %.1f 90%% %.1f 99%% %.1f; within (%d): 50%% %.1f 90%% %.1f 99%% %.1f | car heading: first 50/90/99 %.1f %.1f %.1f, within %.1f %.1f %.1f'
          % (os.path.basename(d.rstrip('/')), q(rates, .5), len(first), q(first, .5), q(first, .9), q(first, .99), len(inner), q(inner, .5), q(inner, .9), q(inner, .99),
             q(hfirst, .5), q(hfirst, .9), q(hfirst, .99), q(hinner, .5), q(hinner, .9), q(hinner, .99)))
