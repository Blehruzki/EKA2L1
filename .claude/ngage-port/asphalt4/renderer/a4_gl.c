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
 * Round 10: stage 5's phone failure was never diagnosed (whether config 6 even has a
 * depth buffer was not known), and depth is in the game itself: its perspective span
 * (0x333f4, the flags-1 class) DOES depth-test, against a per-frame 1/z buffer; its
 * other spans do not touch it. A4_ZBUF reproduces exactly that: depth = min(ZK/ZW, 1)
 * on the flags-1 triangles, GL_GEQUAL, cleared to 0 each frame, every other class
 * drawn untested in arrival order. Every class a race uses is now taken (KIND_*), so
 * nothing software lands on top of the GPU image out of order.
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
/* The texel pointer in a texture descriptor: +0x10, where every span filler reads it
 * (0x333f4, 0x33964, 0x33d18, 0x33ea8). +0x0C, used until round 10, equals it on every
 * texture the earlier classes draw (A4_TEXPROBE: 144 of 144) but is 0 on the 0x33ea8
 * (alpha-map) textures, which then fell to software. */
#define TS_PIX 4
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
#define GL_BLEND 0x0BE2
#define GL_ONE 1
#define GL_IMPLEMENTATION_COLOR_READ_TYPE_OES   0x8B9A
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES 0x8B9B
#define FX 0x10000

/* What a batched triangle is drawn as: the top nibble of its TEXID word (the low
 * bits are the GL texture name, or for a flat triangle its RGB565 colour). A run is
 * a stretch of equal words, so a kind change also starts a new glDrawArrays. */
#define KIND_TEX  0u     /* textured, colour key (flags 1 and the default rasterizer) */
#define KIND_FLAT 1u     /* flat colour, opaque (flags 2: 0x2fbdc -> span 0x33b46) */
#define KIND_ADD  2u     /* textured, saturating add (flags 8: 0x2f358 -> span 0x33d18) */
#define KIND_SHAD 3u     /* untextured darken, dst*12/16 (flags 0x20: 0x2ecb4 -> 0x33f8c) */
#define KIND_TEXZ 4u     /* KIND_TEX, depth-tested and depth-writing (flags 1 under A4_ZBUF) */
#define KIND_ALPHA 5u    /* textured, per-texel 4-bit alpha blend (flags 0x100: 0x2e56c -> 0x33ea8) */
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DEPTH_TEST 0x0B71
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_GEQUAL 0x0206
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_BITS 0x0D57
#define GL_ALPHA_BITS 0x0D55
#define GL_BGRA 0x80E1                   /* IMG/EXT read_format_bgra: the N95's own read pair */
#define EGL_ALPHA_SIZE_Z 0x3021
#define GL_STENCIL_TEST 0x0B90
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_EQUAL 0x0202
#define GL_KEEP 0x1E00
#define GL_INCR 0x1E02
#define EGL_STENCIL_SIZE 0x3026
#define EGL_PBUFFER_BIT 0x0001
#define EGL_DEPTH_SIZE 0x3025
#ifndef EGL_RED_SIZE                     /* (A4_WINDOW defines these too) */
#define EGL_BLUE_SIZE  0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE   0x3024
#endif
#ifndef ZK
#define ZK 128           /* depth = min(ZK/ZW, 1); near road vertices sit at ZW ~200 */
#endif
#define GL_ZERO 0
#define GL_SRC_ALPHA 0x0302

/* GL phase timers and the per-window A/B mode (round 10). a4_tel.c writes them into
 * each capture's header and starts the next window; ticks are User::NTickCount. */
#define A4_PERF ((volatile uint32_t *)0x00412C00u)
enum { PF_MODE, PF_RENDERS, PF_T0, PF_COMP, PF_READ, PF_DRAW, PF_GLFLUSH,
       PF_CALLS, PF_TRIS, PF_FLAT, PF_ADD, PF_DUAL, PF_SHAD, PF_M1BAKE, PF_M1TEX, PF_M1T, PF_AFFINE, PF_LATE, PF_N };
#define AB_FLUSH 1u      /* glFlush after the frame's last batch (start the tile render) */
#define AB_NATIVE 2u     /* read back in the implementation's own pair (OES_read_format): BGRA
                          * on the N95 (round 10), RGB565 on a device that names that */
#ifndef A4_ABMODE
#define A4_ABMODE 0u     /* the mode when nothing cycles it (no A4_ABCYCLE) */
#endif
extern uint32_t a4_ntick(void);         /* a4_gl.s: User::NTickCount's fast exec */

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
    uint32_t dual;                     /* the first GPU frame: software draws it too (no
                                        * previous GPU image to composite yet) */
    uint32_t rdfmt, rdtype, rderr;     /* OES_read_format probe: the implementation's read
                                        * format/type, and the GL error the probe left */
    uint32_t fmode;                    /* the A/B mode this frame runs in (AB_*) */
    uint32_t zcfg, zbits;              /* A4_ZBUF: the pbuffer config chosen, its depth bits */
    uint32_t sbits;                    /* its stencil bits (0: shadows darken overlaps twice) */
    uint32_t abits;                    /* its alpha bits: >0 -> dst alpha is the drawn-mask (A4_IMM) */
    uint32_t cache_m1r[NCACHE];        /* mode-1 entry: the [lo, hi] of map values it uses */
    uint32_t cache_ck[NCACHE];         /* mode-1 entry: m1_sig of the table at its last bake */
    uint32_t cache_fr[NCACHE];         /* mode-1 entry: the present it was last checked in */
    uint32_t cache_tab[NCACHE];        /* mode-1 entry: the table it was baked from (part of the key) */
};

_Static_assert(sizeof(struct glr) <= A4_GL_READBUF - A4_GL_SCRATCH, "glr overlaps the readback strip");

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
static void  gl_disable(uint32_t c){ ((void(*)(uint32_t))F(32))(c); }
static void  gl_blendfunc(uint32_t sf,uint32_t df){ ((void(*)(uint32_t,uint32_t))F(35))(sf,df); }
static void  gl_flush(void){ ((void(*)(void))F(36))(); }
static void  gl_getintegerv(uint32_t pn,int*v){ ((void(*)(uint32_t,int*))F(37))(pn,v); }
static void  gl_stencilfunc(uint32_t f,int r,uint32_t m){ ((void(*)(uint32_t,int,uint32_t))F(38))(f,r,m); }
static void  gl_stencilop(uint32_t a,uint32_t b,uint32_t c){ ((void(*)(uint32_t,uint32_t,uint32_t))F(39))(a,b,c); }
static void  gl_clearstencil(int v){ ((void(*)(int))F(40))(v); }
static void  gl_colormask(uint32_t r,uint32_t g,uint32_t b,uint32_t a) __attribute__((unused));
static void  gl_colormask(uint32_t r,uint32_t g,uint32_t b,uint32_t a){ ((void(*)(uint32_t,uint32_t,uint32_t,uint32_t))F(41))(r,g,b,a); }
static void  gl_depthfunc(uint32_t f) __attribute__((unused));
static void  gl_depthfunc(uint32_t f){ ((void(*)(uint32_t))F(28))(f); }
static void  gl_cleardepthx(int d) __attribute__((unused));
static void  gl_cleardepthx(int d){ ((void(*)(int))F(29))(d); }
static void  gl_texsubimage(uint32_t tg,int l,int xo,int yo,int w,int h,uint32_t f,uint32_t t,const void*p){ ((void(*)(uint32_t,int,int,int,int,int,uint32_t,uint32_t,const void*))F(34))(tg,l,xo,yo,w,h,f,t,p); }
#ifdef A4_WINDOW
extern volatile uint32_t *a4_ws_iat_ptr(void);
extern volatile uint32_t *a4_cone_iat_ptr(void);
#define W(i) (a4_ws_iat_ptr()[i])
#define C(i) (a4_cone_iat_ptr()[i])
static void *coe_static(void){ return ((void*(*)(void))C(0))(); }                 /* CCoeEnv::Static() */

