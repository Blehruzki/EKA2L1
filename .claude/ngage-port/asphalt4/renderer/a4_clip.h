/* a4_clip.h -- round 82: the game's triangles clipped to a guard band before the GPU sees them.
 *
 * The owner's video (phone round 81b) shows lines and bands across the picture at a tunnel exit at top speed, drawn
 * and depth-tested (a line passes behind the car); the crack recorder found the game's own near triangles there with
 * screen corners at x -22,181 and +10,558 (w ~1,100): ~140 screen widths off the panel. The MBX clips such a triangle
 * in fixed point, where an error of a thousandth in the cut moves it tens of pixels and texels along an edge that long.
 * Here a triangle with a corner more than CLIPG pixels off the panel is cut on the CPU to the band [-CLIPG, VP_W +
 * CLIPG] x [-CLIPG, VP_H + CLIPG], in screen space, the depth and the texture words interpolated perspective-correctly
 * (1/w and a/w linear in screen space -- as the GPU will interpolate them from W), and drawn as a fan.
 *
 * Pure integer arithmetic, no divide instruction or helper (the blob is freestanding): shift-subtract divides, only for
 * the triangles cut (rare). Shared with tools/cliptest.c, which checks it against a floating-point reference. */
#ifndef A4_CLIP_H
#define A4_CLIP_H
#include <stdint.h>

#define CLIPV_MAX 10            /* 3 corners, +1 a plane, 4 planes: at most 7; room to spare */
struct clipv { int32_t x, y, z, u, v; uint32_t q; };   /* screen x, y in 1/16 pixel (CLIP_FB), the game's depth w (> 0),
                                                        * the texture words; q = 2^30 / w, set by clip_tri and carried
                                                        * through the cuts (round 82f: a vertex cut twice took its second
                                                        * cut's 1/w from the first's depth rounded to a whole unit --
                                                        * 0.5% off at w ~ 100) */
#define CLIP_FB 4               /* the positions' fraction bits: a cut point keeps 1/16 pixel, not rounded to a whole one */

static uint32_t clip_udiv32(uint32_t n, uint32_t d)
{
    uint32_t q = 0, r = 0;
    if (!d) return 0xFFFFFFFFu;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= 1u << i; }
    }
    return q;
}
static uint64_t clip_udiv64(uint64_t n, uint32_t d)
{
    uint64_t q = 0, r = 0;
    if (!d) return ~(uint64_t)0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    return q;
}
/* n / d for a signed n and d > 0, rounded to nearest */
static int64_t clip_sdivr(int64_t n, uint32_t d)
{
    const uint64_t a = (uint64_t)(n < 0 ? -n : n) + (d >> 1);
    const int64_t q = (int64_t)clip_udiv64(a, d);
    return n < 0 ? -q : q;
}
#define CLIP_QS 30              /* q = 2^30 / w */

/* Round 82f: the game's projection (+51E44 / +51F08, a4_gl.c's mesh_scr_i, MBX.md map 2) with its products in 64 bits:
 * the screen position the game means for view coordinates (x, y, z < 0), in 1/16 pixel. The game multiplies in 32 bits
 * and keeps (product + 0x8000) >> 16, so its own screen word is this value modulo 65,536 (to the rounding) -- past
 * +-32,767 px it wraps to the wrong side. rt2: its reciprocal table (gctx + 0x2008C, RT2[z] = floor(2^22 / z)); lo, hi:
 * the values it takes for z <= 0 and z >= 0x8020 (gctx + 0x4010C, + 0x40108). x, y: the near ranges pre-shift by 4-7
 * bits, as the game's (a 32-bit shift: past 2^24 the game's own value is garbage, and so is this one -- clip_agrees
 * then refuses it). */
static void clip_proj64(const int32_t *rt2, int32_t lo, int32_t hi, int32_t F, int32_t cx, int32_t cy, int32_t x,
                        int32_t y, int32_t z, int64_t *sx, int64_t *sy)
{
    const int m = z > -64 ? 7 : z > -128 ? 6 : z > -256 ? 5 : z > -512 ? 4 : 0;
    const int32_t lz = m ? (int32_t)((uint32_t)(-z) << m) : (int32_t)((uint32_t)(-z) & 0x7FFFu);
    int32_t r = lz <= 0 ? lo : lz >= 0x8020 ? hi : rt2[lz];
    r = (int32_t)((uint32_t)r * (uint32_t)F) >> 6;
    const int64_t xx = (int32_t)((uint32_t)x << m), yy = (int32_t)((uint32_t)y << m);
    *sx = (int64_t)cx * 16 + ((xx * r + 0x800) >> 12);
    *sy = (int64_t)cy * 16 - ((yy * r + 0x800) >> 12);
}
/* the 1/16 px position t agrees with the game's screen word modulo 65,536, to its rounding (1 px) */
static int clip_agrees(int64_t t, int32_t word)
{
    const int32_t e = (int16_t)(uint16_t)(uint32_t)((t >> 4) - word);
    return e >= -1 && e <= 1;
}
#define CLIP_UNWRAP_LIM ((int64_t)1 << 29)   /* positions clamped to +-2^29 (33 million px): clip_cut's differences fit int32 */
static int32_t clip_clamp(int64_t t)
{
    return (int32_t)(t > CLIP_UNWRAP_LIM ? CLIP_UNWRAP_LIM : t < -CLIP_UNWRAP_LIM ? -CLIP_UNWRAP_LIM : t);
}

