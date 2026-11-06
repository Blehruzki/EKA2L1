# offline.py crec.bin [crec.bin ...]: engine map 1 -- how far each frame draws a car (and the chase
# camera) off the game's own path: the straight line between the game's values at the step before
# and the step after the frame, known after the fact. A frame that predicts well stays near it; a
# car that "flies off and snaps back" leaves it. From the 'FRKD' records (a4_gl.c A4_YREC: per frame
# per car, the drawn car and race object, with the step's y, +0D0, x, z, heading) and the 'CREC'
# step records (the player and the race object at each step: the game's camera).
import sys, struct, collections, math
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
pq = lambda a, p: a[min(len(a) - 1, int(p * len(a)))] if a else 0

def load(fn):
    d = open(fn, 'rb').read(); o = 0; cur = None; recs = []
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
        if ln == 0x20: cur = [b]; recs.append(cur)
        elif cur is not None: cur.append(b)
    return recs

for fn in sys.argv[1:]:
    recs = load(fn)
    F = collections.defaultdict(list); CAM = {}; PL = {}
    for r in recs:
        h = struct.unpack_from('<8I', r[0])
        if len(r) < 3: continue
        w = lambda b, off: s32(struct.unpack_from('<I', b, off)[0])
        if h[0] == 0x43455243:           # the step: the player and the camera as the game left them
            CAM[h[1]] = (w(r[2], 0x58), w(r[2], 0x5C), w(r[2], 0x60), w(r[2], 0x68), w(r[2], 0x94))
            PL[h[1]] = (w(r[1], 4), w(r[1], 8), w(r[1], 0xC), w(r[1], 0x678) & 2047)
        elif h[0] == 0x444B5246:
            slot = (h[2] >> 24) & 0x7F
            F[slot].append(dict(step=h[1], q=(h[2] & 0xFFFFFF) / 65536.0, first=h[2] >> 31,
                                yW=s32(h[3]), dW=s32(h[4]), xW=s32(h[5]), zW=s32(h[6]), hW=s32(h[7]) & 2047,
                                x=w(r[1], 4), y=w(r[1], 8), z=w(r[1], 0xC), hd=w(r[1], 0x678) & 2047,
                                cx=w(r[2], 0x58), cy=w(r[2], 0x5C), cz=w(r[2], 0x60), cyaw=w(r[2], 0x68), mode=w(r[2], 0x94),
                                car=struct.unpack_from('<I', r[1], 0)[0]))
    print(fn.split('/')[-1])
    tot = collections.defaultdict(list)
    for slot in sorted(F):
        fr = F[slot]; steps = {}
        for f in fr:
            if f['first']: steps[f['step']] = f
        off = collections.defaultdict(list)
        for f in fr:
            a = steps.get(f['step']); b = steps.get(f['step'] + 1)
            if not a or not b: continue
            if f['mode'] == 0x14 or 0x17 <= f['mode'] <= 0x1D: continue   # A4_SCRIPTHOLD: drawn at the step by design
            q = min(f['q'], 1.0)
            dxz = math.hypot(b['xW'] - a['xW'], b['zW'] - a['zW'])
            if dxz > 2000 or abs(b['yW'] - a['yW']) > 600: continue   # a respawn / a reset between the steps
            lx = a['xW'] + (b['xW'] - a['xW']) * q; lz = a['zW'] + (b['zW'] - a['zW']) * q
            ly = a['yW'] + (b['yW'] - a['yW']) * q; lh = a['hW'] + wrap(b['hW'] - a['hW']) * q
            off['xz'].append(math.hypot(f['x'] - lx, f['z'] - lz)); off['y'].append(abs(f['y'] - ly))
            off['hd'].append(abs(wrap(int(round(f['hd'] - lh)))))
        for k in off: off[k].sort(); tot[k] += off[k]
        if off['y']:
            print('  slot %d (%d frames): off the path -- x-z 50%% %.0f 90%% %.0f 99%% %.0f max %.0f | y 50%% %.0f 90%% %.0f 99%% %.0f max %.0f | heading 99%% %d max %d'
                  % (slot, len(off['y']), pq(off['xz'], .5), pq(off['xz'], .9), pq(off['xz'], .99), off['xz'][-1],
                     pq(off['y'], .5), pq(off['y'], .9), pq(off['y'], .99), off['y'][-1], pq(off['hd'], .99), off['hd'][-1]))
    for k in tot: tot[k].sort()
    if tot['y']:
        print('  all cars: x-z 99%% %.0f max %.0f | y 99%% %.0f max %.0f | heading 99%% %d max %d'
              % (pq(tot['xz'], .99), tot['xz'][-1], pq(tot['y'], .99), tot['y'][-1], pq(tot['hd'], .99), tot['hd'][-1]))
    # the chase camera: drawn against the line between the game's camera at the steps around it,
    # and the player car against the camera (in the stock game rigid: yaw = heading, y = car y + 105 + s)
    pslot = None
    for slot in F:
        if any(f['first'] and f['step'] in PL and (f['xW'], f['yW'], f['zW']) == PL[f['step']][:3] for f in F[slot][:50]): pslot = slot; break
    if pslot is None: continue
    co = collections.defaultdict(list)
    for f in F[pslot]:
        a = CAM.get(f['step']); b = CAM.get(f['step'] + 1)
        if not a or not b or a[4] != 2 or b[4] != 2 or f['mode'] != 2: continue
        q = min(f['q'], 1.0)
        if math.hypot(b[0] - a[0], b[2] - a[2]) > 2000: continue
        co['xz'].append(math.hypot(f['cx'] - (a[0] + (b[0] - a[0]) * q), f['cz'] - (a[2] + (b[2] - a[2]) * q)))
        co['y'].append(abs(f['cy'] - (a[1] + (b[1] - a[1]) * q)))
        co['yaw'].append(abs(wrap(int(round(f['cyaw'] - (a[3] + wrap(b[3] - a[3]) * q))))))
        co['rel_y'].append(f['cy'] - f['y']); co['rel_yaw'].append(wrap(f['cyaw'] - f['hd']))
    for k in co: co[k].sort()
    if co['y']:
        ry = co['rel_y']; rw = co['rel_yaw']
        print('  camera (player slot %d, %d chase frames): off its path -- x-z 99%% %.0f max %.0f | y 99%% %.0f max %.0f | yaw 99%% %d max %d'
              % (pslot, len(co['y']), pq(co['xz'], .99), co['xz'][-1], pq(co['y'], .99), co['y'][-1], pq(co['yaw'], .99), co['yaw'][-1]))
        print('    camera y less the drawn car y: 1%% %d 50%% %d 99%% %d | camera yaw less the drawn heading: 1%% %d 50%% %d 99%% %d'
              % (pq(ry, .01), pq(ry, .5), pq(ry, .99), pq(rw, .01), pq(rw, .5), pq(rw, .99)))