static int  ws_flush(void *w){ return ((int(*)(void*))W(2))(w); }
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

/* what the frame's clear covers: colour, and depth/stencil when the pbuffer has them */
static uint32_t clear_bits(volatile struct glr *s)
{
    return GL_COLOR_BUFFER_BIT | (s->zbits ? GL_DEPTH_BUFFER_BIT : 0u) | (s->sbits ? GL_STENCIL_BUFFER_BIT : 0u);
}

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
    uint32_t pcfg = GLES_CONFIG;
#ifdef A4_ZBUF
    /* A pbuffer config WITH a depth buffer: the game depth-tests its perspective class
     * (span 0x333f4 keeps a per-frame 1/z buffer). Config 6, used until now, is a fixed
     * index whose depth on the N95 was never known -- stage 5's depth build lost the road
     * on the phone and was abandoned without that being checked. Falls back to config 6;
     * glGetIntegerv(GL_DEPTH_BITS) below then says whether depth is really there. */
    /* Depth always; then, best first, alpha (the drawn-mask, A4_IMM) and stencil (shadows
     * darken once, KIND_SHAD): depth+alpha+stencil, depth+alpha, depth alone. The N95 has
     * no stencil config (round 10); whether it has alpha is what zcfg/abits will say. */
    { int za[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6,
                   EGL_BLUE_SIZE, 5, EGL_DEPTH_SIZE, 16, EGL_ALPHA_SIZE_Z, 1, EGL_STENCIL_SIZE, 1,
                   EGL_NONE };
      uint32_t c = 0; int n = 0;
      if (egl_choose_config(disp, za, &c, 1, &n) && n > 0) pcfg = c;
      else {
          za[12] = EGL_NONE;                        /* drop the stencil request */
          if (egl_choose_config(disp, za, &c, 1, &n) && n > 0) pcfg = c;
          else {
              za[10] = EGL_NONE;                    /* and the alpha request */
              if (egl_choose_config(disp, za, &c, 1, &n) && n > 0) pcfg = c;
          }
      } }
#endif
    s->zcfg = pcfg;
    int surf = egl_pbuffer(disp, pcfg, pb);  if (!surf) return 0; s->bstep = 4;
    int ctx  = egl_context(disp, pcfg, 0, 0);if (!ctx)  return 0; s->bstep = 5;
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
    /* The read format the implementation prefers besides RGBA/UNSIGNED_BYTE (GL ES 1.1,
     * OES_read_format). The composite reads 565 only where this says RGB/565 (AB_565). Its
     * own error is kept apart, so an implementation without the query cannot set errc. */
    { int f = 0, t = 0;
      gl_getintegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES, &f);
      gl_getintegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE_OES, &t);
      s->rdfmt = (uint32_t)f; s->rdtype = (uint32_t)t; s->rderr = gl_geterror(); }
#ifdef A4_ZBUF
    /* The game's test passes when the pixel's 1/z is >= the stored one, the buffer zeroed
     * each frame (0x31c70 -> 0x2def4): GL_GEQUAL on depth = min(ZK/ZW, 1), cleared to 0.
     * No depth bits -> zbits 0 -> the flags-1 triangles draw untested, as before. */
    { int zb = 0, sb = 0, ab = 0; gl_getintegerv(GL_DEPTH_BITS, &zb); gl_getintegerv(GL_STENCIL_BITS, &sb);
      gl_getintegerv(GL_ALPHA_BITS, &ab);
      s->zbits = (uint32_t)zb; s->sbits = (uint32_t)sb;
#ifdef A4_IMM
      s->abits = (uint32_t)ab;
#endif
      gl_cleardepthx(0); gl_depthfunc(GL_GEQUAL);
      if (sb > 0) { gl_clearstencil(0); gl_stencilfunc(GL_EQUAL, 0, 0xFFu); gl_stencilop(GL_KEEP, GL_KEEP, GL_INCR); }
      gl_clear(clear_bits(s));
      { uint32_t e = gl_geterror(); if (s->errc == 0) s->errc = e; } }
#endif
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
    const uint16_t *px = (const uint16_t *)ts[TS_PIX];
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


/* Mode 1 of the default span (0x33964 with ctx[0x64] == 1: the 0x104000 class, ~15% of
 * calls). Per texel: T = tex[i] (0 = transparent); M = map2[i] (ctx[0x5c], the texture's
 * own layout); if M, the texel is T plus a quarter of table[M] (ctx[0x60], 16-bit colours),
 * summed with the span's own carry trick (0x33a3c). A function of texture space alone, so
 * it is baked into the GL texture. A black base whose lit parts come from the table is why
 * the speed-camera signs drew as black boxes (round 10); the table can change between
 * frames (a speed display), so each present the table entries the texture uses are hashed
 * and a change re-bakes it. */
#ifndef M1_BIG
#define M1_BIG 16384u            /* texels: a mode-1 texture this big re-bakes at most ... */
#endif
#ifndef M1_BIG_EVERY
#define M1_BIG_EVERY 8u          /* ... every this many presents (~3 a second in a race) */
#endif
static inline uint16_t m1_texel(uint32_t T, uint32_t M, const uint16_t *tab)
{
    if (!M) return (uint16_t)T;
    uint32_t q = tab[M];
    q = (((q & 0xF7DEu) >> 1) & 0xF7DEu) >> 1;
    uint32_t sum = T + q;
    uint32_t cy = (T ^ sum ^ q) & 0x10820u;
    return (uint16_t)((cy - (cy >> 4)) | sum);
}

/* the table the span would use: ctx[0x60], moved on 80 bytes a pixel of width when the
 * frame is not wider than tall (0x339ec; the 320x240 race frame is not moved) */
