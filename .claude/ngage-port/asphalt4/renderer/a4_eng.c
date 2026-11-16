/* a4_eng.c -- round 50: the game's engine in C. Each function here replaces the game's own (a veneer
 * at its entry, tools/injector.py: every symbol a4_engv_XXXXX), first as tools/lift.py drafts it --
 * exact, so a lockstep race against the game's own code must match it word for word -- and only then
 * changed (the step at 3x). Nothing here is compiled without A4_ENGINE. */
#ifdef A4_ENGINE
#include "a4_eng.h"
#if defined(A4_ENG3) && (defined(A4_HALFPHYS) || defined(A4_HALFSPEC))
#error "the 3x engine and the half-step machinery share scratch and cannot be built together"
#endif
#ifdef A4_STKPOISON
/* bench (phone round 68): every lifted function's frame filled with junk at its entry, as the phone's stack
 * holds what its GL driver and kernel calls left there (the bench's GL is the emulator's, off the guest stack):
 * a read of a slot this call never wrote then reads junk here too. Never a phone build. */
__attribute__((noinline)) void a4_stkpoison(uint32_t *p, uint32_t n)
{
    uint32_t x = ENG->dbg[7] | 1u;                   /* its state in the engine's block (the image has no data) */
    for (uint32_t i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; ((volatile uint32_t *)p)[i] = x; }
    ENG->dbg[7] = x;
}
#endif
#if defined(A4_CAMPROBE) || defined(A4_KEYPROBE) || defined(A4_VELPROBE) || defined(A4_CARSUB) || defined(A4_HELIREC) || defined(A4_TRAFDUMP) || defined(A4_SEGREC) || defined(A4_KICKREC) || defined(A4_TKREC) || defined(A4_HCREC)
__attribute__((noinline)) void a4_camp_emit(const void *r) { __asm__ volatile("" : : "r"(r) : "memory"); }
#endif
#ifdef A4_ENG3
/* the step loop's key handler (+25E8, bl at +167AE in +1670E): read before a whole step only -- a press
 * the handler latched on a substep would be gone by the step that acts on it */
extern const int32_t a4_hc_25e8;
#define GAMEFN_PH(ph) ((uint32_t)(uintptr_t)&(ph) + (uint32_t)(ph))
uint64_t a4_eng_keys(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3)
{
    uint32_t r23[2], stk[8] = { 0 };
    if (ENG->magic == ENG_MAGIC && ENG->cnt != 0u && ENG->sub + 1u < ENG->n) return 0;   /* the next step a substep */
#ifdef A4_LIVEKEYS
    ENG->kobj = r0; ENG->kmask = r1; ENG->kok = 1;   /* for the substeps' copy (eng_keys_peek) */
#endif
    return a4_eng_call(GAMEFN_PH(a4_hc_25e8), r0, r1, r2, r3, (uint32_t)(uintptr_t)stk, r23);
}
#endif
#if defined(A4_ENG6) && defined(A4_ENG3)
/* Round 73: the race update's step count (+15B1C, after it ran) in the game's slow motion. The game counts its steps
 * with the period scale x 7111 >> 8 -- at 512 a substep every 56.9 ms, the real steps between filled with +1300C (the
 * effects' frame counters, the camera). While the engine's steps are sixths (the step running is, or the next substep
 * begins one, which the scale makes a sixths step) every real step is a substep: n = m. And one +1300C a whole step,
 * as stock makes one a slowed step: m one more than n when these substeps include a sixths step's substep 3 -- its
 * middle, so the camera's two ticks a step fall three substeps apart (eng_cam_script). */
