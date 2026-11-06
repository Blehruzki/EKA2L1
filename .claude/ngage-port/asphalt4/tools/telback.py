# telback.py <a4tel dir>...: camback for the phone-format records (FRC1 + FRC2): per camera mode, the
# player car's bearing and elevation in the camera's view frame to frame, and their reversals
import sys, struct, glob, os, math, collections
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680; OFF2 = OFF + 16 + 256 * 32
s16 = lambda v: v - 0x10000 if v & 0x8000 else v; s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
pq = lambda a, p: sorted(a)[min(len(a) - 1, int(p * len(a)))] if a else 0
A = lambda r: r * 1024 / math.pi
for d in sys.argv[1:]:
    E1, E2 = {}, {}
    for fn in sorted(glob.glob(os.path.join(d, 'a4tel*.dat'))):
        b = open(fn, 'rb').read()
        mg, n = struct.unpack_from('<2I', b, OFF)
        if mg == 0x31435246:
            for k in range(max(0, n - 256), n): e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32); E1[e[0]] = e
        if len(b) >= OFF2 + 16 + 255 * 16:
            mg, n = struct.unpack_from('<2I', b, OFF2)
            if mg == 0x32435246:
                for k in range(max(0, n - 255), n): e = struct.unpack_from('<4I', b, OFF2 + 16 + (k % 255) * 16); E2[e[0]] = e
    by = collections.defaultdict(list)
    for t in sorted(E1):
        e = E1[t]; f2 = E2.get(t)
        mode = (e[2] >> 20) & 0xFF; x, z = s32(e[3]), s32(e[4]); cx, cz = s32(e[6]), s32(e[7]); yaw = (e[5] >> 16) & 0x7FF
        dx, dz = x - cx, z - cz; dist = math.hypot(dx, dz)
        brg = wrap(int(round(A(math.atan2(dx, dz)) - yaw)))
        elev = wrap(int(round(A(math.atan2(s16(f2[1] & 0xFFFF) - s16(f2[1] >> 16), max(dist, 1)))))) if f2 else 0
        by[mode].append((brg, elev, dist))
    print(os.path.basename(d.rstrip('/')))
    for mode in sorted(by):
        v = by[mode]
        db = [wrap(v[i + 1][0] - v[i][0]) for i in range(len(v) - 1)]; de = [wrap(v[i + 1][1] - v[i][1]) for i in range(len(v) - 1)]
        rb = sum(1 for i in range(len(db) - 1) if db[i] * db[i + 1] < 0 and abs(db[i]) >= 4 and abs(db[i + 1]) >= 4)
        re_ = sum(1 for i in range(len(de) - 1) if de[i] * de[i + 1] < 0 and abs(de[i]) >= 4 and abs(de[i + 1]) >= 4)
        print('  mode 0x%02x: %4d frames | bearing change 50%% %d 90%% %d 99%% %d max %d, reversals %d | elevation change 99%% %d, reversals %d'
              % (mode, len(v), pq([abs(x) for x in db], .5), pq([abs(x) for x in db], .9), pq([abs(x) for x in db], .99), max([abs(x) for x in db] or [0]), rb,
                 pq([abs(x) for x in de], .99), re_))
