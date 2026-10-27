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
#define CLIP_TOP 11                      /* top rows are the HUD banner, not the 3D viewport
                                          * (the frame's scene fill is x0 y11 w320 h229) --
                                          * the blit must not touch them or GPU scene pixels
                                          * leak into the position/lap/WANTED strip. */
#define NCACHE 256
#define START_AFTER 50000u               /* skip the intro video; reachable on the bench */

/* Batch buffers, immediately after the readback strip (0x408000). One frame of
 * triangles in arrival order: per vertex 2 int16 (x,y) + 2 int32 (u,v, GL_FIXED),
 * 3 verts a triangle; one uint32 GL texture name a triangle (for run detection).
 * MAXT caps the frame; an overflow triangle is drawn immediately. Sizes must fit
 * under GL_SCRATCH + GL_SCRATCH_SIZE in injector.py (top 0x410000). */
#define MAXT 640
#define A4_GL_POS   0x00408000u                      /* int16[2] * (3*MAXT) = 0x1E00 */
#define A4_GL_UV    (A4_GL_POS + (3u*MAXT*2u*2u))     /* int32[2] * (3*MAXT) = 0x3C00 */
#define A4_GL_TEXID (A4_GL_UV  + (3u*MAXT*2u*4u))     /* uint32   * MAXT     = 0x0A00 */
                                                      /* end 0x40E600 */

/* EGL */
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PIXMAP_BIT 0x0002            /* probe: does the device offer a pixmap config? */
#define GLES_CONFIG 6u
#ifdef A4_WINDOW
/* The GPU presents: an EGL window surface on a window of our own, swapped at
 * the frame end; no pbuffer, no readback. The window-server objects live in
 * the scratch: RWsSession (8 bytes), RWindowGroup (8), RWindow (24 on 9.x:
 * MWsClientClass + TRect iDrawRect), 32 bytes apart. */
#define EGL_WINDOW_BIT 0x0004
#define EGL_BUFFER_SIZE 0x3020
#define ECOLOR64K 7                      /* TDisplayMode: the game's 565 surface */
#define A4_WS_OBJ 0x00402800u
#endif
/* GL enums */
#define GL_TRIANGLES 0x0004
#define GL_FLAT 0x1D00
#define GL_FIXED 0x140C
#define GL_SHORT 0x1402
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_COLOR_BUFFER_BIT 0x4000
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
    uint32_t cache_ptr[NCACHE];
    uint32_t cache_id[NCACHE];
    uint32_t cache_tr[NCACHE];         /* 1 = texture is a colorkey (transparent) sprite */
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
#ifdef A4_WINDOW
extern volatile uint32_t *a4_ws_iat_ptr(void);
#define W(i) (a4_ws_iat_ptr()[i])
static void ws_ctor(void *w){ ((void(*)(void*))W(0))(w); }
static int  ws_connect(void *w){ return ((int(*)(void*))W(1))(w); }
static int  ws_flush(void *w){ return ((int(*)(void*))W(2))(w); }
static void wg_ctor(void *g, void *w){ ((void(*)(void*,void*))W(3))(g, w); }
static int  wg_construct(void *g, uint32_t h, int focusable){ return ((int(*)(void*,uint32_t,int))W(4))(g, h, focusable); }
static void wg_focus(void *g, int on){ ((void(*)(void*,int))W(5))(g, on); }
static void wn_ordinal(void *n, int pos, int pri){ ((void(*)(void*,int,int))W(14))(n, pos, pri); }   /* position and priority */
static void gl_disable(uint32_t c){ ((void(*)(uint32_t))F(32))(c); }
static void rw_ctor(void *r, void *w){ ((void(*)(void*,void*))W(7))(r, w); }
static int  rw_construct(void *r, void *parent, uint32_t h){ return ((int(*)(void*,void*,uint32_t))W(8))(r, parent, h); }
static void rw_setextent(void *r, const int *pt, const int *sz){ ((void(*)(void*,const int*,const int*))W(9))(r, pt, sz); }
static void rw_activate(void *r){ ((void(*)(void*))W(10))(r); }
static void rw_setvisible(void *r, int v){ ((void(*)(void*,int))W(11))(r, v); }
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
    /* our own window: session, a group that never takes focus (the game's keeps
     * it, and keeps its keys), a full-screen 565 window in front, activated and
     * flushed before EGL sees it. Each step that can fail reports in winstep. */
    uint8_t *wsb = (uint8_t *)A4_WS_OBJ; void *ws = wsb, *wg = wsb + 0x20, *win = wsb + 0x40;
    for (int i = 0; i < 0x60; i++) wsb[i] = 0;
    ws_ctor(ws);        s->winstep = 1; if (ws_connect(ws) != 0) return 0;
    wg_ctor(wg, ws);    s->winstep = 2; if (wg_construct(wg, (uint32_t)wg, 0) != 0) return 0;
    wg_focus(wg, 0); wn_ordinal(wg, 0, 1000);           /* front, above the game's group (ECoeWinPriorityAlwaysAtFront) */
    rw_ctor(win, ws);   s->winstep = 3; if (rw_construct(win, wg, (uint32_t)win) != 0) return 0;
    rw_setdispmode(win, ECOLOR64K);
    { int pt[2] = { 0, 0 }, sz[2] = { VP_W, VP_H }; rw_setextent(win, pt, sz); }
    rw_nontransparent(win);
    rw_activate(win); rw_setvisible(win, 1); ws_flush(ws); s->winstep = 4;
    int ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_BUFFER_SIZE, 16, EGL_NONE };
    uint32_t cfg = 0; int ncfg = 0;
    if (!egl_choose_config(disp, ca, &cfg, 1, &ncfg) || ncfg < 1) return 0; s->winstep = 5;
    int surf = egl_window_surface(disp, cfg, win, 0); if (!surf) return 0; s->bstep = 4; s->winstep = 6;
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
static int tex_index(volatile struct glr *s, uint32_t tsaddr)
{
    uint32_t *ts = (uint32_t *)tsaddr;
    for (uint32_t i = 0; i < s->ntex; i++)
        if (s->cache_ptr[i] == tsaddr) return (int)i;
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const void *pix = (const void *)ts[3];
    if (!pix || w > 1024 || h > 1024) return -1;
    if (s->ntex >= NCACHE) return -1;
    /* colorkey scan: >=1/8 of the texels pure 0 (black) -> a transparent sprite */
    const uint16_t *px16 = (const uint16_t *)pix;
    uint32_t total = w * h, zeros = 0;
    for (uint32_t i = 0; i < total; i++) if (px16[i] == 0) zeros++;
    uint32_t tr = (zeros * 8u >= total) ? 1u : 0u;
    uint32_t id = 0;
    gl_gentex(1, &id);
    gl_bindtex(GL_TEXTURE_2D, id);
    gl_teximage(GL_TEXTURE_2D, 0, GL_RGB, (int)w, (int)h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pix);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    int idx = (int)s->ntex;
    s->cache_ptr[idx] = tsaddr; s->cache_id[idx] = id; s->cache_tr[idx] = tr; s->ntex++;
    return idx;
}

