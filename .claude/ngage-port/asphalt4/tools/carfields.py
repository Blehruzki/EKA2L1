# carfields.py <carmem.bin>: the player car object dumped at each HUD speed read (+206A0):
# which 32-bit fields change, and how -- counters (a constant step), speed-like, position-like.
import sys, struct, collections
d = open(sys.argv[1], 'rb').read(); o = 0; recs = []
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12
    recs.append((addr, d[o:o + ln])); o += ln
addrs = collections.Counter(a for a, _ in recs)
print('%d records, objects %s' % (len(recs), ', '.join('%08x x%d' % kv for kv in addrs.most_common(4))))
car = addrs.most_common(1)[0][0]
B = [b for a, b in recs if a == car]
W = [struct.unpack_from('<%di' % (len(b) // 4), b) for b in B]
def speed(w): return (w[0x7EC // 4] * w[0x7D0 // 4]) // w[0x7E8 // 4] if w[0x7E8 // 4] else 0
sp = [speed(w) for w in W]
print('speed (HUD formula) first..last: %s ... max %d' % (sp[:3], max(sp)))
nw = len(W[0]); rows = []
for k in range(nw):
    v = [w[k] for w in W]
    if len(set(v)) < 3: continue
    dv = [b - a for a, b in zip(v, v[1:])]
    cd = collections.Counter(dv)
    top = cd.most_common(3)
    rows.append((k * 4, len(set(v)), v[0], v[-1], min(v), max(v), top))
print('changing fields: %d of %d' % (len(rows), nw))
for off, nd, a, b, lo, hi, top in rows:
    print('  +%03X  distinct %4d  first %11d last %11d  range [%d, %d]  commonest deltas %s' % (off, nd, a, b, lo, hi, top))
