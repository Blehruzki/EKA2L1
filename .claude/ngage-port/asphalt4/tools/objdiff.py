import sys, struct, collections, numpy as np
src = open('/home/user/EKA2L1/.claude/ngage-port/asphalt4/tools/objorder.py').read()
src = src.split("fs = load(sys.argv_saved[1])")[0]
sys.argv = ['x', 'ldump60.bin']; __file__ = '/home/user/EKA2L1/.claude/ngage-port/asphalt4/tools/objorder.py'
exec(src)
fs = load('ldump60.bin'); same = 0; diff = 0; pairs = collections.Counter(); sizes = collections.Counter()
for h, V, T, Kb, Zb, ext, O in fs[:30]:
    _, n, F, cx, cy, zbits, frame, nk = h
    if n * 36 > len(V) or O is None: continue
    F = max(1, min(16383, F))
    Pv = np.frombuffer(V[:n * 36], dtype='<i2').reshape(n, 18); P = np.stack([Pv[:, 0:3], Pv[:, 6:9], Pv[:, 12:15]], axis=1)
    Tw = struct.unpack_from('<%dI' % n, T, 0); Bx = [struct.unpack_from('<4h', T, 0x2000 + 8 * i) for i in range(n)]
    ORD = struct.unpack_from('<%dH' % n, T, 0x7000); Zz = [struct.unpack_from('<2H', Zb, 4 * i) for i in range(n)]
    ob = struct.unpack_from('<%dH' % n, O, 0)
    cnt = collections.Counter(ob)
    bt = group(n, 'A', Kb[:n], Tw, Bx, ORD, Zz, zbits >= 24)
    a = raster2([(i, R['k'] >> 8, 0) for runs in bt for R in runs for i in R['faces']], P, F, cx, cy)
    d = raster2([(i, 1, 0) for i in range(n)], P, F, cx, cy)
    m = (a != d) & (a >= 0) & (d >= 0)
    for wa, wd in zip(a[m].tolist(), d[m].tolist()):
        if ob[wa] == ob[wd]: same += 1
        else:
            diff += 1; pairs[(min(cnt[ob[wa]], cnt[ob[wd]]) <= 16, max(cnt[ob[wa]], cnt[ob[wd]]) > 64)] += 1
            sizes[(cnt[ob[wa]], cnt[ob[wd]])] += 1
print('changed pixels (both drawn): the same mesh %d, two meshes %d' % (same, diff))
print('two meshes, by (one of them <=16 faces, one of them >64 faces):', dict(pairs))
print('most common (faces of the mesh shown now, faces of the mesh a depth test shows):', sizes.most_common(10))
