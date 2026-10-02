import barfile, rleraw, struct, os, sys
def decode_any(d):
    t,w,h,ds=struct.unpack_from('<HHHH',d,0)
    if t==0:
        _,w,h,pal,lo,hi = rleraw.decode0(d); return w,h,pal,lo
    if t in (1,2):
        _,w,h,pal,px = rleraw.decode(d); return w,h,pal,px
    raise ValueError('type %d'%t)
def rgb(c): return ((c>>8)&15)*17,((c>>4)&15)*17,(c&15)*17
def render(d, path, zoom, flip=False, bg=(24,24,32)):
    w,h,pal,px = decode_any(d)
    key=[i for i,c in enumerate(pal) if c==0xFF0F]
    rows=range(h-1,-1,-1) if flip else range(h)
    out=['P3','%d %d'%(w,h),'255']
    for y in rows:
        for x in range(w):
            i=px[y*w+x]
            if i in key: r,g,b=bg
            else: r,g,b=rgb(pal[i])
            out.append('%d %d %d'%(r,g,b))
    open('_s.ppm','w').write('\n'.join(out)+'\n')
    os.system('convert _s.ppm -filter point -resize %d%% "%s"'%(zoom*100,path))
    return w,h
