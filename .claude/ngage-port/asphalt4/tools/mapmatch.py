# mapmatch.py <dump> <ref.png> <outdir>: find the posts whose minimap track line matches
# the reference frame's (the cyan pixels in the minimap box), and save each match with
# its three posts either side.
import sys, struct, numpy as np, os
from PIL import Image
dump, refp, out = sys.argv[1:4]; os.makedirs(out, exist_ok=True)
def cyan(im):
    b = im[70:111, 4:45]
    return (b[..., 0] < 90) & (b[..., 1] > 180) & (b[..., 2] > 180)
ref = np.asarray(Image.open(refp).convert('RGB')).astype(np.int32); rm = cyan(ref)
fr = []
with open(dump, 'rb') as f:
    while True:
        h = f.read(32)
        if len(h) < 32: break
        mg, post, ms, w, hh, pitch, bpp, _ = struct.unpack('<8I', h); d = f.read(pitch * hh)
        if bpp == 32:
            a = np.frombuffer(d, '<u4').reshape(hh, pitch // 4)[:, :w]
            im = np.dstack([(a >> 16) & 255, (a >> 8) & 255, a & 255]).astype(np.int32)
        else:
            a = np.frombuffer(d, '<u2').reshape(hh, pitch // 2)[:, :w].astype(np.int32)
            im = np.dstack([(a >> 11) << 3, ((a >> 5) & 63) << 2, (a & 31) << 3])
        fr.append((post, ms, im))
sc = []
for k, (p, ms, im) in enumerate(fr):
    m = cyan(im); u = (m | rm).sum()
    sc.append(((m & rm).sum() / u if u else 0, k))
sc.sort(reverse=True); print("ref cyan px", rm.sum(), "top", [(round(s,2), fr[k][0]) for s, k in sc[:8]])
picked = []
for s, k in sc:
    if s < float(os.environ.get("MIN", "0.5")): break
    if all(abs(k - c) > 60 for c in picked): picked.append(k)
for c in sorted(picked):
    print('match post %d (iou %.2f) ms %d' % (fr[c][0], dict((k, s) for s, k in sc)[c], fr[c][1]))
    row = np.concatenate([fr[j][2] for j in range(max(0, c - 6), min(len(fr), c + 7))], axis=1).astype(np.uint8)
    Image.fromarray(row).save(os.path.join(out, 'm%05d.png' % fr[c][0]))
