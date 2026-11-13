#!/usr/bin/env python3
# wreck.py <crec.bin> ...: round 65, the player's wrecks in a race's car records (A4_CARREC, one record a game
# step): each run of steps with +034 (the wreck path's frame) not -1 -- its length in steps, the car's speed
# (+7D0) and step displacement before it, the distance it covered, its largest step, and its height's range.
# A wreck flung across the map (round 50's r7) shows as a step of thousands of units.
import sys, os, math
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'crec.py')).read().split('\nA = load(')[0]
ns = {}; exec(compile(src, 'crec.py', 'exec'), ns); load = ns['load']
for fn in sys.argv[1:]:
    A = load(fn); W = [w for h, w in A]
    ep = []; i = 0
    while i < len(W):
        if W[i][0x34 // 4] != -1:
            j = i
            while j < len(W) and W[j][0x34 // 4] != -1: j += 1
            ep.append((i, j)); i = j
        else: i += 1
    print('%s: %d steps, %d wrecks' % (os.path.basename(fn), len(W), len(ep)))
    for i, j in ep:
        p = lambda k: (W[k][1], W[k][2], W[k][3])
        st = [math.hypot(p(k)[0] - p(k - 1)[0], p(k)[2] - p(k - 1)[2]) for k in range(max(1, i), min(j + 1, len(W)))]
        pre = math.hypot(p(i - 1)[0] - p(i - 2)[0], p(i - 1)[2] - p(i - 2)[2]) if i >= 2 else 0
        ys = [p(k)[1] for k in range(i, j)]
        print('  steps %5d..%5d (%3d): speed before %4d, step before %4.0f; covered %6.0f, first step %4.0f, largest %4.0f; y %d..%d; frames %d..%d'
              % (i, j, j - i, W[i - 1][0x7D0 // 4] if i else 0, pre, sum(st), st[0] if st else 0, max(st) if st else 0, min(ys), max(ys),
                 W[i][0x34 // 4], W[j - 1][0x34 // 4]))
