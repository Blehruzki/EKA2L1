# steptime.py <dir>: round 30's step timer -- per window (header [60..63]) and per segment
# (the step block after the aggregates, a4_gl.c A4_GL_STEP) from the latest capture.
import sys, struct, glob
AGG = 256 + 560 + 153600 + 122 * 4
rows = []
for f in glob.glob(sys.argv[1] + '/a4tel*.dat'):
    d = open(f, 'rb').read()
    h = struct.unpack_from('<64I', d, 0)
    rows.append((h[2], h, d, f.split('/')[-1]))
rows.sort()
print('capidx  rend  win ms  ms/rend | 3D   comp  2D   pres  rest | logic/span steps spans  ms/step  12D5A/span')
for c, h, d, nm in rows:
    r, w = h[33], h[34]
    if not r: continue
    t3d, comp, t2d, pres = h[13], h[18], h[16], h[17]   # composite = TFLUSH (the whole phase)
    rest = w - t3d - comp - t2d - pres
    tl, ns, sp, tc = h[60], h[61], h[62], h[63]
    print('%5d %5d %7d %7.1f | %5.1f %5.1f %4.1f %5.1f %5.1f | %6.1f %5d %5d %7.1f %6.1f'
          % (c, r, w, w / r, t3d / r, comp / r, t2d / r, pres / r, rest / r,
             tl / sp if sp else 0, ns, sp, tl / ns if ns else 0, tc / sp if sp else 0))
c, h, d, nm = rows[-1]
sb = d[AGG + 0x700: AGG + 0x700 + 3 * 128]
if len(sb) < 384: sys.exit('no step block in ' + nm)
for s in range(3):
    b = struct.unpack_from('<32I', sb, s * 128)
    if not b[0]: continue
    print('seg%d (%s, capidx %d): spans %d, steps %d (n 0..5: %s), loads left out %d, fmt %08x'
          % (s, nm, c, b[0], b[1], list(b[8:14]), b[14], b[15]))
    print('   per span %.1f ms, per step %.1f ms, longest %d ms; +12D5A %.2f ms a span; +15A52 %.2f ms a call (%d calls); m-n sum %d'
          % (b[2] / b[0], b[2] / b[1] if b[1] else 0, b[4], b[3] / b[0], b[6] / b[7] if b[7] else 0, b[7], b[5]))
    print('   n=1 spans by length (4 ms bins, last >= 44): %s' % list(b[16:28]))
    if b[30]:
        print('   render call +12992: %.1f ms a call (%d calls, %d with no triangle); before the first triangle %.1f ms a call with one'
              % (b[28] / b[30], b[30], b[31], b[29] / (b[30] - b[31]) if b[30] > b[31] else 0))
