#!/usr/bin/env python3
# frec3.py <a4tel / a4mark files or dirs> [mode]: round 76, FRC3 (a4_gl.c A4_GL_FREC3) beside FRC1, joined by the
# render's ms, from the ring captures (a4telN.dat) and the owner's marks (a4markN.dat, a press of C: header word 2 has
# bit 31). Modes:
#   files  each file: ring or mark, its renders, the span of time it covers
#   rot    the player car's and the other car's turn per render (heading, pitch, roll, the step's forward axis, the
#          angles +A434 gave the draw and, from the 36-word build, the wreck's tumble +DD2A draws: -42 x its path
#          frame +034), the engine's substeps between renders -- the choppy rotation
#   turn   the player's heading, the step's axis (two substeps late), the draw's heading (+A434), the camera's yaw and
#          the car-less-camera angle per render, and its reversals -- the snap-back
#   spr    per render the largest sprite: its rectangle, the cells a list face covers / all covered cells, its
#          bucket and the nearest cover's -- the searchlight
#   obj    per render the camera's segment, the objects taken and refused by reason, the farthest taken bucket, the
#          list's triangles, GL errors -- the far buildings
#   gob [file] a mark's scenery items (box, buckets), sprite list and the list's tiles, drawn over its screenshot
#          (<file>.gob.png)
#   list   every word of every render
#   lines  the grey lines r77 recorded (LIN1): the row, its undrawn run, the faces and items whose box spans it,
#          (round 81) the crack runs the fill recorded, with the surfaces over the run's middle above and below, and
#          the crack counters (CRK1),
#          and (the second build) the stream's triangles whose box ends or begins on the rows beside it
#   wreck  per render with the other car a wreck: substep, camera, its path frame +034 and +048, and (A4_WRECKPROBE)
#          the +048 x 256 the lifted draw used (eng_wt8; the counter's own value x 256 on the step's last substep),
#          with the draw's angles (+A434) -- the takedown's animation at the substep's time
#   shadchk the crack counters with A4_SHADCHECK's [40..47] (round 84); gobjchk with A4_GOBJ86CHECK's [40..43]: the
#          objects' radix sort against the insertion sort's order, their box corners against round 85's (round 86);
#          carchk with A4_CAREARLYCHECK's: the early car take's verdicts against the routine's own cull (round 86)
#   timers A4_PHTIMERS (CRK1 [52..60]): the scene's calls and the readback, ms a scene, capture to capture (round 86)
#   blendcen the crack counters with A4_BLENDCEN's [40..47]: the blended classes' software copies against the GPU's drawn mask (round 86)
#   ngwhy  the crack counters with A4_NGWHY's [40..47]: the meshes refused for more than 12 texture groups (round 87)
#   blurchk the crack counters with A4_BLURCHECK's [40..45]: the nitro blur, the game's against ours (round 88)
#   defer  the crack counters with A4_BLDEFER's [40..44]: the see-through copies drawn after the composite (round 89)
#   deferchk  the same with A4_BLDEFERCHK's [45..47] (bench)
#   phase  A4_FRC4 (round 87): each render's 3D pass, our hook, the composite, 2D, the game's blocks, our flushes and GL calls
#   steps  per render: the engine's substep and step count beside the scene's counts (round 86)
#   objwhy the crack counters with A4_OBJWHY's [40..47]: the objects refused by the take, by the first test failed (round 86)
# A mark covers the 256 renders (~13 s) before the press. Angles are 2,048 a turn.
import sys, struct, glob, os, math
HDR, FPS, FB = 256, 560, 320 * 240 * 2
OFF1 = HDR + FPS + FB + 122 * 4 + 0x700 + 384 + 0x680          # FRC1
OFF3 = OFF1 + 16 + 256 * 32                                     # FRC3: 'FRC3', count, W words a render, N renders
def offs(d):                                                    # the sprite list ('SPL1'), then 'GOB1' + struct gstate
    w, n = struct.unpack_from('<2I', d, OFF3 + 8)
    o = OFF3 + 16 + n * w * 4
    return o, o + 32 + 16 * 16
args = sys.argv[1:]
MODES = ('files', 'rot', 'turn', 'spr', 'obj', 'gob', 'list', 'lines', 'wreck', 'shadchk', 'gobjchk', 'carchk', 'timers', 'objwhy', 'steps', 'blendcen', 'phase', 'ngwhy', 'blurchk', 'defer', 'deferchk')
mode = 'files'
if args and args[-1] in MODES: mode = args.pop()
elif len(args) >= 2 and args[-2] == 'gob': mode = 'gob'; args = args[:-2] + [args[-1]]
files = []
for a in args:
    files += sorted(glob.glob(os.path.join(a, 'a4tel*.dat')) + glob.glob(os.path.join(a, 'a4mark*.dat'))) if os.path.isdir(a) else [a]
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
s16 = lambda v: v - 65536 if v & 0x8000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024

def load(fn):
    d = open(fn, 'rb').read()
    h = struct.unpack_from('<64I', d, 0)
    info = dict(fn=fn, mark=bool(h[2] >> 31), idx=h[2] & 0x7FFFFFFF, ok=False, R={})
    if len(d) < OFF3 + 16 or struct.unpack_from('<I', d, OFF1)[0] != 0x31435246 or struct.unpack_from('<I', d, OFF3)[0] != 0x33435246:
        return info, d
    n1 = struct.unpack_from('<I', d, OFF1 + 4)[0]
    mg3, n3, w3, r3 = struct.unpack_from('<4I', d, OFF3)
    e1 = {}
    for k in range(max(0, n1 - 256), n1):
        e = struct.unpack_from('<8I', d, OFF1 + 16 + (k % 256) * 32); e1[e[0]] = e
    for k in range(max(0, n3 - r3), n3):
        e = struct.unpack_from('<%dI' % w3, d, OFF3 + 16 + (k % r3) * w3 * 4)
        if e[0] in e1: info['R'][e[0]] = (e1[e[0]], e)
    info['ok'] = True
    return info, d

