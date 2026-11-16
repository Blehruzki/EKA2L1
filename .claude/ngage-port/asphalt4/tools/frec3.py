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
MODES = ('files', 'rot', 'turn', 'spr', 'obj', 'gob', 'list')
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

infos = [load(f) for f in files]
if mode == 'files':
    for info, d in infos:
        R = info['R']
        if not info['ok']: print('%s: no FRC3' % info['fn']); continue
        ts = sorted(R)
        print('%-40s %-6s %3d  renders %3d  %d..%d ms (%.1f s)' % (os.path.basename(info['fn']), 'MARK' if info['mark'] else 'ring',
              info['idx'], len(ts), ts[0], ts[-1], (ts[-1] - ts[0]) / 1000.0))
    sys.exit()

if mode == 'gob':
    info, d = infos[0]
    OFFS, g = offs(d)
    mg, size, frames, _ = struct.unpack_from('<4I', d, g)
    print('%s: %s %d, GOB1 %08x size %d frame %d' % (info['fn'], 'mark' if info['mark'] else 'ring', info['idx'], mg, size, frames))
    st = struct.unpack_from('<46I', d, g + 16)
    names = 'magic frame n drawn F nmesh aused taken r_mode r_tex r_far r_full built frames_drawn draws calls r_off r_drawn r_mesh full resets peak batches r_rot r_range r_moved stale gaps r_near'.split()
    print(' '.join('%s=%d' % (k, v) for k, v in zip(names[1:], st[1:29])))
    n = st[2]; items = []
    for k in range(min(n, 192)):
        mesh, tex, dx, dy, dz, b0, b1, b2, b3, qn, qf = struct.unpack_from('<2I3i4h2H', d, g + 16 + 184 + 32 * k)
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
elif mode == 'spr':
    for r in rows:
        if not r['nspr']: continue
        x0, y0, x1, y1 = [(v - 64) * 2 for v in r['rect']]
        print('%9d cam %3d  sprites %2d tested %2d  largest %4d %4d %4d %4d (tex %dx%d)  list cells %04x all %04x  bucket %5d nearest cover %5d'
              % (r['t'], r['cmode'], r['nspr'], r['ntest'], x0, y0, x1, y1, r['tw'], r['th'], r['lmask'], r['amask'], r['sb'], r['nc'] if r['amask'] else -1))
elif mode == 'obj':
    for r in rows:
        print('%9d seg %4d cam %3d  taken %3d  refused range %3d full %3d near %3d rot/moved %3d tex %3d off %3d  calls %3d  far bucket %5d (%3d at 2000+)  list tris %5d lost %3d glerr %d'
              % (r['t'], r['seg'], r['cmode'], r['taken'], r['rrange'], r['rfull'], r['rnear'], r['rrot'], r['rtex'], r['roff'], r['calls'], r['maxq'], r['nfar'], r['ltri'], r['llost'], r['glerr']))
