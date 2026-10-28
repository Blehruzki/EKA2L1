/* a4_gl.c -- MBX stage 6: render the frame's textured 3D on the GPU, RUN-LENGTH
 * batched, no depth buffer.
 *
 * Stage 4 drew each primitive the instant a4_prim.c forwarded it (+0x300B2):
 * bindtex + two pointer sets + glDrawArrays(3), ~490 times a frame. Stage 5
 * sorted the whole frame by texture and used one glDrawElements per texture with
 * a depth buffer to undo the reorder -- but the real MBX renders the depth-sorted
 * scene with wrong occlusion (the road/floor drops out), a divergence the bench's
 * HLE GL never showed.
 *
 * Stage 6 does not reorder anything. It accumulates each triangle (pos x,y +
 * texcoord u,v) in arrival order and, at the 3D->2D boundary, draws it back in
 * that same order -- merging only *consecutive* same-texture triangles into one
 * glDrawArrays. The pixels this produces are byte-for-byte what the per-triangle
 * path produced (same triangles, same order, last-drawn wins), so it cannot
 * occlude wrongly; it just issues ~2.7x fewer GL calls (one per texture *run*,
 * not per triangle). No depth buffer, no sort, nothing for the MBX to get wrong.
 *
 * The composite (readback + sentinel-masked blit) is unchanged; on device it is
 * a zero-copy pixmap and A4_NO_BLIT skips the readback for CPU measurement.
 *
 * Function pointers: a4_gl_iat[] (a4_gl.s), filled by the libGLES_CM import. The
 * depth/eglChooseConfig slots [26..29] are left imported but unused here.
 */
#include <stdint.h>

#define A4_GL_SCRATCH 0x00401000u
#define A4_GL_READBUF 0x00403000u        /* one strip: 320*STRIP*4, ends at 0x408000 */
#define A4_GL_BB      0x00400700u        /* surface base, from a4_cachebb_tramp */
#ifndef STRIP
#define STRIP 16                         /* readback strip height (keeps image .bss small);
                                          * -DSTRIP=n overrides, for the per-call-overhead test */
