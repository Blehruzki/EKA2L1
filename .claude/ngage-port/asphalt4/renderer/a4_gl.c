/* a4_gl.c -- MBX stage 4: render the frame's textured 3D on the GPU.
 *
 * a4_prim.c forwards every textured primitive (+0x300B2) to a4_gl_draw_prim,
 * which keeps a GL ES 1.1 context on a 320x240 pbuffer (the game's viewport),
 * caches each texture by its desc+0x30 pointer (uploaded once as RGB565), and
 * draws the triangle under an ortho matching the 320x240 screen. The fps detour
 * (+0x1E6) calls a4_gl_frame once per frame, which reads the pbuffer back and
 * blits it into the game's surface (ctx[0x4c] = A4_GL_BB), replacing the
 * software scene -- a correctness proof that the whole textured scene renders on
 * the GPU. (The software path still runs; skipping it + compositing the 2D HUD
 * is the perf step. On device the pbuffer+readback is a pixmap surface instead.)
 *
 * Function pointers: a4_gl_iat[] (a4_gl.s), filled by the libGLES_CM import.
 */
#include <stdint.h>

#define A4_GL_SCRATCH 0x00401000u
#define A4_GL_READBUF 0x00403000u        /* one strip: 320*STRIP*4 */
#define A4_GL_BB      0x00400700u        /* surface base, from a4_cachebb_tramp */
#define STRIP 16                         /* readback strip height (keeps image .bss small) */
#define A4_GL_MAGIC   0x6C900DE4u
#define VP_W 320
#define VP_H 240
#define FB_STRIDE VP_W                   /* 320x240 render surface: stride = width (320) */
#define CLIP_TOP 11                      /* top rows are the HUD banner, not the 3D viewport
                                          * (the frame's scene fill is x0 y11 w320 h229) --
                                          * the blit must not touch them or GPU scene pixels
                                          * leak into the position/lap/WANTED strip. */
#define NCACHE 256
#define START_AFTER 50000u               /* skip the intro video; reachable on the bench */

/* EGL */
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_NONE 0x3038
#define GLES_CONFIG 6u
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
    uint32_t cache_ptr[NCACHE];
    uint32_t cache_id[NCACHE];
};

/* sentinel the pbuffer is cleared to each frame: pixels the GPU never drew read
 * back as pure magenta, so the blit can leave them to the software layer (sky,
 * car, HUD) instead of painting a black scene over everything. */
#define SENTINEL_R 0xFFu
#define SENTINEL_G 0x00u
#define SENTINEL_B 0xFFu

/* small typed views of the IAT */
#define F(i) (a4_gl_iat_ptr()[i])       /* pc-relative getter; no writable globals */
static int   egl_get_display(int d){ return ((int(*)(int))F(0))(d); }
static int   egl_initialize(int d,int*a,int*b){ return ((int(*)(int,int*,int*))F(1))(d,a,b); }
static int   egl_pbuffer(int d,uint32_t c,int*a){ return ((int(*)(int,uint32_t,int*))F(2))(d,c,a); }
static int   egl_context(int d,uint32_t c,int s,int*a){ return ((int(*)(int,uint32_t,int,int*))F(3))(d,c,s,a); }
static int   egl_make_current(int d,int r,int w,int c){ return ((int(*)(int,int,int,int))F(4))(d,r,w,c); }
static void  gl_viewport(int x,int y,int w,int h){ ((void(*)(int,int,int,int))F(5))(x,y,w,h); }
static void  gl_clearcolorx(int r,int g,int b,int a){ ((void(*)(int,int,int,int))F(6))(r,g,b,a); }
static void  gl_clear(uint32_t m){ ((void(*)(uint32_t))F(7))(m); }
static void  gl_finish(void){ ((void(*)(void))F(8))(); }
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

static int bringup(volatile struct glr *s)
{
    s->bstep = 1;                                                  /* entered bring-up */
    int disp = egl_get_display(0);                 if (!disp) return 0; s->bstep = 2;
    int a=0,b=0; if (!egl_initialize(disp,&a,&b))   return 0;       s->bstep = 3;
    int pb[] = { EGL_WIDTH, VP_W, EGL_HEIGHT, VP_H, EGL_NONE, 0, 0 };
    int surf = egl_pbuffer(disp, GLES_CONFIG, pb);  if (!surf) return 0; s->bstep = 4;
    int ctx  = egl_context(disp, GLES_CONFIG, 0, 0);if (!ctx)  return 0; s->bstep = 5;
    if (!egl_make_current(disp, surf, surf, ctx))   return 0;       s->bstep = 6;
    gl_viewport(0,0,VP_W,VP_H);
    gl_enable(GL_TEXTURE_2D);
    gl_shademodel(GL_FLAT);
    gl_texenvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    gl_color4x(FX,FX,FX,FX);
    gl_enableclient(GL_VERTEX_ARRAY);
    gl_enableclient(GL_TEXTURE_COORD_ARRAY);
    gl_matrixmode(GL_PROJECTION); gl_loadidentity();
    gl_orthox(0, VP_W*FX, VP_H*FX, 0, -FX, FX);     /* top-left origin */
    gl_matrixmode(GL_MODELVIEW); gl_loadidentity();
    return 1;
}

