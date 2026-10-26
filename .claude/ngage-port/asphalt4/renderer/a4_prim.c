/* a4_prim.c -- MBX stage 4: forward every primitive from the dispatcher
 * (+0x300B2) to the GL renderer (a4_gl.c). Returns 1 if the GPU drew the
 * primitive (the trampoline then returns from the dispatcher, skipping the
 * software rasterizer -- the CPU win), 0 to fall through to software.
 */
#include <stdint.h>

int a4_gl_draw_prim(uint32_t *ctx, uint32_t *desc, uint32_t flags);

int a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    return a4_gl_draw_prim(ctx, desc, flags);
}
