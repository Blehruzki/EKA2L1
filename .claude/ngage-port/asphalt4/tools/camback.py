# camback.py crec.bin [crec.bin ...]: round 49 -- the "back and forth" in the camera modes other than
# the chase, measured where the owner sees it: where the player car sits in the camera's view, frame
# by frame. From the 'FRKD' records (a4_gl.c A4_YREC: the drawn car and the race object per frame, the
# camera at race +0x58.. and its mode at +0x94) and the 'CREC' step records (which slot is the player).
# Per camera mode: frames, the car's bearing in the view (its direction from the camera less the
# camera's yaw) and elevation (its angle above the camera's pitch), their change a frame, and the
# one-frame reversals of either (|change| >= 4 /2048 both ways) -- a car that goes back and forth on
# the screen. The game's own steps are a reference: the same numbers for the step records alone.
import sys, struct, collections, math
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
pq = lambda a, p: sorted(a)[min(len(a) - 1, int(p * len(a)))] if a else 0
A = lambda rad: rad * 1024 / math.pi

def view(car, cam):
    dx, dy, dz = car[0] - cam[0], car[1] - cam[1], car[2] - cam[2]
    d = math.hypot(dx, dz)
    brg = wrap(int(round(A(math.atan2(dx, dz)) - cam[4])))
    elev = wrap(int(round(A(math.atan2(dy, max(d, 1))))) + cam[3])
    return brg, elev, d

for fn in sys.argv[1:]:
    d = open(fn, 'rb').read(); o = 0; cur = None; recs = []
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20: cur = [b]; recs.append(cur)
        elif cur is not None: cur.append(b)
    PL = {}; frames = collections.defaultdict(list); steps = []
    for r in recs:
        h = struct.unpack_from('<8I', r[0])
        if len(r) < 3: continue
        w = lambda b, off: s32(struct.unpack_from('<I', b, off)[0])
        if h[0] == 0x43455243:
            PL[h[1]] = (w(r[1], 4), w(r[1], 8), w(r[1], 0xC))
            cam = tuple(w(r[2], 0x58 + 4 * k) for k in range(6)); mode = w(r[2], 0x94) & 0xFF
            steps.append((h[1], mode, PL[h[1]], cam))
        elif h[0] == 0x444B5246:
            slot = (h[2] >> 24) & 0x7F
            frames[slot].append((h[1], (h[2] & 0xFFFFFF) / 65536, h[2] >> 31, s32(h[3]), s32(h[5]), s32(h[6]),
                                 (w(r[1], 4), w(r[1], 8), w(r[1], 0xC)), tuple(w(r[2], 0x58 + 4 * k) for k in range(6)), w(r[2], 0x94) & 0xFF))
    pslot = None
    for slot, fr in frames.items():
        hits = sum(1 for f in fr[:200] if f[2] and f[0] in PL and (f[4], f[3], f[5]) == PL[f[0]])
        if hits > 20: pslot = slot; break
    print(fn.split('/')[-1], 'player slot', pslot)
    if pslot is None: continue
    def report(label, seq):
        by = collections.defaultdict(list)
        for mode, b, e, dist in seq: by[mode].append((b, e, dist))
        for mode in sorted(by):
            v = by[mode]
            db = [wrap(v[i + 1][0] - v[i][0]) for i in range(len(v) - 1)]
            de = [wrap(v[i + 1][1] - v[i][1]) for i in range(len(v) - 1)]
            rb = sum(1 for i in range(len(db) - 1) if db[i] * db[i + 1] < 0 and abs(db[i]) >= 4 and abs(db[i + 1]) >= 4)
            re_ = sum(1 for i in range(len(de) - 1) if de[i] * de[i + 1] < 0 and abs(de[i]) >= 4 and abs(de[i + 1]) >= 4)
            print('  %-6s mode 0x%02x: %4d | bearing change 50%% %d 90%% %d 99%% %d, reversals %d | elevation change 50%% %d 90%% %d 99%% %d, reversals %d | distance 10-90%% %.0f..%.0f'
                  % (label, mode, len(v), pq([abs(x) for x in db], .5), pq([abs(x) for x in db], .9), pq([abs(x) for x in db], .99), rb,
                     pq([abs(x) for x in de], .5), pq([abs(x) for x in de], .9), pq([abs(x) for x in de], .99), re_,
                     pq([x[2] for x in v], .1), pq([x[2] for x in v], .9)))
    # frames (as drawn), split into runs of one mode
    seq = [(f[8],) + view(f[6], f[7]) for f in frames[pslot]]
    report('frames', seq)
    report('steps', [(m,) + view(p, c) for st, m, p, c in steps])
