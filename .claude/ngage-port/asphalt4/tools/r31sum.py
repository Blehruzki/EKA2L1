# r31sum.py <dir>: round 31 telemetry -- per window (the ring) and per A/B slot over the race
# (aggregates 'AG26': slot bit 1 = AB_HSPEC): renders a second, ms a render, half-steps a
# second (header [61] in a step-timed build: game speed, 23.44 at real time), skipped presents.
import sys, struct, glob
AGG = 256 + 560 + 153600 + 122 * 4
rows = []
for f in glob.glob(sys.argv[1] + '/a4tel*.dat'):
    d = open(f, 'rb').read(); h = struct.unpack_from('<64I', d, 0); rows.append((h[2], h, d))
rows.sort()
print('capidx mode  spec  rend  win ms  ms/rend  rend/s  halfsteps/s  3D   comp  2D   pres')
for c, h, d in rows:
    r, w = h[33], h[34]
    if not r or not w: continue
    print('%5d %5d %4s %5d %7d %7.1f %7.2f %10.2f  %5.1f %5.1f %4.1f %4.1f' % (c, h[32], 'on' if h[32] & 2048 else 'off', r, w, w / r,
          1000.0 * r / w, 1000.0 * h[61] / w, h[13] / r, h[18] / r, h[16] / r, h[17] / r))
c, h, d = rows[-1]
fmt = struct.unpack_from('<I', d, AGG + 0x600)[0]
print('aggregates format %08x' % fmt)
for s in range(3):
    for m in range(4):
        a = struct.unpack_from('<32I', d, AGG + (s * 4 + m) * 128)
        if not a[0]: continue
        hist = a[16:32]; tot = sum(hist)
        print('seg%d slot%d (%s): %d renders in %d windows, %.1f ms a render (%.2f a second) | 3D %.1f 2D %.1f pres %.1f | >= 90 ms %.0f%%'
              % (s, m, 'spec on' if m & 2 else 'spec off', a[0], a[15], a[1] / a[0], 1000.0 * a[0] / a[1], a[2] / a[0], a[7] / a[0], a[8] / a[0],
                 100.0 * sum(hist[7:]) / tot if tot else 0))
