# dualpic.py <dir> <outdir> [x0 y0 x1 y1]: a check build's same-frame captures (A4_DUALCAP): the
# software picture (the game's own) and the GPU picture of each capture, side by side as PNG,
# and how many pixels of the box (default: the whole 3D view) differ by more than 48 in any
# channel -- where the GPU path draws something other than the game.
import sys, struct, glob, os
from PIL import Image
HDR = 256; FPS = 560; FB = 320 * 240 * 2
box = tuple(int(v) for v in sys.argv[3:7]) if len(sys.argv) >= 7 else (0, 11, 320, 240)
os.makedirs(sys.argv[2], exist_ok=True)
def img(d, off):
    im = Image.new('RGB', (320, 240)); px = im.load()
    for y in range(240):
        row = struct.unpack_from('<320H', d, off + y * 640)
        for x, v in enumerate(row): px[x, y] = ((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
    return im
for f in sorted(glob.glob(sys.argv[1] + '/a4tel*.dat')):
    d = open(f, 'rb').read(); cap = struct.unpack_from('<I', d, 8)[0]
    if len(d) < HDR + FPS + 2 * FB: continue
    sw, gp = img(d, HDR + FPS), img(d, HDR + FPS + FB)
    a, b = sw.load(), gp.load(); n = 0; tot = 0
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            tot += 1
            if max(abs(a[x, y][i] - b[x, y][i]) for i in range(3)) > 48: n += 1
    out = Image.new('RGB', (640, 240)); out.paste(sw, (0, 0)); out.paste(gp, (320, 0))
    out.save('%s/cap%04d.png' % (sys.argv[2], cap))
    print('cap %d: %d of %d pixels differ (%.1f%%)' % (cap, n, tot, 100.0 * n / tot))