def row(e1, e3):
    r = dict(t=e1[0], seg=e1[2] & 0xFFF, mode=(e1[2] >> 20) & 0xFF, taken1=(e1[2] >> 13) & 0x7F,
             x=s32(e1[3]), z=s32(e1[4]), hd=e1[5] & 0x7FF, yaw=(e1[5] >> 16) & 0x7FF, cx=s32(e1[6]), cz=s32(e1[7]))
    r.update(cnt=e3[1], sub=e3[2] & 15, n=(e3[2] >> 4) & 15, cmode=(e3[2] >> 8) & 0xFF, scale=e3[2] >> 16,
             phd=e3[3] & 0xFFFF, turn=s16(e3[3] >> 16), pitch=s16(e3[4] & 0xFFFF), roll=s16(e3[4] >> 16),
             fx=s16(e3[5] & 0xFFFF), fz=s16(e3[5] >> 16), fy=s16(e3[6] & 0xFFFF), sy=s16(e3[6] >> 16),
             vx=s16(e3[7] & 0xFFFF), vz=s16(e3[7] >> 16), keys=e3[8] & 0xFFFF, steer=s16(e3[8] >> 16))
    o = e3[9]
    r.update(oslot=o & 31, ofl=(o >> 5) & 3, o34=(o >> 7) & 511, ohd=o >> 16, opitch=s16(e3[10] & 0xFFFF),
             oroll=s16(e3[10] >> 16), ox=s32(e3[11]), oz=s32(e3[12]), oy=s16(e3[13] & 0xFFFF), ofx=s16(e3[13] >> 16),
             ofz=s16(e3[14] & 0xFFFF), ofy=s16(e3[14] >> 16), osx=s16(e3[15] & 0xFFFF), osy=s16(e3[15] >> 16))
    q = e3[16]
    r.update(rect=(q & 255, (q >> 8) & 255, (q >> 16) & 255, q >> 24), lmask=e3[17] & 0xFFFF, amask=e3[17] >> 16,
             sb=s16(e3[18] & 0xFFFF), nc=e3[18] >> 16, nspr=e3[19] & 255, ntest=(e3[19] >> 8) & 255,
             tw=(e3[19] >> 16) & 255, th=e3[19] >> 24)
    r.update(taken=e3[20] & 255, rrange=(e3[20] >> 8) & 255, rfull=(e3[20] >> 16) & 255, rnear=e3[20] >> 24,
             rrot=e3[21] & 255, rtex=(e3[21] >> 8) & 255, roff=(e3[21] >> 16) & 255, calls=e3[21] >> 24,
             maxq=e3[22] & 0xFFFF, nfar=(e3[22] >> 16) & 255, ltri=e3[23] & 0xFFFF, llost=(e3[23] >> 16) & 255,
             glerr=e3[23] >> 24, raw=e3)
    if len(e3) >= 32:   # r76's second build: what +A434 gave the draw, the camera's pitch and roll
        r.update(da=s16(e3[24] >> 16), db=s16(e3[24] & 0xFFFF), dc=s16(e3[25] & 0xFFFF),
                 dox=s16(e3[25] >> 16), doy=s16(e3[26] & 0xFFFF), doz=s16(e3[26] >> 16),
                 oda=s16(e3[27] >> 16), odb=s16(e3[27] & 0xFFFF), odc=s16(e3[28] & 0xFFFF),
                 odox=s16(e3[28] >> 16), odoy=s16(e3[29] & 0xFFFF), odoz=s16(e3[29] >> 16),
                 cpitch=s16(e3[30] & 0xFFFF), croll=s16(e3[30] >> 16),
                 npc=e3[31] & 255, noc=(e3[31] >> 8) & 255, nall=(e3[31] >> 16) & 255, site=(e3[31] >> 24) & 3)
    if len(e3) >= 36:   # r76's third build: the wreck path words (the other car's in [13..15], the player's [32..34])
        r.update(oy=s16(e3[13] & 0xFFFF), o34w=s16(e3[13] >> 16), o44=s16(e3[14] & 0xFFFF), o48=s16(e3[14] >> 16),
                 ob28=e3[15] & 0xFFFF, o3c=s16(e3[15] >> 16), ofx=0, ofz=0, ofy=0, osx=0, osy=0,
                 p34=s16(e3[32] & 0xFFFF), p3c=s16(e3[32] >> 16), p44=s16(e3[33] & 0xFFFF), p48=s16(e3[33] >> 16),
                 pb28=e3[34] & 0xFFFF, pseg=e3[34] >> 16, oaddr=e3[35])
        # the wreck's turn as +DD2A draws it: -42 x the path frame (clamped to the path; the length is not recorded),
        # its sign by +B28 bit 4, only while +034 is not -1 and +B28 bit 11 is set
        for k, f, b in (('otumble', 'o34w', 'ob28'), ('ptumble', 'p34', 'pb28')):
            r[k] = (42 * r[f] * (1 if r[b] & 16 else -1)) if r[f] != -1 and r[b] & 0x800 else None
    return r

# the drawn heading on +678's circle: the forward axis's angle + 1,024 (m76a: heading less it -1,024 in 799 of 1,008)
def ang(x, z): return (int(round(math.atan2(x, z) * 1024 / math.pi)) + 1024) & 0x7FF if x or z else -1

infos = [load(f) for f in files if os.path.getsize(f) >= 256]   # a ring file cut by a kill is empty
if mode == 'files':
    for info, d in infos:
        R = info['R']
        if not info['ok']: print('%s: no FRC3' % info['fn']); continue
        ts = sorted(R)
        print('%-40s %-6s %3d  renders %3d  %d..%d ms (%.1f s)' % (os.path.basename(info['fn']), 'MARK' if info['mark'] else 'ring',
              info['idx'], len(ts), ts[0], ts[-1], (ts[-1] - ts[0]) / 1000.0))
    sys.exit()

