"""romname.py <rom dll> <def file> <offset hex> ... -- name offsets in an RM-409 ROM image by the nearest export at or below."""
import sys, bisect
sys.path.insert(0, '/home/user/EKA2L1/.claude/ngage-port/toolchain/port')
import romimg
d, h = romimg.load(sys.argv[1])
names = {}
for l in open(sys.argv[2]):
    p = l.split()
    if '@' in p: names[int(p[p.index('@') + 1])] = p[0]
base = h['code_address']
ex = sorted(((a & ~1) - base, o) for o, a in romimg.exports(d, h).items())
offs = [e[0] for e in ex]
for s in sys.argv[3:]:
    x = int(s, 16)
    i = bisect.bisect_right(offs, x) - 1
    o, ordn = ex[i]
    print('%6X = ord %4d + %-4X %s' % (x, ordn, x - o, names.get(ordn, '?')))
