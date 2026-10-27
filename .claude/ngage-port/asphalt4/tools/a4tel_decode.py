#!/usr/bin/env python3
# Decode C:\a4telN.dat telemetry. v3: [128B header][560B a4_fps_state][framebuffer].
import sys, struct, os
from PIL import Image
NAMES=['magic','ver','capidx','fcount','W','H','stride','fbbytes','bb','up','frames',
       'prims','errc','ntex','bstep','last_prims','winstep','wvis','idle2d','miny','maxy',
       'pminy','pmaxy','fps','min_dt','max_dt','fps_bytes']
for fn in sys.argv[1:]:
    d=open(fn,'rb').read()
    if len(d)<128: print(fn,'too short'); continue
    h=struct.unpack('<32I',d[:128])
    print('\n%s (%d bytes)'%(fn,len(d)))
    for i,n in enumerate(NAMES): print('  %-10s %d'%(n,h[i]))
    ver=h[1]; fpsb=h[26] if ver>=3 else 0; fboff=128+fpsb
    if fpsb>=560:
        fs=struct.unpack('<140I', d[128:128+560])
        disp,min_dt,max_dt,sum_dt=fs[5],fs[6],fs[7],fs[8]; ring_pos=fs[11]; ring=fs[12:140]
        # order ring oldest->newest
        seq=[ring[(ring_pos+k)%128] for k in range(128)]
        nz=[x for x in seq if x]
        if nz:
            avg=sum(nz)/len(nz)
            print('  fps=%d  frame dt (ticks): min=%d max=%d avg=%.1f  spikes>2x=%d'%(
                disp,min_dt,max_dt,avg,sum(1 for x in nz if x>2*avg)))
            print('  last 40 dt:', ' '.join(str(x) for x in seq[-40:]))
    W,H,st=h[4],h[5],h[6]
    for sname,stride in (('s%d'%st,st),('s720',720)):
        if len(d)<fboff+stride*2: continue
        im=Image.new('RGB',(W,H)); px=im.load(); fb=d[fboff:]
        for y in range(H):
            base=y*stride*2
            if base+W*2>len(fb): break
            for x in range(W):
                v=fb[base+x*2]|(fb[base+x*2+1]<<8)
                px[x,y]=(((v>>11)&0x1f)<<3,((v>>5)&0x3f)<<2,(v&0x1f)<<3)
        o=os.path.splitext(fn)[0]+'_%s.png'%sname; im.save(o); print('   ->',o)