/* returns 1 if the primitive was taken by the GPU path (the caller skips the
 * software rasteriser), 0 to fall through. The triangle is accumulated in arrival
 * order and drawn later, in a4_gl_flush; an overflow past MAXT is drawn now. */
static void composite_prev(volatile struct glr *s);

int a4_gl_draw_prim(uint32_t *gctx, uint32_t *desc, uint32_t flags)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u || !(flags & 1)) return 0;
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
         * frame drawn at the previous frame's end; it is cleared in a4_gl_flush. */
        composite_prev(s);
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
    if (s->cache_tr[ti]) return 0;                   /* transparent sprite: leave to software */
    uint32_t id = s->cache_id[ti];

    int16_t p[6];
    int32_t uv[6];
    const uint32_t voff[3] = { 0x0C, 0x18, 0x24 };
    for (int i = 0; i < 3; i++) {
        uint32_t *vp = (uint32_t *)desc[voff[i] / 4];
        p[i*2]   = (int16_t)(int32_t)vp[0];
        p[i*2+1] = (int16_t)(int32_t)vp[1];
        uv[i*2]   = ((int32_t)desc[voff[i]/4 + 1] * 4096) >> wsh;
        uv[i*2+1] = ((int32_t)desc[voff[i]/4 + 2] * 4096) >> hsh;
    }
    /* grow the frame's drawn-row extent, so the readback covers only the rows
     * the GPU touched (the readback, not the blit, is the composite's cost). */
    for (int i = 0; i < 3; i++) {
        int32_t y = p[i*2+1];
        if (y < 0) y = 0; else if (y >= VP_H) y = VP_H - 1;
        if ((uint32_t)y < s->miny) s->miny = (uint32_t)y;
        if ((uint32_t)y > s->maxy) s->maxy = (uint32_t)y;
    }

    uint32_t n = s->prims;
    if (n >= MAXT) {                                /* batch full: draw this one now */
        gl_bindtex(GL_TEXTURE_2D, id);
        gl_vertexptr(2, GL_SHORT, 0, p);
        gl_texcoordptr(2, GL_FIXED, 0, uv);
        gl_drawarrays(GL_TRIANGLES, 0, 3);
        return 1;
    }
    int16_t  *POS = (int16_t  *)A4_GL_POS;
    int32_t  *UV  = (int32_t  *)A4_GL_UV;
    uint32_t *TEXID = (uint32_t *)A4_GL_TEXID;
    uint32_t vb = n * 3;                            /* first vertex of this triangle */
    for (int k = 0; k < 6; k++) POS[vb*2 + k] = p[k];
    for (int k = 0; k < 6; k++) UV[vb*2 + k]  = uv[k];
    TEXID[n] = id;
    s->prims = n + 1;
    return 1;
}

