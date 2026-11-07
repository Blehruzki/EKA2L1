/* a4_eng.h -- round 50: the game's engine functions rewritten in C (renderer/eng/, first drafted by
 * tools/lift.py from the game's own Thumb code, exact to the instruction). Memory, the game's code
 * (literals read where the loader relocated them), calls into the game (a4_eng_call, a4_eng.s: eight
 * stack words passed, r0:r1 returned) and the barrel shifter's carry. */
#ifndef A4_ENG_H
#define A4_ENG_H
#include <stdint.h>
#define MEM32(a)  (*(volatile uint32_t *)(uintptr_t)(a))
#define MEM16(a)  (*(volatile uint16_t *)(uintptr_t)(a))
#define MEM8(a)   (*(volatile uint8_t *)(uintptr_t)(a))
#define MEMS16(a) (*(volatile int16_t *)(uintptr_t)(a))
#define MEMS8(a)  (*(volatile int8_t *)(uintptr_t)(a))
extern const int32_t a4_g_base;                 /* a4_eng.s: patched to (code + 0) - (this word) */
#define GBASE        ((uint32_t)(uintptr_t)&a4_g_base + (uint32_t)a4_g_base)
#define GAME32(off)  MEM32(GBASE + (off))       /* a literal of the game's code, as the loader left it */
#define GAMEADDR(off) (GBASE + (off))
#define GAMEFN(off)  (GBASE + (off) + 1u)       /* a Thumb function of the game */
#define GAMEARM(off) (GBASE + (off))            /* an ARM one (the runtime helpers, blx) */
uint64_t a4_eng_call(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp, uint32_t *r23);
#define CALLR(x) a4_eng_call((x), r0, r1, r2, r3, sp, r23)
/* ARM shifts with the carry out; a shift by 0 leaves the carry, by 32 or more as the ARM does */
static inline uint32_t SHL_C(uint32_t x, uint32_t n, uint32_t *c)
{ n &= 0xFFu; if (!n) return x; if (n < 32u) { *c = (x >> (32u - n)) & 1u; return x << n; } *c = n == 32u ? (x & 1u) : 0u; return 0u; }
static inline uint32_t SHR_C(uint32_t x, uint32_t n, uint32_t *c)
{ n &= 0xFFu; if (!n) return x; if (n < 32u) { *c = (x >> (n - 1u)) & 1u; return x >> n; } *c = n == 32u ? (x >> 31) : 0u; return 0u; }
static inline uint32_t SAR_C(uint32_t x, uint32_t n, uint32_t *c)
{ n &= 0xFFu; if (!n) return x; if (n < 32u) { *c = (x >> (n - 1u)) & 1u; return (uint32_t)((int32_t)x >> n); } *c = x >> 31; return (uint32_t)((int32_t)x >> 31); }
static inline uint32_t ROR_C(uint32_t x, uint32_t n, uint32_t *c)
{ n &= 0xFFu; if (!n) return x; n &= 31u; { uint32_t r = n ? ((x >> n) | (x << (32u - n))) : x; *c = r >> 31; return r; } }

/* the 3x engine's own state (A4_ENG3): the scratch block the half-step physics used (0x45D000; the two
 * are never built together, a4_eng.c checks) */