#endif
#define A4_GL_MAGIC   0x6C900DE6u
#define VP_W 320
#define VP_H 240
#define FB_STRIDE VP_W                   /* 320x240 render surface: stride = width (320) */
#ifndef CLIP_TOP
#define CLIP_TOP 11                      /* top rows are the HUD banner, not the 3D viewport
                                          * (the frame's scene fill is x0 y11 w320 h229) --
                                          * the blit must not touch them or GPU scene pixels
                                          * leak into the position/lap/WANTED strip.
                                          * That was when the composite ran at the frame
                                          * end, over the HUD; it now runs at the frame's
                                          * first GPU triangle, before the HUD, and these
                                          * rows show the sky backdrop in a tunnel (the
                                          * "blue banner"). -DCLIP_TOP=0 composites them. */
#endif
#define NCACHE 256
#define START_AFTER 50000u               /* skip the intro video; reachable on the bench */

/* Batch buffers, immediately after the readback strip (0x408000). Triangles in
 * arrival order: per vertex a position + UVC int32 texcoords (GL_FIXED), 3 verts a
 * triangle; one uint32 GL texture name a triangle (for run detection). MAXT is the
 * BATCH size, not a frame limit: when the batch fills it is drawn and refilled, so a
 * frame may have any number of triangles. (It used to be a frame cap whose overflow
 * triangles were drawn and then erased by the frame-end clear -- the road "popping
 * out" whenever a frame passed 640 triangles; MBX.md.) Sizes must fit under
 * GL_SCRATCH + GL_SCRATCH_SIZE in injector.py (top 0x413000). */
#ifdef A4_HPERSP
/* A4_HPERSP: perspective-correct texturing through the VERTEX w. Positions are
 * homogeneous GL_FIXED (x*W, y*W, 0, W) with W proportional to ZW (vptr[2], the
 * vertex w: it grows with distance, MBX.md "Measured the perspective input"); the GPU
 * divides the position back to the same screen (x,y) and interpolates the plain
 * (U,V) texcoords with 1/W weighting -- what every 3D GPU does natively. It does not
 * depend on the texcoord q being divided per fragment, which A4_PERSP needs (the bench
 * only does that with EKA2L1_TEXPROJ, and the N95's MBX evidently does not). */
#ifndef MAXT
#define MAXT 512
#endif
#define POSC 4u                         /* int32 (GL_FIXED) x*W, y*W, 0, W */
#define POSB 4u
#define UVC 2u
#else
#ifndef MAXT
#define MAXT 640
#endif
#define POSC 2u                         /* int16 (GL_SHORT) x, y */
#define POSB 2u
/* Texcoord components per vertex: 2 (s,t) affine, or 4 (s,t,0,q) projective
 * (A4_PERSP). GL ES 1.1 samples at (s/q, t/q), so interpolating (u/w, v/w, 1/w) and
 * dividing PER FRAGMENT is perspective-correct -- only where the GL does divide per
 * fragment (the bench with EKA2L1_TEXPROJ). q = 1/ZW; ZW (vptr[2]) is the vertex w. */
#ifdef A4_PERSP
#define UVC 4u
#else
#define UVC 2u
#endif
#endif
#define A4_GL_POS   0x00408000u
#define A4_GL_UV    (A4_GL_POS + (3u*MAXT*POSC*POSB))
#define A4_GL_TEXID (A4_GL_UV  + (3u*MAXT*UVC*4u))    /* uint32 * MAXT */

/* EGL */
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PIXMAP_BIT 0x0002            /* probe: does the device offer a pixmap config? */
#define GLES_CONFIG 6u
#ifdef A4_WINDOW
/* The GPU presents through an EGL window surface. The native window must live
 * in the app's OWN window-server session, because libGLES_CM resolves the
 * session internally via CCoeEnv::Static()->WsSession(); a window in a second
 * session is an invalid handle to it (the device's WSERV 6). So we take the
 * app's session and root group from CCoeEnv (cone Static/RootWin) and create
 * our RWindow as a child of the app's root group, in the app's session -- the
 * RWindow object is ours (in scratch), the session and buffer are the app's. */
#define EGL_WINDOW_BIT 0x0004
#define EGL_BUFFER_SIZE 0x3020
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE  0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE   0x3024
#define ECOLOR64K 7                      /* TDisplayMode: the game's 565 surface */
#define A4_WS_OBJ 0x00402800u            /* our RWindow object (MWsClientClass + TRect) */
#define A4_CTRL_OBJ 0x00402840u          /* our crafted CCoeControl (client handle); free
                                          * gap between the window object and the readback
                                          * strip (0x403000). iFlags@+0x1C, iWin@+0x20. */
#endif
/* GL enums */
#define GL_TRIANGLES 0x0004
#define GL_FLAT 0x1D00
#define GL_FIXED 0x140C
#define GL_SHORT 0x1402
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_ALPHA_TEST 0x0BC0
#define GL_GREATER 0x0204
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_VERTEX_ARRAY 0x8074
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_REPLACE 0x1E01
#define FX 0x10000

extern volatile uint32_t *a4_gl_iat_ptr(void);
#ifdef A4_TEL
extern void a4_tel_tick(uint32_t had);   /* a4_tel.c: on-device telemetry/capture */
#endif

struct glr {
    uint32_t up, frames, prims, errc, ntex, seen;
    uint32_t flushed;                 /* GPU world already composited into this frame */
    uint32_t bstep;                   /* furthest EGL/GL bring-up step reached (diag) */
    uint32_t last_prims;              /* prims of the last completed frame (device probe) */
    uint32_t depthcfg;                /* unused in stage 6 (no depth); kept for the probe */
    uint32_t pixmapcap;               /* 1 = device offers a pixmap-capable EGL config */
    uint32_t miny, maxy;              /* screen-row extent the GPU drew this frame (top-down) */
    uint32_t pminy, pmaxy;            /* extent of the PREVIOUS frame -- what the pipelined
                                       * readback composites (it is what the pbuffer holds) */
    uint32_t disp, surf, winstep;     /* A4_WINDOW: display, window surface, how far the window got */
    uint32_t wvis;                    /* A4_WINDOW: our window is visible (hidden while the game is in 2D) */
    uint32_t idle2d;                  /* A4_WINDOW: consecutive present hooks with no 3D primitives */
    uint32_t env, grp, appbuf, appsess; /* A4_WINDOW diag: CCoeEnv, root group, shared RWsBuffer, app session */
    uint32_t appui, istack, stkcount;  /* A4_WINDOW diag: CCoeAppUi, its control stack, #controls on it */
    uint32_t ctrl[8], ctrlwin[8];      /* A4_WINDOW diag: the stack's iControl + each control's iWin (+0x20) */
    uint32_t ctrlh;                    /* A4_WINDOW diag: the CCoeControl* chosen as our window's client handle */
    uint32_t cache_ptr[NCACHE];
    uint32_t cache_id[NCACHE];
    uint32_t cache_tr[NCACHE];         /* 1 = texture is a colorkey (transparent) sprite */
    uint32_t bfill;                    /* triangles in the batch buffers (drawn when MAXT) */
    uint32_t lasthit;                  /* tex_index: cache slot of the previous lookup */
};

/* sentinel the pbuffer is cleared to each frame: pixels the GPU never drew read
 * back as pure magenta, so the blit can leave them to the software layer. */
#define SENTINEL_R 0xFFu
#define SENTINEL_G 0x00u
#define SENTINEL_B 0xFFu

/* small typed views of the IAT */
#define F(i) (a4_gl_iat_ptr()[i])       /* pc-relative getter; no writable globals */
static int   egl_get_display(int d){ return ((int(*)(int))F(0))(d); }
static int   egl_initialize(int d,int*a,int*b){ return ((int(*)(int,int*,int*))F(1))(d,a,b); }
static int   egl_pbuffer(int d,uint32_t c,int*a) __attribute__((unused));   /* unused in -DA4_WINDOW */
static int   egl_pbuffer(int d,uint32_t c,int*a){ return ((int(*)(int,uint32_t,int*))F(2))(d,c,a); }
static int   egl_context(int d,uint32_t c,int s,int*a){ return ((int(*)(int,uint32_t,int,int*))F(3))(d,c,s,a); }
static int   egl_make_current(int d,int r,int w,int c){ return ((int(*)(int,int,int,int))F(4))(d,r,w,c); }
static void  gl_viewport(int x,int y,int w,int h){ ((void(*)(int,int,int,int))F(5))(x,y,w,h); }
static void  gl_clearcolorx(int r,int g,int b,int a){ ((void(*)(int,int,int,int))F(6))(r,g,b,a); }
static void  gl_clear(uint32_t m){ ((void(*)(uint32_t))F(7))(m); }
static void  gl_finish(void) __attribute__((unused));
static void  gl_finish(void){ ((void(*)(void))F(8))(); }   /* unused since the pipelined readback; kept for A/B builds */
static void  gl_readpixels(int,int,int,int,uint32_t,uint32_t,void*) __attribute__((unused));   /* unused in -DA4_NO_BLIT */
static void  gl_readpixels(int x,int y,int w,int h,uint32_t f,uint32_t t,void*p){ ((void(*)(int,int,int,int,uint32_t,uint32_t,void*))F(9))(x,y,w,h,f,t,p); }
static void  gl_gentex(int n,uint32_t*t){ ((void(*)(int,uint32_t*))F(10))(n,t); }
static void  gl_bindtex(uint32_t tg,uint32_t t){ ((void(*)(uint32_t,uint32_t))F(11))(tg,t); }
static void  gl_teximage(uint32_t tg,int l,int ifmt,int w,int h,int b,uint32_t f,uint32_t t,const void*p){ ((void(*)(uint32_t,int,int,int,int,int,uint32_t,uint32_t,const void*))F(12))(tg,l,ifmt,w,h,b,f,t,p); }
static void  gl_texparamx(uint32_t tg,uint32_t p,int v){ ((void(*)(uint32_t,uint32_t,int))F(13))(tg,p,v); }
static void  gl_enable(uint32_t c){ ((void(*)(uint32_t))F(14))(c); }
static void  gl_matrixmode(uint32_t m){ ((void(*)(uint32_t))F(15))(m); }
static void  gl_loadidentity(void){ ((void(*)(void))F(16))(); }
static void  gl_orthox(int l,int r,int b,int t,int n,int f){ ((void(*)(int,int,int,int,int,int))F(17))(l,r,b,t,n,f); }
static void  gl_enableclient(uint32_t a){ ((void(*)(uint32_t))F(18))(a); }
static void  gl_vertexptr(int s,uint32_t t,int st,const void*p){ ((void(*)(int,uint32_t,int,const void*))F(19))(s,t,st,p); }
static void  gl_texcoordptr(int s,uint32_t t,int st,const void*p){ ((void(*)(int,uint32_t,int,const void*))F(20))(s,t,st,p); }
static void  gl_color4x(int r,int g,int b,int a){ ((void(*)(int,int,int,int))F(21))(r,g,b,a); }
static void  gl_drawarrays(uint32_t m,int f,int c){ ((void(*)(uint32_t,int,int))F(22))(m,f,c); }
static void  gl_shademodel(uint32_t m){ ((void(*)(uint32_t))F(23))(m); }
static void  gl_texenvx(uint32_t tg,uint32_t p,int v){ ((void(*)(uint32_t,uint32_t,int))F(24))(tg,p,v); }
static uint32_t gl_geterror(void){ return ((uint32_t(*)(void))F(25))(); }   /* device probe */
static int   egl_choose_config(int d,const int*a,uint32_t*c,int n,int*num){ return ((int(*)(int,const int*,uint32_t*,int,int*))F(26))(d,a,c,n,num); }
static void  gl_alphafuncx(uint32_t func,int ref){ ((void(*)(uint32_t,int))F(33))(func,ref); }   /* A4_TEL colorkey: alpha test */
static void  gl_texsubimage(uint32_t tg,int l,int xo,int yo,int w,int h,uint32_t f,uint32_t t,const void*p){ ((void(*)(uint32_t,int,int,int,int,int,uint32_t,uint32_t,const void*))F(34))(tg,l,xo,yo,w,h,f,t,p); }
#ifdef A4_WINDOW
extern volatile uint32_t *a4_ws_iat_ptr(void);
extern volatile uint32_t *a4_cone_iat_ptr(void);
#define W(i) (a4_ws_iat_ptr()[i])
#define C(i) (a4_cone_iat_ptr()[i])
static void *coe_static(void){ return ((void*(*)(void))C(0))(); }                 /* CCoeEnv::Static() */

static int  ws_flush(void *w){ return ((int(*)(void*))W(2))(w); }
static void gl_disable(uint32_t c){ ((void(*)(uint32_t))F(32))(c); }
static int  rw_construct(void *r, void *parent, uint32_t h) __attribute__((unused));
static int  rw_construct(void *r, void *parent, uint32_t h){ return ((int(*)(void*,void*,uint32_t))W(8))(r, parent, h); }
static int  rw_construct_backup(void *r, void *parent, int dmode, uint32_t h) __attribute__((unused));
static int  rw_construct_backup(void *r, void *parent, int dmode, uint32_t h){ return ((int(*)(void*,void*,int,uint32_t))W(15))(r, parent, dmode, h); }
static void rw_setextent(void *r, const int *pt, const int *sz){ ((void(*)(void*,const int*,const int*))W(9))(r, pt, sz); }
static void rw_activate(void *r){ ((void(*)(void*))W(10))(r); }
static void rw_setvisible(void *r, int v){ ((void(*)(void*,int))W(11))(r, v); }
static int  rw_setdispmode(void *r, int m) __attribute__((unused));
static int  rw_setdispmode(void *r, int m){ return ((int(*)(void*,int))W(12))(r, m); }
static void rw_nontransparent(void *r){ ((void(*)(void*))W(13))(r); }
static int  egl_window_surface(int d, uint32_t c, void *win, const int *a){ return ((int(*)(int,uint32_t,void*,const int*))F(30))(d, c, win, a); }
static int  egl_swap(int d, int sf){ return ((int(*)(int,int))F(31))(d, sf); }
#endif

static int bringup(volatile struct glr *s)
{
    s->bstep = 1;                                                  /* entered bring-up */
    int disp = egl_get_display(0);                 if (!disp) return 0; s->bstep = 2;
    int a=0,b=0; if (!egl_initialize(disp,&a,&b))   return 0;       s->bstep = 3;
    /* PROBE: does this device's GL offer a pixmap-capable config? That decides
     * whether the zero-copy "render straight into the backbuffer" path is possible
     * on the N95 (vs. the current glReadPixels readback). */
    { int pca[] = { EGL_SURFACE_TYPE, EGL_PIXMAP_BIT, EGL_NONE };
      uint32_t pcfg = 0; int pnum = 0;
      if (egl_choose_config(disp, pca, &pcfg, 1, &pnum) && pnum > 0) s->pixmapcap = 1; }
#ifdef A4_WINDOW
    /* The native window must be in the app's own session. Take the session and
     * root group from CCoeEnv; create our RWindow as a child of the root group,
     * sharing the app's RWsBuffer. The window object is ours (scratch), so the
     * app's framework never sees it, but the server and EGL do, in the right
     * session. The app's own window/DSA region is occluded where ours sits -- on
     * the device the runtime's DSA copy then stops there (CLAUDE.md gap: the
     * bench draws the DSA regardless, so the bench cannot show this). */
    /* The native window must live in the app's own window-server session, the
     * one libGLES_CM resolves via CCoeEnv::Static()->WsSession(). Get CCoeEnv
     * (cone Static, 182, resolves), then find iWsSession and iRootWin INSIDE it
     * by content, not by a fixed offset -- the layout differs across S60
     * editions (SYMBIAN.md), and the bench ROM (RM-409 = Nokia 5320, 3.2) is a
     * different edition from the N95 (3.1), so a measured offset would be wrong
     * on the phone. Both members are MWsClientClass {iWsHandle, iBuffer}; the
     * session has its RSessionBase kernel handle at word+2, the group has a
     * window-server handle at word+0 and the same iBuffer. */
    if (C(0) < 0x10000u) { s->winstep = 0xC0; return 0; }
    void *env = coe_static();           s->winstep = 1; if (!env) return 0;
    s->env = (uint32_t)env;
    volatile uint32_t *e = (volatile uint32_t *)env;
    uint32_t appbuf = 0, appsess = 0, grp = 0;
    /* pass 1: the RWsSession -- word[1] a heap pointer, word[2] a kernel handle */
    for (int o = 0; o + 3 <= 0x80; o++) {
        uint32_t b = e[o + 1], hk = e[o + 2];
        if (b >= 0x00080000u && b < 0x10000000u && hk >= 0x40000000u) { appsess = (uint32_t)env + 4 * o; appbuf = b; break; }
    }
    if (!appsess) { s->winstep = 0xC2; return 0; }
    /* pass 2: the RWindowGroup -- a different member whose iBuffer is the same,
     * preceded by a window-server handle (nonzero, not a pointer). */
    for (int o = 0; o + 2 <= 0x80; o++) {
        uint32_t h = e[o], b = e[o + 1];
        if (b == appbuf && h != 0 && h < 0x00080000u && ((uint32_t)env + 4 * o) != appsess) { grp = (uint32_t)env + 4 * o; break; }
    }
    if (!grp) { s->winstep = 0xC3; return 0; }
    s->appbuf = appbuf; s->appsess = appsess; s->grp = grp;   s->winstep = 3;
    /* build our RWindow object: MWsClientClass {iWsHandle=0, iBuffer=appbuf} */
    uint8_t *wsb = (uint8_t *)A4_WS_OBJ; void *win = wsb;
    for (int i = 0; i < 0x20; i++) wsb[i] = 0;
    ((volatile uint32_t *)win)[1] = appbuf;
#ifdef A4_BACKUP
    /* A BACKED-UP window (window_type backed_up). The server maintains its bitmap
     * and redraws it itself, so the client gets NO redraw events -- the app's cone
     * CCoeRedrawer never sees our window, dissolving the client-handle dilemma
     * (cone wanted 0-or-a-real-control, the window server wants non-zero: WSERV 4).
     * EGL still accepts it (same window_kind::client). Construct takes the display
     * mode. The handle here is a deliberate POISON value (unmapped, aligned,
     * non-zero): if a backed-up window truly bypasses cone, nothing dereferences
     * it and the race runs; if cone ever does touch it, the fault address is this
     * value, proving it does not. */
    if (rw_construct_backup(win, (void *)grp, ECOLOR64K, 0x7FFFFFFCu) != 0) return 0;   s->winstep = 4;
#else
    /* A REDRAW window (it presents; the device rejects a null handle at Construct,
     * WSERV 4) with a CRAFTED minimal CCoeControl as the client handle. The app is
     * a DSA title whose cone control stack is empty (proven: iStack iCount=0,
     * iBase=NULL), so there is no real control to borrow. cone's CCoeRedrawer::RunL
     * asserts (handle&3)==0 (CONE 46), then does window=(CCoeControl*)handle; if
     * (window) window->HandleRedrawEvent(rect). HandleRedrawEvent is
     *   Window().BeginRedraw(r); if(IsReadyToDraw()){...Draw()...} Window().EndRedraw();
     * and IsReadyToDraw() is ((iFlags&(EInvisible|EActivated))==EActivated) ||
     * (iFlags&EBackedUpWindow) -- so iFlags==0 makes it FALSE and the whole draw
     * block (ActivateGc, the virtual Draw, DrawComponents) is skipped. What is left
     * is BeginRedraw/EndRedraw on the control's iWin(+0x20): point it at OUR OWN
     * window and the redraw of our window is a no-op that VALIDATES it (no redraw
     * storm) and dereferences nothing else. No virtual is ever dispatched on this
     * control (Draw is skipped; the keypad-only N95 has no pointer events, the sole
     * aDestination-deref path in CCoeAppUi::HandleWsEventL), so the vtable is unused.
     * This is exactly what the earlier crafted control got wrong -- its iFlags/iWin
     * drove it into the Draw block on a redraw-during-orientation. */
    { uint32_t appui = e[0x24 / 4];   s->appui = appui;   /* diagnostic: confirm empty */
      if (appui >= 0x00080000u && appui < 0x10000000u) {
          volatile uint32_t *au = (volatile uint32_t *)appui;
          uint32_t stk = au[0x0C / 4];   s->istack = stk;
          if (stk >= 0x00080000u && stk < 0x10000000u)
              s->stkcount = ((volatile uint32_t *)stk)[0x04 / 4];
      } }
    uint8_t *cb = (uint8_t *)A4_CTRL_OBJ;
    for (int i = 0; i < 0x28; i++) cb[i] = 0;             /* zeroed: vptr 0, iFlags 0 */
    ((volatile uint32_t *)cb)[0x20 / 4] = (uint32_t)win;  /* iWin -> our own window */
    s->ctrlh = (uint32_t)cb;
    if (rw_construct(win, (void *)grp, (uint32_t)cb) != 0) return 0;   s->winstep = 4;
    rw_setdispmode(win, ECOLOR64K);
#endif
    { int pt[2] = { 0, 0 }, sz[2] = { VP_W, VP_H }; rw_setextent(win, pt, sz); }
    rw_nontransparent(win);
    rw_activate(win); rw_setvisible(win, 1);
    ws_flush((void *)appsess);          s->winstep = 5;
    /* Ask for an OPAQUE 565 config explicitly. Requesting only EGL_BUFFER_SIZE 16
     * lets the device pick a 16-bit config WITH alpha (RGBA4444/5551); the N95
     * window server then screen-door (dither) composites our window -- the
     * checkerboard glitch seen on hardware. EGL_ALPHA_SIZE 0 + 5/6/5 forces the
     * native no-alpha surface, so the window composites opaque. (The bench always
     * composited opaque, so it never showed this; keep 565 explicit for parity.) */
    int ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                 EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
                 EGL_ALPHA_SIZE, 0, EGL_BUFFER_SIZE, 16, EGL_NONE };
    uint32_t cfg = 0; int ncfg = 0;
    if (!egl_choose_config(disp, ca, &cfg, 1, &ncfg) || ncfg < 1) return 0; s->winstep = 6;
    int surf = egl_window_surface(disp, cfg, win, 0); if (!surf) return 0; s->bstep = 4; s->winstep = 7;
    int ctx  = egl_context(disp, cfg, 0, 0);          if (!ctx)  return 0; s->bstep = 5;
    s->disp = (uint32_t)disp; s->surf = (uint32_t)surf; s->wvis = 1;
#else
    int pb[] = { EGL_WIDTH, VP_W, EGL_HEIGHT, VP_H, EGL_NONE, 0, 0 };
    int surf = egl_pbuffer(disp, GLES_CONFIG, pb);  if (!surf) return 0; s->bstep = 4;
    int ctx  = egl_context(disp, GLES_CONFIG, 0, 0);if (!ctx)  return 0; s->bstep = 5;
#endif
    if (!egl_make_current(disp, surf, surf, ctx))   return 0;       s->bstep = 6;
    gl_viewport(0,0,VP_W,VP_H);
    gl_enable(GL_TEXTURE_2D);
    gl_shademodel(GL_FLAT);
    gl_texenvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    gl_enable(GL_ALPHA_TEST);                         /* colorkey: discard texel-0 (alpha 0) */
    gl_alphafuncx(GL_GREATER, 0);                     /* keep fragments with alpha > 0 */
    gl_color4x(FX,FX,FX,FX);
    gl_enableclient(GL_VERTEX_ARRAY);
    gl_enableclient(GL_TEXTURE_COORD_ARRAY);
    gl_matrixmode(GL_PROJECTION); gl_loadidentity();
    gl_orthox(0, VP_W*FX, VP_H*FX, 0, -FX, FX);      /* top-left origin, 2D (no depth) */
    gl_matrixmode(GL_MODELVIEW); gl_loadidentity();
    gl_clearcolorx(FX, 0, FX, FX);                   /* sentinel-clear once, so the first pipelined
                                                      * readback (before any draw) is all-sentinel */
    gl_clear(GL_COLOR_BUFFER_BIT);
    s->pminy = VP_H; s->pmaxy = 0;                   /* nothing to read back yet */
    { uint32_t e = gl_geterror(); if (s->errc == 0) s->errc = e; }  /* catch a bring-up GL error */
    return 1;
}

