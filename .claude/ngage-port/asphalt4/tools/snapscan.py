# snapscan.py crec.bin [min]: drawn-heading zig-zags per car (a frame-to-frame change >= min against the
# frames' own direction, then back), with the game's step headings around it (the FRKD 'first' frames).
import sys, struct, collections
sys.path.insert(0, '/home/user/EKA2L1/.claude/ngage-port/asphalt4/tools')
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
fn = sys.argv[1]; MIN = int(sys.argv[2]) if len(sys.argv) > 2 else 20
d = open(fn, 'rb').read(); o = 0; cur = None; recs = []
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: cur = [b]; recs.append(cur)
    elif cur is not None: cur.append(b)
F = collections.defaultdict(list)
for r in recs:
    h = struct.unpack_from('<8I', r[0])
    if len(r) < 3 or h[0] != 0x444B5246: continue
    w = lambda b, off: s32(struct.unpack_from('<I', b, off)[0])
    slot = (h[2] >> 24) & 0x7F
    F[slot].append(dict(step=h[1], q=(h[2] & 0xFFFFFF) / 65536.0, first=h[2] >> 31, hW=s32(h[7]) & 2047,
                        hd=w(r[1], 0x678) & 2047, x=w(r[1], 4), z=w(r[1], 0xC), mode=w(r[2], 0x94)))
n = 0
for slot in sorted(F):
    fr = F[slot]; steps = {}
    for f in fr:
        if f['first']: steps[f['step']] = f['hW']
    for i in range(1, len(fr) - 1):
        a, b, c = fr[i - 1], fr[i], fr[i + 1]
        d1 = wrap(b['hd'] - a['hd']); d2 = wrap(c['hd'] - b['hd'])
        if abs(d1) >= MIN and abs(d2) >= MIN and (d1 > 0) != (d2 > 0):
            n += 1
            if n > 12: continue
            s = b['step']
            print('slot %d step %d: drawn %s | game steps %s' % (slot, s,
                  ' '.join('%d(q%.2f%s)' % (f['hd'], f['q'], '*' if f['first'] else '') for f in fr[max(0, i - 4):i + 5]),
                  ' '.join('%d:%d' % (k, steps[k]) for k in range(s - 3, s + 4) if k in steps)))
print('%d zig-zags of %d or more' % (n, MIN))