void a4_eng6_count(uint32_t race, int32_t *m, int32_t *n)
{
    uint32_t s, nn, k, ends = 0;
    if (ENG->magic != ENG_MAGIC || ENG->race != race || *m <= 0) return;
    if (*(volatile uint32_t *)(race + 0x84Cu) != 512u) return;
    if (ENG->cnt != 0u && ENG->n != 6u && ENG->sub + 1u < ENG->n) return;   /* a step of thirds still running */
    *n = *m;
    s = ENG->cnt == 0u ? 5u : ENG->sub; nn = ENG->cnt == 0u ? 6u : ENG->n;
    for (k = 0; k < (uint32_t)*n; k++) {
        if (s + 1u >= nn) { s = 0u; nn = 6u; } else s++;
        if (nn == 6u && s == 3u) ends++;
    }
    if (ends) *m = *n + 1;
}
#endif
#if defined(A4_ENG6) && defined(A4_ENG3)
/* Round 73: around +1300C (a4_gl.c a4_step_fill): the chase pose the game's own before it, its camera the next
 * target after it (eng_cam_mid) */
void a4_eng6_fill_pre(void) { if (ENG->magic == ENG_MAGIC && ENG->n == 6u) eng_cam_unblend(); }
void a4_eng6_fill_post(void) { if (ENG->magic == ENG_MAGIC && ENG->n == 6u) eng_cam_mid(); }
#endif
#if defined(A4_BENCH_SEEK) && defined(A4_ENG3)
/* bench (round 73, the seek driver): the traffic array +FB12 was last given (0 before the race's first traffic step) */
uint32_t a4_eng_traffic(void) { return ENG->magic == ENG_MAGIC && ENG->targ ? ENG->targ + GAME32(0xFEB4) : 0u; }
#endif
#if defined(A4_FRC3) && defined(A4_ENG3)
/* Phone round 76 (the owner after round 75: "whatever violent movement the car as an object does around its own axis
 * feels choppy", "the car snaps back in a clean turn"): FRC3's car words for this render, before it draws (a4_gl.c
 * A4_GL_FREC3 has the layout). The player car's angles as stored, its turn term +9F8, the matrix's forward axis +704
 * and side axis's height +714 as the render drew it (a4_eng_frc3_post), its velocity, keys and steering rate; then one other car: the car the camera placement follows (ENG->cam[2]) when that is not the
 * player's, else the wreck (+034 1 .. 1023, a wreck path running) nearest the camera, else the nearest race car --
 * its slot, place, angles and both axes. */