/* the point where the edge P -> Q meets the plane coord == c (P and Q on opposite sides) */
static void clip_cut(const struct clipv *p, const struct clipv *q, int axis, int32_t c, struct clipv *o)
{
    const int32_t pc = axis ? p->y : p->x, qc = axis ? q->y : q->x;
    const int32_t po = axis ? p->x : p->y, qo = axis ? q->x : q->y;
    const uint32_t den = (uint32_t)(qc > pc ? qc - pc : pc - qc);
    const uint32_t num = (uint32_t)(c > pc ? c - pc : pc - c);
    const int32_t oc = (int32_t)(po + clip_sdivr((int64_t)(qo - po) * num, den));
    if (axis) { o->y = c; o->x = oc; } else { o->x = c; o->y = oc; }
    /* t in 2^-30 (num to 2^30 -- positions to +-2^29: num << 30 under 2^61); q = 2^30 / w is linear in screen space; an
     * attribute a at the cut is a_P + (a_Q - a_P) * s, s = t * q_Q / q_N -- the perspective-corrected fraction (every
     * product under 2^62) */
    const uint64_t t30 = num >= den ? (uint64_t)1 << 30 : clip_udiv64((uint64_t)num << 30, den);
    const int64_t qp = p->q, qq = q->q;
    int64_t qn = qp + (((qq - qp) * (int64_t)t30) >> 30);
    if (qn < 1) qn = 1;
    o->q = (uint32_t)qn;
    o->z = (int32_t)clip_udiv32((1u << CLIP_QS) + ((uint32_t)qn >> 1), (uint32_t)qn);   /* rounded */
    uint64_t s30 = clip_udiv64(t30 * (uint64_t)qq, (uint32_t)qn);
    if (s30 > ((uint64_t)1 << 30)) s30 = (uint64_t)1 << 30;
    o->u = (int32_t)(p->u + ((((int64_t)q->u - p->u) * (int64_t)s30) >> 30));
    o->v = (int32_t)(p->v + ((((int64_t)q->v - p->v) * (int64_t)s30) >> 30));
}
/* keep the part with coord >= c (ge) or <= c (!ge); returns the vertex count */
static int clip_plane(const struct clipv *in, int n, struct clipv *out, int axis, int32_t c, int ge)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const struct clipv *p = &in[i], *q = &in[i + 1 < n ? i + 1 : 0];   /* no %: no divide helper in the blob */
        const int32_t pc = axis ? p->y : p->x, qc = axis ? q->y : q->x;
        const int pin = ge ? pc >= c : pc <= c, qin = ge ? qc >= c : qc <= c;
        if (pin) out[m++] = *p;
        if (pin != qin && m < CLIPV_MAX) clip_cut(p, q, axis, c, &out[m++]);
        if (m >= CLIPV_MAX) break;
    }
    return m;
}
/* the triangle cut to [x0, x1] x [y0, y1] (all in 1/16 pixel): the polygon into out (CLIPV_MAX), its count (0: none) */
static int clip_tri(const struct clipv *tri, int32_t x0, int32_t y0, int32_t x1, int32_t y1, struct clipv *out)
{
    struct clipv a[CLIPV_MAX], b[CLIPV_MAX];
    int n = 3;
    for (int i = 0; i < 3; i++) { a[i] = tri[i]; a[i].q = clip_udiv32(1u << CLIP_QS, (uint32_t)(a[i].z > 0 ? a[i].z : 1)); }
    n = clip_plane(a, n, b, 0, x0, 1); if (n < 3) return 0;
    n = clip_plane(b, n, a, 0, x1, 0); if (n < 3) return 0;
    n = clip_plane(a, n, b, 1, y0, 1); if (n < 3) return 0;
    n = clip_plane(b, n, out, 1, y1, 0); if (n < 3) return 0;
    return n;
}
#endif
