/* a4_prim.c -- MBX stage 3b: capture one real textured triangle at the
 * per-primitive dispatcher (+0x300B2) into a shared struct that a4_gl.c draws
 * on the GPU. Picks the first textured primitive (flags bit 0), past the menu,
 * whose texture is small enough to copy (<= 64x64), and deep-copies its
 * geometry + RGB565 texels so the draw is independent of the game's texture
 * lifetime. Changes nothing the game renders.
 *
 * Format (stage 2 + 3a, measured): desc = { ..., 3x {vptr,U,V} @0xC/0x18/0x24,
 * texture*@0x30 }; vptr -> {X@0, Y@4, Z/W@8}; texture = { [1]=w-1, [2]=h-1,
 * [3]=RGB565 pixels, [5]=log2(w) }.
 */
#include <stdint.h>

#define A4_TRI_SCRATCH 0x00402000u      /* kept in sync with injector.py + a4_gl.c */
#define A4_TRI_MAGIC   0x7121CAB2u
#define MAXDIM 64
#define START_AFTER 100000u             /* skip menu/loader; capture a gameplay prim */

struct a4_tri_draw {
    uint32_t magic;
    uint32_t ready;
    uint32_t seen;
    int32_t  x[3], y[3];                 /* screen positions */
    int32_t  u[3], v[3];                 /* raw texel-scale UVs */
    uint32_t tw, th;                     /* texture dimensions */
    uint32_t tex_addr;                   /* source texture pointer (for the record) */
    uint16_t tex[MAXDIM * MAXDIM];       /* copied RGB565 texels */
    /* stage-4 viewport recon: the render context and its draw surface */
    uint32_t ctx_addr;
    uint32_t ctx_dump[32];               /* ctx[0..31] */
    uint32_t surf_addr;                  /* ctx[0x4c] = the surface the 3D renders into */
    uint32_t surf_dump[16];              /* surface[0..15] */
};

void a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    volatile struct a4_tri_draw *t = (volatile struct a4_tri_draw *)A4_TRI_SCRATCH;
    if (t->magic != A4_TRI_MAGIC) { t->magic = A4_TRI_MAGIC; t->ready = 0; t->seen = 0; }
    if (t->ready || !(flags & 1)) return;
    if (++t->seen < START_AFTER) return;

    uint32_t *ts = (uint32_t *)desc[0x30 / 4];       /* texture struct */
    if (!ts) return;
    uint32_t w = ts[1] + 1, h = ts[2] + 1;
    const uint16_t *pix = (const uint16_t *)ts[3];
    if (w > MAXDIM || h > MAXDIM || !pix) return;    /* wait for a copyable one */

    const uint32_t voff[3] = { 0x0C, 0x18, 0x24 };
    for (int i = 0; i < 3; i++) {
        uint32_t *vp = (uint32_t *)desc[voff[i] / 4];
        t->x[i] = (int32_t)vp[0];
        t->y[i] = (int32_t)vp[1];
        t->u[i] = (int32_t)desc[voff[i] / 4 + 1];
        t->v[i] = (int32_t)desc[voff[i] / 4 + 2];
    }
    t->tw = w; t->th = h; t->tex_addr = (uint32_t)pix;
    for (uint32_t i = 0; i < w * h; i++) t->tex[i] = pix[i];
    /* stage-4: record the render context and its draw surface for the viewport */
    t->ctx_addr = (uint32_t)ctx;
    for (int i = 0; i < 32; i++) t->ctx_dump[i] = ctx[i];
    uint32_t *surf = (uint32_t *)ctx[0x4C / 4];
    t->surf_addr = (uint32_t)surf;
    for (int i = 0; i < 16; i++) t->surf_dump[i] = surf ? surf[i] : 0;
    t->ready = 1;
}
