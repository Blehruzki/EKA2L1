# crec.py <crec.bin> [<crec2.bin> ratio]: the per-step car records (A4_CARREC). One file: the
# step range, the key bits seen, and a few fields. Two files: field by field, step k of the
# first against step k*ratio of the second (ratio 1: two runs of one build; 2: normal vs half).
import sys, struct, collections
def load(path):
    d = open(path, 'rb').read(); o = 0; out = []; hdr = None
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20: hdr = struct.unpack_from('<8I', b)
        elif hdr is not None:
            out.append((hdr, struct.unpack_from('<%di' % (ln // 4), b))); hdr = None
    return out
A = load(sys.argv[1])
print('%s: %d steps (step %d..%d), car %08x idx %d' % (sys.argv[1].split('/')[-1], len(A), A[0][0][1], A[-1][0][1], A[-1][0][4], A[-1][0][5]))
keys = collections.Counter(h[3] for h, _ in A); print('  key bits K+4 seen:', ', '.join('%x x%d' % kv for kv in keys.most_common(8)))
F = [(0x004, 'x'), (0x008, 'y'), (0x00C, 'z'), (0x7D0, 'speed'), (0x7D8, '7D8'), (0x0B8, 'B8'), (0x0C0, 'C0'), (0x0E0, 'E0'), (0x820, '820')]
if len(sys.argv) < 3:
    for i in range(0, len(A), max(1, len(A) // 12)):
        h, w = A[i]; print('  step %5d keys %08x  ' % (h[1], h[3]) + ' '.join('%s %d' % (n, w[o // 4]) for o, n in F))
    sys.exit()
B = load(sys.argv[2]); r = int(sys.argv[3])
print('%s: %d steps' % (sys.argv[2].split('/')[-1], len(B)))
# align on the first step whose E0 (a step counter in the car) is 1 in each run
def first(R, v):
    for i, (h, w) in enumerate(R):
        if w[0xE0 // 4] >= v: return i
    return 0
a0, b0 = first(A, 1), first(B, 1)
print('aligned at A[%d], B[%d] (car+0xE0 reaching 1)' % (a0, b0))
nw = len(A[0][1]); diff = collections.Counter(); firstdiff = {}
n = 0
for k in range(0, min(len(A) - a0, (len(B) - b0) // r)):
    wa, wb = A[a0 + k][1], B[b0 + k * r][1]; n += 1
    for j in range(nw):
        if wa[j] != wb[j]:
            diff[j] += 1
            firstdiff.setdefault(j, (k, wa[j], wb[j]))
print('%d aligned steps; words differing: %d of %d' % (n, len(diff), nw))
for j, c in sorted(diff.items(), key=lambda x: firstdiff[x[0]][0])[:40]:
    k, va, vb = firstdiff[j]
    print('  +%03X  differs in %5d steps, first at step %d (%d vs %d)' % (j * 4, c, k, va, vb))