static uint32_t tex_for(volatile struct glr *s, uint32_t tsaddr)
{
    uint32_t *ts = (uint32_t *)tsaddr;
    for (uint32_t i = 0; i < s->ntex; i++)
        if (s->cache_ptr[i] == tsaddr) return s->cache_id[i];
    /* miss: upload */
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const void *pix = (const void *)ts[3];
    if (!pix || w > 1024 || h > 1024) return 0;
    uint32_t id = 0;
    gl_gentex(1, &id);
    gl_bindtex(GL_TEXTURE_2D, id);
    gl_teximage(GL_TEXTURE_2D, 0, GL_RGB, (int)w, (int)h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pix);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl_texparamx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (s->ntex < NCACHE) { s->cache_ptr[s->ntex] = tsaddr; s->cache_id[s->ntex] = id; s->ntex++; }
    return id;
}

/* returns 1 if the primitive was drawn on the GPU (the caller skips the software
 * rasterizer), 0 if it should fall through to software. */
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
        gl_clearcolorx(FX, 0, FX, FX);                  /* magenta sentinel (see SENTINEL_*) */
        gl_clear(GL_COLOR_BUFFER_BIT);
    }

    uint32_t tsaddr = desc[0x30 / 4];
    if (!tsaddr) return 0;
    uint32_t *ts = (uint32_t *)tsaddr;
    uint32_t tw = ts[1] + 1, th = ts[2] + 1;
    int wsh = 0, hsh = 0;
    for (uint32_t w = tw; w > 1; w >>= 1) wsh++;
    for (uint32_t h = th; h > 1; h >>= 1) hsh++;

    int16_t pos[6];
    int32_t uv[6];
    const uint32_t voff[3] = { 0x0C, 0x18, 0x24 };
    for (int i = 0; i < 3; i++) {
        uint32_t *vp = (uint32_t *)desc[voff[i] / 4];
        pos[i*2]   = (int16_t)(int32_t)vp[0];
        pos[i*2+1] = (int16_t)(int32_t)vp[1];
        uv[i*2]    = ((int32_t)desc[voff[i]/4 + 1] * 4096) >> wsh;
        uv[i*2+1]  = ((int32_t)desc[voff[i]/4 + 2] * 4096) >> hsh;
    }
    uint32_t id = tex_for(s, tsaddr);
    gl_bindtex(GL_TEXTURE_2D, id);
    gl_vertexptr(2, GL_SHORT, 0, pos);
    gl_texcoordptr(2, GL_FIXED, 0, uv);
    gl_drawarrays(GL_TRIANGLES, 0, 3);
    s->prims++;
    return 1;                                        /* drawn on the GPU; skip software */
}

/* Composite the GPU-rendered world into the game's surface. Called at the 3D->2D
 * boundary (the frame's first a4_fill_alpha, via a4_gl_fill_hook) so the software
 * HUD, drawn afterwards, lands on top -- and masked by the magenta sentinel so
 * only pixels the GPU actually drew replace the software layer (the sky, the
 * player car and the HUD panels the GPU did not draw are left untouched). On the
 * device this is a zero-copy pixmap and there is no readback at all. */
void a4_gl_flush(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC || s->prims == 0 || s->flushed) return;
    s->flushed = 1;

#ifdef A4_NO_BLIT
    /* measurement build: skip the readback+blit to isolate the guest CPU cost
     * (device uses a zero-copy pixmap surface, so there is no blit there). No
     * glFinish either -- it is only needed to make the readback below see a
     * finished frame; here it would just stall the CPU on the GPU and distort
     * the measurement (the GPU runs async). */
    return;
#endif
    gl_finish();                                         /* needed before glReadPixels */
    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;
    if (!bb) return;
    uint32_t *rb = (uint32_t *)A4_GL_READBUF;
    volatile uint16_t *fb = (volatile uint16_t *)bb;
    /* read the pbuffer back in horizontal strips through a small buffer, so the
     * image .bss stays tiny (a full-frame buffer in .bss corrupts the game's
     * load-time layout). GL is bottom-up; flip to the top-left surface. */
    for (int y0 = 0; y0 < VP_H; y0 += STRIP) {
        gl_readpixels(0, y0, VP_W, STRIP, GL_RGBA, GL_UNSIGNED_BYTE, rb);
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

/* The frame's first 2D translucent fill marks the end of the 3D pass: composite
 * the GPU world now, before the HUD paints. Called from patch/fill.c. */
void a4_gl_fill_hook(void *fill_pixels)
{
    (void)fill_pixels;
    a4_gl_flush();
}

/* GL bring-up state, for the on-screen diagnostic in a measurement build:
 *   0 = libGLES_CM did not link (the import never resolved)
 *   1 = linked, but bring-up never entered (no textured prim past the threshold)
 *   2..6 = bring-up reached this EGL step (2 display, 3 init, 4 pbuffer,
 *          5 context) but the next failed
 *   7 = make-current/context fully up and running on the GPU
 * Used by a4_fps.c to colour the gauge and show the step. */
int a4_gl_state(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return 0;               /* libGLES_CM not linked */
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    return 1 + (int)s->bstep;                      /* 1..7 */
}

/* Present detour (once per frame): flush as a fallback in case no 2D fill ran
 * this frame, then start the next frame. */
void a4_gl_frame(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return;
    volatile struct glr *s = (volatile struct glr *)A4_GL_SCRATCH;
    if (s->up != A4_GL_MAGIC) return;
    a4_gl_flush();                                      /* fallback for a HUD-less frame */
    s->prims = 0;
    s->flushed = 0;
    s->frames++;
}
