/* a4_gl.c -- MBX / PowerVR pbuffer spike (stage 1).
 *
 * Proves injected Asphalt 4 code can link libGLES_CM, bring up an EGL ES 1.1
 * context on an offscreen pbuffer, draw, and read the result back -- the whole
 * risky foundation of the MBX edition, before touching the game's window or
 * its 3D engine. a4_fps_tick() calls a4_gl_spike() once per frame; the spike
 * runs the sequence and records how far it got + the pixels into a fixed scratch
 * a bench MEMSNAP reads. It keeps retrying until it fully succeeds (early frames
 * may run before the graphics stack is ready), then latches.
 *
 * The function pointers come from a4_gl_iat[] (renderer/a4_gl.s), whose slots
 * the Symbian loader fills with resolved libGLES_CM addresses. Indices must
 * match a4_gl.s and the injector's GL_IMPORT_ORDINALS.
 *
 * Build: clang --target=armv5te-none-eabi -marm -O2 -ffreestanding -fno-builtin
 *              -fropi -c a4_gl.c -o a4_gl.o
 */
#include <stdint.h>

#define A4_GL_SCRATCH 0x00401000u       /* kept in sync with injector.py */
#define A4_GL_BB      0x00400700u        /* back-buffer base, stashed by a4_cachebb_tramp (== A4_FPS_BB) */
#define A4_GL_MAGIC   0x6C900DE1u        /* "GL done" */
#define GLW 16
#define GLH 16
/* The display path stripes 2D writes, so (like the fps gauge) the GL result is
 * copied as a CONTIGUOUS run -- which maps to a contiguous screen region at any
 * stride. GL clears to blue (distinct from the gauge's green) so its pixels are
 * unmistakably GL-sourced in the presented frame. */
#define BLIT_OFF 6000                    /* contiguous back-buffer index, clear of the gauge (0..4800) */
#define BLIT_RUN 1200                    /* length of the GL run */

/* EGL / GL constants (Khronos values; EKA2L1 and a real device agree) */
#define EGL_WIDTH            0x3057
#define EGL_HEIGHT           0x3056
#define EGL_NONE             0x3038
#define GL_COLOR_BUFFER_BIT  0x4000
#define GL_RGBA              0x1908
#define GL_UNSIGNED_BYTE     0x1401
#define FIXED_ONE            0x10000     /* 1.0 in 16.16 for glClearColorx */
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
/* The MBX target is fixed-function ES1.1 (config 0b110). EKA2L1's glReadPixels
 * is ES2-only, so A4_GL_READBACK_ES2 builds an ES2 context + attribs purely to
 * confirm actual rendered pixels on the bench; the shipped path is ES1.1. */
#ifdef A4_GL_READBACK_ES2
#define GLES_CONFIG          0x0Eu       /* 0b1110: 32bpp, ES2 */
#else
#define GLES_CONFIG          0x06u       /* 0b110:  32bpp, ES1.1 */
#endif

/* a4_gl_iat[] lives in a4_gl.s; reaching it from C via a plain extern compiles
 * to an un-relocated absolute address (the blob is appended as-is and must be
 * position independent), so the address is resolved pc-relatively in asm. */
extern volatile uint32_t *a4_gl_iat_ptr(void);

typedef int (*fn_get_display)(int);
typedef int (*fn_initialize)(int, int*, int*);
typedef int (*fn_pbuffer)(int, uint32_t, int*);
typedef int (*fn_context)(int, uint32_t, int, int*);
typedef int (*fn_make_current)(int, int, int, int);
typedef void (*fn_viewport)(int, int, int, int);
typedef void (*fn_clearcolorx)(int, int, int, int);
typedef void (*fn_clear)(uint32_t);
typedef void (*fn_finish)(void);
typedef void (*fn_readpixels)(int, int, int, int, uint32_t, uint32_t, void*);

struct a4_gl_state {
    uint32_t magic;       /* A4_GL_MAGIC once the whole sequence succeeded */
    uint32_t step;        /* how far the last attempt got (1..12) */
    uint32_t attempts;
    int32_t  display;
    int32_t  init_ret, major, minor;
    int32_t  surface;
    int32_t  context;
    int32_t  make_current_ret;
    uint32_t pixel0;      /* first pixel read back (expect 0xFFFF0000 = opaque blue RGBA) */
    uint32_t pixels[GLW * GLH];
};

