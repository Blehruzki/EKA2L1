# injcmp.py <stock crec.bin> <3x crec.bin> <index> ...: round 50 -- after a state injection
# (A4_BENCH_INJECT, tools/mkinject.py) both runs go on from stock's car at <index>; their next steps
# side by side: speed B4, gear, rpm, heading +678, the steering +7FC, contact +D4, the slip
# (velocity . the car's direction (+704) >> 12 - the engine's speed +7D0), the distance between them
import sys, os, math
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'oracle.py')).read().split('A = series')[0]
args = sys.argv[1:]; sys.argv = sys.argv[:1]
g = {'__file__': os.path.abspath(__file__)}; exec(src, g); W = g['W']
A = g['load'](args[0]); B = g['load'](args[1])
def slip(w): return ((W(w, 0x10) * W(w, 0x704) + W(w, 0x14) * W(w, 0x708) + W(w, 0x18) * W(w, 0x70C)) >> 12) - W(w, 0x7D0)
for I in (int(x) for x in args[2:]):
    print('injected at record %d:' % I)
    for i in range(I, min(I + 16, len(A), len(B))):
        a, b = A[i][1], B[i][1]
        d = math.hypot(W(a, 4) - W(b, 4), W(a, 0xC) - W(b, 0xC))
        print('  %4d  B4 %3d %3d  g %d %d  rpm %4d %4d  678 %4d %4d  7FC %5d %5d  D4 %04x %04x  slip %4d %4d  apart %6.0f' % (
            i, W(a, 0xB4), W(b, 0xB4), W(a, 0x7BC), W(b, 0x7BC), W(a, 0x7D8), W(b, 0x7D8), W(a, 0x678), W(b, 0x678),
            W(a, 0x7FC), W(b, 0x7FC), W(a, 0xD4) & 0xFFFF, W(b, 0xD4) & 0xFFFF, slip(a), slip(b), d))
