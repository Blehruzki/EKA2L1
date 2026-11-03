# shadowsink.py <dir>: A4_SHADSINK's counters (check build) per capture, as the growth since the
# capture before -- shadow triangles, covered (depth-tested on the GPU) ones, their pixels and
# those a tested shadow loses that the game paints, at biases 0 .. 1/4 -- with each capture's
# brightness (the game's own picture, rows 20-120: the LA tunnel reads under 100).
import sys, struct, glob
HDR = 256; FPS = 560; FB = 320 * 240 * 2
OFF = HDR + FPS + 2 * FB + 125 * 4 + 256
rows = []
for f in glob.glob(sys.argv[1] + '/a4tel*.dat'):
    d = open(f, 'rb').read()
    if len(d) < OFF + 256: continue
    cap = struct.unpack_from('<I', d, 8)[0]
    px = struct.unpack_from('<76800H', d, HDR + FPS); s = n = 0
    for y in range(20, 120, 4):
        for x in range(0, 320, 4):
            p = px[y * 320 + x]; s += ((p >> 11) & 31) * 8 + ((p >> 5) & 63) * 4 + (p & 31) * 8; n += 1
    rows.append((cap, struct.unpack_from('<64I', d, OFF), s // (n * 3)))
rows.sort()
print('cap  lum | tris covered | covered px  hidden@0  @1/64  @1/32  @1/16  @1/8  @1/4 | uncovered px hidden@0')
for (c0, k0, _), (c1, k1, lum) in zip(rows, rows[1:]):
    d = [b - a for a, b in zip(k0, k1)]
    if d[2] <= 0: continue
    print('%4d %4d | %5d %5d | %8d  %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%% | %8d %5.1f%%' % (
        c1, lum, d[0], d[1], d[2], *(100.0 * d[3 + i] / d[2] for i in range(6)), d[9], 100.0 * d[10] / d[9] if d[9] else 0))
