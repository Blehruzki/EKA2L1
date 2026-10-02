import barfile, rleraw, struct, sys, os
def rgb(c): return ((c>>8)&15)*17,((c>>4)&15)*17,(c&15)*17
def render(d, path, zoom=8, flip=False, nib='lo'):
    t,w,h,ds=struct.unpack_from('<HHHH',d,0)
    if t==0:
        _,w,h,pal,lo,hi = rleraw.decode0(d); px = lo if nib=='lo' else hi
    else:
        _,w,h,pal,px = rleraw.decode(d)
    rows=list(range(h-1,-1,-1)) if flip else list(range(h))
    out=['P3','%d %d'%(w,h),'255']
    for y in rows:
        for x in range(w):
            r,g,b=rgb(pal[px[y*w+x]]); out.append('%d %d %d'%(r,g,b))
    open('_t.ppm','w').write('\n'.join(out)+'\n')
    os.system('convert _t.ppm -filter point -resize %d%% %s'%(zoom*100,path))
    return w,h
if __name__=='__main__':
    b=barfile.Bar('lightbar_STOCK.bar')
    for n in sys.argv[1:]:
        full=[k for k in b.names if k.lower().endswith(n.lower()+'.rle')][0]
        w,h=render(b.get(full), '_r_%s.png'%n, 8)
        print(n,w,h)
