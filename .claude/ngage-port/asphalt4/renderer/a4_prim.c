/* a4_prim.c -- MBX stage 4: forward every primitive from the dispatcher
 * (+0x300B2) to the GL renderer (a4_gl.c). Returns 1 if the GPU drew the
 * primitive (the trampoline then returns from the dispatcher, skipping the
 * software rasterizer -- the CPU win), 0 to fall through to software.
 */
#include <stdint.h>

int a4_gl_draw_prim(uint32_t *ctx, uint32_t *desc, uint32_t flags);

#ifdef A4_FORCE_SKIP
/* Diagnostic build (no GL at all): skip the software rasteriser for every
 * textured primitive past the intro, and do nothing else. It isolates the skip
 * mechanism from the GL path. On the device:
 *   - 3D goes blank + fps rises  -> the skip works and the software rasteriser
 *     was the cost (so the GPU path can win, once it stops double-drawing).
 *   - 3D still renders           -> the skip jump (+0x300B2 -> +0x300E0) has no
 *     effect on real ARM; that is the bug, not GL.
 * The counter lives at glr.seen (A4_GL_SCRATCH + 0x14, bss-zeroed), reused so
 * no writable global is needed. */
int a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    (void)ctx; (void)desc;
    if (!(flags & 1)) return 0;                      /* non-textured: leave to software */
    volatile uint32_t *seen = (volatile uint32_t *)0x00401014u;
    if (++(*seen) < 50000u) return 0;                /* past the intro video + menu */
    return 1;                                        /* skip software; no GL */
}
#else
int a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    return a4_gl_draw_prim(ctx, desc, flags);
}
#endif
