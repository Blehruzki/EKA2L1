"""Lift Fireboostx1/2/3 (32x32) out of the S60v2 archive and scale them to the
64x64 the S60v3 boost1b/2b/3b slots use, so the swap is drop-in.

Bilinear in RGB, then snap every pixel back to the source palette -- the sheet
keeps its own 16 colours, no new ones are invented, and the soft radial falloff
survives the 2x better than nearest-neighbour would.
"""
import barfile, rleraw

def _rgb(c): return ((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17

def upscale2x(w, h, pal, px, key):
    W, H = w * 2, h * 2
    rgb = [_rgb(c) for c in pal]
    out = []
    for Y in range(H):
        sy = (Y + 0.5) / 2 - 0.5
        y0 = max(0, min(h - 1, int(sy // 1))); y1 = min(h - 1, y0 + 1); fy = sy - y0
        for X in range(W):
            sx = (X + 0.5) / 2 - 0.5
            x0 = max(0, min(w - 1, int(sx // 1))); x1 = min(w - 1, x0 + 1); fx = sx - x0
            q = [px[y * w + x] for y, x in ((y0,x0),(y0,x1),(y1,x0),(y1,x1))]
            # a texel touching the colour key stays keyed: blending into it would
            # smear the key colour into the glow's edge
            if any(i in key for i in q):
                out.append(key[0]); continue
            acc = [0.0, 0.0, 0.0]
            for (i, wgt) in zip(q, ((1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy)):
                for k in range(3): acc[k] += rgb[i][k] * wgt
            best, bd = 0, 1 << 30
            for i, c in enumerate(rgb):
                if i in key: continue
                dd = sum((acc[k] - c[k]) ** 2 for k in range(3))
                if dd < bd: bd, best = dd, i
            out.append(best)
    return W, H, out

def build(src='lightbar_S60v2.bar'):
    v2 = barfile.Bar(src)
    res = {}
    for i in (1, 2, 3):
        t, w, h, pal, px = rleraw.decode(v2.get('Textures\\Fireboostx%d.RLE' % i))
        key = [k for k, c in enumerate(pal) if c == 0xFF0F]
        assert key, 'no colour key in Fireboostx%d' % i
        W, H, up = upscale2x(w, h, pal, px, key)
        # type 2 to match the boost*b slots it replaces
        res[i] = (rleraw.encode(2, W, H, pal, up), W, H)
    return res

if __name__ == '__main__':
    for i, (blob, W, H) in build().items():
        print('Fireboostx%d -> %dx%d, %d bytes' % (i, W, H, len(blob)))
