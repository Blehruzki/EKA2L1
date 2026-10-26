/* a4_gl.c -- MBX stage 3b: render ONE of the game's real textured triangles on
 * the GPU. a4_prim.c (the +0x300B2 capture) deep-copies one textured triangle's
 * geometry + RGB565 texels into the shared A4_TRI struct; this code, driven once
 * per frame by the fps detour, brings up a GL ES 1.1 context on a small pbuffer,
 * uploads that texture, draws the triangle under an ortho matching its screen
 * bounding box, reads the pbuffer back and blits it (contiguous, 1b-style) into
 * the game's back buffer. So the game's own geometry + texture are rendered by
 * the bench's POWERVR renderer -- the first real piece of Asphalt 4's 3D on the
 * GPU. It renders to a corner, not over the software scene (that composite is
 * stage 4).
 *
 * Function pointers come from a4_gl_iat[] (a4_gl.s), filled by the loader from
 * the libGLES_CM import; indices must match a4_gl.s and injector GL_IMPORT_ORDINALS.
 */
#include <stdint.h>

#define A4_GL_SCRATCH 0x00401000u       /* GL bring-up state */
#define A4_TRI        0x00402000u       /* shared with a4_prim.c */
#define A4_GL_READBUF 0x00405000u       /* 64x64 RGBA readback */
#define A4_GL_BB      0x00400700u       /* back-buffer base (a4_cachebb_tramp) */
#define A4_GL_MAGIC   0x6C900DE2u
#define A4_TRI_MAGIC  0x7121CAB2u
#define PBW 64
#define PBH 64
#define BLIT_OFF 6000                   /* back-buffer index for the blit, clear of the gauge */

/* EGL */
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_NONE 0x3038
#define GLES_CONFIG 6u                  /* 0b110: 32bpp ES1.1 */
/* GL ES 1.1 enums */
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
#define FX 0x10000                      /* 1.0 in 16.16 */

extern volatile uint32_t *a4_gl_iat_ptr(void);

struct a4_tri_draw {
    uint32_t magic, ready, seen;
    int32_t  x[3], y[3], u[3], v[3];
    uint32_t tw, th, tex_addr;
    uint16_t tex[64 * 64];
};
struct a4_gl_state { uint32_t up; uint32_t err; uint32_t texid; uint32_t frames; };