static const uint16_t *m1_table(const uint32_t *gctx)
{
    const uint32_t *base = (const uint32_t *)((uintptr_t)gctx + 0x40100u);
    uint32_t fw = *(const uint32_t *)base[0x10 / 4], fh = *(const uint32_t *)base[0x14 / 4];
    uintptr_t t = gctx[0x60 / 4];
    if (fw <= fh) t += fw * 80u;
    return (const uint16_t *)t;
}

/* Hash of what a mode-1 bake read from the table, plus the map's address: every table[M]
 * for M in [lo, hi] when that range is small, else 64 texels' worth. The table's own
 * address is left out -- contents decide, so a table that moves buffer unchanged does not
 * re-bake. */
static uint32_t m1_sig(const uint16_t *map2, const uint16_t *tab, uint32_t lohi, uint32_t n)
{
    uint32_t c = 0x811C9DC5u ^ (uint32_t)(uintptr_t)map2;
    uint32_t lo = lohi & 0xFFFFu, hi = lohi >> 16;
    if (lo > hi) return c;                          /* no M at all: nothing to track */
    if (hi - lo <= 1024u) {
        for (uint32_t m = lo; m <= hi; m++) c = (c ^ tab[m]) * 16777619u;
    } else {
        uint32_t step = n >> 6; if (!step) step = 1;
        for (uint32_t k = 0; k < n; k += step) c = (c ^ tab[map2[k]]) * 16777619u;
    }
    return c;
}

/* Upload texture ts into GL texture id (bound here). tr: 5551 with alpha 0 on the zero
 * texels (else 565); alloc: (re)specify the level first. map2/tab non-null: the mode-1
 * bake. Converted in row strips through the readback scratch (free here: composite_prev
 * ran at the frame's first triangle). Returns the [lo, hi] range of M seen (lo > hi when
 * none). No integer divide: the freestanding blob has no __aeabi_uidiv. */
static uint32_t tex_upload(uint32_t id, const uint32_t *ts, uint32_t tr, int alloc,
                           const uint16_t *map2, const uint16_t *tab)
{
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const uint16_t *px16 = (const uint16_t *)ts[TS_PIX];
    uint32_t lo = 0xFFFFu, hi = 0;
    gl_bindtex(GL_TEXTURE_2D, id);
    uint32_t fmt = tr ? GL_RGBA : GL_RGB, typ = tr ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_5_6_5;
    if (!tr && !map2) {                             /* plain opaque: straight from the game */
        gl_teximage(GL_TEXTURE_2D, 0, (int)fmt, (int)w, (int)h, 0, fmt, typ, px16);
        return 1;                                   /* lo > hi */
    }
    if (alloc) gl_teximage(GL_TEXTURE_2D, 0, (int)fmt, (int)w, (int)h, 0, fmt, typ, 0);
    uint16_t *conv = (uint16_t *)A4_GL_READBUF;   /* 0x403000..0x408000 = 10240 u16 */
    uint32_t y0 = 0, nrow = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = y * w + x, t16 = px16[i];
            if (map2 && t16) {
                uint32_t M = map2[i];
                if (M) { if (M < lo) lo = M; if (M > hi) hi = M; }
                t16 = m1_texel(t16, M, tab);
            }
            conv[nrow * w + x] = tr ? (uint16_t)((t16 & 0xF800) | (((t16 >> 6) & 0x1F) << 6)
                                                 | ((t16 & 0x1F) << 1) | (px16[i] ? 1u : 0u))
                                    : (uint16_t)t16;
        }
        nrow++;
        if ((nrow + 1) * w > 10240u || y == h - 1) {  /* strip full, or last row */
            gl_texsubimage(GL_TEXTURE_2D, 0, 0, (int)y0, (int)w, (int)nrow, fmt, typ, conv);
            y0 = y + 1; nrow = 0;
        }
    }
    return lo | (hi << 16);
}

/* The 0x33ea8 texture: RGBA8888, colour from the 565 texel, alpha (a>>4)*16 from the byte
 * map at desc+0x2c -- the span's a/16 (to 1/255). Row strips through the readback scratch
 * as tex_upload; returns 1 (no mode-1 range). */
static uint32_t tex_upload_alpha(uint32_t id, const uint32_t *ts)
{
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
#ifdef A4_ALPHAHIST
    {   /* diagnosis: uploads [16], and the first descriptor's 16 words at [17..32] */
        volatile uint32_t *ah = (volatile uint32_t *)0x00412D00u;
        if (ah[16]++ == 0) for (int k = 0; k < 16; k++) ah[17 + k] = ts[k];
    }
#endif
    const uint16_t *px16 = (const uint16_t *)ts[TS_PIX];
    const uint8_t *am = (const uint8_t *)ts[11];
    gl_bindtex(GL_TEXTURE_2D, id);
    gl_teximage(GL_TEXTURE_2D, 0, GL_RGBA, (int)w, (int)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    uint32_t *conv = (uint32_t *)A4_GL_READBUF;   /* 5120 RGBA texels */
    uint32_t y0 = 0, nrow = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = y * w + x, t = px16[i];
            uint32_t r = ((t >> 11) & 31u) << 3, g = ((t >> 5) & 63u) << 2, b = (t & 31u) << 3;
            uint32_t a = am ? (uint32_t)(am[i] >> 4) << 4 : 0xFFu;
#ifdef A4_ALPHAHIST
            /* diagnosis: the alpha nibbles of 0x33ea8 textures, at 0x412D00 [0..15] */
            if (am) ((volatile uint32_t *)0x00412D00u)[am[i] >> 4]++;
#endif
            conv[nrow * w + x] = r | (g << 8) | (b << 16) | (a << 24);
        }
        nrow++;
        if ((nrow + 1) * w > 5120u || y == h - 1) {
            gl_texsubimage(GL_TEXTURE_2D, 0, 0, (int)y0, (int)w, (int)nrow, GL_RGBA, GL_UNSIGNED_BYTE, conv);
            y0 = y + 1; nrow = 0;
        }
    }
    return 1;
}

/* texture cache: the cache INDEX for desc+0x30 (and, for a mode-1 triangle, its map and
 * table), uploading on a miss. A mode-1 entry is keyed desc|1 (descriptors are word
 * aligned), so a texture drawn both ways has two GL textures. -1 on failure (the caller
 * falls through to software). */