/* Draw the accumulated frame in arrival order, one glDrawArrays per run of
 * consecutive same-texture triangles. Identical pixels to the per-triangle path;
 * just fewer GL calls. No depth, no sort. */
static void batch_draw(volatile struct glr *s)
{
    uint32_t nt = s->prims;
    if (nt == 0) return;
    if (nt > MAXT) nt = MAXT;
    uint32_t *TEXID = (uint32_t *)A4_GL_TEXID;

    gl_vertexptr(2, GL_SHORT, 0, (const void *)A4_GL_POS);
    gl_texcoordptr(2, GL_FIXED, 0, (const void *)A4_GL_UV);

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
                if (r == SENTINEL_R && g == SENTINEL_G && b == SENTINEL_B)
                    continue;                           /* GPU drew nothing here; keep software pixel */
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

    /* clear, then draw THIS frame; it finishes on the GPU during the present
     * and the next frame's logic, and composite_prev reads it back then. */
    gl_clearcolorx(FX, 0, FX, FX);                       /* magenta sentinel (see SENTINEL_*) */
    gl_clear(GL_COLOR_BUFFER_BIT);
    batch_draw(s);                                       /* issue the frame's draws */
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
    a4_gl_flush();                                      /* draw this frame on the GPU */
    int presented = 0;
#ifdef A4_WINDOW
    if (s->surf) {
        void *ws = (void *)A4_WS_OBJ, *win = (uint8_t *)A4_WS_OBJ + 0x40;
        /* The present hook fires more than once a game frame (the runtime's
         * cadence, ~1.5x in the profile), so a hook with no primitives is not a
         * 2D frame; only a run of them is (a menu). While our window is up the
         * game's present is skipped every time, or a software frame reaches
         * the panel between two of ours. */
        if (had) {                                      /* a 3D frame: the GPU presents */
            s->idle2d = 0;
            if (!s->wvis) { rw_setvisible(win, 1); ws_flush(ws); s->wvis = 1; }
            bar_draw(fps);
            egl_swap((int)s->disp, (int)s->surf);
        } else if (++s->idle2d > 30u && s->wvis) {       /* 2D for a while (menu): the game presents */
            rw_setvisible(win, 0); ws_flush(ws); s->wvis = 0;
        }
        presented = (int)s->wvis;
    }
#else
    (void)had;
#endif
    s->prims = 0;
    s->flushed = 0;
    s->frames++;
    return presented;
}