/* Blit the GL-rendered pixels (RGBA8888) into the game's back buffer (RGB565)
 * so the game's own present carries them to the screen -- the backbuffer->screen
 * link the fps gauge already proves. This is the stage-1b proof that GL output
 * reaches the frame; the shipped edition will instead render the 3D into the
 * backbuffer directly (pixmap surface / zero-copy on device). */
static void a4_gl_blit(volatile struct a4_gl_state *g)
{
    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;
    if (!bb) return;
    volatile uint16_t *fb = (volatile uint16_t *)bb;
    for (uint32_t i = 0; i < BLIT_RUN; i++) {
        uint32_t px = g->pixels[i & (GLW * GLH - 1)];     /* GL output, RGBA8888 */
        uint32_t r = px & 0xFF, gc = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
        fb[BLIT_OFF + i] = (uint16_t)(((r >> 3) << 11) | ((gc >> 2) << 5) | (b >> 3));
    }
}

void a4_gl_spike(void)
{
    volatile struct a4_gl_state *g = (volatile struct a4_gl_state *)A4_GL_SCRATCH;
    volatile uint32_t *a4_gl_iat = a4_gl_iat_ptr();
    /* Safe in builds without the GL import: unresolved slots still hold the
     * raw ordinals (< 0x10000); a resolved address is a code/ROM pointer. */
    if (a4_gl_iat[0] < 0x10000u)
        return;
    if (g->magic == A4_GL_MAGIC) {               /* already rendered: keep it on screen */
        a4_gl_blit(g);
        return;
    }
    g->attempts++;

    fn_get_display   eglGetDisplay   = (fn_get_display)   a4_gl_iat[0];
    fn_initialize    eglInitialize   = (fn_initialize)    a4_gl_iat[1];
    fn_pbuffer       eglCreatePbuffer = (fn_pbuffer)      a4_gl_iat[2];
    fn_context       eglCreateContext = (fn_context)      a4_gl_iat[3];
    fn_make_current  eglMakeCurrent  = (fn_make_current)  a4_gl_iat[4];
    fn_viewport      glViewport      = (fn_viewport)      a4_gl_iat[5];
    fn_clearcolorx   glClearColorx   = (fn_clearcolorx)   a4_gl_iat[6];
    fn_clear         glClear         = (fn_clear)         a4_gl_iat[7];
    fn_finish        glFinish        = (fn_finish)        a4_gl_iat[8];
    fn_readpixels    glReadPixels    = (fn_readpixels)    a4_gl_iat[9];

    g->step = 1;
    int disp = eglGetDisplay(0);                 /* EGL_DEFAULT_DISPLAY */
    g->display = disp;
    if (!disp) return;

    g->step = 2;
    int major = 0, minor = 0;
    int ir = eglInitialize(disp, &major, &minor);
    g->init_ret = ir; g->major = major; g->minor = minor;
    if (!ir) return;

    g->step = 3;
    /* pbuffer loop in EKA2L1 breaks only on a literal 0; a real device breaks on
     * EGL_NONE. Terminating EGL_NONE,0,0 satisfies both. */
    int pb[] = { EGL_WIDTH, GLW, EGL_HEIGHT, GLH, EGL_NONE, 0, 0 };
    int surf = eglCreatePbuffer(disp, GLES_CONFIG, pb);
    g->surface = surf;
    if (!surf) return;

    g->step = 4;
#ifdef A4_GL_READBACK_ES2
    int ctx_attribs[] = { EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE, 0, 0 };
    int ctx = eglCreateContext(disp, GLES_CONFIG, 0, ctx_attribs);
#else
    int ctx = eglCreateContext(disp, GLES_CONFIG, 0, 0);
#endif
    g->context = ctx;
    if (!ctx) return;

    g->step = 5;
    int mc = eglMakeCurrent(disp, surf, surf, ctx);
    g->make_current_ret = mc;
    if (!mc) return;

    g->step = 6;  glViewport(0, 0, GLW, GLH);
    g->step = 7;  glClearColorx(0, 0, FIXED_ONE, FIXED_ONE);   /* opaque BLUE (distinct from gauge green) */
    g->step = 8;  glClear(GL_COLOR_BUFFER_BIT);
    g->step = 9;  glFinish();
    g->step = 10; glReadPixels(0, 0, GLW, GLH, GL_RGBA, GL_UNSIGNED_BYTE, (void *)g->pixels);
    g->pixel0 = g->pixels[0];
    g->step = 11;
    g->magic = A4_GL_MAGIC;                      /* success: latch */
    a4_gl_blit(g);                               /* show it this frame too */
}
