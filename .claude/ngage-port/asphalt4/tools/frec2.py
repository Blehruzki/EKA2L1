# frec2.py <a4tel dir>: engine map 1 -- the phone's heights. Joins the frame record ('FRC1': per render
# the ms, q16, camera mode, the player's drawn x, z, heading, the camera's yaw, x, z) with 'FRC2'
# (a4_gl.c A4_GL_FREC2: the same ms, the drawn y and camera y, the step's y and +0D0, the step's x/z
# less the drawn ones). Prints how far the drawn player car leaves the game's own path (the line
# between the step values around each frame, known after the fact) in height and in x-z, and the
# camera's height above the drawn car (the game's: car y + 105 + a slope term of 0..20).
import sys, struct, glob, os, math
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
OFF2 = OFF + 16 + 256 * 32
s16 = lambda v: v - 0x10000 if v & 0x8000 else v
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
pq = lambda a, p: a[min(len(a) - 1, int(p * len(a)))] if a else 0
for d in sys.argv[1:]:
    E1, E2 = {}, {}
    for fn in sorted(glob.glob(os.path.join(d, 'a4tel*.dat'))):
        b = open(fn, 'rb').read()
        if len(b) >= OFF + 16 + 256 * 32:
            mg, n = struct.unpack_from('<2I', b, OFF)
            if mg == 0x31435246:
                for k in range(max(0, n - 256), n):
                    e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32); E1[e[0]] = e
        if len(b) >= OFF2 + 16 + 255 * 16:
            mg, n = struct.unpack_from('<2I', b, OFF2)
            if mg == 0x32435246:
                for k in range(max(0, n - 255), n):
                    e = struct.unpack_from('<4I', b, OFF2 + 16 + (k % 255) * 16); E2[e[0]] = e
    if not E2: print(d, 'no FRC2 (a build without it)'); continue
    rows = []
    for t in sorted(E2):
        if t not in E1: continue
        a, b2 = E1[t], E2[t]
        mode = (a[2] >> 20) & 0xFF; q = (a[2] & 0xFFFFF) / 65536.0
        x, z = s32(a[3]), s32(a[4])
        rows.append(dict(t=t, q=q, mode=mode, x=x, z=z, y=s16(b2[1] & 0xFFFF), cy=s16(b2[1] >> 16),
                         yW=s16(b2[2] & 0xFFFF), dW=s16(b2[2] >> 16), xW=x + s16(b2[3] & 0xFFFF), zW=z + s16(b2[3] >> 16)))
    # the steps: a new step where the step values change
    st = []; last = None
    for r in rows:
        key = (r['xW'], r['zW'], r['yW'])
        if key != last: st.append(key); last = key
        r['k'] = len(st) - 1
    oy, oxz, rel = [], [], []
    for r in rows:
        if r['k'] + 1 >= len(st): continue
        a, b2 = st[r['k']], st[r['k'] + 1]
        if math.hypot(b2[0] - a[0], b2[1] - a[1]) > 2000: continue
        q = min(r['q'], 1.0)
        oy.append(abs(r['y'] - (a[2] + (b2[2] - a[2]) * q)))
        oxz.append(math.hypot(r['x'] - (a[0] + (b2[0] - a[0]) * q), r['z'] - (a[1] + (b2[1] - a[1]) * q)))
        if r['mode'] == 2: rel.append(r['cy'] - r['y'])
    oy.sort(); oxz.sort(); rel.sort()
    print('%s: %d frames with heights, %d steps | the drawn car off the game\'s path: height 50%% %d 90%% %d 99%% %d max %d; x-z 50%% %.0f 90%% %.0f 99%% %.0f max %.0f | camera y less car y (chase): 1%% %d 50%% %d 99%% %d'
          % (os.path.basename(d.rstrip('/')), len(rows), len(st), pq(oy, .5), pq(oy, .9), pq(oy, .99), oy[-1] if oy else 0,
             pq(oxz, .5), pq(oxz, .9), pq(oxz, .99), oxz[-1] if oxz else 0, pq(rel, .01), pq(rel, .5), pq(rel, .99)))