/* texture cache: returns the cache INDEX for desc+0x30, uploading on a miss and
 * flagging whether the texture is a colorkey (transparent) sprite -- the game's
 * software filler treats texel value 0 as transparent, so a texture with many 0
 * texels is a sprite we must leave to software (the GPU would draw its black
 * background opaque). -1 on failure (caller falls through to software). */
#ifdef A4_TEXCHK
/* Diagnosis: is a cached GL texture still the texture the game means? The cache is
 * keyed by the descriptor address only. Per entry keep the pixel pointer and a
 * 16-texel checksum taken at upload (free scratch above the batch buffers, 0x412000),
 * and on every hit count a changed pointer or changed content. Counters at 0x412800:
 * [0] hits [1] pointer changed [3] content changed [4] last stale descriptor [5] uploads. */
#define TC_PX ((volatile uint32_t *)0x00412000u)
#define TC_CK ((volatile uint32_t *)0x00412400u)
#define TC_N  ((volatile uint32_t *)0x00412800u)
static uint32_t tex_ck(const uint32_t *ts)
{
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const uint16_t *px = (const uint16_t *)ts[3];
    if (!px) return 0;
    int lw = 0, lh = 0;
    for (uint32_t v = w; v > 1; v >>= 1) lw++;
    for (uint32_t v = h; v > 1; v >>= 1) lh++;
    int sh = lw + lh - 4; if (sh < 0) sh = 0;
    uint32_t c = (w << 16) ^ h;
    for (uint32_t k = 0; k < 16u; k++) {
        uint32_t idx = k << sh;
        if (idx >= w * h) break;
        c = c * 31u + px[idx];
    }
    return c;
}
#endif

