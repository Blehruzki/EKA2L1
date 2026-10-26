/* a4_prim.c -- MBX stage 4: forward every primitive from the dispatcher
 * (+0x300B2) to the GL renderer (a4_gl.c), which draws the textured ones on the
 * GPU. The hook fires with r0=ctx, r1=desc, r2=flags; a4_gl_draw_prim filters
 * for textured primitives itself. Changes nothing the software path does (the
 * GPU render is composited at frame end by a4_gl_frame).
 */
#include <stdint.h>

void a4_gl_draw_prim(uint32_t *ctx, uint32_t *desc, uint32_t flags);

void a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    a4_gl_draw_prim(ctx, desc, flags);
}
