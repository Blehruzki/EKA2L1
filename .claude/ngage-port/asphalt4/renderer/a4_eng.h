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
    uint32_t pad;
    uint32_t sndc[64], sndk[64];     /* their car and effect */
    uint32_t tlast[2];               /* the traffic's two clocks (race+0x378, +0x37C) at the last whole step */
    int32_t tdel[2];                 /* and their change over that step */
    uint32_t aic[32], aik[32], aia[32]; /* the AI's keys and argument per car, from the whole step */
    uint32_t frame[16];              /* +141B0's frame slots its race rules set (eng_frame_off), at the whole step */
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
static inline void ENG_BEGIN(uint32_t race)
{
    if (ENG->magic != ENG_MAGIC || ENG->race != race) {
        ENG->magic = ENG_MAGIC; ENG->race = race; ENG->cnt = 0; ENG->keys = 0; ENG->nfull = 0;
        for (uint32_t i = 0; i < ENG_NREM; i++) { ENG->rk[i] = 0; ENG->rv[i] = 0; }
        for (uint32_t i = 0; i < 32u; i++) ENG->aic[i] = 0;
        ENG->tlast[0] = ENG->tlast[1] = 0; ENG->tdel[0] = ENG->tdel[1] = 0;
    }
    ENG->sub = ENG->cnt % 3u; ENG->cnt++;
    if (ENG->sub == 0u) ENG->nsnd = 0;
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
