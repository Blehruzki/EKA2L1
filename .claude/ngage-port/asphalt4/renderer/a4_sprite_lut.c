/* a4_sprite_lut.c -- the sprite-painter optimization: hoist the per-pixel
 * palette->RGB565 conversion out of the inner loop into a per-blit lookup table.
 *
 * Asphalt 4's sprite blitter (code +29736) reconverts a palette entry (3 colour
 * bytes) to RGB565 for every pixel it paints -- see renderer/palconv_model.py,
 * verified bit-exact against the shipped code (+298D8..+2991E). That result
 * depends only on the pixel's palette index and the sprite's palette, both
 * constant for the whole blit, so it is pure waste per pixel: ~7 memory loads
 * (palette-bank ptr, palette ptr, colorkey, colour-table ptr, 3 colour bytes)
 * plus the shifts, every pixel.
 *
 * a4_build_lut() does that conversion once per blit (<=256 entries). The common
 * menu path -- 8-bit indexed source, no flip, opaque/colour-keyed (blend mode 0,
 * no alpha plane) -- then becomes a4_paint_row_keyed(): index -> lut -> keyed
 * store, structurally identical to the transparent span filler and with no
 * per-pixel palette work at all.
 *
 * SCOPE: this is the inner-loop *shape* of the hot path, not a drop-in for the
 * whole +29736 function. That function is a 5-mode blitter (opaque/keyed,
 * luminance, per-channel RGB via external calls, invert, constant-fill) with
 * X/Y flip and an optional alpha plane; the LUT hoist applies to all modes that
 * read palette colours, but only this common path is a self-contained leaf.
 *
 * Build: clang --target=armv5te-none-eabi -marm -mtune=arm1136j-s -O2 \
 *        -ffreestanding -fno-builtin -fropi -c a4_sprite_lut.c -o a4_sprite_lut.o
 */
#include <stdint.h>

/* Build the per-blit RGB565 lookup table from the sprite's colour table
 * (3 bytes per entry, same packing the shipped code uses). */
void a4_build_lut(uint16_t *lut, const uint8_t *colortab, int ncolors)
{
    for (int i = 0; i < ncolors; i++) {
        uint32_t R = colortab[i * 3 + 0];
        uint32_t G = colortab[i * 3 + 1];
        uint32_t B = colortab[i * 3 + 2];
        lut[i] = (uint16_t)(((B >> 3) << 11) | ((G >> 2) << 5) | (R >> 3));
    }
}

/* Common menu path: 8-bit indexed source row, no flip, opaque/colour-keyed.
 * A source index equal to `colorkey` is transparent and leaves dst untouched;
 * any other index is painted as lut[index]. Pass colorkey < 0 for no key. */
void a4_paint_row_keyed(uint16_t *dst, const uint8_t *src, int n,
                        const uint16_t *lut, int colorkey)
{
    for (int i = 0; i < n; i++) {
        int idx = src[i];
        if (idx != colorkey)
            dst[i] = lut[idx];
    }
}