static inline uint32_t frc3_h(int32_t v) { return (uint32_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v) & 0xFFFFu; }
void a4_eng_frc3(uint32_t race, volatile uint32_t *e)
{
    const uint32_t pl = MEM32(race + 0x390u), pi = pl ? MEM32(pl + 0xCu) : 16u;
    const uint32_t pc = pi < 16u ? MEM32(race + 0x394u + 4u * pi) : 0u;
    const uint32_t fc = ENG->magic == ENG_MAGIC && ENG->camv ? ENG->cam[2] : 0u;
    const uint32_t ctx = MEM32(race + 0x38u), K = ctx ? MEM32(ctx + 0x1114u) : 0u;
    const int32_t cx = (int32_t)MEM32(race + 0x48u + 0x10u), cz = (int32_t)MEM32(race + 0x48u + 0x18u);
    uint32_t oc = 0u, os = 31u, fl = 0u, k;
    e[1] = ENG->cnt;
    e[2] = (ENG->sub & 15u) | (ENG->n & 15u) << 4 | (MEM32(race + 0x94u) & 0xFFu) << 8 | MEM32(race + 0x84Cu) << 16;
    for (k = 3u; k < 16u; k++) e[k] = 0u;
    if (pc) {
        e[3] = (MEM32(pc + 0x678u) & 0xFFFFu) | MEM32(pc + 0x9F8u) << 16;
        e[4] = (MEM32(pc + 0x674u) & 0xFFFFu) | MEM32(pc + 0x67Cu) << 16;
        e[7] = frc3_h((int32_t)MEM32(pc + 0x10u)) | frc3_h((int32_t)MEM32(pc + 0x18u)) << 16;
        e[8] = (K ? MEM32(K + 4u) & 0xFFFFu : 0u) | MEM32(pc + 0xA1Cu) << 16;
        /* the player's own wreck path (a crash): its frame, path, ease words, flags, segment */
        e[32] = (MEM32(pc + 0x34u) & 0xFFFFu) | MEM32(pc + 0x3Cu) << 16;
        e[33] = (MEM32(pc + 0x44u) & 0xFFFFu) | MEM32(pc + 0x48u) << 16;
        e[34] = (MEM32(pc + 0xB28u) & 0xFFFFu) | MEM32(pc + 0x1Cu) << 16;
    }
    e[30] = pc;                                      /* the player car, for a4_h_a434 during the render */
    if (fc && fc != pc) {
        for (k = 0; k < 16u; k++) if (MEM32(race + 0x394u + 4u * k) == fc) break;
        oc = fc; os = k < 16u ? k : 30u; fl = 1u;
    } else {
        int64_t bw = 0x7FFFFFFFFFFFLL, bn = 0x7FFFFFFFFFFFLL; uint32_t wc = 0u, ws = 31u, nc = 0u, ns = 31u;
        for (k = 0; k < 16u; k++) {
            const uint32_t c = MEM32(race + 0x394u + 4u * k), s = c ? MEM32(c + 0x34u) : 0xFFFFFFFFu;
            if (!c || c == pc || (s >= 1024u && s != 0xFFFFFFFFu)) continue;   /* +034 a pointer: not a car (-1: no path) */
            const int64_t dx = (int64_t)(int32_t)MEM32(c + 4u) - cx, dz = (int64_t)(int32_t)MEM32(c + 0xCu) - cz;
            const int64_t d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
            if (s >= 1u && s < 1024u && d < bw) { bw = d; wc = c; ws = k; }
            if (d < bn) { bn = d; nc = c; ns = k; }
        }
        if (wc) { oc = wc; os = ws; fl = 2u; } else if (nc) { oc = nc; os = ns; }
    }
    if (oc) {
        const uint32_t s = MEM32(oc + 0x34u);
        e[9] = os | fl << 5 | (s > 511u ? 511u : s) << 7 | MEM32(oc + 0x678u) << 16;
        e[10] = (MEM32(oc + 0x674u) & 0xFFFFu) | MEM32(oc + 0x67Cu) << 16;
        e[11] = MEM32(oc + 4u); e[12] = MEM32(oc + 0xCu);
        /* the wreck's draw (+DD2A, +DEFE): its path frame +034, ease words +044 / +048, flags +B28, path +03C */
        e[13] = (MEM32(oc + 8u) & 0xFFFFu) | MEM32(oc + 0x34u) << 16;
        e[14] = (MEM32(oc + 0x44u) & 0xFFFFu) | MEM32(oc + 0x48u) << 16;
        e[15] = (MEM32(oc + 0xB28u) & 0xFFFFu) | MEM32(oc + 0x3Cu) << 16;
        e[35] = oc;                                  /* the car, for a4_h_a434 during the render */
    } else e[9] = 31u;
}
/* After the render: the player's step axes (+704 forward, +714 the side's height: +1971E's, two substeps behind the
 * heading -- the render does not draw from them, round 31 rone8) and the camera's pitch and roll */