#ifdef A4_ENG3
#define A4_ENG_STATE 0x0045D000u
#define ENG_NREM 1024u                /* remainders of a third, one per word that keeps thirds (a hash on its address) */
struct eng {
    uint32_t magic, race;            /* reset when a new race object appears */
    uint32_t cnt;                    /* race steps run (substeps): the next one's substep is cnt % 3 */
    uint32_t sub;                    /* the substep running: 0 the game's whole step, 1 and 2 motion only */
    uint32_t keys;                   /* the player's keys as substep 0 built them, replayed on 1 and 2 */
    uint32_t nfull;                  /* words the remainder table had no room for (kept as plain thirds) */
    uint32_t nsnd;                   /* the step's effect calls (+1B2D6) so far: one per car and effect a step */
    uint32_t flush;                  /* bench (A4_BENCH_INJECT): a car was overwritten -- the lag rings start again */
    uint32_t sndc[64], sndk[64];     /* their car and effect */
    uint32_t tlast[2];               /* the traffic's two clocks (race+0x378, +0x37C) at the last whole step */
    int32_t tdel[2];                 /* and their change over that step */
    uint32_t aic[32], aik[32], aia[32]; /* the AI's keys and argument per car, from the whole step */
    uint32_t frame[16];              /* +141B0's frame slots its race rules set (eng_frame_off), at the whole step */
    uint32_t camv, cam[8];           /* the whole step's last camera placement (+5968 from +12DEC): r0-r3, 4 stack words */
    uint32_t loop;                   /* the whole step entered the car loop (+14672): the substeps run only then */
    uint32_t meas1;                  /* +22832 measures a wreck: its displacement is a whole step's (x 1) */
    uint32_t lc[64], ls[64][3], lv[64][3]; /* the orientation's angles a step late: the word, the last three substeps' count and value */
    uint32_t kc[16], ks[16];         /* the wall's first push: the car (+678's address) and the step it hit, bit 31 still to come */
    uint32_t dbg[8];                 /* A4_ENGDBG: the substep's path bits and the player's keys, emitted per substep */
    uint32_t rk[ENG_NREM];           /* the word's address (0: free) */
    int32_t rv[ENG_NREM];            /* its remainder, 0..2 */
};
_Static_assert(A4_ENG_STATE + sizeof(struct eng) <= 0x00460000u, "the engine state past the scratch block");
#define ENG ((volatile struct eng *)A4_ENG_STATE)
/* the slots +141B0's race rules (+1433C..+1452C) write, read by the traffic and car loop after them */
static const uint16_t eng_frame_off[] = { 0xC8, 0xCC, 0xD0, 0xD4, 0x110, 0x114, 0x118, 0x11C, 0x120, 0x124 };
#define ENG_NFRAME (sizeof(eng_frame_off) / sizeof(eng_frame_off[0]))
#define ENG_MAGIC 0x33454E47u        /* 'GNE3' */
#define ENG_SUB (ENG->sub)
#define ENG_KEYS (ENG->keys)
/* a car's last substep of the step: 2; or 0 when the step's substeps will not run it -- a wreck (its
 * control and movement run on the whole step only: +1DFE6, +1EA68), or a car run outside the race step's
 * car loop (the countdown: ENG->loop clear, the substeps return at once; r14 left +7C4 at 10 from the
 * countdown) -- where a countdown that substeps read is counted, after the step's reads of it, as the
 * game counts it after its step's */
#define ENG_SUB_END(car) (ENG_SUB == 2u || (ENG_SUB == 0u && (!ENG->loop || *(volatile uint32_t *)(uintptr_t)((car) + 0x34u) != 0xFFFFFFFFu)))
static inline void ENG_BEGIN(uint32_t race)
{
    if (ENG->magic != ENG_MAGIC || ENG->race != race) {
        ENG->magic = ENG_MAGIC; ENG->race = race; ENG->cnt = 0; ENG->keys = 0; ENG->nfull = 0;
        for (uint32_t i = 0; i < ENG_NREM; i++) { ENG->rk[i] = 0; ENG->rv[i] = 0; }
        for (uint32_t i = 0; i < 32u; i++) ENG->aic[i] = 0;
        for (uint32_t i = 0; i < 16u; i++) { ENG->kc[i] = 0; ENG->ks[i] = 0; }
        for (uint32_t i = 0; i < 64u; i++) ENG->lc[i] = 0;
        ENG->flush = 0;
        ENG->tlast[0] = ENG->tlast[1] = 0; ENG->tdel[0] = ENG->tdel[1] = 0;
    }
#ifdef A4_ENGDBG
    {   /* bench: the last substep's record ('ENGS': count, substep, path bits, keys) with the player car */
        extern void a4_rec_emit2(const void *hdr, const void *car, const void *race);
        volatile uint32_t h[8];
        uint32_t pl = *(volatile uint32_t *)(race + 0x390), idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0u;
        uint32_t car = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0u;
        h[0] = 0x53474E45u; h[1] = ENG->cnt; h[2] = ENG->sub; h[3] = ENG->dbg[0]; h[4] = ENG->dbg[1]; h[5] = ENG->dbg[2]; h[6] = ENG->dbg[3]; h[7] = car;
        if (car && ENG->magic == ENG_MAGIC) a4_rec_emit2((const void *)h, (const void *)car, (const void *)race);
        {   /* and the opponent the car recorder follows (race slot 1, or 2 if the player is 1): 'ENGA' */
            uint32_t ai = idx == 1u ? 2u : 1u, aic = *(volatile uint32_t *)(race + 0x394 + 4u * ai);
            h[0] = 0x41474E45u; h[7] = aic; h[4] = 0xFFFFFFFFu; h[5] = 0xFFFFFFFFu;
            for (uint32_t q = 0; q < 32u; q++) if (ENG->aic[q] == aic) { h[4] = ENG->aik[q]; h[5] = ENG->aia[q]; }
            if (aic && ENG->magic == ENG_MAGIC) a4_rec_emit2((const void *)h, (const void *)aic, (const void *)race);
        }
        ENG->dbg[0] = 0; ENG->dbg[1] = 0; ENG->dbg[2] = 0; ENG->dbg[3] = 0;
    }
#endif
    if (ENG->flush) { for (uint32_t i = 0; i < 64u; i++) ENG->lc[i] = 0; ENG->flush = 0; }
    ENG->sub = ENG->cnt % 3u; ENG->cnt++;
    if (ENG->sub == 0u) { ENG->nsnd = 0; ENG->camv = 0; ENG->loop = 0; }
}
/* the camera's placement as the whole step's +12DEC made it, for the substeps to make again on the moved cars */
static inline void eng_cam_save(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp)
{
    ENG->cam[0] = r0; ENG->cam[1] = r1; ENG->cam[2] = r2; ENG->cam[3] = r3;
    for (uint32_t k = 0; k < 4u; k++) ENG->cam[4 + k] = ((volatile uint32_t *)(uintptr_t)sp)[k];
    ENG->camv = 1;
}
/* A substep's camera: the whole step's last placement made again on the moved car, for the views that
 * are placed from the car (the chase family 1, 2, 3, 0x2A; 0 and 4-7, car-relative). The camera's own
 * state -- its mode, timers, focal length, script -- is put back after, so its logic stays the whole
 * step's; only the pose it computed (cam+0x10..+0x2C: x, y, z, pitch, yaw, roll, segment, flag) is the
 * substep's. The scripted views (0x14, 0x16-0x1D: the fly-in, the crash, the takedowns) walk keyframe
 * lists and free their nodes as they go: not made again, their pose waits for the whole step. */