void a4_gl_spike(void)
{
    volatile uint32_t *iat = a4_gl_iat_ptr();
    if (iat[0] < 0x10000u) return;                   /* not a GL build */
    volatile struct a4_tri_draw *tri = (volatile struct a4_tri_draw *)A4_TRI;
    if (tri->magic != A4_TRI_MAGIC || !tri->ready) return;
    volatile struct a4_gl_state *s = (volatile struct a4_gl_state *)A4_GL_SCRATCH;

    /* resolve the calls we use */
    int (*eglGetDisplay)(int)                        = (void *)iat[0];
    int (*eglInitialize)(int, int *, int *)          = (void *)iat[1];
    int (*eglCreatePbuffer)(int, uint32_t, int *)    = (void *)iat[2];
    int (*eglCreateContext)(int, uint32_t, int, int*)= (void *)iat[3];
    int (*eglMakeCurrent)(int, int, int, int)        = (void *)iat[4];
    void (*glViewport)(int, int, int, int)           = (void *)iat[5];
    void (*glClearColorx)(int, int, int, int)        = (void *)iat[6];
    void (*glClear)(uint32_t)                        = (void *)iat[7];
    void (*glFinish)(void)                           = (void *)iat[8];
    void (*glReadPixels)(int,int,int,int,uint32_t,uint32_t,void*) = (void *)iat[9];
    void (*glGenTextures)(int, uint32_t *)           = (void *)iat[10];
    void (*glBindTexture)(uint32_t, uint32_t)        = (void *)iat[11];
    void (*glTexImage2D)(uint32_t,int,int,int,int,int,uint32_t,uint32_t,const void*) = (void *)iat[12];
    void (*glTexParameterx)(uint32_t, uint32_t, int) = (void *)iat[13];
    void (*glEnable)(uint32_t)                       = (void *)iat[14];
    void (*glMatrixMode)(uint32_t)                   = (void *)iat[15];
    void (*glLoadIdentity)(void)                     = (void *)iat[16];
    void (*glOrthox)(int,int,int,int,int,int)        = (void *)iat[17];
    void (*glEnableClientState)(uint32_t)            = (void *)iat[18];
    void (*glVertexPointer)(int,uint32_t,int,const void*)   = (void *)iat[19];
    void (*glTexCoordPointer)(int,uint32_t,int,const void*) = (void *)iat[20];
    void (*glColor4x)(int, int, int, int)            = (void *)iat[21];
    void (*glDrawArrays)(uint32_t, int, int)         = (void *)iat[22];
    void (*glShadeModel)(uint32_t)                   = (void *)iat[23];
    void (*glTexEnvx)(uint32_t, uint32_t, int)       = (void *)iat[24];
    uint32_t (*glGetError)(void)                     = (void *)iat[25];

    if (s->up != A4_GL_MAGIC) {                       /* one-time bring-up + upload */
        int disp = eglGetDisplay(0);            if (!disp) return;
        int maj = 0, min = 0;
        if (!eglInitialize(disp, &maj, &min))   return;
        int pb[] = { EGL_WIDTH, PBW, EGL_HEIGHT, PBH, EGL_NONE, 0, 0 };
        int surf = eglCreatePbuffer(disp, GLES_CONFIG, pb);   if (!surf) return;
        int ctx  = eglCreateContext(disp, GLES_CONFIG, 0, 0); if (!ctx)  return;
        if (!eglMakeCurrent(disp, surf, surf, ctx)) return;

        uint32_t tid = 0;
        glGenTextures(1, &tid);
        glBindTexture(GL_TEXTURE_2D, tid);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (int)tri->tw, (int)tri->th, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (const void *)tri->tex);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
        glShadeModel(GL_FLAT);
        glColor4x(FX, FX, FX, FX);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        s->texid = tid;
        s->up = A4_GL_MAGIC;
    }

    /* per-frame draw of the captured triangle, zoomed to its bounding box */
    int minx = tri->x[0], maxx = tri->x[0], miny = tri->y[0], maxy = tri->y[0];
    for (int i = 1; i < 3; i++) {
        if (tri->x[i] < minx) minx = tri->x[i];
        if (tri->x[i] > maxx) maxx = tri->x[i];
        if (tri->y[i] < miny) miny = tri->y[i];
        if (tri->y[i] > maxy) maxy = tri->y[i];
    }
    if (maxx == minx) maxx++;
    if (maxy == miny) maxy++;

    /* textures are power-of-two, so divide-by-dim is a shift (no libgcc idiv in
     * freestanding). raw U/V are texel<<4, so normalized 16.16 =
     * U*65536/(16*w) = (U*4096) >> log2(w). (U=512 on a 32-wide tex -> 1.0.) */
    int wsh = 0, hsh = 0;
    for (uint32_t w = tri->tw; w > 1; w >>= 1) wsh++;
    for (uint32_t h = tri->th; h > 1; h >>= 1) hsh++;
    int16_t pos[6];
    int32_t uv[6];
    for (int i = 0; i < 3; i++) {
        pos[i * 2]     = (int16_t)tri->x[i];
        pos[i * 2 + 1] = (int16_t)tri->y[i];
        uv[i * 2]     = (tri->u[i] * 4096) >> wsh;
        uv[i * 2 + 1] = (tri->v[i] * 4096) >> hsh;
    }

    glViewport(0, 0, PBW, PBH);
    glClearColorx(FX / 4, 0, FX / 4, FX);            /* dark magenta so the tri stands out */
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthox(minx * FX, maxx * FX, maxy * FX, miny * FX, -FX, FX);  /* Y flipped */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBindTexture(GL_TEXTURE_2D, s->texid);
    glVertexPointer(2, GL_SHORT, 0, pos);
    glTexCoordPointer(2, GL_FIXED, 0, uv);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    s->err = glGetError();

    uint32_t *rb = (uint32_t *)A4_GL_READBUF;
    glReadPixels(0, 0, PBW, PBH, GL_RGBA, GL_UNSIGNED_BYTE, rb);

    uint32_t bb = *(volatile uint32_t *)A4_GL_BB;
    if (bb) {
        volatile uint16_t *fb = (volatile uint16_t *)bb;
        for (int i = 0; i < PBW * PBH; i++) {
            uint32_t px = rb[i];
            uint32_t r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
            fb[BLIT_OFF + i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    s->frames++;
}
