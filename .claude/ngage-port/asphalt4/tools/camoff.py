# camoff.py crec.bin...: the chase camera's yaw = the drawn heading + an offset; the offset as drawn (the game's at the
# step, + the build's carry) against blended (the step before's offset moving to this step's over the step: no jump at a
# step, a step late). One-frame yaw reversals (>= 4, the frame record's measure) and the yaw off the game camera's path.
import sys, struct
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
pq = lambda a, p: sorted(a)[min(len(a) - 1, int(p * len(a)))] if a else 0
for fn in sys.argv[1:]:
    d = open(fn, 'rb').read(); o = 0; cur = None; recs = []
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20: cur = [b]; recs.append(cur)
        elif cur is not None: cur.append(b)
    CAM = {}; PL = {}; FR = []
    for r in recs:
        h = struct.unpack_from('<8I', r[0])
        if len(r) < 3: continue
        w = lambda b, off: s32(struct.unpack_from('<I', b, off)[0])
        if h[0] == 0x43455243: CAM[h[1]] = (w(r[2], 0x68) & 2047, w(r[2], 0x94)); PL[h[1]] = w(r[1], 0x678) & 2047
        elif h[0] == 0x444B5246 and ((h[2] >> 24) & 0x7F) == 0:
            FR.append(dict(step=h[1], q=min(1.0, (h[2] & 0xFFFFFF) / 65536.0), yaw=w(r[2], 0x68) & 2047, hd=w(r[1], 0x678) & 2047, mode=w(r[2], 0x94)))
    off = {k: wrap(CAM[k][0] - PL[k]) for k in CAM}
    seqs = {'drawn': [], 'rigid': [], 'blended': []}; errs = {'drawn': [], 'rigid': [], 'blended': []}
    for f in FR:
        k = f['step']
        if f['mode'] != 2 or k not in off or k - 1 not in off or CAM[k][1] != 2 or CAM[k - 1][1] != 2: 
            for s in seqs.values(): s.append(None)
            continue
        ob = off[k - 1] + wrap(off[k] - off[k - 1]) * f['q']
        yb = (f['hd'] + int(round(ob))) & 2047
        yr = (f['hd'] + off[k]) & 2047                   # round 47's rigid camera: the step's offset all through it
        seqs['drawn'].append(f['yaw']); seqs['blended'].append(yb); seqs['rigid'].append(yr)
        if k + 1 in CAM and CAM[k + 1][1] == 2:
            line = CAM[k][0] + wrap(CAM[k + 1][0] - CAM[k][0]) * f['q']
            errs['drawn'].append(abs(wrap(int(round(f['yaw'] - line))))); errs['blended'].append(abs(wrap(int(round(yb - line)))))
            errs['rigid'].append(abs(wrap(int(round(yr - line)))))
    out = []
    for k in ('drawn', 'rigid', 'blended'):
        s = seqs[k]
        rev = sum(1 for i in range(1, len(s) - 1) if None not in (s[i - 1], s[i], s[i + 1]) and abs(wrap(s[i] - s[i - 1])) >= 4
                  and abs(wrap(s[i + 1] - s[i])) >= 4 and wrap(s[i] - s[i - 1]) * wrap(s[i + 1] - s[i]) < 0)
        out.append('%s: reversals %d, off the game camera 90%% %d 99%% %d max %d' % (k, rev, pq(errs[k], .9), pq(errs[k], .99), max(errs[k] or [0])))
    print('%-20s %d chase frames | %s' % (fn.split('/')[-1], sum(1 for x in seqs['drawn'] if x is not None), ' | '.join(out)))
