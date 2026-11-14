#!/usr/bin/env python3
# Round 72: the scenery per render, from the frame record (FRC1) of each capture (a4_gl.c A4_GL_FREC under
# A4_ENG3): e[2] = the camera's track segment (12 bits) | its direction word set << 12 | the scenery objects the
# GPU took (7 bits, saturated) << 13 | the camera mode (race+0x94) << 20 | the engine's substep << 28.
# Prints each capture's renders where the objects fell under half the render before's (from over 16), with the
# renders around them, and a summary of objects by segment (direction 0, chase modes). Usage: objrec.py <dir> [ctx]
import sys, struct, glob, os, collections
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
s32 = lambda v: v - (1 << 32) if v >> 31 else v
ctx = int(sys.argv[2]) if len(sys.argv) > 2 else 4
byseg = collections.defaultdict(list)
for fn in sorted(glob.glob(os.path.join(sys.argv[1], 'a4tel*.dat'))):
    d = open(fn, 'rb').read()
    if len(d) < OFF + 16 + 256 * 32: continue
    mg, n, lc, last = struct.unpack_from('<4I', d, OFF)
    if mg != 0x31435246: continue
    cap = struct.unpack_from('<I', d, 8)[0]
    k = min(n, 256); E = []
    for i in range(n - k, n):
        e = struct.unpack_from('<8I', d, OFF + 16 + (i % 256) * 32)
        E.append(dict(ms=e[0], seg=e[2] & 0xFFF, dir=(e[2] >> 12) & 1, obj=(e[2] >> 13) & 0x7F, mode=(e[2] >> 20) & 0xFF,
                      sub=(e[2] >> 28) & 3, car=(s32(e[3]), s32(e[4])), hd=e[5] & 0xFFFF, yaw=e[5] >> 16, cam=(s32(e[6]), s32(e[7]))))
    print('%s capidx %d renders %d (kept %d) line rows %d%s' % (fn.split('/')[-1], cap, n, k, lc,
          ' last: row %d undrawn %d frame %d' % (last & 0xFF, (last >> 8) & 0xFF, last >> 16) if lc else ''))
    for e in E:
        if not e['dir'] and e['mode'] in (1, 2, 3, 0x2A): byseg[e['seg']].append(e['obj'])
    drops = [i for i in range(1, len(E)) if E[i - 1]['obj'] > 16 and 2 * E[i]['obj'] < E[i - 1]['obj']]
    for i in drops:
        print('  -- drop')
        for j in range(max(0, i - ctx), min(len(E), i + ctx + 1)):
            e = E[j]
            print('  %s ms %9d seg %4d dir %d obj %3d mode %3d sub %d cam (%7d %7d) yaw %5d car (%7d %7d) hd %5d' % (
                '>>' if j == i else '  ', e['ms'], e['seg'], e['dir'], e['obj'], e['mode'], e['sub'], e['cam'][0], e['cam'][1],
                e['yaw'], e['car'][0], e['car'][1], e['hd']))
print('objects by segment (direction 0, chase modes): ' + ' '.join('%d:%d-%d' % (s, min(v), max(v)) for s, v in sorted(byseg.items())))
