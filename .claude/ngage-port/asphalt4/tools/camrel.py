# camrel.py <dirs>: from the frame record, the player car as seen from the camera: its bearing
# off the camera's yaw and its distance, per frame -- a chase camera keeps both nearly constant
import sys, struct, glob, os, math, statistics as st
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
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
    R = [E[t] for t in sorted(E)]
    rows = []
    for e in R:
        mode = (e[2] >> 20) & 0xFF
        x, z, hd, yaw, cx, cz = s32(e[3]), s32(e[4]), e[5] & 0x7FF, (e[5] >> 16) & 0x7FF, s32(e[6]), s32(e[7])
        dx, dz = x - cx, z - cz; dist = math.hypot(dx, dz)
        if mode != 2 or dist > 3000: continue
        brg = math.atan2(dx, dz) * 1024 / math.pi      # some angle convention; consistent across runs
        rows.append((dist, brg, yaw, hd))
    if len(rows) < 20: print(d, 'too few chase frames', len(rows)); continue
    # the bearing less the yaw, and the heading less the yaw: their spread
    rel = [wrap(int(r[1] - r[2])) for r in rows]; med = st.median(rel)
    relc = sorted(abs(wrap(int(x - med))) for x in rel)
    hrel = [wrap(r[3] - r[2]) for r in rows]; hm = st.median(hrel)
    hc = sorted(abs(wrap(int(x - hm))) for x in hrel)
    ds = sorted(r[0] for r in rows)
    q = lambda a, p: a[min(len(a) - 1, int(p * len(a)))]
    print('%-10s %3d chase frames: distance median %.0f (10-90%% %.0f..%.0f); car bearing off its median: 50%% %d, 90%% %d, 99%% %d /2048; car heading less camera yaw off its median: 50%% %d, 90%% %d, 99%% %d'
          % (os.path.basename(d.rstrip('/')), len(rows), q(ds, .5), q(ds, .1), q(ds, .9), q(relc, .5), q(relc, .9), q(relc, .99), q(hc, .5), q(hc, .9), q(hc, .99)))
