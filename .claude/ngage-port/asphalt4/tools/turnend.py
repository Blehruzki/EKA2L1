#!/usr/bin/env python3
# turnend.py <a4tel dir> [list]: round 77 -- how a turn ends, from the frame record (FRC1, any build: the stock step's or
# the 3x engine's). Per render the car's heading and the camera's yaw (2,048 a turn); a turn is a run of renders whose
# heading moves, its end the first render that holds after it. Per end: the heading's last moves, ms a render, and how
# far the car less the camera swings after it (the most, and the renders until it is back within 2) -- the owner's
# snap-back is the car's angle on screen swinging out and back while the camera eases. 'list': every render.
import sys, struct, glob, os
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
args = sys.argv[1:]; mode = args.pop() if args and args[-1] == 'list' else ''
E = {}
for d in args:
    for f in (sorted(glob.glob(os.path.join(d, 'a4tel*.dat'))) if os.path.isdir(d) else [d]):
        b = open(f, 'rb').read()
        if len(b) < OFF + 16 or struct.unpack_from('<I', b, OFF)[0] != 0x31435246: continue
        n = struct.unpack_from('<I', b, OFF + 4)[0]
        for k in range(max(0, n - 256), n):
            e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32)
            E[e[0]] = e
wrap = lambda a: ((a + 1024) % 2048) - 1024
R = [(t, E[t][5] & 0x7FF, (E[t][5] >> 16) & 0x7FF, (E[t][2] >> 20) & 0xFF) for t in sorted(E)]
R = [r for r in R if r[3] in (1, 2, 3, 0x2A)]          # chase cameras
print('%d renders (chase cameras)' % len(R))
if mode == 'list':
    for a, b in zip(R, R[1:]):
        print('%9d +%3d  hd %4d (%+4d)  yaw %4d (%+4d)  car-cam %+4d' % (b[0], b[0] - a[0], b[1], wrap(b[1] - a[1]), b[2], wrap(b[2] - a[2]), wrap(b[1] - b[2])))
    sys.exit()
ends, i = [], 1
while i < len(R) - 1:
    if wrap(R[i][1] - R[i - 1][1]) != 0 and wrap(R[i + 1][1] - R[i][1]) == 0 and R[i + 1][0] - R[i][0] < 300:
        ends.append(i + 1)
    i += 1
sw, back = [], []
for j in ends:
    pre = [wrap(R[k][1] - R[k - 1][1]) for k in range(max(1, j - 4), j)]
    post = [wrap(R[k][1] - R[k][2]) for k in range(j, min(len(R), j + 30))]
    if not post: continue
    base = wrap(R[j - 1][1] - R[j - 1][2])
    m = max(post, key=lambda v: abs(v - base)) - base
    nb = next((k for k, v in enumerate(post) if abs(v) <= 2), len(post))
    sw.append(abs(m)); back.append(nb)
    print('end at %9d: heading moves before %s  car-cam %+d -> swing %+d, back within 2 after %d renders' % (R[j][0], pre, base, m, nb))
if sw:
    sw.sort(); back.sort()
    print('%d turn ends: swing median %d max %d; renders back median %d' % (len(sw), sw[len(sw) // 2], sw[-1], back[len(back) // 2]))