if mode == 'timers':
    # round 86 (A4_PHTIMERS): CRK1 [52..60] summed from the boot -- scenes, then ticks (1 ms) of the scenery walk, the
    # cars, the shadow's insert, +31A26/+31A50, the ordering table's draw; the readback's first strip, the others,
    # composites. Each ring capture against the previous one: the window's ms a scene.
    # ring captures and the owner's marks together, in the order of their scene count (summed from the launch): the
    # stretch between two of them, and the whole session from the launch to each
    rows = []
    for info, d in infos:
        i = d.rfind(b'CRK1')
        if i < 0 or len(d) < i + 248: continue
        j = d.rfind(b'TIM2')                        # round 86 (r86d on): the GL calls by class and by block
        t2 = struct.unpack_from('<60I', d, j) if j >= 0 and len(d) >= j + 240 else (0,) * 60
        rows.append((struct.unpack_from('<64I', d, i) + t2, ('mark%d' if info['mark'] else 'cap%d') % info['idx']))
    rows.sort(key=lambda r: r[0][52])
    names = ('walk', 'cars', 'shadow', '31A26/50', 'table draw', 'read 1st', 'read rest', 'obj flush', 'car flush', 'shad draw')
    print('stretch            scenes  ms a scene: ' + ' '.join('%10s' % n for n in names) + '   (read: per composite; the flushes mod 65,536 ticks: the launch line wrong past it)')
    z = ((0,) * 124, 'launch')
    for (a, la), (b, lb) in [(z, rows[-1])] + list(zip(rows, rows[1:])) if rows else []:
        dn = b[52] - a[52]; dc = b[60] - a[60]
        if dn <= 0: continue
        v = [(b[k] - a[k]) / dn for k in range(53, 58)] + [(b[k] - a[k]) / max(dc, 1) for k in (58, 59)]
        # round 86 (r86d on): [62] the objects' flush (low 16) and the cars' (high 16), mod 65,536; [63] the shadow's draws
        v += [((b[62] & 0xFFFF) - (a[62] & 0xFFFF)) % 65536 / dn, ((b[62] >> 16) - (a[62] >> 16)) % 65536 / dn,
              (b[63] - a[63]) / dn]
        print('%-8s-%-8s  %6d  %s' % (la, lb, dn, ' '.join('%10.2f' % x for x in v)))
        if b[64] == 0x324D4954:                     # 'TIM2': calls a scene (draw / bind / matrix / other), by block
            g = lambda k: (b[64 + k] - a[64 + k]) / dn
            blk = ' '.join('%s %.0f/%.0f/%.0f/%.0f' % (nm, g(o), g(o + 1), g(o + 2), g(o + 3))
                           for nm, o in (('objects', 6), ('cars', 10), ('batch', 14), ('list', 18), ('shadow', 22)))
            print('    GL calls a scene: draws %.1f, binds %.1f, matrix %.1f, other %.1f; corners %.0f | %s'
                  % (g(1), g(2), g(3), g(4), g(5), blk))
        if b[13] or b[15]:                          # round 88 (A4_RESTTIME): the post, the convert, the shadow's flushes
            dp = b[13] - a[13]; dv = b[15] - a[15]; dz = b[19] - a[19]
            print('    the post %.2f ms a post (%d posts, %.2f a scene); our convert %.2f ms (its window-size calls %.2f); '
                  'the batch flushed inside the shadow\'s draws %.2f ms a scene (%.2f flushes a scene)'
                  % ((b[12] - a[12]) / max(dp, 1), dp, dp / dn, (b[14] - a[14]) / max(dv, 1), (b[18] - a[18]) / max(dz, 1),
                     (b[16] - a[16]) / dn, (b[17] - a[17]) / dn))
        m1v1 = (b[110] & 0xFFFF) in (256, 512) and (b[110] >> 16) in (128, 192, 256)   # r91b's [46]: the half sky's image
        if b[64] == 0x324D4954 and not b[64 + 48] and b[108] and not m1v1:   # round 88, after r91b: the capture's pass
            dr = b[104] - a[104]; dc = b[108] - a[108]   # TIM2 [40..49]; [48] is A4_M1LIVE's pace, never 0 there
            print('    the reflection from the frame: %d second passes (%.2f a scene), %d not drawn (no recent capture), '
                  'captures refused %d; %d captures (%.2f ms each; texture names last %d, highest %d); %d point tables '
                  'made (%d ms in all), faces drawn again %d (%d fitted flat); paints made %d'
                  % (dr, dr / dn, (b[105] & 0xFFFF) - (a[105] & 0xFFFF), b[105] >> 16, dc, (b[110] - a[110]) / max(dc, 1),
                     b[109] & 0xFFFF, b[109] >> 16, b[106], b[113] >> 16, b[107] & 0xFFFF, b[107] >> 16, b[111]))
            if b[101] or b[123]:                    # round 89 (A4_M1HOOK): the car the game draws itself, TIM2 [37..39] [59]
                g = lambda k: b[64 + k] - a[64 + k]
                print('    the game\'s own car: %d paint triangles matched to a face (%.1f a scene), %d of them reflecting, '
                      '%d second passes (%.2f a scene); %d paint triangles matching no face (left baked)'
                      % (g(37), g(37) / dn, g(38), g(39), g(39) / dn, g(59)))
        elif b[64] == 0x324D4954 and b[64 + 46] >> 16 and not b[64 + 48]:   # round 88 (A4_M1SKY, r91b): the GPU's pass
            dr = b[104] - a[104]                    # TIM2 [40..47]; [48] is A4_M1LIVE's pace, never 0 there
            print('    the reflection on the GPU: %d second passes (%.2f a scene), %d on an earlier frame\'s mapping, %d not '
                  'drawn (no sky lately); '
                  '%d point tables made (%d ms in all), faces drawn again %d (%d fitted flat); half skies made %d '
                  '(%d ms in all; image %dx%d, mapping %d,%d); paints made %d'
                  % (dr, dr / dn, (b[105] & 0xFFFF) - (a[105] & 0xFFFF), (b[105] >> 16) - (a[105] >> 16), b[106], b[113] >> 16, b[107] & 0xFFFF, b[107] >> 16, b[108],
                     b[113] & 0xFFFF, b[110] & 0xFFFF, b[110] >> 16, b[109] & 0xFFFF, b[109] >> 16, b[111]))
        elif b[64] == 0x324D4954 and b[64 + 45]:     # round 88 (A4_M1LIVE): the paint's reflection after each composite
            dr = b[104] - a[104]                    # TIM2 [40..47]
            if dr > 0:
                print('    the reflection: %d refreshes (%.2f a scene), %.2f ms each making its texels, %.2f uploading them; '
                      '%.0f texels in %.1f rectangles each; textures noted %d, refreshes skipped %d, dropped %d; '
                      'the pace every %d composites (at most %d)'
                      % (dr, dr / dn, (b[105] - a[105]) / dr, (b[106] - a[106]) / dr, (b[107] - a[107]) / dr,
                         (b[108] - a[108]) / dr, b[109], b[110] - a[110], b[111] - a[111], b[112] & 0xFFFF, b[112] >> 16))
    sys.exit()