static int tex_index(volatile struct glr *s, uint32_t tsaddr)
{
    uint32_t *ts = (uint32_t *)tsaddr;
#ifndef A4_TEXCHK
    /* consecutive triangles usually share a texture: try the previous slot first
     * (the linear search below runs per triangle over up to NCACHE entries) */
    { uint32_t li = s->lasthit; if (li < s->ntex && s->cache_ptr[li] == tsaddr) return (int)li; }
#endif
    for (uint32_t i = 0; i < s->ntex; i++)
        if (s->cache_ptr[i] == tsaddr) {
            s->lasthit = i;
#ifdef A4_TEXCHK
            TC_N[0]++;
            if (TC_PX[i] != ts[3]) { TC_N[1]++; TC_N[4] = tsaddr; }
            else if (TC_CK[i] != tex_ck(ts)) { TC_N[3]++; TC_N[4] = tsaddr; }
#endif
            return (int)i;
        }
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const void *pix = (const void *)ts[3];
    if (!pix || w > 1024 || h > 1024) return -1;
    if (s->ntex >= NCACHE) return -1;
    /* The game's software filler treats texel value 0 (pure black) as transparent.
     * A texture with ANY such texel, GPU-drawn opaque, shows black where software
     * shows through (the "black background trees"). Handle it ON the GPU: a colorkey
     * texture is uploaded as 5551 with alpha=0 on the zero texels, and the alpha
     * test (enabled in bring-up) discards them -- matching software exactly, and
     * keeping the sprite on the GPU (no slow software fallback). Opaque textures
     * (no zero texels) stay 565; GL_REPLACE on an RGB texture yields alpha 1, so the
     * alpha test passes and they are unaffected. */
    const uint16_t *px16 = (const uint16_t *)pix;
    uint32_t total = w * h, zeros = 0;
    for (uint32_t i = 0; i < total; i++) if (px16[i] == 0) zeros++;
    uint32_t tr = (zeros > 0) ? 1u : 0u;              /* colorkey (has transparent texels) */
    uint32_t id = 0;
    gl_gentex(1, &id);
    gl_bindtex(GL_TEXTURE_2D, id);
    if (tr) {
        /* allocate 5551, then convert+upload in row strips through the readback
         * scratch (free here: composite_prev ran at the frame's first prim). */
        gl_teximage(GL_TEXTURE_2D, 0, GL_RGBA, (int)w, (int)h, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 0);
        uint16_t *conv = (uint16_t *)A4_GL_READBUF;   /* 0x403000..0x408000 = 10240 u16 */
        /* convert+upload in row strips, filling conv until the next row won't fit
         * (no integer divide -- the freestanding blob has no __aeabi_uidiv). */
        uint32_t y0 = 0, nrow = 0;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint16_t t16 = px16[y * w + x];
                conv[nrow * w + x] = (uint16_t)((t16 & 0xF800) | (((t16 >> 6) & 0x1F) << 6)
                                                | ((t16 & 0x1F) << 1) | (t16 ? 1u : 0u));
            }
            nrow++;
            if ((nrow + 1) * w > 10240u || y == h - 1) {   /* strip full, or last row */
                gl_texsubimage(GL_TEXTURE_2D, 0, 0, (int)y0, (int)w, (int)nrow,
                               GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, conv);
                y0 = y + 1; nrow = 0;
            }
        }
    } else {
        gl_teximage(GL_TEXTURE_2D, 0, GL_RGB, (int)w, (int)h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pix);
    }
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    int idx = (int)s->ntex;
    s->cache_ptr[idx] = tsaddr; s->cache_id[idx] = id; s->cache_tr[idx] = tr; s->ntex++;
