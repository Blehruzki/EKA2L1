#!/usr/bin/env python3
# poststamp.py <EKA2L1_POSTDUMP file> <trace.regs> <frame-mark-pc hex>: round 37's bench
# check. An A4_FRAMEMARK build stamps each present's count in the bottom row and calls
# a4_frame_mark(had, prev, frames, skipped) for EKA2L1_PCTRACE_REGS; this ties every post
# to the triangles of the frame it shows, and lists the posts of frames under 300
# triangles (with the 3D frame before) -- an empty frame that reached the panel shows here.
import sys, struct
dump, regs, mpc = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
marks = {}
d = open(regs, 'rb').read()
for i in range(len(d) // 68):
    e = struct.unpack_from('<17I', d, i * 68)
    if e[0] == mpc: marks.setdefault(e[3], []).append((e[1], e[2], e[4]))   # frames -> (had, prev, skipped)
posts = []
with open(dump, 'rb') as f:
    while True:
        h = f.read(32)
        if len(h) < 32: break
        mg, post, ms, w, hh, pitch, bpp, caller = struct.unpack('<8I', h); px = f.read(pitch * hh)
        row = struct.unpack_from('<32I', px, (hh - 1) * pitch)
        st = sum(1 << k for k in range(32) if (row[k] & 0xFF) > 0x80)
        posts.append((post, ms, caller, st))
print('%d posts, %d marked presents' % (len(posts), len(marks)))
tied = sum(1 for p in posts if p[3] in marks)
print('%d posts tied to a mark' % tied)
for post, ms, caller, st in posts:
    m = marks.get(st)
    if not m: continue
    had, prev, sk = m[-1]
    if had and had < 300:
        print('post %d (caller %08x) shows present %d: %d triangles, the 3D frame before %d, present %s'
              % (post, caller, st, had, prev, 'held' if sk else 'made'))
