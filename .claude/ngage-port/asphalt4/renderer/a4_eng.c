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
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7) \
    { return engr_##x(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7, (uint32_t)(uintptr_t)__builtin_return_address(0)); }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wparentheses-equality"
#pragma clang diagnostic ignored "-Wself-assign"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-label"
#if defined(A4_PAINTCHECK) || defined(A4_PAINTFAST)
#define a4_engv_29736 a4_paint_lifted_entry   /* the lifter's entry renamed: the painter's entry is below */
#endif
#if defined(A4_PAINTFAST) && !(defined(A4_ENG_29736) || defined(A4_ENG_ALL))
#error "A4_PAINTFAST replaces the lifted painter: build it with A4_ENG_29736 (or A4_ENG_ALL)"
#endif
#ifdef A4_LIFTCHECK
#if defined(A4_ALPHAHIST) || defined(A4_LEANCHECK) || defined(A4_DEFERCHECK) || defined(A4_REOCHECK) || defined(A4_BLITCHECK) || defined(A4_STRIPCHECK)
#error "A4_LIFTCHECK counts in census words 100-114, which that check uses too"
#endif
#ifndef A4_MESH
#error "A4_LIFTCHECK lists the mesh routine's inserts at a4_mesh_insert (A4_MESH)"
#endif
#if !defined(A4_NODIRECT_349F8) || !defined(A4_NODIRECT_349B0)
#error "A4_LIFTCHECK checks at the veneers: build it with A4_NODIRECT_349F8 and A4_NODIRECT_349B0 (lifted callers, +CE8C's among them, then call through them)"
#endif
#define a4_engv_349F8 a4_lck_l349F8
#define a4_engv_349B0 a4_lck_l349B0
#endif
#ifdef A4_FASTXF
/* +345E4 by hand (round 50): the mesh routine's vertex transform and outcodes. For each 40-byte vertex from
 * r1 to r2: z = (m2 x + m5 y + m8 z) >> 14 + m11 (the matrix at cam+0x3C; model x y z at +0x18), outcode
 * 0x20 when z > lit (+349A4) or z <= lit (+349A8) -- then only z is stored -- else x and y (rows 0 and 1),
 * 0x10 when z > -64, and the four planes (cam+0xC/+0x10 and +0x18/+0x1C on y, +0x20/+0x28 and +0x2C/+0x34
 * on x, each with z) over 2^18 give 1, 2, 4, 8; stored: x +0xC, y +0x10, z +0x14, the outcode +0x24, +8
 * cleared; cam[0] = 0xFF and every outcode; r0 the OR of them. The game's code reads the matrix and planes
 * again for every vertex (its stores might reach them); here they are read once, when the vertices reach
 * neither (else the lifted one runs). Products wrap as MULS does. Checked by A4_LIFTCHECK (the mesh routine
 * whole against the game's). */
static uint64_t engi_345E4(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t);
static uint64_t eng_h_345e4(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp, uint32_t *r23)
{
    const uint32_t cam = r0, vb = r1, ve = r2, m = L32(cam + 0x3Cu);
    if (vb > ve || !((ve <= cam || vb >= cam + 0x38u) && (ve <= m || vb >= m + 0x30u)) || ((vb | m | cam) & 3u))
        return engi_345E4(r0, r1, r2, r3, L32(sp), L32(sp + 4u), L32(sp + 8u), L32(sp + 12u), L32(sp + 16u), L32(sp + 20u),
                          L32(sp + 24u), L32(sp + 28u), r23, GAMEFN(0x34A10u));
    const int32_t m0 = (int32_t)L32(m), m1 = (int32_t)L32(m + 4u), m2 = (int32_t)L32(m + 8u), m3 = (int32_t)L32(m + 0xCu),
                  m4 = (int32_t)L32(m + 0x10u), m5 = (int32_t)L32(m + 0x14u), m6 = (int32_t)L32(m + 0x18u), m7 = (int32_t)L32(m + 0x1Cu),
                  m8 = (int32_t)L32(m + 0x20u), m9 = (int32_t)L32(m + 0x24u), m10 = (int32_t)L32(m + 0x28u), m11 = (int32_t)L32(m + 0x2Cu);
    const uint32_t a1 = L32(cam + 0xCu), b1 = L32(cam + 0x10u), a2 = L32(cam + 0x18u), b2 = L32(cam + 0x1Cu),
                   a3 = L32(cam + 0x20u), b3 = L32(cam + 0x28u), a4 = L32(cam + 0x2Cu), b4 = L32(cam + 0x34u);
    const int32_t zfar = (int32_t)GAMEW(0x349A4u), znear = (int32_t)GAMEW(0x349A8u);
    uint32_t any = 0, all = 0xFFu;
    uint32_t o1 = vb, o2 = ve, o3 = r3;             /* r1-r3 as the game's leaves them (the routine returns them when
                                                     * every vertex shares an outcode bit: liftfuzz.py caught a 0) */
    for (uint32_t v = vb; v != ve; v += 0x28u) {
        const uint32_t vx = L32(v + 0x18u), vy = L32(v + 0x1Cu), vz = L32(v + 0x20u);
        const uint32_t zp = (uint32_t)m2 * vx + (uint32_t)m5 * vy;
        const int32_t z = ((int32_t)(zp + (uint32_t)m8 * vz) >> 14) + m11;
        uint32_t oc;
        if (z > zfar || z <= znear) { oc = 0x20u; o1 = (uint32_t)zfar; o2 = (uint32_t)m8; o3 = zp; }
        else {
            const int32_t x = ((int32_t)((uint32_t)m0 * vx + (uint32_t)m3 * vy + (uint32_t)m6 * vz) >> 14) + m9;
            const int32_t y = ((int32_t)((uint32_t)m1 * vx + (uint32_t)m4 * vy + (uint32_t)m7 * vz) >> 14) + m10;
            oc = z > -64 ? 0x10u : 0u;
            if ((int32_t)(a1 * (uint32_t)y + b1 * (uint32_t)z) > 0x40000) oc |= 1u;
            if ((int32_t)(a2 * (uint32_t)y + b2 * (uint32_t)z) > 0x40000) oc |= 2u;
            if ((int32_t)(a3 * (uint32_t)x + b3 * (uint32_t)z) > 0x40000) oc |= 4u;
            const uint32_t p4 = a4 * (uint32_t)x + b4 * (uint32_t)z;
            if ((int32_t)p4 > 0x40000) oc |= 8u;
            o1 = (int32_t)p4 > 0x40000 ? 8u : p4; o2 = (uint32_t)x; o3 = 0x40000u;
            L32(v + 0xCu) = (uint32_t)x; L32(v + 0x10u) = (uint32_t)y;
        }
        any |= oc; all &= oc;
        L32(v + 0x24u) = oc; L32(v + 0x14u) = (uint32_t)z; L32(v + 8u) = 0;
    }
    L32(cam) = all;
    r23[0] = o2; r23[1] = o3;
    return ((uint64_t)o1 << 32) | any;
}
#endif
#define sp ((uint32_t)(uintptr_t)spp)     /* the lifted code's stack pointer as a value (lift.py: spp the pointer) */
#include "eng/lifted.c"
#undef sp
#pragma clang diagnostic pop
#ifdef A4_CECHECK
#if defined(A4_LIFTCHECK) || defined(A4_BBCHECK) || defined(A4_MEMOCHECK)
#error "A4_CECHECK shares the census words 100-104 and 0x460000 with that check"
#endif
/* bench (round 50): +CE8C's loops by hand against the lifted ones (a4_eng.h ce_verts, ce_faces) */
static void ce_check(int which)
{
    volatile uint32_t *k = (volatile uint32_t *)0x00460000u, *ck = (volatile uint32_t *)0x00412D00u;
    if (k[0] != 1u + (uint32_t)which) return;
    k[0] = 0;
    uint32_t lo = k[1], n = k[2], d = 0;
    for (uint32_t i = 0; i < n / 4u; i++) d += L32(lo + 4u * i) != ((uint32_t *)0x00473F00u)[i];
#ifdef A4_CECHECK_MUTANT
    d++;
#endif
    ck[100 + 2 * which]++;
    if (d) ck[101 + 2 * which]++;
}
#endif
#ifdef A4_BBCHECK
/* bench (round 50): bb_row2 (a4_eng.h) against the lifted game walk, row for row -- census words [100] rows
 * compared, [101] not (over 1,024 pixels), [102] rows that differed, [103] pixels, [104] rows whose u
 * accumulator, x or end pixel differed */
#ifdef A4_LIFTCHECK
#error "A4_BBCHECK and A4_LIFTCHECK share the census words 100-104 and 0x460000"
#endif
static void bb_row2_check(uint32_t r0, uint32_t r12, uint8_t *spp)
{
    volatile uint32_t *bk = (volatile uint32_t *)0x00460000u, *ck = (volatile uint32_t *)0x00412D00u;
    if (!bk[0]) return;
    bk[0] = 0;
    const uint16_t *bB = (const uint16_t *)0x00460840u;
    uint32_t d0 = bk[1], n = bk[2], dd = 0;
    for (uint32_t k = 0; k < n; k++) dd += L16(d0 + 2u * k) != bB[k];
    ck[100]++;
    if (dd) { ck[102]++; ck[103] += dd; }
    if (L32(spp + 0x24u) != bk[3] || r12 != bk[4] || r0 != bk[5]) ck[104]++;
}
#endif
#ifdef A4_LIFTCHECK
#undef a4_engv_349F8
#undef a4_engv_349B0
/* Bench (round 50): the lifted 3D front end against the game's own, call for call, on the same input, as
 * A4_PAINTCHECK does the painter. Build with A4_SCRATCH_SIZE=0x7F000: the check's buffers are 0x460000-
 * 0x480000 -- [0] the insert list's mode (1 the game's run, 2 the lifted run: a4_mesh_insert lists
 * instead of inserting), [1] [2] each run's inserts, the two lists at 0x460010 and 0x464010 (2,048
 * each), the snapshots at 0x468100 and 0x473F00.
 * The mesh routine +349F8 (r0 the camera, r1 the vertices, r2 their count, r3 the faces, s0 their
 * count; with it +345E4, +523A2/C4/E6 and +3118E, inline in the lifted one) writes the camera's first
 * word, the vertices' projected words and outcodes (+345E4), the faces' flags (+4) and inserts each face
 * it keeps (+34ADC). All three saved, the game's run, its result kept, everything put back, the lifted
 * run, compared; then the lifted run's inserts made. Census words: [100] calls compared, [101] not
 * (too large, or inside another check), [102] calls whose memory differed, [103] bytes that differed,
 * [104] calls whose inserts differed, [105] calls whose r0 differed (no caller reads it: only noted).
 * The insert +349B0 (r0 the camera, r1 the face; the face's depth key, its vtable's first entry, inline
 * when it is +3121E) links the face into one of 4,096 slots (camera+0x5C) and marks one of 64 bytes
 * (camera+0x405C): run once, the slot found by the face it now holds, put back, the lifted run, compared.
 * One call in 16 (the slot search reads the whole table, three times): [106] calls compared, [107] not
 * (the face already in the table, or inside the mesh check), [108] calls that differed, [110] calls
 * neither run inserted (the key past the table), [111] calls left to the lifted one alone (the 15 in
 * 16). [3] counts the calls. */
#define LCK_SNAP 0xBE00u
static uint32_t lck_copy(uint8_t *dst, uint32_t a, uint32_t n, int out)   /* n a multiple of 4, a word-aligned */
{
    uint32_t *p = (uint32_t *)(uintptr_t)a, *q = (uint32_t *)dst;
    if (out) { for (uint32_t i = 0; i < n / 4u; i++) q[i] = p[i]; }
    else { for (uint32_t i = 0; i < n / 4u; i++) p[i] = q[i]; }
    return n;
}
static uint32_t lck_diff(const uint8_t *ref, uint32_t a, uint32_t n)       /* bytes that differ */
{
    const uint32_t *p = (const uint32_t *)(uintptr_t)a, *q = (const uint32_t *)ref; uint32_t d = 0;
    for (uint32_t i = 0; i < n / 4u; i++) {
        uint32_t x = p[i] ^ q[i];
        if (x) d += ((x & 0xFFu) != 0) + ((x & 0xFF00u) != 0) + ((x & 0xFF0000u) != 0) + ((x >> 24) != 0);
    }
    return d;
}
extern void a4_orig_349F8(void), a4_orig_349B0(void);
uint64_t a4_engv_349F8(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2,
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)
{
    volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u, *lk = (volatile uint32_t *)0x00460000u;
    uint32_t vb = r2 * 0x28u, fb = s0 * 0x34u;
    if (lk[0] || r2 > 0x1000u || s0 > 0x1000u || 4u + vb + fb > LCK_SNAP || ((r0 | r1 | r3) & 3u)) {
        ck[101]++;
        return eng_349F8(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
    }
    uint8_t *A = (uint8_t *)0x00468100u, *B = (uint8_t *)0x00473F00u;
    uint32_t stk[8] = { s0, s1, s2, s3, s4, s5, s6, s7 }, r23[2];
    lck_copy(A, r0, 4u, 1); lck_copy(A + 4u, r1, vb, 1); lck_copy(A + 4u + vb, r3, fb, 1);
    lk[1] = 0; lk[0] = 1u;
    uint64_t ro = a4_eng_call((uint32_t)(uintptr_t)&a4_orig_349F8, r0, r1, r2, r3, (uint32_t)(uintptr_t)stk, r23);
    lk[0] = 0;
    lck_copy(B, r0, 4u, 1); lck_copy(B + 4u, r1, vb, 1); lck_copy(B + 4u + vb, r3, fb, 1);
    lck_copy(A, r0, 4u, 0); lck_copy(A + 4u, r1, vb, 0); lck_copy(A + 4u + vb, r3, fb, 0);
    lk[2] = 0; lk[0] = 2u;
    uint64_t rl = eng_349F8(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
    lk[0] = 0;
#ifdef A4_LIFTCHECK_MUTANT
    if (vb) MEM8(r1 + 0x10u) ^= 1u;                 /* the check's own test: one bit wrong a call */
#endif
    uint32_t d = lck_diff(B, r0, 4u) + lck_diff(B + 4u, r1, vb) + lck_diff(B + 4u + vb, r3, fb);
    uint32_t na = lk[1], nb = lk[2], bad = na != nb;
    volatile uint32_t *LA = (volatile uint32_t *)0x00460010u, *LB = (volatile uint32_t *)0x00464010u;
    for (uint32_t i = 0; !bad && i < nb && i < 2048u; i++) bad = LA[2u * i] != LB[2u * i] || LA[2u * i + 1u] != LB[2u * i + 1u];
    ck[100]++;
    if (d) { ck[102]++; ck[103] += d; }
    if (bad) ck[104]++;
    if ((uint32_t)ro != (uint32_t)rl) ck[105]++;
    for (uint32_t i = 0; i < nb && i < 2048u; i++)
        a4_mesh_insert((uint32_t *)(uintptr_t)LB[2u * i], (uint32_t *)(uintptr_t)LB[2u * i + 1u]);
    return rl;
}
static int32_t lck_slot(uint32_t cam, uint32_t face)
{
    int32_t k = -1;
    for (uint32_t i = 0; i < 4096u; i++)
        if (MEM32(cam + 0x5Cu + 4u * i) == face) { if (k >= 0) return -2; k = (int32_t)i; }
    return k;
}
uint64_t a4_engv_349B0(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2,
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)
{
    volatile uint32_t *ck = (volatile uint32_t *)0x00412D00u, *lk = (volatile uint32_t *)0x00460000u;
    if ((++lk[3] & 15u) != 0) { ck[111]++; return eng_349B0(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7); }
    if (lk[0] || ((r0 | r1) & 3u) || lck_slot(r0, r1) != -1) { ck[107]++; return eng_349B0(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7); }
    uint8_t f0[64], fa[64];
    uint32_t stk[8] = { s0, s1, s2, s3, s4, s5, s6, s7 }, r23[2];
    uint32_t l0 = MEM32(r1 + 8u);
    lck_copy(f0, r0 + 0x405Cu, 64u, 1);
    (void)a4_eng_call((uint32_t)(uintptr_t)&a4_orig_349B0, r0, r1, r2, r3, (uint32_t)(uintptr_t)stk, r23);
    int32_t ka = lck_slot(r0, r1);
    uint32_t la = MEM32(r1 + 8u);
    lck_copy(fa, r0 + 0x405Cu, 64u, 1);
    if (ka >= 0) MEM32(r0 + 0x5Cu + 4u * (uint32_t)ka) = la;
    MEM32(r1 + 8u) = l0; lck_copy(f0, r0 + 0x405Cu, 64u, 0);
    uint64_t rl = eng_349B0(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
#ifdef A4_LIFTCHECK_MUTANT
    MEM8(r0 + 0x405Cu) ^= 0x80u;
#endif
    int32_t kb = lck_slot(r0, r1);
    ck[106]++;
    if (ka != kb || MEM32(r1 + 8u) != la || lck_diff(fa, r0 + 0x405Cu, 64u)) ck[108]++;
    if (ka == -1 && kb == -1) ck[110]++;
    return rl;
}
#endif
#if defined(A4_PAINTCHECK) || defined(A4_PAINTFAST)
#undef a4_engv_29736
#endif
#ifdef A4_PAINTFAST
/* The HUD sprite painter +29736 by hand (round 50), the lifted one's behaviour exactly -- clip, the flips,
 * the walk, every pixel's colour, alpha and store -- with what the game's code works out again for every
 * pixel worked out once a call: the sprite's format flags, its palette (the palette bank's entry, its
 * colour key, its colour table), the global alpha's scale of each of the 16 alpha levels, the effect mode,
 * the blend's masks; and for a paletted sprite of enough pixels each index's colour (565, the mode's effect
 * on it) in a table. Mode 2 (each channel through three divisions) is left to the lifted painter whole.
 * Checked against the game's own painter call for call by A4_PAINTCHECK. */
static inline uint32_t pf_mode(uint32_t c, uint32_t mode, uint32_t lit4)
{
    if (mode == 1u) {                                   /* +2996E: a grey from the three channels */
        uint32_t r = ((c << 16) >> 27) * 0x26u >> 7, g = ((c << 21) >> 27) * 0x4Cu >> 7, b = ((c << 27) >> 27) * 14u >> 7;
        uint32_t v = r + g + b;
        c = (v << 11) | ((v << 27) >> 21) | ((v << 27) >> 27);
        return (uint32_t)(int32_t)(int16_t)c;
    }
    if (mode == 3u) {                                   /* +29A4A: the channels inverted */
        uint32_t a = ~(c >> 11), b = ~(c >> 6), d = ~c;
        c = (a << 11) | ((b << 27) >> 21) | ((d << 27) >> 27);
        return (uint32_t)(int32_t)(int16_t)c;
    }
    if (mode == 4u) return lit4;                        /* +29960: one colour */
    return c;
}
struct pfw {
    uint32_t sx0, sx1, sy0, sy1, xdir, ydir, rowoff, dst, dstepb, rowskipb, sw;
    uint32_t isrc, csrc, asrc, key, ct, mode, lit4, m1, m3;
    const int32_t *AT;
};
/* the walk for one sprite format: PAL paletted, NIB 4-bit indices, APL an alpha plane, MODE the effect
 * applied (a paletted colour gets it when its index is first met) -- each a constant where it is called,
 * so each format is one loop with nothing in it the format does not need. The colours of a paletted
 * sprite in a table filled as the walk meets each index (the colour table read only where the game's
 * loop reads it); 16-bit entries and a bit an index, ~560 bytes of the thread's 20 KB stack (0x5000). */
static inline __attribute__((always_inline)) void pf_walk(const struct pfw *p, const int PAL, const int NIB, const int APL, const int MODE)
{
    uint16_t lut[256];
    uint32_t lutv[8];
    if (PAL) for (uint32_t k = 0; k < (NIB ? 1u : 8u); k++) lutv[k] = 0;
    const uint32_t sx1 = p->sx1, xdir = p->xdir, dstepb = p->dstepb, key = p->key, ct = p->ct;
    const uint32_t isrc = p->isrc, csrc = p->csrc, asrc = p->asrc, mode = p->mode, lit4 = p->lit4, m1 = p->m1, m3 = p->m3;
    const int32_t *AT = p->AT;
    const int32_t a15 = AT[15];
    uint32_t sy = p->sy0, rowoff = p->rowoff, dst = p->dst;
    do {
        uint32_t sx = p->sx0;
        while (sx != sx1) {
            const uint32_t i = rowoff + sx;
            uint32_t c;
            int32_t al;
            if (PAL) {
                const uint32_t idx = NIB ? (L8(isrc + (uint32_t)((int32_t)i >> 1)) >> ((1u & ~i) << 2)) & 15u : L8(isrc + i);
                if ((lutv[idx >> 5] >> (idx & 31u)) & 1u) c = mode == 4u ? lit4 : (uint32_t)(int32_t)(int16_t)lut[idx];
                else {
                    c = pf_mode((uint32_t)(int32_t)(int16_t)(((L8(ct + 3u * idx + 2u) >> 3) << 11) | ((L8(ct + 3u * idx + 1u) >> 2) << 5) | (L8(ct + 3u * idx) >> 3)), mode, lit4);
                    lut[idx] = (uint16_t)c; lutv[idx >> 5] |= 1u << (idx & 31u);
                }
                al = idx == key ? 0 : a15;
            } else {
                c = (uint32_t)(int32_t)LS16(csrc + 2u * i);
                if (MODE) c = pf_mode(c, mode, lit4);
                al = a15;
            }
            if (APL) al = AT[(L8(asrc + (uint32_t)((int32_t)i >> 1)) >> ((1u & ~i) << 2)) & 15u];
            if (al == 15) L16(dst) = (uint16_t)c;
            else if (al != 0) {
                const uint32_t dv = L16(dst);
                const uint32_t c1 = (c & m1) | ((c & 0x7E0u) << 16), d1 = (dv & m1) | ((dv & 0x7E0u) << 16);
                const uint32_t t = (c1 - d1) * (uint32_t)al + (d1 << 4);
                L16(dst) = (uint16_t)(((t & m3) >> 4) | ((t & 0x7E000000u) >> 20));
            }
            dst += dstepb; sx += xdir;
        }
        dst += p->rowskipb;
        rowoff += p->sw * p->ydir;
        sy += p->ydir;
    } while (sy != p->sy1);
}
static uint64_t a4_paint_fast(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2,
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)
{
    const uint32_t o = r0, surf = r1, d = s0, fl = s1;
    a4_gl_hud();                                        /* edits.py ALWAYS at +2973A */
    int32_t sx0 = 0, sy0 = 0, w = LS16(d + 4u), h = LS16(d + 6u), sx1 = w, sy1 = h;
    const uint32_t px = L32(surf + 4u), W = L16(surf + 8u), H = L16(surf + 0xAu);
    if (LS16(surf + 0x38u) == -1) { L16(surf + 0x38u) = 0; L16(surf + 0x3Au) = 0; L16(surf + 0x3Cu) = (uint16_t)W; L16(surf + 0x3Eu) = (uint16_t)H; }
    const int32_t cx0 = LS16(surf + 0x38u), cx1 = (int32_t)((uint32_t)cx0 + (uint32_t)(int32_t)LS16(surf + 0x3Cu));
    const int32_t cy0 = LS16(surf + 0x3Au), cy1 = (int32_t)((uint32_t)cy0 + (uint32_t)(int32_t)LS16(surf + 0x3Eu));
    int32_t x = (int32_t)r2, y = (int32_t)r3;
    uint32_t rr1 = (uint32_t)cy1;
    if (x < cx0) { sx0 = (int32_t)((uint32_t)cx0 - (uint32_t)x); w = (int32_t)((uint32_t)w - (uint32_t)sx0); x = cx0; }
    if (y < cy0) { sy0 = (int32_t)((uint32_t)cy0 - (uint32_t)y); h = (int32_t)((uint32_t)h - (uint32_t)sy0); y = cy0; }
    {   int32_t e = (int32_t)((uint32_t)x + (uint32_t)w);
        if (e > cx1) { e = (int32_t)((uint32_t)e - (uint32_t)cx1); w = (int32_t)((uint32_t)w - (uint32_t)e); sx1 = (int32_t)((uint32_t)sx1 - (uint32_t)e); } }
    {   int32_t e = (int32_t)((uint32_t)y + (uint32_t)h);
        if (e > cy1) { e = (int32_t)((uint32_t)e - (uint32_t)cy1); h = (int32_t)((uint32_t)h - (uint32_t)e); sy1 = (int32_t)((uint32_t)sy1 - (uint32_t)e); rr1 = (uint32_t)sy1; } }
    if (w <= 0 || h <= 0) return ((uint64_t)rr1 << 32) | (uint32_t)sy0;
    uint32_t dstep = 1u, rowskip = W - (uint32_t)w;
    if (fl & 1u) { uint32_t t = (uint32_t)(int32_t)LS16(d + 4u) - (uint32_t)sx0; sx1 = (int32_t)(int16_t)(t - (uint32_t)w) - 1; sx0 = (int32_t)(t - 1u); }
    if (fl & 2u) { uint32_t t = (uint32_t)(int32_t)LS16(d + 6u) - (uint32_t)sy0; sy1 = (int32_t)(int16_t)(t - (uint32_t)h) - 1; sy0 = (int32_t)(t - 1u); }
    if (fl & 4u) {
        uint32_t t = (uint32_t)(int32_t)LS16(d + 6u) - (uint32_t)sy0; sy1 = (int32_t)(int16_t)(t - (uint32_t)h) - 1; sy0 = (int32_t)(t - 1u);
        dstep = W; rowskip = 1u - W * (uint32_t)w;
    }
    const int32_t xdir = sx0 < sx1 ? 1 : -1, ydir = sy0 < sy1 ? 1 : -1;
    uint32_t rowoff = (uint32_t)(int32_t)LS16(d + 4u) * (uint32_t)sy0;
    uint32_t dst = px + 2u * (W * (uint32_t)y + (uint32_t)x);
    if (sy0 == sy1) return ((uint64_t)1u << 32) | (uint32_t)sy0;
    /* from here the game's loop paints at least a pixel (w > 0: a row's walk is never empty); what it
     * reads again for every pixel is read once -- and only what it reads (liftfuzz.py: a draft that read
     * the object's mode before the clip faulted where the game, clipped away, reads nothing) */
    const uint32_t mode = L32(o + 0x34u);
    if (mode == 2u) return eng_29736(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
    const uint32_t pal8 = L8(d + 9u), nib = pal8 ? L8(d + 0xAu) : 0u, apl = L8(d + 8u);
    const uint32_t isrc = pal8 ? L32(d + 0x10u) : 0u, csrc = pal8 ? 0u : L32(d + 0xCu), asrc = apl ? L32(d + 0x14u) : 0u;
    const uint32_t m1 = GAMEW(0x29E14u), m3 = GAMEW(0x29E18u), lit4 = GAMEW(0x29A34u);
    const int32_t g = LS16(o + 0x38u);
    int32_t AT[16];
    for (uint32_t k = 0; k < 16u; k++) AT[k] = g == 15 ? (int32_t)k : (int32_t)((uint32_t)k * (uint32_t)g << 12) >> 16;
    uint32_t key = 0x10000u, ct = 0;                    /* key: past any index when not paletted */
    if (pal8) {
        uint32_t pal = L32(L32(o + 0x14u) + 4u * ((uint32_t)L8(o + 0xCu) * (uint32_t)L8(d + 0xBu) + (uint32_t)(int32_t)LS16(o + 0x24u)));
        key = (uint32_t)(int32_t)LS16(pal + 0xCu); ct = L32(pal + 8u);
    }
    struct pfw P;
    P.sx0 = (uint32_t)sx0; P.sx1 = (uint32_t)sx1; P.sy0 = (uint32_t)sy0; P.sy1 = (uint32_t)sy1;
    P.xdir = (uint32_t)xdir; P.ydir = (uint32_t)ydir; P.rowoff = rowoff; P.dst = dst;
    P.dstepb = 2u * dstep; P.rowskipb = 2u * rowskip; P.sw = (uint32_t)(int32_t)LS16(d + 4u);
    P.isrc = isrc; P.csrc = csrc; P.asrc = asrc; P.key = key; P.ct = ct; P.mode = mode; P.lit4 = lit4;
    P.m1 = m1; P.m3 = m3; P.AT = AT;
    if (pal8) { if (nib) { if (apl) pf_walk(&P, 1, 1, 1, 1); else pf_walk(&P, 1, 1, 0, 1); }
                else { if (apl) pf_walk(&P, 1, 0, 1, 1); else pf_walk(&P, 1, 0, 0, 1); } }
    else { int m0 = mode != 1u && mode != 3u && mode != 4u;     /* the colour as the source has it */
           if (apl) { if (m0) pf_walk(&P, 0, 0, 1, 0); else pf_walk(&P, 0, 0, 1, 1); }
           else { if (m0) pf_walk(&P, 0, 0, 0, 0); else pf_walk(&P, 0, 0, 0, 1); } }
    return ((uint64_t)(uint32_t)sy1 << 32) | (uint32_t)sy1;
}
#define PAINT_UT a4_paint_fast
#ifndef A4_PAINTCHECK
uint64_t a4_engv_29736(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2,
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)
{ return a4_paint_fast(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7); }
#endif
#else
#define PAINT_UT eng_29736
#endif
#ifdef A4_PAINTCHECK
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
        return PAINT_UT(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
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
    uint64_t rr = PAINT_UT(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
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