#ifdef A4_TEXCHK
    TC_PX[idx] = ts[3]; TC_CK[idx] = tex_ck(ts); TC_N[5]++;
#endif
    return idx;
}

/* returns 1 if the primitive was taken by the GPU path (the caller skips the
 * software rasteriser), 0 to fall through. The triangle is accumulated in arrival
 * order and drawn later, in a4_gl_flush; an overflow past MAXT is drawn now. */
static void composite_prev(volatile struct glr *s);
static void batch_draw(uint32_t nt);
#ifdef A4_HPERSP
static int bitlen(uint32_t v) { int n = 0; while (v) { n++; v >>= 1; } return n; }
#endif

#ifdef A4_PERSP
/* Unsigned 32/32 divide by restoring shift-subtract -- the renderer is
 * freestanding (no libgcc), so neither hardware divide nor a compiler __aeabi
 * helper is available; this uses only shifts, compares and subtracts. Used for
 * the per-vertex reciprocal 1/w (2^22 / ZW). ~32 iterations; a few per triangle,
 * well under 2% of the frame. */
static uint32_t a4_udiv(uint32_t num, uint32_t den) __attribute__((unused));
static uint32_t a4_udiv(uint32_t num, uint32_t den)
{
    if (den == 0) return 0xFFFFFFFFu;
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; --i) {
        r = (r << 1) | ((num >> i) & 1u);
        if (r >= den) { r -= den; q |= (1u << i); }
    }
    return q;
}
#endif

int a4_gl_draw_prim(uint32_t *gctx, uint32_t *desc, uint32_t flags)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
#ifdef A4_FLAGHIST
    /* mode census: every draw_primitive by (flags, ctx[0x64] -- the per-object span
     * mode the transparent/opaque spans switch on). Table at 0x412000: [0]=n, then n
     * entries (flags, mode, count), max 40; [121] = calls that found the table full. */
    {
        volatile uint32_t *fh = (volatile uint32_t *)0x00412000u;
        uint32_t md = gctx[0x64 / 4], nn = fh[0], i;
        for (i = 0; i < nn; i++) if (fh[1+i*3] == flags && fh[2+i*3] == md) break;
        if (i == nn) { if (nn < 40u) { fh[1+i*3] = flags; fh[2+i*3] = md; fh[3+i*3] = 0; fh[0] = nn + 1; } else i = 99; }
        if (i < 40u) fh[3+i*3]++; else fh[121]++;
    }
