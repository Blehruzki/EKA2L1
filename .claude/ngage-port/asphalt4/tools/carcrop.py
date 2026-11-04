#!/usr/bin/env python3
# carcrop.py <EKA2L1_POSTDUMP file> <out.png> <post>...: the player car's area (x 80-240,
# y 130-230) of the given posts, side by side at 2x, for looking at its shadow.
import sys, struct
import numpy as np
from PIL import Image
dump, out, want = sys.argv[1], sys.argv[2], [int(x) for x in sys.argv[3:]]
got = {}
with open(dump, 'rb') as f:
    while True:
        h = f.read(32)
        if len(h) < 32: break
        mg, post, ms, w, hh, pitch, bpp, caller = struct.unpack('<8I', h); px = f.read(pitch * hh)
        if len(px) < pitch * hh: break
        if post in want:
            a = np.frombuffer(px, '<u4').reshape(hh, pitch // 4)[:, :w]
            got[post] = np.dstack([(a >> 16) & 255, (a >> 8) & 255, a & 255]).astype(np.uint8)[130:230, 80:240]
        if post > max(want): break
row = np.concatenate([got[p] for p in want if p in got], axis=1)
Image.fromarray(row).resize((row.shape[1] * 2, row.shape[0] * 2), Image.NEAREST).save(out)
print('posts', [p for p in want if p in got])
