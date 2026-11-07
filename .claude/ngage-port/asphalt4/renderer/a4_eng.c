/* a4_eng.c -- round 50: the game's engine in C. Each function here replaces the game's own (a veneer
 * at its entry, tools/injector.py: every symbol a4_engv_XXXXX), first as tools/lift.py drafts it --
 * exact, so a lockstep race against the game's own code must match it word for word -- and only then
 * changed (the step at 3x). Nothing here is compiled without A4_ENGINE. */
#ifdef A4_ENGINE
#include "a4_eng.h"
#if defined(A4_ENG3) && (defined(A4_HALFPHYS) || defined(A4_HALFSPEC))
#error "the 3x engine and the half-step machinery share scratch and cannot be built together"
#endif
#ifdef A4_ENG3
/* the step loop's key handler (+25E8, bl at +167AE in +1670E): read before a whole step only -- a press
 * the handler latched on a substep would be gone by the step that acts on it */
extern const int32_t a4_hc_25e8;
#define GAMEFN_PH(ph) ((uint32_t)(uintptr_t)&(ph) + (uint32_t)(ph))
uint64_t a4_eng_keys(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3)
{
    uint32_t r23[2], stk[8] = { 0 };
    if (ENG->magic == ENG_MAGIC && (ENG->cnt % 3u) != 0u) return 0;
    return a4_eng_call(GAMEFN_PH(a4_hc_25e8), r0, r1, r2, r3, (uint32_t)(uintptr_t)stk, r23);
}
#endif
#define ENGV(x) uint64_t a4_engv_##x(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2, \
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7) { return eng_##x(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7); }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wparentheses-equality"
#pragma clang diagnostic ignored "-Wself-assign"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-label"
#ifdef A4_PAINTCHECK
#define a4_engv_29736 a4_paintchk_lifted   /* the lifter's entry renamed: the check below is the painter's entry */
#endif
#include "eng/lifted.c"
#pragma clang diagnostic pop
#ifdef A4_PAINTCHECK
#undef a4_engv_29736
/* Bench (round 50): the lifted HUD painter against the game's own, call for call, on the same input. The
 * frame is composited first (a4_gl_hud; the lifted painter's own call is then a no-op), the rows and
 * columns the sprite can reach (its rectangle on the surface) and the clip words it may set saved, the
 * game's painter run and its result kept, everything put back, the lifted one run, and the two results
 * compared. 0x412D00 (the census a capture carries): [115] calls compared, [116] calls not compared (the
 * rectangle past the buffers: 20 KB at the readback strip, 36 KB at the reorder's arrays -- both free
 * between a frame's composite and its next 3D pass), [117] calls that differed, [120] pixels that
 * differed, [121] calls whose clip words differed. */
extern void a4_paint_orig(void);
uint64_t a4_engv_29736(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2,
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)
{
    volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u;
    uint32_t px = MEM32(r1 + 4u), W = MEM16(r1 + 8u), H = MEM16(r1 + 0xAu);
    int32_t x = (int32_t)r2, y = (int32_t)r3, w = (int16_t)MEM16(s0 + 4u), h = (int16_t)MEM16(s0 + 6u);
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > (int32_t)W ? (int32_t)W : x + w, y1 = y + h > (int32_t)H ? (int32_t)H : y + h;
    a4_gl_hud();
    if (x1 <= x0 || y1 <= y0 || !px || (uint32_t)((x1 - x0) * (y1 - y0) * 2) > 20480u) {
        ck[116]++;
        return eng_29736(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
    }
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)px;
    uint16_t *S = (uint16_t *)0x00403000u, *G = (uint16_t *)0x00415000u;
    uint32_t rw = (uint32_t)(x1 - x0), n = 0, i;
    for (int32_t yy = y0; yy < y1; yy++) for (uint32_t k = 0; k < rw; k++) S[n++] = fb[(uint32_t)yy * W + (uint32_t)x0 + k];
    uint32_t c0 = MEM32(r1 + 0x38u), c1 = MEM32(r1 + 0x3Cu);
    uint32_t stk[8] = { s0, s1, s2, s3, s4, s5, s6, s7 }, r23[2];
    (void)a4_eng_call((uint32_t)(uintptr_t)&a4_paint_orig, r0, r1, r2, r3, (uint32_t)(uintptr_t)stk, r23);
    n = 0;
    for (int32_t yy = y0; yy < y1; yy++) for (uint32_t k = 0; k < rw; k++) G[n++] = fb[(uint32_t)yy * W + (uint32_t)x0 + k];
    uint32_t g0 = MEM32(r1 + 0x38u), g1 = MEM32(r1 + 0x3Cu);
    n = 0;
    for (int32_t yy = y0; yy < y1; yy++) for (uint32_t k = 0; k < rw; k++) fb[(uint32_t)yy * W + (uint32_t)x0 + k] = S[n++];
    MEM32(r1 + 0x38u) = c0; MEM32(r1 + 0x3Cu) = c1;
    uint64_t rr = eng_29736(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
#ifdef A4_PAINTCHECK_MUTANT
    fb[(uint32_t)y0 * W + (uint32_t)x0] ^= 1u;      /* the check's own test: one pixel wrong a call */
#endif
    uint32_t d = 0;
    n = 0;
    for (int32_t yy = y0; yy < y1; yy++) for (uint32_t k = 0; k < rw; k++) { if (fb[(uint32_t)yy * W + (uint32_t)x0 + k] != G[n]) d++; n++; }
    ck[115]++;
    if (d) { ck[117]++; ck[120] += d; }
    if (MEM32(r1 + 0x38u) != g0 || MEM32(r1 + 0x3Cu) != g1) ck[121]++;
    (void)i;
    return rr;
}
#endif
#endif