#endif
#ifdef A4_TAKEDEF
    /* Also take the game's DEFAULT rasterizer (0x2e1c8): flags with none of the
     * draw_primitive dispatch bits 0x1 0x2 0x8 0x10 0x20 0x40 0x100 (0x4000, 0x104000,
     * 0x20000, 0x10000 -- ~47% of all triangles in a race). Its span (0x33964) is a
     * colour key -- texel 0 leaves the pixel -- which the 5551 upload + alpha test already
     * reproduce; span mode 1 (ctx[0x64]==1) also adds 1/4 of a per-scanline table colour
     * indexed by a second map, not reproduced (drawn as the plain key). Same descriptor
     * layout as flags 1. Drawn in arrival order with the flags-1 triangles, so the game's
     * painter's order between them is kept. */
    if (iat[0] < 0x10000u || !((flags & 1u) || (flags & 0x17Bu) == 0u)) return 0;
#else
    if (iat[0] < 0x10000u || !(flags & 1)) return 0;
#endif
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (++s->seen < START_AFTER) return 0;           /* past the intro video + menu */
    if (s->up != A4_GL_MAGIC) {
        if (!bringup(s)) return 0;                   /* glr is bss-zeroed */
        s->up = A4_GL_MAGIC;
    }
    if (s->prims == 0) {                                /* first prim of the frame */
        s->flushed = 0;
        s->miny = VP_H; s->maxy = 0;                    /* empty drawn-row extent */
        /* Composite the previous frame's GPU image NOW, before this frame's
         * software draws (the car, the trees, the HUD) land on top of it in
         * painter's order. The old 3D->2D boundary was the frame's first
         * translucent fill, which a race frame does not have (draw-order trace,
         * MBX.md): the flush fell to the frame end and overwrote the car and
         * the bottom HUD -- the device glitch. The pbuffer still holds the
         * frame drawn at the previous frame's end. */
        composite_prev(s);
        /* Clear for THIS frame now, after the readback of the previous one, so a
         * batch drawn mid-frame (when the batch buffers fill) is kept. The clear used
         * to be at the frame end, which erased every triangle past the batch size. */
        gl_clearcolorx(FX, 0, FX, FX);                   /* magenta sentinel (see SENTINEL_*) */
        gl_clear(GL_COLOR_BUFFER_BIT);
        s->bfill = 0;
    }

    uint32_t tsaddr = desc[0x30 / 4];
    if (!tsaddr) return 0;
    uint32_t *ts = (uint32_t *)tsaddr;
    uint32_t tw = ts[1] + 1, th = ts[2] + 1;
    int wsh = 0, hsh = 0;
    for (uint32_t w = tw; w > 1; w >>= 1) wsh++;
    for (uint32_t h = th; h > 1; h >>= 1) hsh++;

    int ti = tex_index(s, tsaddr);
    if (ti < 0) return 0;
    /* colorkey sprites are now GPU-drawn with alpha test (5551 upload), so no
     * software fallback -- s->cache_tr is kept for diagnostics only. */
    uint32_t id = s->cache_id[ti];

    int16_t p[6];
    int32_t uv[3 * UVC];
    const uint32_t voff[3] = { 0x0C, 0x18, 0x24 };
    for (int i = 0; i < 3; i++) {
        uint32_t *vp = (uint32_t *)desc[voff[i] / 4];
        p[i*2]   = (int16_t)(int32_t)vp[0];
        p[i*2+1] = (int16_t)(int32_t)vp[1];
        int32_t un = ((int32_t)desc[voff[i]/4 + 1] * 4096) >> wsh;   /* U_norm * 65536 */
        int32_t vn = ((int32_t)desc[voff[i]/4 + 2] * 4096) >> hsh;   /* V_norm * 65536 */
#if defined(A4_PERSP) && !defined(A4_HPERSP)
        /* q = 1/w (ZW is w: it grows with distance, measured on road triangles --
         * MBX.md "Measured the perspective input"). s = U_norm*q, t = V_norm*q
         * (GL_FIXED); a GL that divides (s/q, t/q) PER FRAGMENT is then
         * perspective-correct. The absolute scale of q cancels in the division, so any
         * fixed multiple of 1/ZW works; 2^22 keeps q and s*q in range for ZW in
         * ~[8, 2^16]. (Round 7 briefly used vp[2] directly as q on a sky/building
         * sample that does not track depth -- wrong; reverted.) */
        int32_t zw = (int32_t)vp[2];
        uint32_t iw = a4_udiv(1u << 22, (uint32_t)(zw > 0 ? zw : 1));
        uv[i*UVC + 0] = (int32_t)(((int64_t)un * (int32_t)iw) >> 16);
        uv[i*UVC + 1] = (int32_t)(((int64_t)vn * (int32_t)iw) >> 16);
        uv[i*UVC + 2] = 0;
        uv[i*UVC + 3] = (int32_t)iw;
#else
        uv[i*UVC + 0] = un;
        uv[i*UVC + 1] = vn;
#endif
    }
    /* grow the frame's drawn-row extent, so the readback covers only the rows
     * the GPU touched (the readback, not the blit, is the composite's cost). */
    for (int i = 0; i < 3; i++) {
        int32_t y = p[i*2+1];
        if (y < 0) y = 0; else if (y >= VP_H) y = VP_H - 1;
        if ((uint32_t)y < s->miny) s->miny = (uint32_t)y;
        if ((uint32_t)y > s->maxy) s->maxy = (uint32_t)y;
    }

#ifdef A4_HPERSP
    /* homogeneous positions (x*W, y*W, 0, W), W proportional to ZW. One shift for the
     * whole triangle -- the perspective weighting depends only on the ratios of W
     * across it -- chosen so |x|*W, |y|*W < 2^26 (GL_FIXED, headroom for a fixed-point
     * vertex pipeline). Bit lengths, not a 64-bit multiply: the blob has no libgcc. */
    int32_t P[12];
    {
        uint32_t zw[3], zmax = 1, m = 1;
        for (int i = 0; i < 3; i++) {
            int32_t z = (int32_t)((uint32_t *)desc[voff[i] / 4])[2];
            zw[i] = (uint32_t)(z > 0 ? z : 1);
            if (zw[i] > zmax) zmax = zw[i];
            uint32_t ax = (uint32_t)(p[i*2]   < 0 ? -p[i*2]   : p[i*2]);
            uint32_t ay = (uint32_t)(p[i*2+1] < 0 ? -p[i*2+1] : p[i*2+1]);
            if (ax > m) m = ax;
            if (ay > m) m = ay;
        }
        int shift = (26 - bitlen(m)) - bitlen(zmax);
        for (int i = 0; i < 3; i++) {
            int32_t W = (int32_t)(shift >= 0 ? (zw[i] << shift) : (zw[i] >> -shift));
            if (W < 1) W = 1;
            P[i*4+0] = (int32_t)p[i*2]   * W;
            P[i*4+1] = (int32_t)p[i*2+1] * W;
            P[i*4+2] = 0;
            P[i*4+3] = W;
        }
    }