static int tex_index(volatile struct glr *s, uint32_t tsaddr, const uint32_t *gctx, int var)
{
    uint32_t *ts = (uint32_t *)tsaddr;
    int m1 = var == 1;                              /* 1: mode-1 bake; 2: the alpha-map texture */
    uint32_t key = tsaddr | (uint32_t)var;
    /* a mode-1 entry is also keyed by its table: objects sharing a texture can each bring
     * their own (ctx[0x60]) -- the tunnel-exit gantry's signs drew black with one image a
     * texture (round 11) */
    uint32_t tabk = m1 ? (uint32_t)(uintptr_t)m1_table(gctx) : 0u;
    int i = -1;
#ifndef A4_TEXCHK
    /* consecutive triangles usually share a texture: try the previous slot first
     * (the linear search below runs per triangle over up to NCACHE entries) */
    { uint32_t li = s->lasthit; if (li < s->ntex && s->cache_ptr[li] == key && s->cache_tab[li] == tabk) i = (int)li; }
#endif
    for (uint32_t k = 0; i < 0 && k < s->ntex; k++)
        if (s->cache_ptr[k] == key && s->cache_tab[k] == tabk) {
            s->lasthit = k; i = (int)k;
#ifdef A4_TEXCHK
            TC_N[0]++;
            if (TC_PX[k] != ts[TS_PIX]) { TC_N[1]++; TC_N[4] = tsaddr; }
            else if (TC_CK[k] != tex_ck(ts)) { TC_N[3]++; TC_N[4] = tsaddr; }
#endif
        }
    if (i >= 0) {
        uint32_t n = (ts[1] + 1) * (ts[2] + 1);
        /* once a present: has the table moved? A big texture at most every M1_BIG_EVERY
         * presents: the one that re-bakes all the time is the player's car body (256x256,
         * its paint reflection, a table the game rewrites every frame), and a re-bake is
         * the whole texture through the CPU and glTexSubImage2D. */
        if (m1 && s->cache_fr[i] != s->frames && (n < M1_BIG || s->frames - s->cache_fr[i] >= M1_BIG_EVERY)) {
            s->cache_fr[i] = s->frames;
            const uint16_t *map2 = (const uint16_t *)gctx[0x5c / 4], *tab = m1_table(gctx);
            if (m1_sig(map2, tab, s->cache_m1r[i], n) != s->cache_ck[i]) {
                uint32_t t0 = a4_ntick();
                s->cache_m1r[i] = tex_upload(s->cache_id[i], ts, s->cache_tr[i], 0, map2, tab);
                s->cache_ck[i] = m1_sig(map2, tab, s->cache_m1r[i], n);
                A4_PERF[PF_M1BAKE]++; A4_PERF[PF_M1TEX] += n; A4_PERF[PF_M1T] += a4_ntick() - t0;
#ifdef A4_M1PROBE
                {   /* diagnosis: a ring of the last 8 re-bakes at 0x412E00: [0] count, then
                     * (desc, w | h<<16, lo | hi<<16, table entries that differ from the
                     * previous re-bake's copy at 0x412F00, up to 64 entries) */
                    volatile uint32_t *mp = (volatile uint32_t *)0x00412E00u;
                    volatile uint16_t *cp = (volatile uint16_t *)0x00412F00u;
                    uint32_t k = mp[0] & 7u, lo = s->cache_m1r[i] & 0xFFFFu, hi = s->cache_m1r[i] >> 16, dif = 0;
                    for (uint32_t m = lo; m <= hi && m - lo < 64u; m++) { if (cp[m - lo] != tab[m]) dif++; cp[m - lo] = tab[m]; }
                    mp[1+k*4] = tsaddr; mp[2+k*4] = (ts[1] + 1) | ((ts[2] + 1) << 16);
                    mp[3+k*4] = s->cache_m1r[i]; mp[4+k*4] = dif; mp[0]++;
                }
#endif
            }
        }
        return i;
    }
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const void *pix = (const void *)ts[TS_PIX];
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
    for (uint32_t k = 0; k < total; k++) if (px16[k] == 0) zeros++;
    uint32_t tr = (zeros > 0) ? 1u : 0u;              /* colorkey (has transparent texels) */
    uint32_t id = 0;
    gl_gentex(1, &id);
    const uint16_t *map2 = 0, *tab = 0;
    if (m1) { map2 = (const uint16_t *)gctx[0x5c / 4]; tab = m1_table(gctx); }
    uint32_t r = var == 2 ? tex_upload_alpha(id, ts) : tex_upload(id, ts, tr, 1, map2, tab);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    int idx = (int)s->ntex;
    s->cache_ptr[idx] = key; s->cache_id[idx] = id; s->cache_tr[idx] = tr; s->ntex++;
    s->cache_m1r[idx] = r; s->cache_fr[idx] = s->frames; s->cache_tab[idx] = tabk;
    s->cache_ck[idx] = m1 ? m1_sig(map2, tab, r, total) : 0;
#ifdef A4_TEXCHK
    TC_PX[idx] = ts[TS_PIX]; TC_CK[idx] = tex_ck(ts); TC_N[5]++;
#endif
    return idx;
}

/* returns 1 if the primitive was taken by the GPU path (the caller skips the
 * software rasteriser), 0 to fall through. The triangle is accumulated in arrival
 * order and drawn later, in a4_gl_flush; an overflow past MAXT is drawn now. */
static void composite_prev(volatile struct glr *s) __attribute__((unused));
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

#ifdef A4_ZBUF
/* The perspective rasterizer's own affine fallback (0x2f6fc, read from the image): with
 * its vertices ordered by y as 0x2ff60 orders them, if the top vertex's ZW is over 500 and
 * the reciprocal-table values (RT[ZW], ctx+0x8C, 32768 words, 0x522c8) of the other two
 * differ from the top's by dq1, dq2 with dq1*dq1 + dq2*dq2 < 100 (32-bit, as the game does
 * it), the triangle goes to 0x2e1c8 instead -- affine, colour key, the ctx[0x64] modes,
 * and NO depth test or write. So the game's distant flat-depth triangles are painter's
 * order, and only the others depth-test. */
static int32_t persp_rt(const uint32_t *gctx, int32_t z)
{
    const int32_t *rt = (const int32_t *)((uintptr_t)gctx + 0x8Cu);
    return rt[z < 0 ? 0 : z >= 32768 ? 32767 : z];
}
static int persp_fallback(const uint32_t *gctx, const uint32_t *desc)
{
    const int32_t *v0 = (const int32_t *)desc[0x0C / 4], *v1 = (const int32_t *)desc[0x18 / 4],
                  *v2 = (const int32_t *)desc[0x24 / 4];
    int32_t y0 = v0[1], y1 = v1[1], y2 = v2[1];
    const int32_t *top;                             /* 0x2ff60's first output, branch for branch */
    if (y0 <= y1) top = (y1 <= y2 || y0 <= y2) ? v0 : v2;
    else          top = (y2 >= y0 || y2 > y1) ? v1 : v2;
    if (top[2] <= 500) return 0;
    const int32_t *a = top == v0 ? v1 : v0, *b = top == v2 ? v1 : v2;
    int32_t q = persp_rt(gctx, top[2]);
    uint32_t d1 = (uint32_t)(persp_rt(gctx, a[2]) - q), d2 = (uint32_t)(persp_rt(gctx, b[2]) - q);
    return (int32_t)(d1 * d1 + d2 * d2) < 100;
}
#endif