void a4_eng_frc3_post(uint32_t race, volatile uint32_t *e)
{
    const uint32_t pl = MEM32(race + 0x390u), pi = pl ? MEM32(pl + 0xCu) : 16u;
    const uint32_t pc = pi < 16u ? MEM32(race + 0x394u + 4u * pi) : 0u;
    e[30] = (MEM32(race + 0x64u) & 0xFFFFu) | MEM32(race + 0x6Cu) << 16;   /* the camera's pitch, roll */
    if (pc) {
        e[5] = frc3_h((int32_t)MEM32(pc + 0x704u)) | frc3_h((int32_t)MEM32(pc + 0x70Cu)) << 16;
        e[6] = frc3_h((int32_t)MEM32(pc + 0x708u)) | frc3_h((int32_t)MEM32(pc + 0x714u)) << 16;
    }
}
#endif
#if defined(A4_FRC3) && defined(A4_ENG3)
/* The angles the render draws a car at: +A434 (car, &a, &b, &c, offset[3]) -- from the car draws +DBE0 and +E88C
 * (tools/injector.py) -- reads +678, adds the turn term +9F8 when the car slides (+818 / +824), the lean from +A04 ..
 * +A24 (and moves +A14 as it draws), and gives three angles and a position offset. Its outputs for the player car and
 * FRC3's other car go into this render's entry (a4_gl.c A4_GL_FREC3 [24..31]), the first call of the render each. */
uint32_t a4_h_a434_at(uint32_t car, int32_t *a, int32_t *b, int32_t *c, int32_t *o, uint32_t site)
{
    const uint32_t r = ((uint32_t (*)(uint32_t, int32_t *, int32_t *, int32_t *, int32_t *))GAMEFN(0xA434u))(car, a, b, c, o);
    volatile uint32_t *F3 = (volatile uint32_t *)A4_GL_FREC3;
    if (F3[0] != 0x33435246u) return r;
    volatile uint32_t *e = F3 + 4u + (F3[1] % A4_FREC3_N) * A4_FREC3_W;
    const uint32_t pc = e[30], oc = (e[9] & 31u) != 31u ? e[35] : 0u;
    uint32_t n = e[31];
    if (car && car == pc) {
        if ((n & 0xFFu) == 0u) {
            e[24] = frc3_h(*b) | frc3_h(*a) << 16; e[25] = frc3_h(*c) | frc3_h(o[0]) << 16;
            e[26] = frc3_h(o[1]) | frc3_h(o[2]) << 16;
            if (site) n |= 1u << 24;
        }
        if ((n & 0xFFu) < 255u) n++;
    }
    if (car && car == oc) {
        if (((n >> 8) & 0xFFu) == 0u) {
            e[27] = frc3_h(*b) | frc3_h(*a) << 16; e[28] = frc3_h(*c) | frc3_h(o[0]) << 16;
            e[29] = frc3_h(o[1]) | frc3_h(o[2]) << 16;
            if (site) n |= 1u << 25;
        }
        if (((n >> 8) & 0xFFu) < 255u) n += 1u << 8;
    }
    if (((n >> 16) & 0xFFu) < 255u) n += 1u << 16;
    e[31] = n;
    return r;
}
uint32_t a4_h_a434_dbe0(uint32_t car, int32_t *a, int32_t *b, int32_t *c, int32_t *o) { return a4_h_a434_at(car, a, b, c, o, 0u); }
uint32_t a4_h_a434_e88c(uint32_t car, int32_t *a, int32_t *b, int32_t *c, int32_t *o) { return a4_h_a434_at(car, a, b, c, o, 1u); }
#endif
#if defined(A4_TELMARK) && defined(A4_ENG3)
/* Phone round 76: the owner's mark. The game's key-down (+1EE0) translates the runtime's key (+1D80: a bit of its key
 * word) to its own code; C, the clear key (0x4000, ck1 on the bench), has none and is dropped there, so the game never
 * sees it. Hooked at +1EE0's bl +1D80 (tools/injector.py): a press of C asks a4_tel.c for a mark capture. */
uint32_t a4_key_down(uint32_t kobj, uint32_t key)
{
    if (key == 0x4000u) (*(volatile uint32_t *)A4_MARK_STATE)++;
    return ((uint32_t (*)(uint32_t, uint32_t))GAMEFN(0x1D80u))(kobj, key);
}
#endif
#if defined(A4_TKREC) && defined(A4_ENG3)
/* bench (round 73, phone round 73's "stop motion in the takedown cams"): every render -- the frame, the engine's
 * count, substep | n << 8 | the scale (race+0x84C) << 16, the camera mode (race+0x94), the camera x and z (race+0x48's
 * +0x10, +0x18), the followed car's x and z (the camera placement's car, ENG->cam[2]), its heading +678 | its address's
 * low 16 bits << 16; then (round 74) its pitch, roll, height, the camera's height, and the first wrecked race car's
 * place, angles and matrix -- 32 words, a ring of 64 at 0x5A2000 (A4_SCRATCH_SIZE >= 0x1A6000) handed to a4_camp_emit */