#endif
#ifdef A4_VTXDUMP
    /* perspective diagnosis: record each triangle's 3 (screen-y, vp[2]) pairs so we can
     * see offline whether vp[2] grows or shrinks toward the bottom of the screen (z vs
     * 1/z). Table at 0x400B00: [0]=count, then count*6 words (y0,zw0,y1,zw1,y2,zw2).
     * Reset at the frame's first prim (prims==0). Read via MEMSNAP. */
    {
        volatile uint32_t *vt = (volatile uint32_t *)0x00400B00u;
        uint32_t vn2 = vt[0];                /* accumulate across frames: catches ground tris */
        if (vn2 < 80u) {
            for (int i = 0; i < 3; i++) {
                uint32_t *vpd = (uint32_t *)desc[voff[i] / 4];
                vt[1 + vn2*6 + i*2 + 0] = (uint32_t)(int32_t)(int16_t)(int32_t)vpd[1];
                vt[1 + vn2*6 + i*2 + 1] = vpd[2];
            }
            vt[0] = vn2 + 1;
        }
    }
#endif
    if (s->bfill >= MAXT) {                         /* batch full: draw it, then refill */
        batch_draw(s->bfill);
        s->bfill = 0;
    }
    uint32_t n = s->bfill;
    uint32_t vb = n * 3;                            /* first vertex of this triangle */
#ifdef A4_HPERSP
    int32_t  *POS = (int32_t  *)A4_GL_POS;
    for (int k = 0; k < 12; k++) POS[vb*4 + k] = P[k];
#else
    int16_t  *POS = (int16_t  *)A4_GL_POS;
    for (int k = 0; k < 6; k++) POS[vb*2 + k] = p[k];
#endif
    int32_t  *UV  = (int32_t  *)A4_GL_UV;
    uint32_t *TEXID = (uint32_t *)A4_GL_TEXID;
    for (uint32_t k = 0; k < 3u*UVC; k++) UV[vb*UVC + k] = uv[k];
    TEXID[n] = id;
    s->bfill = n + 1;
    s->prims++;                                     /* the frame's total (not capped) */
    return 1;
}

/* Draw the accumulated frame in arrival order, one glDrawArrays per run of
 * consecutive same-texture triangles. Identical pixels to the per-triangle path;
 * just fewer GL calls. No depth, no sort. */
static void batch_draw(uint32_t nt)
{
    if (nt == 0) return;
    if (nt > MAXT) nt = MAXT;
    uint32_t *TEXID = (uint32_t *)A4_GL_TEXID;

#ifdef A4_HPERSP
    gl_vertexptr(4, GL_FIXED, 0, (const void *)A4_GL_POS);
#else
    gl_vertexptr(2, GL_SHORT, 0, (const void *)A4_GL_POS);
#endif
    gl_texcoordptr((int)UVC, GL_FIXED, 0, (const void *)A4_GL_UV);

    uint32_t run_start = 0;
    uint32_t cur = TEXID[0];
    for (uint32_t t = 1; t < nt; t++) {
        if (TEXID[t] != cur) {
            gl_bindtex(GL_TEXTURE_2D, cur);
            gl_drawarrays(GL_TRIANGLES, (int)(run_start*3), (int)((t - run_start)*3));
            run_start = t;
            cur = TEXID[t];
        }
    }
    gl_bindtex(GL_TEXTURE_2D, cur);                 /* final run */
    gl_drawarrays(GL_TRIANGLES, (int)(run_start*3), (int)((nt - run_start)*3));
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    {   /* draw calls this frame (one per same-texture run); folded at flush */
        uint32_t runs = 1; for (uint32_t t = 1; t < nt; t++) if (TEXID[t] != TEXID[t-1]) runs++;
        ((volatile uint32_t *)0x00412000u)[4] += runs;
    }
#endif
}

/* Composite the GPU-rendered world into the game's surface, at the 3D->2D
 * boundary (a4_gl_fill_hook), masked by the magenta sentinel so only pixels the
 * GPU drew replace the software layer. On device this is a zero-copy pixmap. */
/* Read back and blit the GPU frame drawn at the previous frame's end: the
 * previous frame's drawn extent (pminy..pmaxy), sentinel-masked. Called at the
 * first primitive of a frame; the GPU had the present and the game logic to
 * finish it (~2.4M instructions), so the read should not stall on a real GPU. */
