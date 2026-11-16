# camoff.py <dir|file>...: per run, the chase camera's yaw less the car's heading (FRC1), split by heading still / moving
import sys, struct, glob, os
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
wrap = lambda a: ((a + 1024) % 2048) - 1024
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
for d in sys.argv[1:]:
    E = {}
    for f in (sorted(glob.glob(os.path.join(d, '*.dat'))) if os.path.isdir(d) else [d]):
        b = open(f, 'rb').read()
        if len(b) < OFF + 16 or struct.unpack_from('<I', b, OFF)[0] != 0x31435246: continue
        n = struct.unpack_from('<I', b, OFF + 4)[0]
        for k in range(max(0, n - 256), n):
            e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32); E[e[0]] = e
    T = [t for t in sorted(E) if ((E[t][2] >> 20) & 0xFF) == 2]
    st, mv, spd = [], [], []
    for a, b in zip(T, T[1:]):
        if b - a > 300: continue
        hd, yaw = E[b][5] & 0x7FF, (E[b][5] >> 16) & 0x7FF
        dh = wrap(hd - (E[a][5] & 0x7FF)); off = abs(wrap(yaw - hd))
        spd.append(((s32(E[b][3]) - s32(E[a][3])) ** 2 + (s32(E[b][4]) - s32(E[a][4])) ** 2) ** .5)
        (mv if dh else st).append(off)
    m = lambda L: sum(L) / len(L) if L else 0
    spd.sort()
    print('%-8s chase renders %4d, speed median %4.0f | heading still %4d: |yaw-hd| mean %4.1f, >=3 %3d%% | moving %4d: mean %4.1f, >=3 %3d%%'
          % (os.path.basename(d.rstrip('/')), len(st) + len(mv), spd[len(spd) // 2] if spd else 0, len(st), m(st), 100 * sum(x >= 3 for x in st) // max(1, len(st)),
             len(mv), m(mv), 100 * sum(x >= 3 for x in mv) // max(1, len(mv))))
