#!/usr/bin/env python3
# Decode C:\a4telN.dat telemetry. v3: [128B header][560B a4_fps_state][framebuffer];
# v4 (round 10): the header is 256 bytes, words 32.. the GL phase timers of the window.
# Word 49 was dual_frames up to round 12 (0 under A4_IMM); fill_gpu from round 13.
import sys, struct, os
from PIL import Image
NAMES=['magic','ver','capidx','fcount','W','H','stride','fbbytes','bb','up','frames',
       'prims','errc','ntex','bstep','last_prims','winstep','wvis','idle2d','miny','maxy',
       'pminy','pmaxy','fps','min_dt','max_dt','fps_bytes',
       'gl_renders','gl_calls','gl_tris','gl_maxcalls','gl_lastcalls',
       # v4
       'ab_mode','w_renders','w_nticks','w_comp','w_read','w_draw','w_glflush','w_calls','w_tris',
       'ntick_now','tick_now','home_us','rd_fmt','rd_type','rd_err','flat_tris','add_tris','fill_gpu','shad_tris','pb_config','depth_bits','m1_rebakes','ntex_now','w_m1texels','w_m1ticks','stencil_bits','affine_tris','alpha_bits','late_tris','bb_gpu','bb_sw','bb2_calls']
for fn in sys.argv[1:]:
    d=open(fn,'rb').read()
    if len(d)<128: print(fn,'too short'); continue
    ver=struct.unpack('<I',d[4:8])[0]
    hb=256 if ver>=4 else 128
    h=struct.unpack('<%dI'%(hb//4),d[:hb])
    print('\n%s (%d bytes)'%(fn,len(d)))
    for i,n in enumerate(NAMES[:hb//4]): print('  %-10s %d'%(n,h[i]))
    if ver>=4 and h[33]:
        r=h[33]
        print('  window: mode %d (%s%s), %d renders in %d nticks = %.2f nticks/render; per render:'
              ' composite %.2f (read %.2f), batch draw %.2f, glFlush %.2f; %.0f calls, %.0f tris'
              % (h[32], 'flush' if h[32]&1 else 'noflush', '+native' if h[32]&2 else '', r, h[34], h[34]/r,
                 h[35]/r, h[36]/r, h[37]/r, h[38]/r, h[39]/r, h[40]/r))
    if h[27]:
        print('  GL census: %d renders, %.0f draw calls/render, %.0f triangles/render, max %d calls, last %d'
              % (h[27], h[28]/h[27], h[29]/h[27], h[30], h[31]))
    fpsb=h[26] if ver>=3 else 0; fboff=hb+fpsb
    if fpsb>=560:
        fs=struct.unpack('<140I', d[hb:hb+560])
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
