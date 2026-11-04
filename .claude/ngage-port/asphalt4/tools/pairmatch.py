#!/usr/bin/env python3
# pairmatch.py <dumpA> <a0:a1> <dumpB> <b0:b1> <out.png> [n]: for posts a0..a1 of dump A,
# the post of b0..b1 in dump B whose minimap (x 4-45, y 70-111) is nearest, and n pairs
# (A over B) of the player car's area (x 80-240, y 120-230) at 2x -- the same place in two
# builds. Prints each pair's posts and minimap distance.
import sys, struct
import numpy as np
from PIL import Image
def load(fn, lo, hi):
    out = {}
    with open(fn, 'rb') as f:
        while True:
            h = f.read(32)
            if len(h) < 32: break
            mg, post, ms, w, hh, pitch, bpp, caller = struct.unpack('<8I', h); px = f.read(pitch * hh)
            if len(px) < pitch * hh: break
            if lo <= post <= hi:
                a = np.frombuffer(px, '<u4').reshape(hh, pitch // 4)[:, :w]
                out[post] = np.dstack([(a >> 16) & 255, (a >> 8) & 255, a & 255]).astype(np.int32)
            if post > hi: break
    return out
A = load(sys.argv[1], *map(int, sys.argv[2].split(':')))
B = load(sys.argv[3], *map(int, sys.argv[4].split(':')))
n = int(sys.argv[6]) if len(sys.argv) > 6 else 6
mm = lambda im: im[70:111, 4:45]
pairs = []
for pa in sorted(A)[::max(1, len(A) // n)][:n]:
    d, pb = min((float(np.abs(mm(A[pa]) - mm(B[pb])).mean()), pb) for pb in B)
    pairs.append((pa, pb, d)); print('A %d ~ B %d (minimap %.1f)' % (pa, pb, d))
cols = [np.concatenate([A[pa][120:230, 80:240], B[pb][120:230, 80:240]], axis=0) for pa, pb, d in pairs]
row = np.concatenate(cols, axis=1).astype(np.uint8)
Image.fromarray(row).resize((row.shape[1] * 2, row.shape[0] * 2), Image.NEAREST).save(sys.argv[5])
