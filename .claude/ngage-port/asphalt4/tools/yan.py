# yan.py crec.bin: the 'FRKD' per-frame records (a4_gl.c A4_YREC): per car slot, the drawn height
# against the game's heights at the steps around the frame, and +014 against the true change
import sys, struct, collections, statistics as st
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
d = open(sys.argv[1], 'rb').read(); o = 0; recs = []; cur = None
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: cur = [b]; recs.append(cur)
    elif cur is not None: cur.append(b)
F = collections.defaultdict(list)   # slot -> frames
for r in recs:
    h = struct.unpack_from('<8I', r[0])
    if h[0] != 0x444B5246 or len(r) < 2: continue
    car = r[1]
    w = lambda off: s32(struct.unpack_from('<I', car, off)[0])
    slot = (h[2] >> 24) & 0x7F; first = h[2] >> 31; q = (h[2] & 0xFFFFFF) / 65536.0
    F[slot].append(dict(step=h[1], q=q, first=first, yW=s32(h[3]), dW=s32(h[4]), Ey=s32(h[5]) / 256.0, cy=s32(h[6]), gW=s32(h[7]),
                        y=w(8), x=w(4), z=w(0xC), vx=w(0x10), vy=w(0x14), vz=w(0x18), gnd=w(0xD0), addr=r[1] and 0))
for slot in sorted(F):
    fr = F[slot]
    steps = {}
    for f in fr:
        if f['first']: steps[f['step']] = f
    ks = sorted(steps)
    # +014 against the true change of the step's y
    dif = []
    for a, b in zip(ks, ks[1:]):
        if b != a + 1: continue
        dif.append((steps[b]['vy'], steps[b]['yW'] - steps[a]['yW']))
    # the drawn y against the line between the steps around it (known after the fact)
    over = []
    for f in fr:
        a = steps.get(f['step']); b = steps.get(f['step'] + 1)
        if not a or not b: continue
        # q of this frame: fraction of the step since the whole step (q16 includes the step's time)
        lin = a['yW'] + (b['yW'] - a['yW']) * min(f['q'], 1.0)
        over.append((f['y'] - lin, f))
    if not dif or not over: continue
    ad = sorted(abs(v - t) for v, t in dif); ao = sorted(abs(e) for e, f in over)
    pq = lambda a, p: a[min(len(a) - 1, int(p * len(a)))]
    big = sum(1 for v, t in dif if abs(v - t) > 8)
    print('slot %d: %d steps, %d frames | +014 less the true change: 50%% %d 90%% %d 99%% %d max %d (>8 in %d steps) | drawn y off the line: 50%% %.0f 90%% %.0f 99%% %.0f max %.0f'
          % (slot, len(ks), len(fr), pq(ad, .5), pq(ad, .9), pq(ad, .99), ad[-1], big, pq(ao, .5), pq(ao, .9), pq(ao, .99), ao[-1]))
    worst = sorted(over, key=lambda t: -abs(t[0]))[:3]
    for e, f in worst:
        print('    off %+.0f at step %d q %.2f: drawn y %d, step y %d, +014 %d, corrector %+.0f, +0D0 %d' % (e, f['step'], f['q'], f['y'], f['yW'], f['vy'], f['Ey'], f['dW']))
