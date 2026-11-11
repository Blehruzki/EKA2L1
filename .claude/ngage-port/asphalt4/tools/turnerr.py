# turnerr.py crec.bin: per frame, the drawn heading against the game's path (the line between the steps around
# the frame), signed in the turn's direction: ahead = overshoot (corrected back, against the motion), behind = lag.
import sys, struct, collections
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
pq = lambda a, p: a[min(len(a) - 1, int(p * len(a)))] if a else 0
d = open(sys.argv[1], 'rb').read(); o = 0; cur = None; recs = []
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: cur = [b]; recs.append(cur)
    elif cur is not None: cur.append(b)
F = collections.defaultdict(list)
for r in recs:
    h = struct.unpack_from('<8I', r[0])
    if len(r) < 3 or h[0] != 0x444B5246: continue
    w = lambda b, off: s32(struct.unpack_from('<I', b, off)[0])
    F[(h[2] >> 24) & 0x7F].append(dict(step=h[1], q=(h[2] & 0xFFFFFF) / 65536.0, first=h[2] >> 31, hW=s32(h[7]) & 2047,
                                       hd=w(r[1], 0x678) & 2047, mode=w(r[2], 0x94)))
for label, slots in (('slot 0', [0]), ('all cars', sorted(F))):
    over, lag, n = [], [], 0
    for slot in slots:
        fr = F.get(slot, []); steps = {f['step']: f['hW'] for f in fr if f['first']}
        for f in fr:
            a, b = steps.get(f['step']), steps.get(f['step'] + 1)
            if a is None or b is None: continue
            tw = wrap(b - a)
            if tw == 0 or abs(tw) > 200: continue
            e = wrap(int(round(f['hd'] - (a + tw * min(f['q'], 1.0)))))
            sg = e * (1 if tw > 0 else -1); n += 1
            (over if sg > 0 else lag).append(abs(sg))
    over.sort(); lag.sort()
    print('%-8s turning frames %5d: ahead %4d (99%% %3d max %3d, >10: %3d) | behind %4d (99%% %3d max %3d, >10: %3d)'
          % (label, n, len(over), pq(over, .99), over[-1] if over else 0, sum(1 for x in over if x > 10),
             len(lag), pq(lag, .99), lag[-1] if lag else 0, sum(1 for x in lag if x > 10)))