int a4_gl_draw_prim(uint32_t *gctx, uint32_t *desc, uint32_t flags)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
#ifdef A4_FLAGHIST
    /* mode census: every draw_primitive by (flags, ctx[0x64] -- the per-object span
     * mode the transparent/opaque spans switch on). Table at 0x412D00 (clear of the
     * telemetry's draw-call census at 0x412000; a4_tel.c appends it to each capture):
     * [0]=n, then n entries (flags, mode, count), max 40; [121] = calls that found the
     * table full. Counted before anything else, so taken and software calls alike. */
    {
        volatile uint32_t *fh = (volatile uint32_t *)0x00412D00u;
        uint32_t md = gctx[0x64 / 4], nn = fh[0], i;
        for (i = 0; i < nn; i++) if (fh[1+i*3] == flags && fh[2+i*3] == md) break;
        if (i == nn) { if (nn < 40u) { fh[1+i*3] = flags; fh[2+i*3] = md; fh[3+i*3] = 0; fh[0] = nn + 1; } else i = 99; }
        if (i < 40u) fh[3+i*3]++; else fh[121]++;
    }
#endif
    if (iat[0] < 0x10000u) return 0;
    /* Which rasterizer the game would use: draw_primitive (0x300AE) tests flags in the
     * order 0x2, 0x1, 0x8, 0x10, 0x20, 0x40, 0x100, else the default (MBX.md). Anything
     * left to software is drawn ON TOP of the previous frame's GPU image (it lands after
     * the composite), whatever the game's draw order says -- so every class taken here
     * is one less source of things showing through walls. */
    uint32_t kind;
    int m1 = 0;                                     /* default span in its mode 1 (tex_index) */
    if (flags & 2u) {
        /* 0x2fbdc: untextured, the colour in desc+0x30 (its low half; a zero word draws
         * nothing). Solid when neither 0x4 nor 0x8 (span 0x33b46); 0x4 is a 50% blend
         * (0x33b86), 0x8 a stipple (0x33c44) -- those two stay software: blending on the
         * GPU would mix with the sentinel where the GPU drew nothing. A race uses only
         * the solid one (0x20002, 1.6% of calls: the grey/khaki "slabs", round 9). */
#ifdef A4_TAKEFLAT
        if (flags & 0xCu) return 0;
        kind = KIND_FLAT;
#else
        return 0;
#endif
    } else if (flags & 1u) {
        /* 0x2f6fc, perspective textured. Its span depth-tests (see A4_ZBUF); the other
         * classes neither test nor write the game's 1/z buffer. */
#ifdef A4_ZBUF
        kind = KIND_TEXZ;
        if (persp_fallback(gctx, desc)) { kind = KIND_TEX; m1 = gctx[0x64 / 4] == 1u; A4_PERF[PF_AFFINE]++; }
#else
        kind = KIND_TEX;
#endif
    } else if (flags & 8u) {
        /* 0x2f358 -> span 0x33d18: affine textured, dst + texel per channel, saturating
         * (0x10008, 0.2%: glows). GL_ONE/GL_ONE is the same sum -- but over the sentinel
         * (pixels whose content is the software layer, e.g. the sky) it adds to magenta. */
#ifdef A4_TAKEADD
        kind = KIND_ADD;
#else
        return 0;
#endif
    } else if ((flags & 0x30u) == 0x20u) {
        /* 0x2ecb4 -> span 0x33f8c: shadows, dst*12/16 (22% of calls). The span keeps a
         * screen-sized byte map so an overlap darkens once; GL_ZERO/GL_SRC_ALPHA at alpha
         * 12/16 darkens again where two shadow triangles overlap. */
#ifdef A4_TAKESHADOW
        kind = KIND_SHAD;
#else
        return 0;
#endif
    } else if ((flags & 0x150u) == 0x100u) {
        /* 0x2e56c -> span 0x33ea8: affine textured, dst + (T - dst) * a/16 with a the top
         * nibble of a byte alpha map at desc+0x2c (per texel; no colour key). 0x14100,
         * 1.1% of a full race (the short round-9 census missed it): the roadside chevron
         * boards. RGBA8888 with alpha a*16 and SRC_ALPHA blending -- over the sentinel a
         * part-transparent texel mixes with magenta (leak.py measures it). */
#ifdef A4_TAKEALPHA
        kind = KIND_ALPHA;
#else
        return 0;
#endif
    } else if (flags & 0x150u) {
        return 0;                                    /* 0x10/0x40: none in a race */
    } else {
#ifdef A4_TAKEDEF
        /* The game's DEFAULT rasterizer (0x2e1c8): 0x4000, 0x104000, 0x20000, 0x10000 --
         * ~47% of all triangles in a race. Its span (0x33964) is a colour key -- texel 0
         * leaves the pixel -- which the 5551 upload + alpha test already reproduce; span
         * mode 1 (ctx[0x64]==1) also adds 1/4 of a per-scanline table colour indexed by a
         * second map, not reproduced (drawn as the plain key). Same descriptor layout as
         * flags 1. Mode 1 (ctx[0x64] == 1) is baked into its texture (m1_texel). */
        kind = KIND_TEX;
        m1 = gctx[0x64 / 4] == 1u;
#else
        return 0;
#endif
    }
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (++s->seen < START_AFTER) return 0;           /* past the intro video + menu */
    if (s->up != A4_GL_MAGIC) {
        if (!bringup(s)) return 0;                   /* glr is bss-zeroed */
        s->up = A4_GL_MAGIC;
#ifndef A4_IMM
        s->dual = 1;                                 /* see the return below */
#endif
    }
#ifdef A4_IMM
    /* the frame was composited already (a4_gl_hud: the HUD has begun): a triangle now is
     * an overlay drawn after it, and the GPU's image of this frame is spent -- software */
    if (s->flushed) { A4_PERF[PF_LATE]++; return 0; }
#endif
    if (s->prims == 0) {                                /* first prim of the frame */
        s->flushed = 0;
#if defined(A4_TEL) && defined(A4_ABCYCLE)
        s->fmode = A4_PERF[PF_MODE];                   /* a4_tel.c steps it per capture window */
#else
        s->fmode = A4_ABMODE;
#endif
        s->miny = VP_H; s->maxy = 0;                    /* empty drawn-row extent */
        /* Composite the previous frame's GPU image NOW, before this frame's
         * software draws (the car, the trees, the HUD) land on top of it in
         * painter's order. The old 3D->2D boundary was the frame's first
         * translucent fill, which a race frame does not have (draw-order trace,
         * MBX.md): the flush fell to the frame end and overwrote the car and
         * the bottom HUD -- the device glitch. The pbuffer still holds the
         * frame drawn at the previous frame's end. */
#ifndef A4_IMM
        { uint32_t t0 = a4_ntick(); composite_prev(s); A4_PERF[PF_COMP] += a4_ntick() - t0; }
#endif
        /* Clear for THIS frame now, after the readback of the previous one, so a
         * batch drawn mid-frame (when the batch buffers fill) is kept. The clear used
         * to be at the frame end, which erased every triangle past the batch size.
         * Alpha 0: with dst alpha (A4_IMM) only an opaque triangle sets it, so it marks
         * what the GPU covered (see composite). */
        gl_clearcolorx(FX, 0, FX, 0);                    /* magenta sentinel (see SENTINEL_*) */
        gl_clear(clear_bits(s));
        s->bfill = 0;
    }

