/* a4_span_transparent.c -- ARM rewrite of Asphalt 4's keyed/transparent span
 * filler (shipped Thumb loop at code +33B16, verified bit-exact by
 * renderer/transp_model.py + transp_harness.py).
 *
 * Per pixel: affine texture lookup; colour-key test -- a zero texel is
 * transparent and leaves the destination pixel untouched, a non-zero texel is
 * copied. No light blend (unlike the textured filler), so the whole working set
 * fits in ARM's registers with no per-pixel stack traffic at all.
 *
 * Build (position independent, no relocations):
 *   clang --target=armv5te-none-eabi -marm -mtune=arm1136j-s -O2 -ffreestanding \
 *         -fno-builtin -fropi -c a4_span_transparent.c -o a4_span_transparent.o
 */
#include <stdint.h>

void a4_span_transparent(uint16_t *dst, int n,
                         uint32_t u, uint32_t v, uint32_t ustep, uint32_t vstep,
                         uint32_t umask, uint32_t vmask, uint32_t vshift,
                         const uint16_t *tex)
{
    for (int i = 0; i < n; i++) {
        uint32_t tu = (u >> 16) & umask;
        uint32_t tv = (v >> 16) & vmask;
        uint16_t texel = tex[(tv << vshift) | tu];
        if (texel) *dst = texel;   /* zero texel = transparent: leave dst as-is */
        dst++;
        u += ustep;
        v += vstep;
    }
}
