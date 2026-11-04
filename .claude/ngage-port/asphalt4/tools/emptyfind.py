#!/usr/bin/env python3
# emptyfind.py <EKA2L1_POSTDUMP file> <empty.png>...: round 37. The posts that look like a
# known empty frame (the LA hill's: skyline, car, road, no terrain) -- mean absolute
# difference over the picture below the top HUD and above the bottom HUD (y 40-200), the
# minimap left out (x 50-320), against each reference; prints the posts under the limit
# (env LIMIT, default 28) with their nearest neighbours' distances for scale.
import sys, os, struct
import numpy as np
from PIL import Image
dump, refs = sys.argv[1], sys.argv[2:]
lim = float(os.environ.get('LIMIT', '28'))
R = [np.asarray(Image.open(r).convert('RGB')).astype(np.int32)[40:200, 50:320] for r in refs]
prev = None; rows = []
with open(dump, 'rb') as f:
    while True:
        h = f.read(32)
        if len(h) < 32: break
        mg, post, ms, w, hh, pitch, bpp, caller = struct.unpack('<8I', h); px = f.read(pitch * hh)
        if len(px) < pitch * hh: break
        a = np.frombuffer(px, '<u4').reshape(hh, pitch // 4)[:, :w]
        im = (np.dstack([(a >> 16) & 255, (a >> 8) & 255, a & 255]).astype(np.int32) & 0xF8)[40:200, 50:320]
        dmin = min(float(np.abs(im - r).mean()) for r in R)
        rows.append((post, caller, dmin))
for i, (post, caller, dm) in enumerate(rows):
    if dm < lim:
        print('post %d (caller %08x): %.1f from an empty frame; the posts either side %.1f, %.1f'
              % (post, caller, dm, rows[i - 1][2] if i else -1, rows[i + 1][2] if i + 1 < len(rows) else -1))
print('%d posts; the closest: %s' % (len(rows), ', '.join('%d:%.1f' % (p, d) for p, c, d in sorted(rows, key=lambda r: r[2])[:6])))
