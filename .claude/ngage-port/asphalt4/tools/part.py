# part.py <stock crec.bin> <3x crec.bin> [apart] [from]: where two runs of the same race part (round 50).
# Both record once a game step (A4_CARREC), aligned by record index (the countdown is exact). Prints
# every 6th step until the player cars are more than <apart> units apart (default 40), then each step
# around the parting: speed, rpm, heading, steering, the wall's countdown +B18, contact flags +D4, the
# slip the engine's check reads, the car-contact counter +E0 and the keys +B1C (bit 0x100: the AI drives).
import sys, os, math
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'oracle.py')).read().split('A = series')[0]
args = sys.argv[1:]; sys.argv = sys.argv[:1]
g = {'__file__': os.path.abspath(__file__)}; exec(src, g)
A = g['load'](args[0]); B = g['load'](args[1])
lim = int(args[2]) if len(args) > 2 else 40
k0 = int(args[3]) if len(args) > 3 else 0
def W(w, o): return w[o // 4]
def slip(w): return ((W(w, 0x10) * W(w, 0x704) + W(w, 0x14) * W(w, 0x708) + W(w, 0x18) * W(w, 0x70C)) >> 12) - W(w, 0x7D0)
def row(w): return 'B4 %3d g%d rpm %4d 678 %4d 7FC %5d B18 %3d D4 %04x slip %4d E0 %3d B1C %3x' % (
    W(w, 0xB4), W(w, 0x7BC), W(w, 0x7D8), W(w, 0x678), W(w, 0x7FC), W(w, 0xB18), W(w, 0xD4) & 0xFFFF, slip(w), W(w, 0xE0), W(w, 0xB1C) & 0xFFF)
n = min(len(A), len(B)); part = None
for k in range(k0, n):
    a, b = A[k][1], B[k][1]
    d = math.hypot(W(a, 4) - W(b, 4), W(a, 0xC) - W(b, 0xC))
    if part is None and d > lim: part = k
    if (part is None and k % 6 == 0) or (part is not None and part - 8 <= k):
        print('%5d apart %5d | stock %s | 3x %s' % (k, d, row(a), row(b)))
    if part is not None and k >= part + 4: break
if part is None: print('no parting over %d records (%d apart at most)' % (n, lim))
else:
    for k in range(max(k0, part - 8), part + 1):
        diff = [o * 4 for o in range(min(len(A[k][1]), len(B[k][1]))) if A[k][1][o] != B[k][1][o]]
        if diff: print('first words apart at %d: %s' % (k, ' '.join('%X' % o for o in diff[:48]))); break
