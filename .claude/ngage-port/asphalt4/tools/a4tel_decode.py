#!/usr/bin/env python3
# Decode C:\a4telN.dat telemetry. v3: [128B header][560B a4_fps_state][framebuffer];
# v4 (round 10): the header is 256 bytes, words 32.. the GL phase timers of the window.
# Word 49 was dual_frames up to round 12 (0 under A4_IMM); fill_gpu from round 13.
# Word 38 was w_glflush, 46 rd_err, up to round 13; tex_remade and tex_evicted from round 14.
# Word 43 was home_us up to round 14; surf2_tris from round 15.
# Words 13,14,16,17,18 were ntex,bstep,winstep,wvis,idle2d up to round 15; the window's phase timers from round 16.
# Words 57, 59 were stencil_bits (0), alpha_bits (8) up to round 17; the reorder's ticks and calls before it from round 18.
# Words 44, 45 were rd_fmt, rd_type up to round 17; the window's skipped re-presents and presents made from round 18.
import sys, struct, os
from PIL import Image
NAMES=['magic','ver','capidx','fcount','W','H','stride','fbbytes','bb','up','frames',
       'prims','errc','t_3d','t_hook','last_prims','t_2d','t_pres','t_flush','miny','maxy',
       'pminy','pmaxy','fps','min_dt','max_dt','fps_bytes',
       'gl_renders','gl_calls','gl_tris','gl_maxcalls','gl_lastcalls',
       # v4
       'ab_mode','w_renders','w_nticks','w_comp','w_read','w_draw','tex_remade','w_calls','w_tris',
       'ntick_now','tick_now','surf2_tris','w_skipped','w_presents','tex_evicted','flat_tris','add_tris','fill_gpu','shad_tris','pb_config','depth_bits','m1_rebakes','ntex_now','w_m1texels','w_m1ticks','w_reo_ticks','affine_tris','w_runs0','late_tris','bb_gpu','bb_sw','bb2_calls']
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
              ' composite %.2f (read %.2f), batch draw %.2f, [38]/r %.2f; %.0f calls, %.0f tris'
              % (h[32], 'flush' if h[32]&1 else 'noflush', '+native' if h[32]&2 else '', r, h[34], h[34]/r,
                 h[35]/r, h[36]/r, h[37]/r, h[38]/r, h[39]/r, h[40]/r))
        if h[13] or h[16] or h[17] or h[18]:
            # round 16 phase timers: the window split into 3D pass, composite, 2D, the game's
            # present, and the rest (game logic before the 3D, and between re-presents)
            other = h[34] - h[13] - h[18] - h[16] - h[17]
            print('  phases per render (ms): 3D pass %.1f (our hook %.1f of it), composite %.1f, 2D %.1f,'
                  ' present %.1f, rest (logic) %.1f' % (h[13]/r, h[14]/r, h[18]/r, h[16]/r, h[17]/r, other/r))
        if h[59]:
            m = h[32]
            print('  mode %d: re-presents %s, reorder %s%s | %.0f calls a render before grouping, %.0f made;'
                  ' grouping %.2f ms | presents %.2f a render, %.2f skipped'
                  % (m, 'SKIPPED' if m & 4 else 'kept', 'ON' if m & 8 else 'off', (' INDEXED' if m & 16 else '') + (' LEAN' if m & 32 else '') + (' DEFER' if m & 64 else '') + (' PLD' if m & 128 else '') + (' STRIP24' if m & 256 else '') + (' ATLAS' if m & 512 else '') + (' MESH' if m & 1024 else ''), h[59]/r, h[39]/r, h[57]/r, h[45]/r, h[44]/r))
    if ver>=4 and h[33] and h[32] & 1024:
        r=h[33]
        print('  MESH (mode bit 1024; a mesh build\'s words 38/43/46): %.0f list triangles, %.1f list draw calls, %.2f ms drawing it a render'
              % (h[38]/r, h[46]/r, h[43]/r))
    if h[27]:
        print('  GL census: %d renders, %.0f draw calls/render, %.0f triangles/render, max %d calls, last %d'
              % (h[27], h[28]/h[27], h[29]/h[27], h[30], h[31]))
    fpsb=h[26] if ver>=3 else 0; fboff=hb+fpsb
    if len(d) >= fboff + h[7] + 122*4:
        tl = struct.unpack('<122I', d[fboff+h[7]:fboff+h[7]+488])
        if tl[101]: print('  REOCHECK: %d batches, %d pairs out of order, %d overlapping (must be 0), %d batches lost a triangle'
                          % (tl[101], tl[102], tl[100], tl[103]))
        if tl[111]: print('  DEFERCHECK: %d triangles, %d differing from the lean path (must be 0), first at word %d; %d lost at the batch' % (tl[111], tl[110], tl[112], tl[109]))
        if tl[118]: print('  ATLAS: made %s; %d triangles drawn from it, %d copies placed, %d uploaded after a composite, %d textures refused (full)' % ({1: '1024x1024 each', 3: '1024x512 each'}.get(tl[118], 'FAILED, GL error 0x%x' % tl[118]), tl[115], tl[116], tl[119], tl[117]))
        if tl[114]: print('  STRIPCHECK: %d 16-row strips re-read, %d drawn pixels differing from the 24-row composite (must be 0)' % (tl[114], tl[113]))
        if tl[107]: print('  LEANCHECK: %d triangles, %d differing from the old path (must be 0), first at word %d' % (tl[107], tl[106], tl[108]))
        if tl[105]: print('  BLITCHECK: %d strips, %d drawn pixels differing from the general loop (must be 0)' % (tl[105], tl[104]))
    ao = fboff + h[7] + 122*4
    if len(d) >= ao + 384*4:
        ag = struct.unpack('<384I', d[ao:ao+384*4])
        fw = struct.unpack('<I', d[ao+384*4:ao+385*4])[0] if len(d) >= ao + 385*4 else 0
        ag20 = fw in (0x41473230, 0x41473231, 0x41473232, 0x41473233, 0x41473234, 0x41473235)
        b1, b0 = {0x41473231: ('lean', 'old'), 0x41473232: ('deferred', 'lean')}.get(fw, ('indexed', 'copy'))
        for sg in range(3):
            for m in range(4):
                a = ag[sg*128 + m*32: sg*128 + m*32 + 32]
                if not a[0]: continue
                r = a[0]; hist = a[16:32]; nh = sum(hist)
                over = sum(hist[7:]) if nh else 0          # bins >= 90 ms
                if fw == 0x41473235: lbl = 'mesh list' if m & 2 else 'no mesh list'
                elif fw == 0x41473234: lbl = 'atlas' if m & 2 else 'no atlas'
                elif fw == 0x41473233: lbl = '%s, %s' % ('PLD' if m & 1 else 'no PLD', '24-row strips' if m & 2 else '16-row strips')
                else: lbl = ('%s, %s' % ('skip' if m & 1 else 'kept', b1 if m & 2 else b0)) if ag20 else ('skip' if m & 1 else 'kept')
                print('  AGG seg%d slot %d (%s): %d windows, %d renders, %.1f ms a render | 3D %.1f (hook %.1f) comp %.1f (read %.1f)'
                      ' draw %.1f 2D %.1f pres %.1f grouping %.2f | calls %.0f (before %.0f) tris %.0f | presents %.2f skipped %.2f a render'
                      % (sg, m, lbl, a[15], r, a[1]/r, a[2]/r, a[3]/r, a[4]/r, a[5]/r, a[6]/r, a[7]/r, a[8]/r,
                         a[9]/r, a[10]/r, a[11]/r, a[12]/r, a[13]/r, a[14]/r))
                if nh:
                    lab = ['<60'] + ['%d' % (60 + 5*i) for i in range(14)] + ['130+']
                    print('     render-to-render ms (%d): ' % nh + ' '.join('%s:%d' % (l, c) for l, c in zip(lab, hist) if c)
                          + '  | >=90 ms: %.0f%%' % (100.0 * over / nh))
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
