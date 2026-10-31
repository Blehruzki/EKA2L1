"""seqan.py <snapdir> : per-frame triangle sequences (A4_SEQDUMP) -> GL draw calls under merge strategies."""
import os, sys, struct, collections
TEXZ, SHAD = 4, 3
def frames(d):
    seen = set()
    for f in sorted(os.listdir(d)):
        b = open(os.path.join(d, f), 'rb').read()
        seq, n = struct.unpack_from('<II', b, 0)
        if not seq or seq in seen or n == 0: continue
        seen.add(seq)
        w = struct.unpack_from('<%dI' % (4 * n), b, 0xC010)
        tris = []
        for i in range(n):
            tid, a, c, info = w[i*4:i*4+4]
            tris.append((tid, a & 0xFFFF, a >> 16, c & 0xFFFF, c >> 16, info))
        yield seq, tris
def runs(keys, batch=None):
    n = 0; prev = None
    for i, k in enumerate(keys):
        if k != prev or (batch and i % batch == 0): n += 1
        prev = k
    return n
def commute(a, b):
    ka, kb = a[0] >> 28, b[0] >> 28
    if ka == kb and ka in (TEXZ, SHAD): return True
    return a[3] < b[1] or b[3] < a[1] or a[4] < b[2] or b[4] < a[2]
def greedy(tris, key):
    """each triangle joins the latest run of its key it can legally move back to"""
    out = []   # runs: [key, [tris], bbox, allkind]
    for t in tris:
        k = key(t); placed = False
        for r in reversed(out):
            if r[0] == k: r[1].append(t); placed = True; break
            # can t move before every triangle of run r?
            x0, y0, x1, y1, kinds = r[2]
            if not (t[3] < x0 or x1 < t[1] or t[4] < y0 or y1 < t[2]):
                kt = t[0] >> 28
                if not (len(kinds) == 1 and kt in kinds and kt in (TEXZ, SHAD)):
                    if not all(commute(t, u) for u in r[1]): break
        if not placed:
            out.append([k, [t], [t[1], t[2], t[3], t[4], {t[0] >> 28}]])
        else:
            bb = r[2]; bb[0] = min(bb[0], t[1]); bb[1] = min(bb[1], t[2]); bb[2] = max(bb[2], t[3]); bb[3] = max(bb[3], t[4]); bb[4].add(t[0] >> 28)
    return len(out)
def atlas_key(maxdim):
    def k(t):
        info = t[5]; w, h = (info >> 1) & 0x7FF, (info >> 12) & 0x7FF
        if (info & 1) and w <= maxdim and h <= maxdim: return ('atlas', t[0] >> 28)
        return t[0]
    return k
tot = collections.Counter(); nf = 0
for seq, tris in frames(sys.argv[1]):
    nf += 1
    ids = [t[0] for t in tris]
    kinds = collections.Counter(t[0] >> 28 for t in tris)
    texz_sorted = []
    seg = []
    for t in tris:
        if t[0] >> 28 == TEXZ: seg.append(t)
        else:
            texz_sorted += sorted(seg, key=lambda u: u[0]); seg = []; texz_sorted.append(t)
    texz_sorted += sorted(seg, key=lambda u: u[0])
    inr = sum(1 for t in tris if t[5] & 1)
    r = dict(n=len(tris), s0=runs(ids, 512), s1=runs(ids), s2=runs([t[0] for t in texz_sorted]),
             s3=greedy(tris, lambda t: t[0]), s4=greedy(tris, atlas_key(128)), s5=greedy(tris, atlas_key(256)),
             a1=runs([atlas_key(128)(t) for t in tris]), lb=len(set(ids)), inr=inr)
    for k, v in r.items(): tot[k] += v
    if nf <= 12 or nf % 10 == 0:
        print('seq %5d tris %4d | now %3d  1batch %3d  texzsort %3d  reorder %3d  +atlas128 %3d  +atlas256 %3d | atlas-only %3d | distinct %3d | inrange %2d%% | kinds %s'
              % (seq, r['n'], r['s0'], r['s1'], r['s2'], r['s3'], r['s4'], r['s5'], r['a1'], r['lb'], 100 * inr // max(1, r['n']), dict(sorted(kinds.items()))))
print('%d frames, mean per frame: ' % nf + '  '.join('%s %.0f' % (k, v / nf) for k, v in tot.items()))
