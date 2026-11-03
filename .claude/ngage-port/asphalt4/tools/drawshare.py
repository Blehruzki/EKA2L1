# drawshare.py <crec.bin>: the speculative frames' share of a whole step's motion in what the
# render draws (round 31 phone follow-up). Per car slot ('CRNA' dumps, 0xA00 or 0xC7C bytes):
# the physics-side matrix +6D4/+6EC as an angle (not drawn: the render reads +678), the heading +678, the wheels +020, the
# position, and the words past +A00 the render reads (rcar9); per race ('CRNR'): the camera
# words the render reads (rcam8). 0.50 = half way; 1.00 = a whole step on the speculative
# frame and none on the next; 0.00 = none. LOCKSTEP 1 / LOCKSTEP_P 10667 runs (hs odd = whole).
import sys, struct, math, statistics as st
d = open(sys.argv[1], 'rb').read(); o = 0; hdr = None; part = 0; R = []; cur = None
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20:
        hdr = struct.unpack_from('<8I', b); part = 0
        if hdr[0] == 0x434E5243: cur = {'hs': hdr[1], 'c': {}, 'r': None}; R.append(cur)
        continue
    if cur is None or not hdr: continue
    if hdr[0] == 0x414E5243 and ln in (0xA00, 0xC7C): cur['c'][hdr[2]] = struct.unpack_from('<%di' % (ln // 4), b)
    elif hdr[0] == 0x524E5243 and ln == 0x4000 and part == 0: cur['r'] = struct.unpack_from('<%di' % (0x9EC // 4), b); part = 1
R = [r for r in R if r['c']]; R = R[len(R) // 3:]
def wrapd(x, m): return (x + m / 2) % m - m / 2
def share(get, mod=None):
    sh = []
    for a, b, c in zip(R, R[1:], R[2:]):
        if not (a['hs'] & 1 and b['hs'] & 1 == 0 and c['hs'] & 1): continue
        try: x0, x1, x2 = get(a), get(b), get(c)
        except (KeyError, IndexError, TypeError): continue
        tot = wrapd(x2 - x0, mod) if mod else x2 - x0
        if abs(tot) < 1e-9: continue
        sh.append((wrapd(x1 - x0, mod) if mod else x1 - x0) / tot)
    if not sh: return '   -'
    s = sorted(sh); return '%5.2f (q %5.2f..%5.2f, n %d)' % (st.median(s), s[len(s) // 4], s[3 * len(s) // 4], len(s))
slots = sorted({s for r in R for s in r['c']})
for slot in slots:
    has = lambda r, i: slot in r['c'] and len(r['c'][slot]) > i
    print('slot %d' % slot)
    print('   physics matrix +6D4 ', share(lambda r: math.atan2(r['c'][slot][0x6EC // 4], r['c'][slot][0x6D4 // 4]), 2 * math.pi))
    print('   heading +678        ', share(lambda r: r['c'][slot][0x678 // 4], 2048))
    print('   wheels +020         ', share(lambda r: r['c'][slot][0x020 // 4], 4096))
    print('   position +004       ', share(lambda r: r['c'][slot][0x004 // 4]))
    late = []
    for off in (0xA04, 0xA20, 0xA24, 0xA68, 0xA78, 0xA7C, 0xAAC, 0xAB0, 0xAC0, 0xAE0, 0xB14, 0xB28, 0xC3C, 0x7D0, 0x7F8):
        s = share(lambda r: r['c'][slot][off // 4])
        if s.strip() != '-': late.append('+%03X %s' % (off, s))
    if late: print('   past +A00 (moving):', '; '.join(late))
print('camera')
for off, mod in ((0x58, None), (0x60, None), (0x64, 2048), (0x68, 2048), (0x70, None), (0x388, None)):
    print('   +%03X' % off, share(lambda r: r['r'][off // 4], mod))
