/* a4_prim.c -- MBX stage 3a: capture the game's textured-triangle data at the
 * per-primitive dispatcher (+0x300AE) to verify the stage-2 format against live
 * data before emitting any GL. a4_prim_capture(ctx, desc, flags) records the
 * first few textured primitives (flags bit 0) into a fixed scratch a bench
 * MEMSNAP reads; it changes nothing the game sees.
 *
 * Format pinned in stage 2 (MBX.md):
 *   desc:   v0*@0x0C, v1*@0x18, v2*@0x24, texture*@0x30, flags selects rasterizer
 *   vertex: position*@0x00, U@0x04, V@0x08
 *   position: X@0x00, Y@0x04, Z/W@0x08  (screen-space, post-projection)
 *
 * Build: clang --target=armv5te-none-eabi -marm -O2 -ffreestanding -fno-builtin
 *              -fropi -c a4_prim.c -o a4_prim.o
 */
#include <stdint.h>

#define A4_PRIM_SCRATCH 0x00401800u     /* kept in sync with injector.py */
#define A4_PRIM_MAGIC   0x7121CAB1u
#define NCAP 4
#define START_AFTER 100000u             /* skip the menu/loader; capture gameplay prims */

/* Raw dump so the struct layout is decoded by inspection, not assumption:
 * desc[0..15], then for each of the 3 vertex pointers (desc+0xC/0x18/0x24):
 * the wrapper's [0..7] and the struct its [0] points at ([0..7]). */
struct a4_rawtri {
    uint32_t flags, desc_addr;
    uint32_t desc[16];       /* desc[0..15]: vtable,flags,?,{vptr,U,V}x3,texture,... */
    uint32_t vtx[3][8];      /* each vptr -> { X, Y, Z/W, ... } */
    uint32_t texs[16];       /* the texture struct at desc[0x30] */
};
struct a4_prim_cap {
    uint32_t magic, count, seen;
    struct a4_rawtri tri[NCAP];
};

static void dump8(volatile uint32_t *dst, uint32_t addr)
{
    uint32_t *p = (uint32_t *)addr;
    for (int i = 0; i < 8; i++) dst[i] = p ? p[i] : 0;
}

void a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    volatile struct a4_prim_cap *c = (volatile struct a4_prim_cap *)A4_PRIM_SCRATCH;
    if (c->magic != A4_PRIM_MAGIC) { c->magic = A4_PRIM_MAGIC; c->count = 0; c->seen = 0; }
    if (!(flags & 1)) return;                        /* textured only */
    c->seen++;
    if (c->seen < START_AFTER || c->count >= NCAP) return;

    volatile struct a4_rawtri *t = &c->tri[c->count];
    t->flags = flags;
    t->desc_addr = (uint32_t)desc;
    for (int i = 0; i < 16; i++) t->desc[i] = desc[i];
    const uint32_t voff[3] = { 0x0C, 0x18, 0x24 };    /* {vptr,U,V} triples */
    for (int i = 0; i < 3; i++)
        dump8(t->vtx[i], desc[voff[i] / 4]);          /* vptr -> {X,Y,Z/W,...} */
    uint32_t *ts = (uint32_t *)desc[0x30 / 4];        /* texture struct */
    for (int i = 0; i < 16; i++) t->texs[i] = ts ? ts[i] : 0;
    c->count++;
}