#define ENG_CAMW 56u
static inline void eng_cam_substep(uint32_t sp, uint32_t *r23)
{
    volatile uint32_t *cam = (volatile uint32_t *)(uintptr_t)ENG->cam[0];
    uint32_t mode, k, save[ENG_CAMW];
    if (!ENG->camv || !cam) return;
    mode = cam[0x4C / 4];
    if (!(mode <= 7u || mode == 0x2Au)) return;
    for (k = 0; k < ENG_CAMW; k++) save[k] = cam[k];
    for (k = 0; k < 4u; k++) ((volatile uint32_t *)(uintptr_t)sp)[k] = ENG->cam[4 + k];
    (void)a4_eng_call(GAMEFN(0x5968), ENG->cam[0], ENG->cam[1], ENG->cam[2], ENG->cam[3], sp, r23);
    for (k = 0; k < ENG_CAMW; k++) if (k < 4u || k > 11u) cam[k] = save[k];
}
/* +1B2D6(car, effect, a2, a3): a car's sound or effect event. The game calls it from conditions it
 * checks once a step (a drift held, wrong way, a landing); on three substeps the same condition would
 * fire it three times. Once per car and effect a game step, at the first substep it happens on. */
static inline uint64_t eng_c_1b2d6(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp, uint32_t *r23)
{
    uint32_t k = r1 | (r2 << 16), i;
    for (i = 0; i < ENG->nsnd; i++)
        if (ENG->sndc[i] == r0 && ENG->sndk[i] == k) { r23[0] = r2; r23[1] = r3; return ((uint64_t)r1 << 32) | r0; }
    if (ENG->nsnd < 64u) { ENG->sndc[ENG->nsnd] = r0; ENG->sndk[ENG->nsnd] = k; ENG->nsnd++; }
    return a4_eng_call(GAMEFN(0x1B2D6), r0, r1, r2, r3, sp, r23);
}
/* an unsigned divide (the build has no divide routine; ARMv5 has no divide instruction) */
static inline uint32_t eng_udiv(uint32_t n, uint32_t d)
{
    uint32_t q = 0, r = 0;
    if (!d) return 0;
    for (int b = 31; b >= 0; b--) { r = (r << 1) | ((n >> b) & 1u); if (r >= d) { r -= d; q |= 1u << b; } }
    return q;
}
/* A rate's store on a substep: the word keeps a third of the change the game's whole step would make,
 * floor-divided, the remainder (0..2) carried in the word's own slot, so three substeps of the same
 * change add up to the whole step's exactly. */
static inline uint32_t eng_third(uint32_t ea, uint32_t old, uint32_t nw)
{
    uint32_t h = (ea * 2654435761u) >> 22, n;
    int32_t t, q;
    for (n = 0; n < 16u; n++, h = (h + 1u) & (ENG_NREM - 1u)) {
        if (ENG->rk[h] == ea) break;
        if (ENG->rk[h] == 0u) { ENG->rk[h] = ea; ENG->rv[h] = 0; break; }
    }
    t = (int32_t)(nw - old);
    if (n == 16u) { ENG->nfull++; return old + (uint32_t)(t / 3); }
    t += ENG->rv[h];
    q = t >= 0 ? t / 3 : -((2 - t) / 3);
    ENG->rv[h] = t - 3 * q;
    return old + (uint32_t)q;
}
/* A third of a value with its remainder carried under a key of its own (an odd address, never a word's):
 * a per-step change the game applies through code that is not a store (a vector add, a call). */