static void composite_prev(volatile struct glr *s)
{
#if !defined(A4_NO_BLIT) && !defined(A4_WINDOW)
    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;
    if (bb && s->pminy <= s->pmaxy) {
    uint32_t *rb = (uint32_t *)A4_GL_READBUF;
    volatile uint16_t *fb = (volatile uint16_t *)bb;
    /* strips covering only the rows the previous frame drew. Screen rows are
     * top-down, GL rows bottom-up: screen [miny,maxy] -> GL [H-1-maxy, H-1-miny].
     * y0 steps on STRIP multiples and VP_H is one, so a strip never over-reads. */
    int gl_lo = VP_H - 1 - (int)s->pmaxy; if (gl_lo < 0) gl_lo = 0;
    int gl_hi = VP_H - 1 - (int)s->pminy; if (gl_hi > VP_H - 1) gl_hi = VP_H - 1;
    for (int y0 = (gl_lo / STRIP) * STRIP; y0 <= gl_hi; y0 += STRIP) {
        gl_readpixels(0, y0, VP_W, STRIP, GL_RGBA, GL_UNSIGNED_BYTE, rb);
#ifdef A4_READBACK_ONLY
        continue;                                        /* measurement: readback cost without the blit */
#endif
        for (int sy = 0; sy < STRIP; sy++) {
            int screen_y = VP_H - 1 - (y0 + sy);
            if (screen_y < CLIP_TOP) continue;           /* leave the HUD banner to software */
            const uint32_t *src = rb + sy * VP_W;
            volatile uint16_t *dstrow = fb + screen_y * FB_STRIDE;
            for (int x = 0; x < VP_W; x++) {
                uint32_t px = src[x];
                uint32_t r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
                if (r == SENTINEL_R && g == SENTINEL_G && b == SENTINEL_B) {
#ifdef A4_SENTINEL_VIS
                    /* diagnosis: paint GPU-didn't-draw (software-kept) pixels bright
                     * green, so the retained software region is visible in the capture. */
                    dstrow[x] = (uint16_t)0x07E0;
#endif
                    continue;                           /* GPU drew nothing here; keep software pixel */
                }
                dstrow[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }
    }
#endif /* !A4_NO_BLIT && !A4_WINDOW */
#if defined(A4_NO_BLIT) || defined(A4_WINDOW)
    (void)s;
#endif
}

void a4_gl_flush(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC || s->prims == 0 || s->flushed) return;
    s->flushed = 1;
    s->last_prims = s->prims;                            /* frame's prim count (device probe) */

    /* PIPELINED: this frame's primitives are drawn now, at the frame's end, and
     * read back at the NEXT frame's first primitive (composite_prev), so the
     * readback never waits on the frame just drawn. One pbuffer, no glFinish,
     * one frame of latency for the GPU world against the software layers. */

    /* draw the rest of THIS frame (the clear was at its first primitive, and any
     * full batch was drawn as it filled); it finishes on the GPU during the present
     * and the next frame's logic, and composite_prev reads it back then. */
    batch_draw(s->bfill);
    s->bfill = 0;
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    {   /* 0x412000: [0] frames [1] draw calls [2] triangles [3] max calls in a frame
         * [5] calls in the last frame (a4_tel.c writes these into the capture header) */
        volatile uint32_t *rd = (volatile uint32_t *)0x00412000u;
        rd[0]++; rd[1] += rd[4]; rd[2] += s->prims; if (rd[4] > rd[3]) rd[3] = rd[4]; rd[5] = rd[4]; rd[4] = 0;
    }
#endif
    { uint32_t e = gl_geterror(); if (s->errc == 0) s->errc = e; }  /* catch a draw GL error */

    /* this frame's drawn extent is what composite_prev reads back next frame */
    s->pminy = s->miny; s->pmaxy = s->maxy;
}

/* Formerly the 3D->2D boundary (the frame's first translucent fill). A race
 * frame has no such fill, so the boundary is now the frame's first primitive
 * (composite_prev) and the frame end (a4_gl_frame). Kept for patch/fill.c. */
void a4_gl_fill_hook(void *fill_pixels)
{
    (void)fill_pixels;
}

/* GL bring-up state, for the on-screen diagnostic in a measurement build:
 *   0 = libGLES_CM did not link; 1 = linked, bring-up never entered;
 *   2..6 = bring-up reached this EGL step; 7 = up and running. */
int a4_gl_state(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return 1 + (int)s->bstep;
}

/* Device probe accessors (read by a4_fps.c's -DA4_GAUGE gauge). */
int a4_gl_prims(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->last_prims;
}
int a4_gl_errc(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->errc;
}
int a4_gl_depthcfg(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->depthcfg;
}
int a4_gl_pixmapcap(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return (int)s->pixmapcap;
}

/* A4_WINDOW diagnostic: how far window bring-up got. 0 = not entered; 7 = the
 * EGL window surface is up; 0xC0..0xC3 = a bring-up step bailed (cone Static
 * missing / no session found / no group found); see bringup(). */
int a4_gl_winstep(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC && s->winstep == 0) return 0;
    return (int)s->winstep;
}

#ifdef A4_WINDOW
/* The fps bar as GL: untextured quads along the top rows of the window, the
 * same scale as the software bar (4 px per fps, red tick at 30, white at 15
 * and 45), because the software buffer is not presented while the GPU is. */
static void bar_quad(int x0, int y0, int x1, int y1, int r, int g, int b)
{
    int16_t q[12] = { (int16_t)x0,(int16_t)y0, (int16_t)x1,(int16_t)y0, (int16_t)x1,(int16_t)y1,
                      (int16_t)x0,(int16_t)y0, (int16_t)x1,(int16_t)y1, (int16_t)x0,(int16_t)y1 };
    gl_color4x(r, g, b, FX);
    gl_vertexptr(2, GL_SHORT, 0, q);
    gl_drawarrays(GL_TRIANGLES, 0, 6);
}
static void bar_draw(uint32_t fps)
{
    if (fps > 60u) fps = 60u;
    gl_disable(GL_TEXTURE_2D);
    bar_quad(0, 0, 240, 8, FX/4, FX/4, FX/4);               /* 0..60 reference, dim */
    bar_quad(0, 0, (int)fps * 4, 8, 0, FX, 0);              /* length = fps, green */
    bar_quad(120, 0, 122, 8, FX, 0, 0);                     /* 30 */
    bar_quad(60, 0, 61, 8, FX, FX, FX);                     /* 15 */
    bar_quad(180, 0, 181, 8, FX, FX, FX);                   /* 45 */
    gl_color4x(FX, FX, FX, FX);
    gl_enable(GL_TEXTURE_2D);
}
#endif

/* Present detour (once per frame): draw the frame's captured primitives on the
 * GPU, then start the next frame. Returns 1 when the GPU presented the frame
 * itself (A4_WINDOW), so the detour skips the game's own present -- the 565 ->
 * 32-bit convert into the hardware bitmap that would paint over the window. */
int a4_gl_frame(uint32_t fps)
{
    (void)fps;
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC) return 0;
    uint32_t had = s->prims;
#ifdef A4_PRESENTDIAG
    /* choppy/wobble diagnosis: count presents that carried new geometry vs presents
     * with none (a re-present of the same world). depthcfg/pixmapcap are unused in
     * the blit build; read them back via MEMSNAP (g[9]=empty, g[10]=real). */
    if (had) s->pixmapcap++; else s->depthcfg++;
#endif
#ifdef A4_LRCAP
    /* histogram the DoPresent caller (stashed at 0x400840 by a4_fps_tramp) split by
     * whether 3D geometry was drawn this frame (had). Table at 0x400A00:
     * [0]=n, then n entries of (caller, real_count, empty_count). MEMSNAP reads it. */
    {
        uint32_t caller = *(volatile uint32_t *)0x00400840u;
        volatile uint32_t *tab = (volatile uint32_t *)0x00400A00u;
        uint32_t n = tab[0], i;
        for (i = 0; i < n; i++) if (tab[1 + i*3] == caller) break;
        if (i == n && n < 40u) { tab[1 + i*3] = caller; tab[1 + i*3 + 1] = 0; tab[1 + i*3 + 2] = 0; tab[0] = n + 1; }
        if (i < 40u) { if (had) tab[1 + i*3 + 1]++; else tab[1 + i*3 + 2]++; }
    }
#endif
    a4_gl_flush();                                      /* draw this frame on the GPU */
    int presented = 0;
#ifdef A4_WINDOW
    if (s->surf) {
        void *appsess = (void *)s->appsess, *win = (void *)A4_WS_OBJ;
        /* The present hook fires more than once a game frame (the runtime's
         * cadence, ~1.5x in the profile), so a hook with no primitives is not a
         * 2D frame; only a run of them is (a menu). While our window is up the
         * game's present is skipped every time, or a software frame reaches
         * the panel between two of ours. */
        if (had) {                                      /* a 3D frame: the GPU presents */
            s->idle2d = 0;
            if (!s->wvis) { rw_setvisible(win, 1); ws_flush(appsess); s->wvis = 1; }
            bar_draw(fps);
            egl_swap((int)s->disp, (int)s->surf);
        } else if (++s->idle2d > 30u && s->wvis) {       /* 2D for a while (menu): the game presents */
            rw_setvisible(win, 0); ws_flush(appsess); s->wvis = 0;
        }
        presented = (int)s->wvis;
    }
#else
    (void)had;
#endif
    s->prims = 0;
    s->flushed = 0;
    s->frames++;
#ifdef A4_TEL
    a4_tel_tick(had);   /* on-device capture: C:\a4telN.dat; had = 3D prims this present */
#endif
    return presented;
}
