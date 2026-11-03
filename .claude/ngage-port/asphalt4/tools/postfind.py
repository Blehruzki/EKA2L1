#!/usr/bin/env python3
# postfind.py <EKA2L1_POSTDUMP file> <telemetry dir>: for each capture C:\a4telN.dat in the
# directory, the posted frame nearest its framebuffer (mean absolute difference per channel,
# 0 = the very frame). Round 37: a drop-frame capture (A4_DROPCAP) whose picture is among the
# posts reached the panel; under A4_DROPHOLD it must not (nearest well above 0).
import sys, struct, glob, os
import numpy as np
dump, teld = sys.argv[1], sys.argv[2]
def rgb565(d, w, h, stride):
    a = np.frombuffer(d, '<u2', count=(stride // 2) * h).reshape(h, stride // 2)[:, :w].astype(np.int32)
    return np.dstack([(a >> 11) << 3, ((a >> 5) & 63) << 2, (a & 31) << 3])
posts = []
with open(dump, 'rb') as f:
    while True:
        hd = f.read(32)
        if len(hd) < 32: break
        mg, post, ms, w, h, pitch, bpp, _ = struct.unpack('<8I', hd); d = f.read(pitch * h)
        if len(d) < pitch * h: break
        if bpp == 32:
            a = np.frombuffer(d, '<u4').reshape(h, pitch // 4)[:, :w]
            im = np.dstack([(a >> 16) & 255, (a >> 8) & 255, a & 255]).astype(np.int32)
        else:
            im = rgb565(d, w, h, pitch)
        posts.append((post, ms, im & 0xF8))     # compare at 5 bits a channel: the post is 565 widened
print('%d posts' % len(posts))
for fn in sorted(glob.glob(os.path.join(teld, 'a4tel*.dat'))):
    d = open(fn, 'rb').read()
    if len(d) < 256: continue
    h = struct.unpack_from('<64I', d)
    W, H, stride, fbb, fpsb = h[4], h[5], h[6], h[7], h[26]
    if stride < W * 2: stride *= 2          # FB_STRIDE is in pixels
    im = rgb565(d[256 + fpsb: 256 + fpsb + fbb], W, H, stride) & 0xF8
    best = min((float(np.abs(p[2][:H, :W] - im).mean()), k) for k, p in enumerate(posts))
    k = best[1]
    print('%s capidx %d fcount %d: nearest post %d at %.2f (the posts either side %s)'
          % (os.path.basename(fn), h[2], h[3], posts[k][0], best[0],
             ', '.join('%.1f' % np.abs(posts[j][2][:H, :W] - im).mean() for j in (k - 1, k + 1) if 0 <= j < len(posts))))
