import rleraw, barfile
SRC='lightbar_STOCK.bar'; SW=240
def slice_rows(y0,y1,pal,px,w):
    bh=y1-y0+1; out=[0]*(SW*bh)
    for yy in range(bh):
        for xx in range(SW):
            out[yy*SW+xx]=px[(y0+yy)*w+int(xx*w/SW)]
    return rleraw.encode(2,SW,bh,pal,out), bh
def build():
    b=barfile.Bar(SRC)
    t,w,h,pal,px=rleraw.decode(b.get(r'Textures\interf\hud\newhud.RLE'))
    empty,eh = slice_rows(31,56,pal,px,w)   # silver: empty bar + empty squares
    goldbar,gh = slice_rows(72,90,pal,px,w) # gold bar only  (rel 0..18)
    squares,sh = slice_rows(91,95,pal,px,w) # lit squares    (rel 19..23)
    return b, empty, goldbar, squares, (eh,gh,sh)