#ifdef A4_SWONLY
    /* reference build: GL up and the frame counted (so telemetry captures it), but
     * nothing drawn on the GPU -- every triangle is the game's own software, i.e. the
     * stock picture, for comparing captures against */
    s->prims++;
    return 0;
#endif
    uint32_t tsaddr = desc[0x30 / 4];               /* texture descriptor, or flat colour */
    if (!tsaddr && kind != KIND_SHAD) return 0;
    int wsh = 0, hsh = 0;
    uint32_t id;                                    /* the triangle's TEXID word */
#ifdef A4_M1VIS
    /* diagnosis: mode-1 triangles of a texture of 64K texels or more, flat red */
    if (m1 && (((uint32_t *)tsaddr)[1] + 1) * (((uint32_t *)tsaddr)[2] + 1) >= 65536u) kind = KIND_FLAT, tsaddr = 0xF800u;
#endif
    if (kind == KIND_SHAD) {
        id = KIND_SHAD << 28;                       /* desc+0x30 unused by 0x33f8c */
    } else if (kind == KIND_FLAT) {
        id = (KIND_FLAT << 28) | (tsaddr & 0xFFFFu);
    } else {
        uint32_t *ts = (uint32_t *)tsaddr;
        uint32_t tw = ts[1] + 1, th = ts[2] + 1;
        for (uint32_t w = tw; w > 1; w >>= 1) wsh++;
        for (uint32_t h = th; h > 1; h >>= 1) hsh++;
        int ti = tex_index(s, tsaddr, gctx, kind == KIND_ALPHA ? 2 : m1);
        if (ti < 0) return 0;
        /* colorkey sprites are GPU-drawn with alpha test (5551 upload), so no software
         * fallback -- s->cache_tr is kept for diagnostics only. */
        id = (kind << 28) | s->cache_id[ti];
    }

    int16_t p[6];
    int32_t uv[3 * UVC];
    const uint32_t voff[3] = { 0x0C, 0x18, 0x24 };
    for (int i = 0; i < 3; i++) {
        uint32_t *vp = (uint32_t *)desc[voff[i] / 4];
        p[i*2]   = (int16_t)(int32_t)vp[0];
        p[i*2+1] = (int16_t)(int32_t)vp[1];
        int32_t un = 0, vn = 0;                     /* flat/shadow triangles sample nothing */
        if (kind == KIND_TEX || kind == KIND_TEXZ || kind == KIND_ADD || kind == KIND_ALPHA) {
            un = ((int32_t)desc[voff[i]/4 + 1] * 4096) >> wsh;      /* U_norm * 65536 */
            vn = ((int32_t)desc[voff[i]/4 + 2] * 4096) >> hsh;      /* V_norm * 65536 */
        }
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
        /* depth (A4_ZBUF, flags 1 only): z_ndc = ZK/ZW, the game's 1/z up to scale. The
         * ortho maps z_clip = -z, so z = -ZK*2^shift over w = ZW*2^shift; clamped to w so
         * z_ndc <= 1 and no vertex is ever clipped by the depth range (a near vertex just
         * saturates). Window depth is interpolated linearly on screen, as 1/z is. */
        int32_t zq = 0;
        if (kind == KIND_TEXZ) zq = shift >= 0 ? (ZK << shift) : (ZK >> -shift);
        for (int i = 0; i < 3; i++) {
            int32_t W = (int32_t)(shift >= 0 ? (zw[i] << shift) : (zw[i] >> -shift));
            if (W < 1) W = 1;
            P[i*4+0] = (int32_t)p[i*2]   * W;
            P[i*4+1] = (int32_t)p[i*2+1] * W;
            P[i*4+2] = -(zq < W ? zq : W);
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
        uint32_t t0 = a4_ntick(); batch_draw(s->bfill); A4_PERF[PF_DRAW] += a4_ntick() - t0;
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
    if (kind == KIND_FLAT) A4_PERF[PF_FLAT]++;
    else if (kind == KIND_ADD) A4_PERF[PF_ADD]++;
    else if (kind == KIND_SHAD) A4_PERF[PF_SHAD]++;
    /* The first GPU frame has no previous GPU image to composite, so its GPU-taken
     * triangles would be missing for a frame (the countdown flash: 76% of the scene,
     * round 9). Let software draw that frame as well; the GPU still draws it, and the
     * next frame composites it as usual. */
#ifdef A4_DUALCAP
    return 0;                                       /* bench instrument: software draws it all */
#elif defined(A4_IMM)
    /* Blended classes are drawn by software as well: the GPU's version is right where it
     * blends over GPU-drawn pixels, software's where it blends over the backdrop -- the
     * composite takes the GPU's only where an opaque GPU triangle set dst alpha. */
    return (kind == KIND_ADD || kind == KIND_ALPHA) ? 0 : 1;
#else
    return s->dual ? 0 : 1;
#endif
}

/* Undo what entering a kind changed, back to the bring-up state (KIND_TEX). */
static void kind_leave(uint32_t st)
{
    if ((st == KIND_ADD || st == KIND_ALPHA || st == KIND_SHAD) && ((volatile struct glr *)A4_GL_SCRATCH)->abits)
        gl_colormask(1, 1, 1, 1);
    if (st == KIND_FLAT || st == KIND_SHAD) { gl_enable(GL_TEXTURE_2D); gl_color4x(FX, FX, FX, FX); }
    if (st == KIND_ADD || st == KIND_SHAD || st == KIND_ALPHA) gl_disable(GL_BLEND);
    if (st == KIND_TEXZ) gl_disable(GL_DEPTH_TEST);
    if (st == KIND_SHAD && ((volatile struct glr *)A4_GL_SCRATCH)->sbits) gl_disable(GL_STENCIL_TEST);
}

/* One run of equal TEXID words: switch GL state if its kind differs from the
 * previous run's (*st), then draw. The state left between runs is the kind's own;
 * batch_draw restores KIND_TEX (the bring-up state) when it is done. */
static void draw_run(uint32_t mat, uint32_t first, uint32_t count, uint32_t *st)
{
    uint32_t kind = mat >> 28;
    if (kind == KIND_TEXZ && !((volatile struct glr *)A4_GL_SCRATCH)->zbits)
        kind = KIND_TEX;                            /* no depth buffer: draw untested */
#ifdef A4_KINDVIS
    /* diagnosis: flat triangles bright green, additive ones bright cyan, shadows bright
     * red, all opaque and untextured, so a capture shows which class an object is */
    if (kind == KIND_FLAT || kind == KIND_ADD || kind == KIND_SHAD) {
        kind_leave(*st);
        gl_disable(GL_TEXTURE_2D);
        *st = KIND_FLAT;
        if (kind == KIND_FLAT) gl_color4x(0, FX, 0, FX);
        else if (kind == KIND_ADD) gl_color4x(0, FX, FX, FX);
        else gl_color4x(FX, 0, 0, FX);                 /* shadows red */
        gl_drawarrays(GL_TRIANGLES, (int)(first*3), (int)(count*3));
        return;
    }
#endif
    if (kind != *st) {
        kind_leave(*st);
        /* blended classes leave dst alpha (the drawn-mask) as the opaque ones set it */
        if ((kind == KIND_ADD || kind == KIND_ALPHA || kind == KIND_SHAD) && ((volatile struct glr *)A4_GL_SCRATCH)->abits)
            gl_colormask(1, 1, 1, 0);
        if (kind == KIND_FLAT) gl_disable(GL_TEXTURE_2D);
        else if (kind == KIND_TEXZ) gl_enable(GL_DEPTH_TEST);
        else if (kind == KIND_ADD) { gl_enable(GL_BLEND); gl_blendfunc(GL_ONE, GL_ONE); }
        else if (kind == KIND_ALPHA) { gl_enable(GL_BLEND); gl_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
        else if (kind == KIND_SHAD) {
            gl_disable(GL_TEXTURE_2D); gl_enable(GL_BLEND); gl_blendfunc(GL_ZERO, GL_SRC_ALPHA);
            gl_color4x(0, 0, 0, (FX * 12) / 16);     /* dst * 12/16 */
            /* once a pixel a frame (the span's stamp map): pass where the stencil is 0,
             * then INCR it (func/op set at bring-up, cleared with the frame) */
            if (((volatile struct glr *)A4_GL_SCRATCH)->sbits) gl_enable(GL_STENCIL_TEST);
        }
        *st = kind;
    }
    if (kind == KIND_FLAT) {
        /* RGB565 -> GL_FIXED, c*FX/31 (or /63) without a divide; exact at both ends */
        uint32_t r = (mat >> 11) & 31u, g = (mat >> 5) & 63u, b = mat & 31u;
        gl_color4x(r == 31u ? FX : (int)(r * 2114u), g == 63u ? FX : (int)(g * 1040u + (g >> 2)),
                   b == 31u ? FX : (int)(b * 2114u), FX);
    } else if (kind != KIND_SHAD) {
        gl_bindtex(GL_TEXTURE_2D, mat & 0x0FFFFFFFu);
    }
    gl_drawarrays(GL_TRIANGLES, (int)(first*3), (int)(count*3));
}

/* Draw the accumulated frame in arrival order, one glDrawArrays per run of
 * consecutive equal TEXID words (same texture or colour, same kind). Identical
 * pixels to the per-triangle path; just fewer GL calls. No depth, no sort. */
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

    uint32_t run_start = 0, st = KIND_TEX;
    uint32_t cur = TEXID[0];
    for (uint32_t t = 1; t < nt; t++) {
        if (TEXID[t] != cur) {
            draw_run(cur, run_start, t - run_start, &st);
            run_start = t;
            cur = TEXID[t];
        }
    }
    draw_run(cur, run_start, nt - run_start, &st);  /* final run */
    kind_leave(st);
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    {   /* draw calls this frame (one per same-texture run); folded at flush */
        uint32_t runs = 1; for (uint32_t t = 1; t < nt; t++) if (TEXID[t] != TEXID[t-1]) runs++;
        ((volatile uint32_t *)0x00412000u)[4] += runs;
    }
#endif
}

/* Read back the GPU image of rows [miny, maxy] and write the pixels the GPU drew into the
 * game's surface. Which pixels: with dst alpha (A4_IMM and abits) those an opaque triangle
 * covered (alpha set; the clear leaves 0 and the blended classes do not write it), so a
 * blend over the backdrop is left to software's own drawing of it; without, every pixel
 * that is not the magenta sentinel -- nor the sentinel with a glow added to it (R and B
 * saturated, G short of full: additive over the backdrop), so pure pinks are lost there.
 * Read in the implementation's own pair when AB_NATIVE says so (BGRA on the N95). */
static void composite_rows(volatile struct glr *s, uint32_t miny, uint32_t maxy)
{
#ifdef A4_DUALCAP
    return;                                         /* the game's own picture stays untouched */
#endif
#if !defined(A4_NO_BLIT) && !defined(A4_WINDOW)
    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;
    if (!bb || miny > maxy) return;
    uint32_t *rb = (uint32_t *)A4_GL_READBUF;
    volatile uint16_t *fb = (volatile uint16_t *)bb;
    /* strips covering only the rows drawn. Screen rows are top-down, GL rows bottom-up:
     * screen [miny,maxy] -> GL [H-1-maxy, H-1-miny]. y0 steps on STRIP multiples and VP_H
     * is one, so a strip never over-reads. */
    int gl_lo = VP_H - 1 - (int)maxy; if (gl_lo < 0) gl_lo = 0;
    int gl_hi = VP_H - 1 - (int)miny; if (gl_hi > VP_H - 1) gl_hi = VP_H - 1;
    int nat = (s->fmode & AB_NATIVE) != 0, amask = s->abits != 0;
    int r565 = nat && !amask && s->rdfmt == GL_RGB && s->rdtype == GL_UNSIGNED_SHORT_5_6_5;
    int bgra = nat && s->rdfmt == GL_BGRA && s->rdtype == GL_UNSIGNED_BYTE;
    int rsh = bgra ? 16 : 0, bsh = bgra ? 0 : 16;   /* where R and B sit in a read word */
    for (int y0 = (gl_lo / STRIP) * STRIP; y0 <= gl_hi; y0 += STRIP) {
        uint32_t t0 = a4_ntick();
        if (r565)      gl_readpixels(0, y0, VP_W, STRIP, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rb);
        else if (bgra) gl_readpixels(0, y0, VP_W, STRIP, GL_BGRA, GL_UNSIGNED_BYTE, rb);
        else           gl_readpixels(0, y0, VP_W, STRIP, GL_RGBA, GL_UNSIGNED_BYTE, rb);
        A4_PERF[PF_READ] += a4_ntick() - t0;
#ifdef A4_READBACK_ONLY
        continue;                                        /* measurement: readback cost without the blit */
#endif
        if (r565) {
            const uint16_t *rb16 = (const uint16_t *)rb;
            for (int sy = 0; sy < STRIP; sy++) {
                int screen_y = VP_H - 1 - (y0 + sy);
                if (screen_y < CLIP_TOP) continue;
                const uint16_t *src = rb16 + sy * VP_W;
                volatile uint16_t *dstrow = fb + screen_y * FB_STRIDE;
                for (int x = 0; x < VP_W; x++) {
                    uint32_t px = src[x];
                    if ((px & 0xF81Fu) == 0xF81Fu && (px & 0x07E0u) != 0x07E0u) continue;   /* sentinel (+glow) */
                    dstrow[x] = (uint16_t)px;
                }
            }
            continue;
        }
        for (int sy = 0; sy < STRIP; sy++) {
            int screen_y = VP_H - 1 - (y0 + sy);
            if (screen_y < CLIP_TOP) continue;           /* leave the HUD banner to software */
            const uint32_t *src = rb + sy * VP_W;
            volatile uint16_t *dstrow = fb + screen_y * FB_STRIDE;
            for (int x = 0; x < VP_W; x++) {
                uint32_t px = src[x];
                uint32_t r = (px >> rsh) & 0xFF, g = (px >> 8) & 0xFF, b = (px >> bsh) & 0xFF;
                if (amask ? (px >> 24) < 0x80u : (r == 0xFFu && b == 0xFFu && g != 0xFFu)) {
#ifdef A4_SENTINEL_VIS
                    /* diagnosis: paint GPU-didn't-draw (software-kept) pixels bright
                     * green, so the retained software region is visible in the capture. */
                    dstrow[x] = (uint16_t)0x07E0;
#endif
                    continue;                           /* GPU drew nothing (opaque) here */
                }
                dstrow[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }
#else
    (void)s; (void)miny; (void)maxy;
#endif /* !A4_NO_BLIT && !A4_WINDOW */
}

/* the pipelined composite: the previous frame's image, at this frame's first triangle */
static void composite_prev(volatile struct glr *s) __attribute__((unused));
static void composite_prev(volatile struct glr *s)
{
    composite_rows(s, s->pminy, s->pmaxy);
}

void a4_gl_flush(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC || s->prims == 0 || s->flushed) return;
    s->flushed = 1;
    s->last_prims = s->prims;                            /* frame's prim count (device probe) */

    /* PIPELINED (no A4_IMM): this frame's primitives are drawn now, at the frame's end,
     * and read back at the NEXT frame's first primitive (composite_prev) -- one frame of
     * latency for the GPU world against the software layers. IMMEDIATE (A4_IMM): drawn
     * and composited here, at the end of the 3D pass (a4_gl_hud) or the present.
     * Draw the rest of THIS frame (the clear was at its first primitive, and any full
     * batch was drawn as it filled). */
    { uint32_t t0 = a4_ntick(); batch_draw(s->bfill); A4_PERF[PF_DRAW] += a4_ntick() - t0; }
    s->bfill = 0;
    if (s->fmode & AB_FLUSH) {
        /* A tile renderer (the MBX) renders nothing until something makes it: without
         * this the frame is only rendered when composite_prev's first glReadPixels asks,
         * and the pipelining buys nothing. */
        uint32_t t0 = a4_ntick(); gl_flush(); A4_PERF[PF_GLFLUSH] += a4_ntick() - t0;
    }
    if (s->dual) { s->dual = 0; A4_PERF[PF_DUAL]++; }
#ifdef A4_IMM
    /* IMMEDIATE: composite this frame's own image now -- at the end of the 3D pass when
     * the HUD painter called (a4_gl_hud), else at the present (a frame with no HUD). No
     * frame of latency between the GPU's objects and what software draws over them (the
     * tags, the HUD, the blended classes, anything not through draw_primitive). */
    { uint32_t t0 = a4_ntick(); composite_rows(s, s->miny, s->maxy); A4_PERF[PF_COMP] += a4_ntick() - t0; }
#endif
#if defined(A4_RUNDIAG) || defined(A4_TEL)
    {   /* 0x412000: [0] frames [1] draw calls [2] triangles [3] max calls in a frame
         * [5] calls in the last frame (a4_tel.c writes these into the capture header) */
        volatile uint32_t *rd = (volatile uint32_t *)0x00412000u;
        A4_PERF[PF_CALLS] += rd[4]; A4_PERF[PF_TRIS] += s->prims;
        rd[0]++; rd[1] += rd[4]; rd[2] += s->prims; if (rd[4] > rd[3]) rd[3] = rd[4]; rd[5] = rd[4]; rd[4] = 0;
    }
#endif
    A4_PERF[PF_RENDERS]++;
    { uint32_t e = gl_geterror(); if (s->errc == 0) s->errc = e; }  /* catch a draw GL error */

    /* this frame's drawn extent is what composite_prev reads back next frame */
    s->pminy = s->miny; s->pmaxy = s->maxy;
}

/* The HUD sprite painter's every call (a4_hud_tramp, +2973A). Its first call of a race
 * frame comes after the frame's last 3D triangle (MBX.md, the draw order), so under A4_IMM
 * the frame is drawn out and composited here, before the HUD goes over it. A no-op in a
 * frame with nothing drawn on the GPU (menus) or once done. */
void a4_gl_hud(void)
{
#ifdef A4_IMM
    a4_gl_flush();
#endif
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

#ifdef A4_DUALCAP
/* Bench instrument (round 10): the GPU's picture of the frame just drawn, read back now
 * (the pbuffer holds it until the next frame's first triangle) as top-down RGB565 rows,
 * the sentinel as 0xF81F, handed to wr() strip by strip -- a4_tel.c appends it to the
 * capture, beside the software picture of the same frame (A4_DUALCAP makes software draw
 * every triangle as well). */
void a4_gl_dumpgpu(void (*wr)(int pos, const void *p, int len), int base)
{
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC) return;
    uint32_t *rb = (uint32_t *)A4_GL_READBUF;   /* the strip buffer is exactly one RGBA strip */
    for (int y0 = 0; y0 < VP_H; y0 += STRIP) {
        gl_readpixels(0, y0, VP_W, STRIP, GL_RGBA, GL_UNSIGNED_BYTE, rb);
        for (int sy = STRIP - 1; sy >= 0; sy--) {     /* GL rows bottom-up -> screen rows */
            const uint32_t *src = rb + sy * VP_W;
            uint16_t *out = (uint16_t *)src;            /* in place: 2 bytes out per 4 read, forward */
            for (int x = 0; x < VP_W; x++) {
                uint32_t px = src[x], r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
                out[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
            int screen_y = VP_H - 1 - (y0 + sy);
            wr(base + screen_y * VP_W * 2, out, VP_W * 2);
        }
    }
}
#endif

/* Bring-up facts for the telemetry header: 0/1/2 the OES_read_format probe (format,
 * type, its error), 3 the pbuffer config, 4 its depth bits. */
uint32_t a4_gl_rdinfo(int k)
{
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    switch (k) {
    case 0: return s->rdfmt;
    case 1: return s->rdtype;
    case 2: return s->rderr;
    case 3: return s->zcfg;
    case 4: return s->zbits;
    case 6: return s->sbits;
    case 7: return s->abits;
    default: return s->ntex;
    }
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
