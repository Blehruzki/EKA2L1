# camfind.py <crec.bin>: per render ('CRNP' car, 'CRNR' race + context), the words of the race
# object and its context that move like a camera: they change into renders after whole steps,
# stay put into renders after speculative half-steps, and sit near the car's position.
import sys, struct, collections
d = open(sys.argv[1], 'rb').read(); o = 0; hdr = None; cur = {}; R = []
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20:
        hdr = struct.unpack_from('<8I', b); part = 0; continue
    if not hdr: continue
    m = hdr[0]
    if m == 0x504E5243: cur = {'hs': hdr[1], 'car': struct.unpack_from('<%di' % (ln // 4), b)}
    elif m == 0x524E5243:
        if part == 0: cur['race'] = struct.unpack_from('<%di' % (ln // 4), b); part = 1
        else:
            cur['ctx'] = struct.unpack_from('<%di' % (ln // 4), b)
            if 'car' in cur: R.append(cur)
            cur = {}; hdr = None
R = R[len(R) // 3:]
print('%d renders with car, race and context' % len(R))
for name in ('race', 'ctx'):
    whole = collections.Counter(); spec = collections.Counter(); near = collections.Counter()
    for a, b in zip(R, R[1:]):
        after_whole = b['hs'] & 1
        cx, cz = b['car'][1], b['car'][3]
        for j, (x, y) in enumerate(zip(a[name], b[name])):
            if x != y:
                (whole if after_whole else spec)[j] += 1
            if abs(y - cx) < 3000 or abs(y - cz) < 3000: near[j] += 1
    n = len(R) - 1
    cands = [j for j in whole if whole[j] > n * 0.3 and spec[j] < n * 0.05]
    print('%s: words changing after whole steps but not after speculative ones: %d' % (name, len(cands)))
    for j in sorted(cands)[:40]:
        print('   +%04X  whole %4d spec %4d  near car %4d  last %d' % (j * 4, whole[j], spec[j], near[j], R[-1][name][j]))