void a4_eng_tkrec(uint32_t race, uint32_t frames)
{
    extern void a4_camp_emit(const void *);
#if defined(A4_TRAFDUMP) || defined(A4_SPRCEN) || defined(A4_SPINREC) || defined(A4_KICKREC)
    volatile uint32_t *rg = (volatile uint32_t *)0x005A8000u;   /* past the traffic dump's / sprite census's ring (A4_SCRATCH_SIZE >= 0x1AA000) */
#else
    volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
#endif
    const uint32_t i = rg[0] & 63u, fc = ENG->camv ? ENG->cam[2] : 0u;
    volatile uint32_t *e = rg + 4u + 32u * i;
    uint32_t wc = 0u, k;
    e[0] = frames; e[1] = ENG->cnt; e[2] = ENG->sub | ENG->n << 8 | MEM32(race + 0x84Cu) << 16; e[3] = MEM32(race + 0x94u);
    e[4] = MEM32(race + 0x48u + 0x10u); e[5] = MEM32(race + 0x48u + 0x18u);
    e[6] = fc ? (MEM32(fc + 4u) & 0xFFFFu) | MEM32(fc + 0xCu) << 16 : 0u;
    e[7] = fc ? (MEM32(fc + 0x678u) & 0xFFFFu) | fc << 16 : 0u;
    /* round 74: the followed car's pitch +674, roll +67C, height, the camera's height; then the first wrecked race car
     * (+034 not -1) other than it: its address, x y z, pitch, heading, roll, its matrix +6D4's first four words, +034 */
    e[8] = fc ? MEM32(fc + 0x674u) : 0u; e[9] = fc ? MEM32(fc + 0x67Cu) : 0u; e[10] = fc ? MEM32(fc + 8u) : 0u;
    e[11] = MEM32(race + 0x48u + 0x14u);
    for (k = 0; k < 16u && !wc; k++) {
        const uint32_t c = MEM32(race + 0x394u + 4u * k);
        if (c && c != fc && MEM32(c + 0x34u) < 1024u) wc = c;                   /* a wreck path's frame (not a pointer) */
    }
#ifdef A4_TKREC_SELF
    /* bench (round 74): for EKA2L1_WATCHPTR on a wreck's heading -- any car, the followed one included (the player's
     * own crash: its camera 0x18 is a takedown's), whose wreck path is running (+034 1 .. 1023: tw2 found cars at 0
     * that were parked, not wrecked) */
    for (k = 0, wc = 0u; k < 16u && !wc; k++) {
        const uint32_t c = MEM32(race + 0x394u + 4u * k);
        if (c && MEM32(c + 0x34u) - 1u < 1023u) wc = c;
    }
#endif
    for (k = 12u; k < 32u; k++) e[k] = 0u;
    if (wc) {
        e[12] = wc; e[13] = MEM32(wc + 4u); e[14] = MEM32(wc + 8u); e[15] = MEM32(wc + 0xCu);
        e[16] = MEM32(wc + 0x674u); e[17] = MEM32(wc + 0x678u); e[18] = MEM32(wc + 0x67Cu);
        for (k = 0; k < 4u; k++) e[19u + k] = MEM32(wc + 0x6D4u + 4u * k);
        e[23] = MEM32(wc + 0x34u);
    }
    rg[2] = wc;                                  /* round 74: the wreck A4_SPINREC's entry probes watch */
    rg[0] = i + 1u;
    if (((i + 1u) & 63u) == 0u) {
        a4_camp_emit((const void *)(rg + 4u));
#ifdef A4_SPINREC
        a4_camp_emit((const void *)0x005A2010u);  /* the heading probes' ring as it stands (it fills slowly) */
#endif
    }
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
#if !defined(A4_NODIRECT_349F8) || ((defined(A4_ENG_349B0) || defined(A4_ENG_ALL)) && !defined(A4_NODIRECT_349B0))
#error "A4_LIFTCHECK checks at the veneers: build it with A4_NODIRECT_349F8 and, if the insert is lifted, A4_NODIRECT_349B0 (lifted callers, +CE8C's among them, then call through them)"
#endif
#define a4_engv_349F8 a4_lck_l349F8
#if defined(A4_ENG_349B0) || defined(A4_ENG_ALL)
#define a4_engv_349B0 a4_lck_l349B0
#endif
#endif
#ifdef A4_ABCYCLE13
#define a4_engv_349F8 a4_ab_l349F8      /* the lifter's entry renamed: the per-window switch is below */
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
#ifdef A4_ABCYCLE13
#undef a4_engv_349F8
/* Round 53: the lifted mesh routine against the game's own, a telemetry window each (a4_tel.c steps AB_FRONT,
 * 32768, and AB_MEMO, 65536, in the mode word, A4_PERF[PF_MODE] at 0x412C00): +349F8 lifted to ARM with the hand
 * transform (A4_FASTXF, which leaves out the game's projection helpers +523E6/A2/C4), or the game's Thumb replayed
 * past its veneer (a4_eng.s a4_orig_349F8, as A4_LIFTCHECK runs it) -- the same calls, the same results (lc53:
 * 30,857 calls compared, 0 differing). The insert +349B0 is the game's in both (round 53's first draft lifted
 * it too: its veneer and switch cost both window types ~0.35 ms on the model). AB_MEMO adds the corner memo
 * (a4_gl.c MEMO_ON) in the lifted windows: a phone's cost of the game's Thumb and its table reads against our
 * ARM, and the memo's own, inside one race. */
extern void a4_orig_349F8(void);
#define FRONT_ON() ((*(volatile uint32_t *)0x00412C00u & 32768u) != 0u)
uint64_t a4_engv_349F8(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t s0, uint32_t s1, uint32_t s2,
    uint32_t s3, uint32_t s4, uint32_t s5, uint32_t s6, uint32_t s7)
{
    if (FRONT_ON()) return eng_349F8(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
    uint32_t stk[8] = { s0, s1, s2, s3, s4, s5, s6, s7 }, r23[2];
    return a4_eng_call((uint32_t)(uintptr_t)&a4_orig_349F8, r0, r1, r2, r3, (uint32_t)(uintptr_t)stk, r23);
}
#endif
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
#if defined(A4_ENG_349B0) || defined(A4_ENG_ALL)    /* round 53: the insert checked only where it is lifted */
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
#endif   /* the insert's check */
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
#ifdef A4_PAINTCHECK_BIG
    /* round 51: a whole 320x240 surface's two copies past the phone layout's scratch (0x460000 up; build
     * with A4_SCRATCH_SIZE=0xAB000), so the calls the 20 KB buffers turned away are compared too */
#define PCK_MAX 153600u
    uint16_t *S = (uint16_t *)0x00460000u, *G = (uint16_t *)0x00486000u;
#else
#define PCK_MAX 20480u
    uint16_t *S = (uint16_t *)0x00403000u, *G = (uint16_t *)0x00415000u;
#endif
    if (x1 <= x0 || y1 <= y0 || !px || (uint32_t)((x1 - x0) * (y1 - y0) * 2) > PCK_MAX) {
        ck[116]++;
        return PAINT_UT(r0, r1, r2, r3, s0, s1, s2, s3, s4, s5, s6, s7);
    }
    volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)px;
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
