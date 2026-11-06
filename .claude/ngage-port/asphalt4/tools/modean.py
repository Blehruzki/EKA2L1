# modean.py <dir>: frames by camera mode (FRC1 + FRC2): per mode, how the camera and the player car
# move frame to frame -- a back-and-forth shows as reversals of the camera's or the car's motion
import sys, struct, glob, os, math, collections
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680; OFF2 = OFF + 16 + 256 * 32
s16 = lambda v: v - 0x10000 if v & 0x8000 else v; s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
d = sys.argv[1]; caps = []
for fn in sorted(glob.glob(os.path.join(d, 'a4tel*.dat'))):
    b = open(fn, 'rb').read(); E1 = {}; E2 = {}
    mg, n = struct.unpack_from('<2I', b, OFF)
    if mg == 0x31435246:
        for k in range(max(0, n - 256), n): e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32); E1[e[0]] = e
    if len(b) >= OFF2 + 16 + 255 * 16:
        mg, n = struct.unpack_from('<2I', b, OFF2)
        if mg == 0x32435246:
            for k in range(max(0, n - 255), n): e = struct.unpack_from('<4I', b, OFF2 + 16 + (k % 255) * 16); E2[e[0]] = e
    caps.append((os.path.basename(fn), E1, E2))
for name, E1, E2 in caps:
    R = []
    for t in sorted(E1):
        e = E1[t]; f2 = E2.get(t)
        R.append(dict(t=t, p=e[1], q=(e[2] & 0xFFFFF) / 65536, comp=e[2] >> 31, mode=(e[2] >> 20) & 0xFF, x=s32(e[3]), z=s32(e[4]),
                      hd=e[5] & 0x7FF, yaw=(e[5] >> 16) & 0x7FF, cx=s32(e[6]), cz=s32(e[7]),
                      y=s16(f2[1] & 0xFFFF) if f2 else None, cy=s16(f2[1] >> 16) if f2 else None,
                      xW=s32(e[3]) + s16(f2[3] & 0xFFFF) if f2 else None, zW=s32(e[4]) + s16(f2[3] >> 16) if f2 else None))
    modes = collections.Counter(r['mode'] for r in R)
    seq = []; 
    for r in R:
        if not seq or seq[-1][0] != r['mode']: seq.append([r['mode'], 1])
        else: seq[-1][1] += 1
    print('%s: %d frames, modes %s; sequence %s' % (name, len(R), dict(modes), ' '.join('%d:%d' % (m, c) for m, c in seq[:30])))
# the last capture's frames in detail: the player car relative to the camera, in the camera's own
# frame (forward/side distance), and the camera's move -- for the frames of a given mode
import sys as _s
if len(_s.argv) > 2:
    want = int(_s.argv[2]); name, E1, E2 = caps[-1]
    R = []
    for t in sorted(E1):
        e = E1[t]; f2 = E2.get(t)
        R.append((t, e[1], (e[2] & 0xFFFFF) / 65536, (e[2] >> 20) & 0xFF, s32(e[3]), s32(e[4]), e[5] & 0x7FF, (e[5] >> 16) & 0x7FF, s32(e[6]), s32(e[7]),
                  s16(f2[1] & 0xFFFF) if f2 else 0, s16(f2[1] >> 16) if f2 else 0, (s32(e[3]) + s16(f2[3] & 0xFFFF)) if f2 else 0, (s32(e[4]) + s16(f2[3] >> 16)) if f2 else 0))
    prev = None
    for r in R:
        if r[3] != want: continue
        t, p, q, m, x, z, hd, yaw, cx, cz, y, cy, xW, zW = r
        dx, dz = x - cx, z - cz; dist = math.hypot(dx, dz); brg = math.atan2(dx, dz) * 1024 / math.pi
        rel = wrap(int(brg - yaw))
        mv = '' if not prev else 'cam moved %4.0f, car moved %4.0f, bearing change %+4d' % (math.hypot(cx - prev[8], cz - prev[9]), math.hypot(x - prev[4], z - prev[5]), wrap(rel - prev[-1]))
        print('t %6d q %.2f | car %7d %7d (step %7d %7d) | cam %7d %7d yaw %4d | dist %4.0f bearing %+5d | %s' % (t % 100000, q, x, z, xW, zW, cx, cz, yaw, dist, rel, mv))
        prev = r + (rel,)
