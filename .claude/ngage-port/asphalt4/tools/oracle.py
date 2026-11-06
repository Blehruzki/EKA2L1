# oracle.py <stock crec.bin> <3x crec.bin>: the round 50 oracle -- the game at its own step against
# the engine at 3x, compared in game time (both record once a game step, A4_CARREC), not word by word
# (the two part at once: thirds are not the game's integer steps). From the race's start (the car's
# contact counter E0 reaching 1): the speed curve (+B4, the game's own speed, and +7D0 the engine's
# target), the gear (+7BC), the distance driven (the sum of each step's |dx, dz|), top speed, the
# time to each speed, the air (B28 bit 3, A60), the boost (AE0), the drift (824). Same autopilot,
# same seeds: what differs is the engine's step.
import sys, struct, math, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
KIND = 0x41455243 if os.environ.get('CREA') else 0x43455243
def load(path):
    d = open(path, 'rb').read(); o = 0; out = []; hdr = None
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20:
            hdr = struct.unpack_from('<8I', b)
            if hdr[0] != KIND: hdr = None
        elif hdr is not None and ln == 0xA00:
            if not out or out[-1][0][1] != hdr[1]: out.append((hdr, struct.unpack_from('<%di' % (ln // 4), b)))
        elif hdr is not None and ln == 0x27C and out and out[-1][0] is hdr and len(out[-1][1]) == 0x280:
            out[-1] = (hdr, out[-1][1] + struct.unpack_from('<%di' % (ln // 4), b))   # the car's +A00..+C7C
        elif hdr is not None and ln == 0x100 and out and out[-1][0] is hdr:
            RACE[id(out[-1][1])] = struct.unpack_from('<64i', b)                     # the race's first 0x100 bytes
    return out
RACE = {}
def W(w, off): return w[off // 4]
def series(R):
    # the race's start: its own step counter race+0x30 (counted in the race rules, once a game step)
    a0 = next((i for i, (h, w) in enumerate(R) if RACE.get(id(w), [0] * 64)[0x30 // 4] >= 1), 0)
    S = []; dist = 0.0; px = pz = None
    for h, w in R[a0:]:
        x, z = W(w, 0x004), W(w, 0x00C)
        if px is not None:
            dd = math.hypot(x - px, z - pz)
            if dd < 20000: dist += dd
        px, pz = x, z
        S.append(dict(step=h[1], keys=h[3], b4=W(w, 0xB4), tgt=W(w, 0x7D0), gear=W(w, 0x7BC), rpm=W(w, 0x7D8),
                      dist=dist, seg=W(w, 0x01C), air=(W(w, 0xB28) >> 3) & 1, a60=W(w, 0xA60), boost=W(w, 0xAE0),
                      drift=W(w, 0x824) & 0xFF, y=W(w, 0x008), head=W(w, 0x678), grip=W(w, 0x9FC), steer=W(w, 0x7FC)))
    return S
A = series(load(sys.argv[1])); B = series(load(sys.argv[2]))
n = min(len(A), len(B))
print('stock %d steps, 3x %d steps from the start; compared over %d (%.1f s)' % (len(A), len(B), n, n * 0.08533))
print('%6s %6s | %5s %5s | %6s %6s | %4s %4s | %9s %9s | %5s %5s | %3s %3s' % ('step', 's', 'B4', 'B4', 'tgt', 'tgt', 'gear', 'gear', 'dist', 'dist', 'rpm', 'rpm', 'air', 'air'))
for k in list(range(0, min(n, 120), 6)) + list(range(120, n, 48)):
    a, b = A[k], B[k]
    print('%6d %6.2f | %5d %5d | %6d %6d | %4d %4d | %9.0f %9.0f | %5d %5d | %3d %3d' % (
        k, k * 0.08533, a['b4'], b['b4'], a['tgt'], b['tgt'], a['gear'], b['gear'], a['dist'], b['dist'], a['rpm'], b['rpm'], a['air'], b['air']))
def first(S, f):
    return next((i for i, s in enumerate(S[:n]) if f(s)), None)
print('top speed (B4) over the run: stock %d, 3x %d' % (max(s['b4'] for s in A[:n]), max(s['b4'] for s in B[:n])))
for v in (20, 40, 60, 80, 100, 120, 140):
    ia, ib = first(A, lambda s: s['b4'] >= v), first(B, lambda s: s['b4'] >= v)
    print('  B4 >= %3d: stock step %s, 3x step %s' % (v, ia, ib))
for g in range(2, 7):
    ia, ib = first(A, lambda s: s['gear'] >= g), first(B, lambda s: s['gear'] >= g)
    print('  gear %d: stock step %s, 3x step %s' % (g, ia, ib))
def runs(S, key):
    out = []; st = None
    for i, s in enumerate(S[:n]):
        if s[key] and st is None: st = i
        if not s[key] and st is not None: out.append((st, i - st)); st = None
    return out
for key in ('air', 'boost', 'drift'):
    ra, rb = runs(A, key), runs(B, key)
    print('%s: stock %d (%s), 3x %d (%s)' % (key, len(ra), ' '.join('%d+%d' % r for r in ra[:8]), len(rb), ' '.join('%d+%d' % r for r in rb[:8])))
for t in (10, 20, 30, 40, 50):
    k = int(t / 0.08533)
    if k < n: print('distance at %2d s: stock %.0f, 3x %.0f (%.1f%%)' % (t, A[k]['dist'], B[k]['dist'], 100.0 * (B[k]['dist'] - A[k]['dist']) / max(1.0, A[k]['dist'])))
