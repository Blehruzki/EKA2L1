#!/usr/bin/env python3
# tunnelfind.py <EKA2L1_POSTDUMP file>: the posts inside the LA tunnel (its orange walls:
# the share of pixels in y 30-150 with R > 150, 60 <= G <= 150, B < 90), as runs of posts.
import sys, struct
import numpy as np
runs = []; cur = None
with open(sys.argv[1], 'rb') as f:
    while True:
        h = f.read(32)
        if len(h) < 32: break
        mg, post, ms, w, hh, pitch, bpp, caller = struct.unpack('<8I', h); px = f.read(pitch * hh)
        if len(px) < pitch * hh: break
        a = np.frombuffer(px, '<u4').reshape(hh, pitch // 4)[30:150, :w]
        r, g, b = (a >> 16) & 255, (a >> 8) & 255, a & 255
        sh = float(((r > 150) & (g >= 60) & (g <= 150) & (b < 90)).mean())
        if sh > 0.35:
            if cur and post <= cur[1] + 3: cur[1] = post
            else: cur = [post, post]; runs.append(cur)
print('tunnel runs (first post, last post):', [tuple(r) for r in runs if r[1] - r[0] >= 10])
