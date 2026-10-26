/* a4_span_textured.c -- ARM rewrite of Asphalt 4's textured + light span filler.
 *
 * Same semantics as the shipped Thumb inner loop at code +3357A (reverse-
 * engineered and verified bit-exact by renderer/span_model.py + span_harness.py),
 * but compiled for ARM state so the ~12-value working set stays in registers
 * instead of spilling to the stack 21 times per pixel.
 *
 * Per pixel: affine texture lookup, a per-pixel light gradient (light = light*K1
 * + K2, wrapped) expanded to a grey-ish RGB565 and added to the texel with
 * per-channel saturation, then the coverage byte cleared.
 *
 * Build (position independent, no relocations, like the other port code):
 *   clang --target=armv5te-none-eabi -marm -mtune=arm1136j-s -O2 -ffreestanding \
 *         -fno-builtin -fropi -c a4_span_textured.c -o a4_span_textured.o
 */
#include <stdint.h>

#define K1 0x0019660Du
#define K2 0x3C6EF35Fu

/* Returns the final light accumulator after n steps. The shipped loop leaves
 * this in r7 at the loop exit (+3361C) and the function saves it to [ctx+0x48]
 * for the next span/polygon, so a drop-in replacement must reproduce it, not
 * just the pixels. The pixel writes are byte-identical to the void version. */
uint32_t a4_span_textured(uint16_t *dst, uint8_t *cov, int n,
                      uint32_t u, uint32_t v, uint32_t ustep, uint32_t vstep,
                      uint32_t umask, uint32_t vmask, uint32_t vshift,
                      const uint16_t *tex,
                      uint32_t light, uint32_t thresh, uint32_t lightmask)
{
    for (int i = 0; i < n; i++) {
        light = light * K1 + K2;
        uint32_t li = (light >> 16) & lightmask;
        if (li >= thresh) li -= thresh;
        li &= 0xFFFFu;
        uint32_t lc = (li << 11) | (li << 6) | li;

        uint32_t tu = (u >> 16) & umask;
        uint32_t tv = (v >> 16) & vmask;
        uint32_t texel = tex[(tv << vshift) | tu];

        /* per-channel saturating RGB565 add (verified against the original) */
        uint32_t s  = texel + lc;
        uint32_t x  = texel ^ lc;
        uint32_t c1 = (x ^ s) & 0x20u;
        uint32_t sa = s - c1;
        uint32_t c2 = (sa ^ x) & 0x800u;
        uint32_t s2 = sa - c2;
        uint32_t c3 = s2 & 0x10000u;
        uint32_t carries = c3 | c2 | c1;
        uint32_t mask = carries - (carries >> 4);

        *dst++ = (uint16_t)(s2 | mask);
        *cov++ = 0;
        u += ustep;
        v += vstep;
    }
    return light;
}