static inline int32_t eng_thirdv(uint32_t key, int32_t v) { return (int32_t)eng_third(key | 1u, 0u, (uint32_t)v); }
/* The wall's first push (+1ED4C: the heading turned off the wall by half the angle met, clamped 20..64,
 * while +B18 counts the contact's 5 steps). The game turns the car the whole push at the step the contact
 * starts; three thirds, each on the angle the last one left, add up to less (30 against stock's 49 at a
 * wide hit, so a hit cost less speed). The substep where +B18 leaves 0 marks the car; its push is then
 * the game's whole one and the step's later substeps add none; later steps are thirded as before. */
static inline void eng_kick_mark(uint32_t ea)
{
    uint32_t st = (ENG->cnt - 1u) / 3u, i, f = 16u;
    for (i = 0; i < 16u; i++) {
        if (ENG->kc[i] == ea) { f = i; break; }
        if (f == 16u && (ENG->kc[i] == 0u || (ENG->ks[i] & 0x7FFFFFFFu) != st)) f = i;
    }
    if (f == 16u) f = (ea >> 4) & 15u;
    ENG->kc[f] = ea; ENG->ks[f] = st | 0x80000000u;
}
static inline uint32_t eng_kick(uint32_t ea, uint32_t nw)
{
    uint32_t st = (ENG->cnt - 1u) / 3u, i;
    for (i = 0; i < 16u; i++)
        if (ENG->kc[i] == ea && (ENG->ks[i] & 0x7FFFFFFFu) == st) {
            if (ENG->ks[i] & 0x80000000u) { ENG->ks[i] = st; return nw; }   /* the contact's substep: all of it */
            return MEM32(ea);                                              /* the step's later ones: none */
        }
    return eng_third(ea, MEM32(ea), nw);
}
/* The car's orientation, a game step late (+1971E, from +1DE78 after the move: the matrix +6D4 from the
 * heading +678, +674 and +67C, then +704 its forward axis, +710 its side). The control reads +704 before
 * it makes it again -- the engine's slip check (+1BB98: dot(+010, +704) >> 12 - +7D0) and the target
 * velocity (+1E56E: +680 = +704 x +7D0 / 4096) -- so a heading change reaches the car's motion one step
 * later: 85 ms in the game, one substep (28 ms) at three a step. The lag is part of the game's handling:
 * a wall's turn (+1ED4C) leaves the target pointing into the wall and the slip check comparing the move
 * along the wall with the old heading for the step after it, which costs the engine two rpm penalties
 * (-384 each) and a downshift; at one substep the wall cost a third of that. So +1971E reads its three
 * angles as they were two substeps ago (with its own substep's lag, three: the game's step); when this
 * car's control did not run then (a wreck, the race's start), the current ones. */
static inline uint32_t eng_alag(uint32_t ea, uint32_t v)
{
    uint32_t now = ENG->cnt - 1u, i, f = 64u, k = now % 3u, o = (now + 1u) % 3u, r = v;
    for (i = 0; i < 64u; i++) {
        if (ENG->lc[i] == ea) break;
        if (f == 64u && ENG->lc[i] == 0u) f = i;
    }
    if (i == 64u) {
        if (f == 64u) f = (ea >> 2) & 63u;
        i = f; ENG->lc[i] = ea; ENG->ls[i][0] = ENG->ls[i][1] = ENG->ls[i][2] = 0xFFFFFFFFu;
    }
    if (ENG->ls[i][o] == now - 2u) r = ENG->lv[i][o];
    ENG->ls[i][k] = now; ENG->lv[i][k] = v;
    return r;
}
/* the AI's per-car slot: its keys and +1DE78's stack argument, as the whole step's AI chose them */
static inline uint32_t eng_ai_slot(uint32_t car)
{
    uint32_t i, f = 32u;
    for (i = 0; i < 32u; i++) { if (ENG->aic[i] == car) return i; if (ENG->aic[i] == 0u && f == 32u) f = i; }
    if (f == 32u) f = (car >> 4) & 31u;
    ENG->aic[f] = car; ENG->aik[f] = 0; ENG->aia[f] = 0;
    return f;
}
/* The traffic's clock on a substep: the whole step's clock plus the substep's share of its last change
 * (t * 3 + sub * dt, the multiply by the traffic speed then divided by 3: +FB12's position on the road) */
static inline uint32_t eng_tmul(uint32_t t, uint32_t speed, uint32_t which)
{
    int64_t f = (int64_t)ENG->sub * ENG->tdel[which & 1u] * (int32_t)speed;   /* the substep's share, x 3 */
    if (f > 0x7FFFFFFFll || f < -0x7FFFFFFFll) f = 0;                         /* (not a clock: its whole step) */
    return t * speed + (uint32_t)((int32_t)f / 3);                            /* the game's own product, wrapped */
}
#endif
#endif