if mode in ('lines', 'shadchk', 'gobjchk', 'carchk', 'objwhy', 'blendcen', 'ngwhy', 'blurchk', 'defer', 'deferchk'):
    # r77's grey-line ring ('LIN1' after the merged tile grid): each hit -- row, undrawn run, the faces and items whose
    # screen box starts or ends on it (list faces' corners in camera space, as the list holds them)
    seen = set()
    for info, d in infos:
        so, g = offs(d)
        if len(d) < g + 16: continue
        size = struct.unpack_from('<I', d, g + 4)[0]
        lo = g + 16 + size + 16 + 600 + 600
        if len(d) < lo + 16 or struct.unpack_from('<I', d, lo)[0] != 0x314E494C: print(os.path.basename(info['fn']), 'no LIN1'); continue
        n = struct.unpack_from('<I', d, lo + 4)[0]
        W = 114 if len(d) >= lo + 16 + 8 * 114 * 4 else 48      # r77's second build: the stream's row edges from [48]
        for k in range(max(0, n - 8), n):
            e = struct.unpack_from('<%dI' % W, d, lo + 16 + (k % 8) * W * 4)
            key = (e[0], e[1], e[2])
            if key in seen: continue
            seen.add(key)
            nf, ng = (e[3] >> 16) & 255, e[3] >> 24
            kind, th = (e[1] >> 8) & 255, (e[1] >> 16) & 255
            KN = {0: 'line', 1: 'line', 2: 'crack 1-row', 3: 'crack 2-row', 4: 'gap %d rows (not filled)' % th}
            print('frame %5d row %3d %s undrawn %3d x %3d..%3d (rows below/above undrawn %d / %d) faces %d items %d F %d centre %d,%d'
                  % (e[0] & 0xFFFF, e[0] >> 16, KN.get(kind, 'kind %d' % kind), e[1] & 255, e[2] & 0xFFFF, e[2] >> 16, e[3] & 255,
                     (e[3] >> 8) & 255, nf, ng, e[46], e[47] & 0xFFFF, e[47] >> 16))
            if True:   # every record (a round-77 line as a one-row crack)
                # round 81: which of the recorded surfaces covers the run's middle pixel in the drawn row above and below
                # (list faces projected as the list's insert does, x = cx + F X / -Z, y = cy - F Y / -Z; the stream's
                # triangles as drawn; the GPU items by their boxes only)
                row, xm = e[0] >> 16, ((e[2] & 0xFFFF) + (e[2] >> 16)) / 2 + 0.5
                F, cx, cy = e[46], e[47] & 0xFFFF, e[47] >> 16
                def inside(P, x, y):
                    (ax, ay), (bx, by), (qx, qy) = P
                    d1 = (bx - ax) * (y - ay) - (by - ay) * (x - ax); d2 = (qx - bx) * (y - by) - (qy - by) * (x - bx)
                    d3 = (ax - qx) * (y - qy) - (ay - qy) * (x - qx)
                    return (d1 >= 0 and d2 >= 0 and d3 >= 0) or (d1 <= 0 and d2 <= 0 and d3 <= 0)
                tris = []
                for j in range(nf):
                    f = e[4 + 6 * j: 10 + 6 * j]
                    c = [s16(f[0] & 0xFFFF), s16(f[0] >> 16), s16(f[1] & 0xFFFF), s16(f[1] >> 16), s16(f[2] & 0xFFFF), s16(f[2] >> 16),
                         s16(f[3] & 0xFFFF), s16(f[3] >> 16), s16(f[4] & 0xFFFF)]
                    P = [(cx + F * c[3 * v] / -c[3 * v + 2], cy - F * c[3 * v + 1] / -c[3 * v + 2]) for v in range(3) if c[3 * v + 2] < 0]
                    if len(P) == 3: tris.append(('list', P))
                for j in range(6 if W == 114 else 0):
                    q = e[48 + 11 * j: 59 + 11 * j]
                    if not q[0]: continue
                    P = [(s32(q[2 + 3 * v]) / s32(q[4 + 3 * v]), s32(q[3 + 3 * v]) / s32(q[4 + 3 * v])) for v in range(3) if s32(q[4 + 3 * v])]
                    if len(P) == 3: tris.append(('stream %08x' % q[1], P))
                def owners(y):
                    o = [n for n, P in tris if inside(P, xm, y + 0.5)]
                    for j in range(ng):
                        a, b = e[40 + 2 * j], e[41 + 2 * j]
                        if s16(a & 0xFFFF) <= xm <= s16(b & 0xFFFF) and s16(a >> 16) <= y <= s16(b >> 16): o.append('item box')
                    return o or ['none recorded']
                below = row + (2 if kind == 3 else th if kind == 4 else 1)
                print('   owners at x %.1f: above (row %d) %s; below (row %d) %s' % (xm, row - 1, owners(row - 1), below, owners(below)))
            for j in range(nf):
                f = e[4 + 6 * j: 10 + 6 * j]
                c = [s16(f[0] & 0xFFFF), s16(f[0] >> 16), s16(f[1] & 0xFFFF), s16(f[1] >> 16), s16(f[2] & 0xFFFF), s16(f[2] >> 16),
                     s16(f[3] & 0xFFFF), s16(f[3] >> 16), s16(f[4] & 0xFFFF)]
                print('   face bucket %5d key %3d box rows %4d..%4d  corners (%d %d %d) (%d %d %d) (%d %d %d)' % ((f[4] >> 16), f[5] >> 24, s16((f[5] & 0xFFF) << 4) >> 4, s16(((f[5] >> 12) & 0xFFF) << 4) >> 4, *c))
            for j in range(ng):
                a, b = e[40 + 2 * j], e[41 + 2 * j]
                print('   item box %d %d %d %d' % (s16(a & 0xFFFF), s16(a >> 16), s16(b & 0xFFFF), s16(b >> 16)))
            for j in range(6 if W == 114 else 0):     # rows y-1, y, y+1 x (box ends there, box begins there)
                q = e[48 + 11 * j: 59 + 11 * j]
                if not q[0]: continue
                vs = []
                for v in range(3):
                    xw, yw, w = s32(q[2 + 3 * v]), s32(q[3 + 3 * v]), s32(q[4 + 3 * v])
                    vs.append('(%.2f %.2f w %d)' % (xw / w, yw / w, w) if w else '(w 0)')
                print('   stream tri, box %s row %+d: tex %08x  %s' % ('ends on' if j % 2 == 0 else 'begins on', j // 2 - 1, q[1], ' '.join(vs)))
    best = None   # round 81: the crack counters ('CRK1'), the latest (largest) of the files
    for info, d in infos:
        i = d.rfind(b'CRK1')
        if i >= 0 and len(d) >= i + 96:
            c = struct.unpack_from('<%dI' % (52 if d[i + 256:i + 260] == b'CUT1' or len(d) >= i + 256 + 1544 else 48 if d[i + 192:i + 196] == b'CUT1' or len(d) >= i + 192 + 1544 else 40 if len(d) >= i + 160 else 32 if len(d) >= i + 128 else 24), d, i)
            if best is None or c[1] + c[2] > best[1] + best[2]: best = c
    if best:
        c = best
        print('cracks (CRK1, from the boot): one-row runs %d, two-row %d, >= 64 px %d, pixels %d; recorded %d' % (c[1], c[2], c[3], c[4], c[23]))
        print('  by 16-row band:', ' '.join('%d-%d:%d' % (16 * b, 16 * b + 15, c[5 + b]) for b in range(15) if c[5 + b]))
        print('  longest %d px, row %d, frame %d, F %d' % (c[20], c[21] & 0xFFFF, c[21] >> 16, c[22]))
        if len(c) >= 32:
            print('  gaps of 3-6 rows (not filled): %d runs, %d of 64+, the thickest %d rows; automatic marks asked %d' % (c[24], c[25], c[28], c[26]))
            print('  triangles cut (A4_CLIPG): %d, pieces drawn %d, the farthest corner %d px' % (c[29], c[30], c[31]))
        if len(c) >= 40:
            print('  handed back to the game beyond the band: list faces %d, GPU objects %d, GPU cars %d' % (c[32], c[33], c[34]))
        if len(c) >= 48:
            print('  wrapped corners (the game projection past int16, undone): %d; projected again %d; refused (word disagreed) %d; handed back by the exact test alone: faces %d, items %d' % (c[35], c[39], c[36], c[37], c[38]))
            if len(c) >= 52:
                print('  filled (from the boot): %d rows, %d pixels%s' % (c[48], c[49], ('; mask check (bench): %d rows skipped, %d not all drawn, %d said drawn wrongly' % (c[46], c[45], c[47])) if (c[46] or c[45] or c[47]) and mode not in ('objwhy', 'blendcen', 'ngwhy', 'blurchk', 'defer', 'deferchk') else ''))
            if len(c) >= 52 and (c[50] or c[51]):   # round 84 (A4_SHADBATCH)
                print('  shadow batch (from the boot): faces kept %d; buckets lost (never drawn: their faces but the trigger) %d' % (c[50], c[51]))
            if mode == 'carchk':    # round 86, bench A4_CAREARLYCHECK: [40..43] are its
                print('  early car take check (bench): judged inside %d, of them culled by the routine %d (unsound); not certain %d' % (c[40], c[41], c[42]))
            elif mode in ('defer', 'deferchk'):   # round 89, A4_BLDEFER: [40..44] are its; A4_BLDEFERCHK (bench): [45..47]
                print('  see-through copies deferred (A4_BLDEFER, from the boot): listed %d, drawn again after the composite %d (%d rows), drawn whole before another software draw %d, in bands %d (%d rows; %d left alone, drawn there already), lists dropped %d'
                      % (c[40], c[41], c[42] & 0xFFFF, c[43] & 0xFFFF, c[44] >> 16, c[42] >> 16, c[43] >> 16, c[44] & 0xFFFF))
                if mode == 'deferchk':
                    print('  deferral check (bench): inputs compared %d, differing %d; hole pixels changed under a listed copy %d; pixels changed before an early draw %d'
                          % (c[45] >> 16, c[45] & 0xFFFF, c[46], c[47]))
            elif mode == 'blurchk':   # round 88, bench A4_BLURCHECK: [40..45] are its
                print('  nitro blur check (bench): blurs %d, pixels differing %d, blurs with any %d; screen not the composite\'s surface %d; square root checked over %d values, %d differing'
                      % (c[40], c[41], c[42], c[43], c[44], c[45]))
                if len(c) >= 49 and c[40]: print('  timed: the game\'s %.2f ms a blur, ours %.2f; strength %.0f on average' % (c[46] / c[40], c[47] / c[40], c[48] / c[40]))
            elif mode == 'ngwhy':     # round 87, bench A4_NGWHY: [40..47] are its
                print('  meshes of more than 12 groups (bench): refusals %d, groups %.1f each (most %d), faces %.1f each; every texture plain %d; groups <= 16: %d, <= 24: %d, <= 32: %d'
                      % (c[40], c[41] / max(c[40], 1), c[42], c[43] / max(c[40], 1), c[44], c[45], c[46], c[47]))
            elif mode == 'blendcen':  # round 86, bench A4_BLENDCEN: [40..47] are its
                print('  blended copies (bench): composites %d; alpha triangles %d, pixels %d, undrawn by the GPU %d (%.1f%%), triangles with any %d (%.1f%%) holding %d pixels (%.1f%%); additive %d, pixels %d'
                      % (c[40], c[41], c[42], c[43], 100.0 * c[43] / max(c[42], 1), c[44], 100.0 * c[44] / max(c[41], 1), c[47], 100.0 * c[47] / max(c[42], 1), c[45], c[46]))
            elif mode == 'objwhy':    # round 86, bench A4_OBJWHY: [40..47] are its
                print('  object refusals (bench): texture none %d, +0x28 != 0xFF %d, alpha map %d, > 1024 %d (their faces %d); mode %d; mesh %d (> 12 groups %d)' % (c[40], c[41], c[42], c[43], c[47], c[44], c[45], c[46]))
            elif mode == 'gobjchk':   # round 86, bench A4_GOBJ86CHECK: [40..43] are its
                print('  objects check (bench): flushes %d, sort faults %d (against the insertion sort); box corners %d, differing %d' % (c[40], c[41], c[42], c[43]))
            elif mode == 'shadchk':   # round 84, bench A4_SHADCHECK: [40..47] are its
                print('  shadow check (bench): faces found %d, not found %d; corners differing %d; buckets differing %d; pending items differing %d (of %d faces with any); tests: candidates %d, full scans %d' % (c[40], c[41], c[42], c[43], c[44], c[45], c[46], c[47]))
            elif (c[42] or c[43]) and not c[44] and mode not in ('gobjchk', 'carchk', 'objwhy', 'blendcen', 'ngwhy', 'blurchk', 'defer', 'deferchk'):
                print('  mask skip by frame (bench): composite less readback %.3f ms a frame with the skip (%d frames), %.3f without (%d)' % (c[41] / max(c[43], 1), c[43], c[40] / max(c[42], 1), c[42]))
            if c[44] and mode not in ('shadchk', 'gobjchk', 'carchk', 'objwhy', 'blendcen', 'ngwhy', 'blurchk', 'defer', 'deferchk'):
                print('  wrap probe (bench): corners past int16 %d; of %d compared, %d off the view projection by > 2 px (%d inside int16); largest |projection| %d' % (c[40], c[44], c[41], c[42], c[43]))
    if mode == 'lines':   # round 82f: 'CUT1', the last 16 triangles cut, from every file
        seen = set()
        for info, d in infos:
            i = d.rfind(b'CUT1')
            if i < 0 or len(d) < i + 8 + 16 * 96: continue
            tot = struct.unpack_from('<I', d, i + 4)[0]
            print('%s: CUT1, %d cut from the boot, the last %d:' % (info['fn'], tot, min(tot, 16)))
            for k in range(min(tot, 16)):
                j = (tot - 1 - k) & 15
                w = struct.unpack_from('<24I', d, i + 8 + j * 96)
                if w in seen: continue
                seen.add(w)
                s32 = lambda u: u - (1 << 32) if u & 0x80000000 else u
                s16 = lambda u: u - 65536 if u & 0x8000 else u
                fr, F = w[0] & 0xFFFFF, w[0] >> 20
                kind, n, fix, ref, wrp = w[1] & 255, (w[1] >> 8) & 255, (w[1] >> 16) & 15, (w[1] >> 20) & 15, (w[1] >> 24) & 15
                cs = []
                for v in range(3):
                    wx, wy = s16(w[2 + 5 * v] & 0xFFFF), s16(w[2 + 5 * v] >> 16)
                    cs.append('word (%d,%d) zw %d view (%d,%d,%d) used (%.1f,%.1f)' % (wx, wy, s32(w[3 + 5 * v]), s32(w[4 + 5 * v]), s32(w[5 + 5 * v]), s32(w[6 + 5 * v]), s32(w[17 + 2 * v]) / 16, s32(w[18 + 2 * v]) / 16))
                print('  frame %d F %d kind %d -> %d vertices; projected again %d, refused %d, wrapped %d; tex %08x' % (fr, F, kind, n, fix, ref, wrp, w[23]))
                for c_ in cs: print('     ' + c_)
    sys.exit()

if mode == 'gob':
    info, d = infos[0]
    OFFS, g = offs(d)
    mg, size, frames, _ = struct.unpack_from('<4I', d, g)
    print('%s: %s %d, GOB1 %08x size %d frame %d' % (info['fn'], 'mark' if info['mark'] else 'ring', info['idx'], mg, size, frames))
    # round 86b added carlate after gaps: struct gstate's head is 47 words from then (size 6,332), 46 before (6,328)
    # round 87 (A4_GOBJNG) adds ngsum after cpdiff: 48 head words (R, T, o included), 6,336
    hw = (size - 192 * 32) // 4
    st = struct.unpack_from('<%dI' % hw, d, g + 16)
    names = ('magic frame n drawn F nmesh aused taken r_mode r_tex r_far r_full built frames_drawn draws calls r_off r_drawn r_mesh full resets peak batches r_rot r_range r_moved stale gaps '
             + ('carlate ' if hw >= 47 else '') + 'r_near cpchk cpdiff' + (' ngsum' if hw >= 48 else '')).split()
    print(' '.join('%s=%d' % (k, v) for k, v in zip(names[1:], st[1:len(names)])))
    n = st[2]; items = []
    for k in range(min(n, 192)):
        mesh, tex, dx, dy, dz, b0, b1, b2, b3, qn, qf = struct.unpack_from('<2I3i4h2H', d, g + 16 + 4 * hw + 32 * k)
        items.append((b0, b1, b2, b3, qn, qf)); print('  item %3d box %4d %4d %4d %4d  buckets %4d..%4d  d %6d %6d %6d' % (k, b0, b1, b2, b3, qn, qf, dx, dy, dz))
    mgs, ns, seen = struct.unpack_from('<3I', d, OFFS)
    print('sprites (SPL1 %08x): %d kept of %d seen' % (mgs, ns, seen))
    S = []
    for k in range(min(ns, 16)):
        q = struct.unpack_from('<4I', d, OFFS + 32 + 16 * k)
        x0, y0, x1, y1 = [((q[0] >> s) & 255 - 0) for s in (0, 8, 16, 24)]
        rc = [(v - 64) * 2 for v in (x0, y0, x1, y1)]
        S.append(rc)
        print('  sprite %2d rect %4d %4d %4d %4d  list cells %04x all %04x  bucket %5d nearest cover %5d  tex %dx%d dist %d'
              % (k, rc[0], rc[1], rc[2], rc[3], q[1] & 0xFFFF, q[1] >> 16, s16(q[2] & 0xFFFF), q[2] >> 16, q[3] & 255, (q[3] >> 8) & 255, q[3] >> 16))
    go = g + 16 + size
    grd = None
    if len(d) >= go + 16 + 1200 and struct.unpack_from('<I', d, go)[0] == 0x31445247:
        gm, gfr, gwh, gnf = struct.unpack_from('<4I', d, go)
        gw, gh = gwh & 0xFFFF, gwh >> 16
        lst = struct.unpack_from('<%dH' % (gw * gh), d, go + 16)
        mrg = struct.unpack_from('<%dH' % (gw * gh), d, go + 16 + 2 * gw * gh)
        grd = (gw, gh, lst, mrg)
        print('list tiles (GRD1): frame %d (%s the objects\' frame), %d list faces; tiles with a list face %d, with only GPU objects %d, with nothing %d of %d'
              % (gfr, 'is' if gfr == st[1] else 'NOT', gnf, sum(1 for v in lst if v != 0xFFFF),
                 sum(1 for a, b in zip(lst, mrg) if a == 0xFFFF and b != 0xFFFF), sum(1 for b in mrg if b == 0xFFFF), gw * gh))
    try:
        from PIL import Image, ImageDraw
        px = struct.unpack_from('<%dH' % (320 * 240), d, HDR + FPS)
        im = Image.new('RGB', (320, 240))
        im.putdata([(((p >> 11) & 31) << 3, ((p >> 5) & 63) << 2, (p & 31) << 3) for p in px])
        im = im.resize((640, 480), Image.NEAREST); dr = ImageDraw.Draw(im)
        if grd:   # tiles: blue tick where the list drew a face, red cross where neither the list nor a GPU object did
            gw, gh, lst, mrg = grd
            for ty in range(gh):
                for tx in range(gw):
                    x, y = tx * 32, ty * 32
                    if lst[ty * gw + tx] != 0xFFFF: dr.line([x + 2, y + 2, x + 8, y + 2], fill=(0, 128, 255), width=2)
                    elif mrg[ty * gw + tx] == 0xFFFF: dr.line([x + 12, y + 12, x + 20, y + 20], fill=(255, 0, 0)); dr.line([x + 20, y + 12, x + 12, y + 20], fill=(255, 0, 0))
        for b0, b1, b2, b3, qn, qf in items:
            dr.rectangle([b0 * 2, b1 * 2, b2 * 2, b3 * 2], outline=(0, 255, 0) if qf < 2000 else (255, 0, 255))
        for rc in S: dr.rectangle([v * 2 for v in rc], outline=(255, 255, 0))
        out = info['fn'] + '.gob.png'; im.save(out); print('drawn:', out, '(green items, magenta at bucket 2,000+, yellow sprites; blue ticks list tiles, red crosses tiles no list face or GPU object covers -- the game\'s own table may still draw there)')
    except ImportError:
        pass
    sys.exit()

R = {}
for info, d in infos:
    for t, (e1, e3) in info['R'].items(): R[t] = (e1, e3, info)
rows = [row(R[t][0], R[t][1]) for t in sorted(R)]
print('%d files, %d renders with FRC3' % (len(infos), len(rows)))
if not rows: sys.exit()

if mode == 'list':
    for r in rows: print(r['t'], ' '.join('%08x' % w for w in r['raw']))
elif mode == 'rot':
    # per render: the change of each angle and of the drawn forward axis's heading; a 'hold' is a render that did
    # not move while both neighbours did
    for who, H, P, Ro, FX, FZ in (('player', 'phd', 'pitch', 'roll', 'fx', 'fz'), ('other', 'ohd', 'opitch', 'oroll', 'ofx', 'ofz')):
        print('--', who)
        prev = None
        for r in rows:
            if who == 'other' and r['oslot'] == 31: prev = None; continue
            key = r['oslot'] if who == 'other' else 0
            if prev and prev[0] == key:
                p = prev[1]
                dh, dp, dr = wrap(r[H] - p[H]), wrap(r[P] - p[P]), wrap(r[Ro] - p[Ro])
                fa, pa = ang(r[FX], r[FZ]), ang(p[FX], p[FZ])
                df = wrap(fa - pa) if fa >= 0 and pa >= 0 else 0
                if dh or dp or dr or df or mode == 'rot':
                    dd = ''
                    tk = 'otumble' if who == 'other' else 'ptumble'
                    if r.get(tk) is not None or p.get(tk) is not None:
                        dd += '  tumble %5s (d %4s)' % (r.get(tk), (r[tk] - p[tk]) if r.get(tk) is not None and p.get(tk) is not None else '-')
                    if 'db' in r:   # the draw's angles (+A434) and their change
                        A, B, C = ('oda', 'odb', 'odc') if who == 'other' else ('da', 'db', 'dc')
                        dd = '  draw %5d %5d %5d (d %4d %4d %4d)' % (r[A], r[B], r[C], wrap(r[A] - p[A]), wrap(r[B] - p[B]), wrap(r[C] - p[C]))
                    print('%9d +%3d ms cnt %6d (+%d) sub %d/%d cam %3d  %s  d heading %5d pitch %5d roll %5d  axis %5d  (hd %4d p %5d r %5d)%s'
                          % (r['t'], r['t'] - p['t'], r['cnt'], r['cnt'] - p['cnt'], r['sub'], r['n'], r['cmode'],
                             ('slot %2d %s +034 %3d' % (r['oslot'], 'cam' if r['ofl'] == 1 else 'wreck' if r['ofl'] == 2 else 'near', r['o34'])) if who == 'other' else 'player',
                             dh, dp, dr, df, r[H] & 0x7FF, r[P], r[Ro], dd))
            prev = (key, r)
elif mode == 'turn':
    prevd = None; rev = 0; moving = 0
    for r in rows:
        fa = ang(r['fx'], r['fz'])
        rel = wrap(r['hd'] - r['yaw'])
        flag = ''
        if prevd is not None:
            d = rel - prevd[0]
            if abs(r['vx']) + abs(r['vz']) > 20: moving += 1
            if prevd[1] and d and (d > 0) != (prevd[1] > 0) and abs(d) >= 3: rev += 1; flag = '  <- reversal'
            prevd = (rel, d if d else prevd[1])
        else: prevd = (rel, 0)
        dr = '  draw %5d (car-cam %5d)' % (r['db'], wrap(r['db'] - r['yaw'])) if 'db' in r else ''
        print('%9d cnt %6d sub %d cam %3d  hd %4d axis %4d yaw %4d  car-cam %5d  turn %6d steer %6d keys %04x v %6d %6d%s%s'
              % (r['t'], r['cnt'], r['sub'], r['cmode'], r['hd'], fa, r['yaw'], rel, r['turn'], r['steer'], r['keys'], r['vx'], r['vz'], dr, flag))
    print('car-less-camera reversals (3 or more): %d of %d moving renders' % (rev, moving))
elif mode == 'wreck':
    pv = None
    for r in rows:
        if 'o34w' not in r or r['o34w'] == -1 or r['o34w'] == 0 and r['o48'] == 0: pv = None; continue
        t8 = r['pseg']
        print('%9d cnt %6d sub %d/%d cam %3d slot %2d  +034 %4d +048 %4d  t8 %6d (%5.2f)%s  draw %5d %5d %5d'
              % (r['t'], r['cnt'], r['sub'], r['n'], r['cmode'], r['oslot'], r['o34w'], r['o48'], t8, t8 / 256.0,
                 '  +%5.2f' % ((t8 - pv) / 256.0) if pv is not None else '        ', r.get('oda', 0), r.get('odb', 0), r.get('odc', 0)))
        pv = t8
elif mode == 'spr':
    for r in rows:
        if not r['nspr']: continue
        x0, y0, x1, y1 = [(v - 64) * 2 for v in r['rect']]
        print('%9d cam %3d  sprites %2d tested %2d  largest %4d %4d %4d %4d (tex %dx%d)  list cells %04x all %04x  bucket %5d nearest cover %5d'
              % (r['t'], r['cmode'], r['nspr'], r['ntest'], x0, y0, x1, y1, r['tw'], r['th'], r['lmask'], r['amask'], r['sb'], r['nc'] if r['amask'] else -1))
elif mode == 'phase':   # round 87 (A4_FRC4): each render's own split, joined to its FRC1/FRC3 row by its start tick
    F4 = {}
    for info, d in infos:
        i = d.rfind(b'FRC4')
        if i < 0 or len(d) < i + 16 + 256 * 32: continue
        n4 = struct.unpack_from('<I', d, i + 4)[0]
        for k in range(max(0, n4 - 256), n4):
            e = struct.unpack_from('<8I', d, i + 16 + (k % 256) * 32); F4[e[0]] = e
    print('FRC4: %d renders' % len(F4))
    B = lambda w, k: (w >> (8 * k)) & 255
    out = []
    for a, b in zip(rows, rows[1:]):
        e = F4.get(a['t'])
        if not e: continue
        dt = (b['t'] - a['t']) & 0xFFFFFFFF
        if not 0 < dt < 400: continue
        ph = dict(dt=dt, t3d=B(e[1], 0), hook=B(e[1], 1), comp=B(e[1], 2), read=B(e[1], 3), d2=B(e[2], 0), pres=B(e[2], 1),
                  bdraw=B(e[2], 2), ldraw=B(e[2], 3), walk=B(e[3], 0), cars=B(e[3], 1), shins=B(e[3], 2), a26=B(e[3], 3),
                  table=B(e[4], 0), oflush=B(e[4], 1), cflush=B(e[4], 2), sdraw=B(e[4], 3), draws=e[5] & 0xFFFF, binds=e[5] >> 16,
                  mtx=e[6] & 0xFFFF, oth=e[6] >> 16, corners=e[7] & 0xFFFFFF, logic=(e[7] >> 24) & 127, back=e[7] >> 31)
        ph['rest'] = dt - ph['t3d'] - ph['comp'] - ph['d2'] - ph['pres']
        ph['game3d'] = ph['t3d'] - ph['hook']
        ph['ltri'] = b['ltri']  # the list's triangles are counted one entry late (a4_frc3_pre)
        out.append((a, ph))
    keys = 'dt t3d hook game3d comp read d2 pres rest logic walk cars shins a26 table oflush cflush sdraw bdraw ldraw draws binds mtx oth corners'.split()
    for b, ph in out:
        print('%9d seg %4d taken %3d tex %3d list %4d | ' % (b['t'], b['seg'], b['taken'], b['rtex'], ph['ltri']) + ' '.join('%s %d' % (k, ph[k]) for k in keys) + (' BACK' if ph['back'] else ''))
    for lab, sel in (('renders under 60 ms', lambda x: x[1]['dt'] < 60), ('60-75 ms', lambda x: 60 <= x[1]['dt'] < 75), ('75 ms or more', lambda x: x[1]['dt'] >= 75)):
        L = [x for x in out if sel(x) and not x[1]['back']]
        if L: print('%-20s n %4d: ' % (lab, len(L)) + ' '.join('%s %.1f' % (k, sum(x[1][k] for x in L) / len(L)) for k in keys)
                    + ' | taken %.1f list %.1f' % (sum(x[0]['taken'] for x in L) / len(L), sum(x[1]['ltri'] for x in L) / len(L)))
elif mode == 'steps':   # round 86: per render, the engine's substep and steps with the scene's counts, for the time a render
    for r in rows:
        print('%9d sub %2d n %2d cam %3d taken %3d tex %3d calls %3d list tris %5d keys %04x speed %5d seg %4d' % (r['t'], r['sub'], r['n'], r['cmode'], r['taken'], r['rtex'], r['calls'], r['ltri'], r['keys'], int(math.hypot(r['vx'], r['vz'])), r['seg']))
elif mode == 'obj':
    for r in rows:
        print('%9d seg %4d cam %3d  taken %3d  refused range %3d full %3d near %3d rot/moved %3d tex %3d off %3d  calls %3d  far bucket %5d (%3d at 2000+)  list tris %5d lost %3d glerr %d'
              % (r['t'], r['seg'], r['cmode'], r['taken'], r['rrange'], r['rfull'], r['rnear'], r['rrot'], r['rtex'], r['roff'], r['calls'], r['maxq'], r['nfar'], r['ltri'], r['llost'], r['glerr']))
