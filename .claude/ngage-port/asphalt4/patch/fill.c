/* Replacement for Asphalt 4's translucent rectangle fill, code +0x2ABAA.
 *
 * The original clips the rectangle, then for every pixel calls the RGB565
 * blend at +0x52F44: spread both colours so green sits in the high half
 * (C = c & 0xF81F | (c & 0x7E0) << 16), form (C - P) * a + 16 * P, mask with
 * 0xF81F0 and 0x7E000000 and shift back. Modulo 2^32 that sum is
 * C * a + P * (16 - a), so C * a is formed once per rectangle and each pixel
 * costs one multiply-accumulate. Same arithmetic, same bits: checked against
 * the original by tools/equiv.py.
 */
#include <stdint.h>

struct surface {
    uint32_t pad0;
    uint16_t *pixels;     /* +4 */
    uint16_t pitch;       /* +8, in pixels */
    uint8_t pad1[0x4c - 0x0a];
    int32_t *width;       /* +0x4C */
    int32_t *height;      /* +0x50 */
};

void a4_fill_alpha(struct surface *s, int x, int y, int w, int h, uint32_t colour, uint32_t alpha)
{
    const int width = *s->width;
    if (width <= x) return;
    const int height = *s->height;
    if (height <= y) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w < 0 || h < 0) return;

    const uint32_t C = (colour & 0xF81Fu) | ((colour & 0x7E0u) << 16);
    const uint32_t ca = C * alpha;
    const uint32_t inv = 16u - alpha;
    const uint32_t pitch = s->pitch;
    uint16_t *row = s->pixels + pitch * (uint32_t)y + (uint32_t)x;

    for (int j = 0; j < h; j++, row += pitch) {
        uint16_t *p = row;
        for (int i = 0; i < w; i++) {
            const uint32_t v = p[i];
            const uint32_t P = (v & 0xF81Fu) | ((v & 0x7E0u) << 16);
            const uint32_t r = ca + P * inv;
            p[i] = (uint16_t)(((r & 0xF81F0u) >> 4) | ((r & 0x7E000000u) >> 20));
        }
    }
}
