# hits.py <crec.bin> [<crec.bin> ...]: every wall contact of a run's player car and what it cost (round 50).
# Two runs of one race part after a few hundred steps (the bench car only holds '5' and the walls steer
# it, so a heading a degree off meets the next wall elsewhere): a lap time compares two paths, not two
# engines. This compares the price of a hit instead. A contact starts where +D4 bit 0x1000 rises; its
# turn is the heading's change over the contact's first two steps, its cost the speed (+B4) six steps
# later against the step before, and whether it took the car down a gear. Hits are grouped by the turn
# (the angle met: the push is half of it, 20..64 a step) and the speed.
import sys, os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'oracle.py')).read().split('A = series')[0]
args = sys.argv[1:]; sys.argv = sys.argv[:1]
g = {'__file__': os.path.abspath(__file__)}; exec(src, g)
def W(w, o): return w[o // 4]
def hits(R):
    out = []; prev = 0
    for k in range(1, len(R) - 7):
        w = R[k][1]; d = W(w, 0xD4) & 0x1000
        if d and not prev and W(w, 0x034) == -1 and not (W(w, 0xB1C) >> 8) & 1:
            p = R[k - 1][1]; q = R[k + 6][1]
            turn = abs(((W(R[k + 1][1], 0x678) - W(p, 0x678) + 1024) % 2048) - 1024)
            out.append(dict(k=k, b4=W(p, 0xB4), cost=W(p, 0xB4) - W(q, 0xB4), turn=turn,
                            down=W(q, 0x7BC) < W(p, 0x7BC), steps=sum(1 for j in range(k, k + 7) if W(R[j][1], 0xD4) & 0x1000)))
        prev = d
    return out
bins = [(0, 40), (40, 80), (80, 200), (200, 2048)]
print('%-16s %5s | %s' % ('run', 'hits', ' | '.join('turn %3d-%-4d n  cost  down' % b for b in bins)))
for path in args:
    H = hits(g['load'](path)); cells = []
    for lo, hi in bins:
        s = [h for h in H if lo <= h['turn'] < hi and h['b4'] > 60]
        cells.append('%15d %4.0f %4.0f%%' % (len(s), sum(h['cost'] for h in s) / max(1, len(s)), 100.0 * sum(h['down'] for h in s) / max(1, len(s))) if s else '%15s %4s %5s' % ('0', '-', '-'))
    print('%-16s %5d | %s' % (os.path.basename(path), len(H), ' | '.join(cells)))
    if os.environ.get('HITS'):
        for h in H: print('   %5d B4 %3d turn %4d cost %4d down %d contact %d' % (h['k'], h['b4'], h['turn'], h['cost'], h['down'], h['steps']))
