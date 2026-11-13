#!/usr/bin/env python3
# carcen.py <pcmem dump>: round 67, the A4_CARCENSUS ring (a4_car_census, a4_gl.c): the car meshes (CBasicMesh,
# the mesh routine called from +34AEA) per call -- per frame, the calls and faces; the distinct meshes with their
# vertex / face counts, mode, view z range and vertices nearer than ZW 126 / 64; faces by vtable, flag class and
# texture kind; and one mesh's first faces raw.
import sys, struct, collections
d = open(sys.argv[1], 'rb').read(); o = 0; R = {}
while o + 12 <= len(d):
    pc, ad, n = struct.unpack_from('<3I', d, o); o += 12
    for k in range(0, n - 255, 256):
        e = struct.unpack_from('<64I', d, o + k)
        if e[0]: R[(e[0], e[1], e[6], k)] = e
    o += n
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
E = sorted(R.values())
fr = collections.defaultdict(lambda: [0, 0])
for e in E: fr[e[0]][0] += 1; fr[e[0]][1] += e[4]
c = sorted(fr.values())
print('%d calls over %d frames: calls a frame median %d (max %d), faces a frame median %d (max %d)' % (len(E), len(fr), sorted(x[0] for x in c)[len(c) // 2], max(x[0] for x in c), sorted(x[1] for x in c)[len(c) // 2], max(x[1] for x in c)))
M = {}
for e in E: M.setdefault((e[1], e[3]), []).append(e)
print('%d distinct meshes (vertex array, face array)' % len(M))
for (v, f), L in sorted(M.items(), key=lambda x: -len(x[1]))[:16]:
    e = L[-1]
    near = sum(1 for x in L if x[9]); near64 = sum(1 for x in L if x[10])
    print('  verts %08x n %4d faces %08x n %4d mode %d | calls %4d, z %6d..%6d (last), calls with vertices nearer than 126: %d, 64: %d'
          % (v, e[2], f, e[4], e[5], len(L), s32(e[7]), s32(e[8]), near, near64))
    print('     vtables %s | flags %s | colour key %d, alpha map %d, descriptors %d (first %dx%d), flags OR %08x, u..%d v..%d'
          % (', '.join('%08x x%d' % (e[11 + 2 * k], e[12 + 2 * k]) for k in range(4) if e[11 + 2 * k]),
             ', '.join('%06x x%d' % (e[19 + 2 * j], e[20 + 2 * j]) for j in range(3) if e[20 + 2 * j]), e[59], e[60], e[25], e[57], e[58], e[26], e[61], e[62]))
e = max(E, key=lambda x: x[4])
print('largest mesh, first faces raw:')
for k in range(2): print('  ', ' '.join('%08x' % w for w in e[27 + 13 * k: 40 + 13 * k]))
