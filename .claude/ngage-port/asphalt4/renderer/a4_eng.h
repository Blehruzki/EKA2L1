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
/* the lifted code's own accessors (tools/lift.py): plain loads and stores that may alias anything, so
 * clang keeps the instruction's width and order against every store but may combine, schedule and drop
 * the redundant ones; S* is the function's own frame through the pointer-typed stack (spp), which clang
 * keeps in registers where the frame's address never leaves the function */
typedef uint32_t __attribute__((may_alias)) a4_u32a;
typedef uint16_t __attribute__((may_alias)) a4_u16a;
typedef uint8_t __attribute__((may_alias)) a4_u8a;
typedef int16_t __attribute__((may_alias)) a4_s16a;
typedef int8_t __attribute__((may_alias)) a4_s8a;
#define L32(a)  (*(a4_u32a *)(uintptr_t)(a))
#define L16(a)  (*(a4_u16a *)(uintptr_t)(a))
#define L8(a)   (*(a4_u8a *)(uintptr_t)(a))
#define LS16(a) (*(a4_s16a *)(uintptr_t)(a))
#define LS8(a)  (*(a4_s8a *)(uintptr_t)(a))
#define S32(o)  (*(a4_u32a *)(spp + (o)))
#define S16(o)  (*(a4_u16a *)(spp + (o)))
#define S8(o)   (*(a4_u8a *)(spp + (o)))
#define SS16(o) (*(a4_s16a *)(spp + (o)))
#define SS8(o)  (*(a4_s8a *)(spp + (o)))
extern const int32_t a4_g_base;                 /* a4_eng.s: patched to (code + 0) - (this word) */
#define GBASE        ((uint32_t)(uintptr_t)&a4_g_base + (uint32_t)a4_g_base)
#define GAME32(off)  MEM32(GBASE + (off))       /* a literal of the game's code, as the loader left it */
#define GAMEW(off)   L32(GBASE + (off))         /* the same, as the lifted code reads it */
#define GAMEADDR(off) (GBASE + (off))
#define GAMEFN(off)  (GBASE + (off) + 1u)       /* a Thumb function of the game */
#define GAMEARM(off) (GBASE + (off))            /* an ARM one (the runtime helpers, blx) */
uint64_t a4_eng_call(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp, uint32_t *r23);
uint64_t a4_eng_call0(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *r23);   /* no stack words */
void a4_mesh_insert(uint32_t *cam, uint32_t *face);  /* a4_gl.c (A4_MESH): the mesh routine's insert, kept for the list */
#ifdef A4_GPUOBJ
int a4_obj_take(uint32_t obj, uint32_t ctx, uint32_t textab, uint32_t r3);   /* a4_gl.c, round 60: 1 = drawn by the GPU */
#define A4_OBJ_TAKE(a, b, c, d) a4_obj_take((a), (b), (c), (d))
#else
#define A4_OBJ_TAKE(a, b, c, d) 0
#endif
#ifdef A4_GPUCAR
int a4_car_take(uint32_t cam, uint32_t verts, uint32_t nv, uint32_t faces, uint32_t nf, uint32_t mode, uint32_t nobf, uint32_t lr);   /* a4_gl.c */
#define A4_CAR_TAKE(c, v, nv, f, nf, m, nb, lr) a4_car_take((c), (v), (nv), (f), (nf), (m), (nb), (lr))
#else
#define A4_CAR_TAKE(c, v, nv, f, nf, m, nb, lr) 0
#endif
#ifdef A4_CARCENSUS
void a4_car_census(uint32_t ctx, uint32_t verts, uint32_t nv, uint32_t faces, uint32_t nf, uint32_t mode, uint32_t lr);   /* a4_gl.c, bench */
#define A4_CAR_CENSUS(c, v, nv, f, nf, m, lr) a4_car_census((c), (v), (nv), (f), (nf), (m), (lr))
#else
#define A4_CAR_CENSUS(c, v, nv, f, nf, m, lr) ((void)0)
#endif
void a4_gl_hud(void);
int a4_gl_bb(const uint32_t *r, const uint32_t *f);
#ifdef A4_MESHMEMO
#include "a4_memo.h"
#define MESHMEMO_BUMP() ((*(volatile uint32_t *)A4_MEMO_STAMP)++)   /* a4_gl.c: A4_MESHMEMO's generation */
#elif defined(A4_LISTDUMP)
#include "a4_memo.h"
#define MESHMEMO_BUMP() ((*(volatile uint32_t *)A4_MEMO_STAMP)++)   /* bench (round 60): the list dump's mesh call */
#else
#define MESHMEMO_BUMP() ((void)0)
#endif   /* a4_gl.c: the billboard capture hook (1: a GPU quad) */
/* +3015E's mode 2 row (+30412..+30484, a billboard added to the frame, each 565 channel saturating) by hand.
 * In: r0 the row's first pixel, r1 the v accumulator, r3 the texels, r4 the end x, r5 the u step, r12 the x,
 * lr the u mask, the frame's [0x1C] the v shift, [0x20] the v mask, [0x24] the u accumulator. Out, as the
 * game's walk leaves them: r0 past the row, r12 = the end x, [0x24] the u accumulator, [0x48] the row's v
 * part (r2, r6, r7, [8] and [0xC] it leaves too are written before any read after +30486). The texel
 * index: ((u >> 8) & umask | ((v >> 8) & vmask) << vshift) -- a shift by 32 or more is 0, as Thumb's LSL by a
 * register. The add: s = t + d, the carries out of blue (bit 5), green (11), red (16) taken off and each
 * channel they left filled, as the game computes it. */
#ifdef A4_BBCHECK
#define BB_ROW2_CHECK(r0, r12, spp) bb_row2_check(r0, r12, spp)
static void bb_row2_check(uint32_t r0, uint32_t r12, uint8_t *spp);
#else
#define BB_ROW2_CHECK(r0, r12, spp) ((void)0)
#endif
/* +CE8C's unpack loops by hand. Vertices (+CEC2..+CEDA): for each 40-byte vertex from r0 to r2, its model
 * x y z (+0x18..+0x20) from three int16 at r1, sign-extended, each loaded and stored in the game's order.
 * Faces (+CEEC..+CF48): for each 52-byte face from r0 to r2, ten bytes at r1 -- the three corners (bytes 1,
 * 0, 2: the vertex at r4 + 40 x index, to +0xC, +0x18, +0x24), the texture (byte 3: the table's entry, to
 * +0x30, stored last), six texture coordinates x16 (bytes 4..9 to +0x1C, +0x20, +0x10, +0x14, +0x28, +0x2C),
 * +4 cleared. Both walk until r0 == r2, as the game's (a count that never meets it wraps there too).
 * A4_CECHECK (bench): the arrays kept, this loop run, its result kept, the arrays put back, the lifted loop
 * then runs and CE_CHECK compares (census [100] vertex loops, [101] differing, [102] face loops, [103]
 * differing, [104] not compared: past the buffers). */
#ifdef A4_CECHECK
static void ce_check(int which);
#define CE_CHECK(w) ce_check(w)
#define CE_KEEP(lo, hi, which) do { uint32_t n_ = (hi) - (lo); volatile uint32_t *ck_ = (volatile uint32_t *)0x00460000u; \
    if (n_ > 0xBE00u || ((lo) & 3u)) { ((volatile uint32_t *)0x00412D00u)[104]++; ck_[0] = 0; break; } \
    ck_[0] = 1u + (which); ck_[1] = (lo); ck_[2] = n_; for (uint32_t k_ = 0; k_ < n_ / 4u; k_++) ((uint32_t *)0x00468100u)[k_] = L32((lo) + 4u * k_); } while (0)
#define CE_DONE(lo) do { volatile uint32_t *ck_ = (volatile uint32_t *)0x00460000u; if (ck_[0]) { uint32_t n_ = ck_[2]; \
    for (uint32_t k_ = 0; k_ < n_ / 4u; k_++) { ((uint32_t *)0x00473F00u)[k_] = L32((lo) + 4u * k_); L32((lo) + 4u * k_) = ((uint32_t *)0x00468100u)[k_]; } } } while (0)
#else
#define CE_CHECK(w) ((void)0)
#endif
static inline int ce_verts(uint32_t r0, uint32_t r1, uint32_t r2)
{
    if (r0 == r2) return 1;
#ifdef A4_CECHECK
    const uint32_t lo = r0; CE_KEEP(r0, r2, 0);
#endif
    do {
        L32(r0 + 0x18u) = (uint32_t)(int32_t)LS16(r1); r1 += 2u;
        L32(r0 + 0x1Cu) = (uint32_t)(int32_t)LS16(r1); r1 += 2u;
        L32(r0 + 0x20u) = (uint32_t)(int32_t)LS16(r1); r1 += 2u;
        r0 += 0x28u;
    } while (r0 != r2);
#ifdef A4_CECHECK
    CE_DONE(lo);
    return 0;                                       /* the lifted loop next, on the arrays as they were */
#endif
    return 1;
}
static inline int ce_faces(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r4, uint32_t tex)
{
    if (r0 == r2) return 1;
#ifdef A4_CECHECK
    const uint32_t lo = r0; CE_KEEP(r0, r2, 1);
#endif
    do {
        L32(r0 + 0x0Cu) = (uint32_t)L8(r1 + 1u) * 40u + r4;
        L32(r0 + 0x18u) = (uint32_t)L8(r1) * 40u + r4;
        L32(r0 + 0x24u) = (uint32_t)L8(r1 + 2u) * 40u + r4;
        const uint32_t t_ = L32(tex + 4u * (uint32_t)L8(r1 + 3u));
        L32(r0 + 0x1Cu) = (uint32_t)L8(r1 + 4u) << 4;
        L32(r0 + 0x20u) = (uint32_t)L8(r1 + 5u) << 4;
        L32(r0 + 0x10u) = (uint32_t)L8(r1 + 6u) << 4;
        L32(r0 + 0x14u) = (uint32_t)L8(r1 + 7u) << 4;
        L32(r0 + 0x28u) = (uint32_t)L8(r1 + 8u) << 4;
        L32(r0 + 0x2Cu) = (uint32_t)L8(r1 + 9u) << 4;
        L32(r0 + 4u) = 0;
        L32(r0 + 0x30u) = t_;
        r1 += 10u; r0 += 0x34u;
    } while (r0 != r2);
#ifdef A4_CECHECK
    CE_DONE(lo);
    return 0;
#endif
    return 1;
}
static inline int bb_row2(uint32_t *pr0, uint32_t r1, uint32_t r3, uint32_t r4, uint32_t r5, uint32_t *pr12, uint32_t lr, uint8_t *spp)
{
    const uint32_t vsh = L32(spp + 0x1Cu) & 0xFFu;
    uint32_t vp = (uint32_t)((int32_t)r1 >> 8) & L32(spp + 0x20u);
    vp = vsh >= 32u ? 0u : vp << vsh;
    uint32_t u = L32(spp + 0x24u), d = *pr0, x = *pr12;
#ifdef A4_BBCHECK
    /* bench: the row's pixels kept, this walk run, its result kept, the pixels put back; the lifted walk
     * then runs on the same row and +30486 compares (0x460000: [0] pending, [1] first pixel, [2] count,
     * [3] u, [4] x, [5] end pixel; the row before at +0x40, after at +0x840) */
    volatile uint32_t *bk = (volatile uint32_t *)0x00460000u;
    uint32_t n = (uint32_t)((int32_t)r4 - (int32_t)x);
    uint16_t *bA = (uint16_t *)0x00460040u, *bB = (uint16_t *)0x00460840u;
    if (n > 1024u) { ((volatile uint32_t *)0x00412D00u)[101]++; return 0; }
    for (uint32_t k = 0; k < n; k++) bA[k] = L16(d + 2u * k);
    const uint32_t d0 = d;
#endif
    do {
        const uint32_t t = L16(r3 + (((((uint32_t)((int32_t)u >> 8)) & lr) | vp) << 1));
        if (t) {
            const uint32_t dv = L16(d), xx = dv ^ t;
            uint32_t s = t + dv;
            const uint32_t c = (s ^ xx) & 0x20u; s -= c;
            const uint32_t c2 = (s ^ xx) & 0x800u; s -= c2;
            const uint32_t C = c | c2 | (s & 0x10000u);
            L16(d) = (uint16_t)(s | (C - (uint32_t)((int32_t)C >> 4)));
        }
        u += r5; d += 2u; x++;
    } while ((int32_t)x < (int32_t)r4);
#ifdef A4_BBCHECK
    for (uint32_t k = 0; k < n; k++) { bB[k] = L16(d0 + 2u * k); L16(d0 + 2u * k) = bA[k]; }
#ifdef A4_BBCHECK_MUTANT
    bB[0] ^= 1u;                                    /* the check's own test: one pixel wrong a row */
#endif
    bk[0] = 1u; bk[1] = d0; bk[2] = n; bk[3] = u; bk[4] = x; bk[5] = d;
    return 0;
#endif
    L32(spp + 0x24u) = u; L32(spp + 0x48u) = vp; *pr0 = d; *pr12 = x;
    return 1;
}                 /* a4_gl.c: the end of the 3D pass (the lifted HUD painter calls it) */
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
    uint32_t cbok, cbmode;           /* round 63: the chase camera's offset from its car, the whole step before's and this */
    int32_t cbprev[5], cbcur[5];     /* one's: x, z (world, at that step's heading), height, yaw less heading, pitch */
    int32_t cbhd;                    /* the car's heading at this whole step */
    uint32_t cbw;                    /* the pose words hold the blend's last write (cbwp): the game's own (cbgp) go back first */
    int32_t cbgp[5], cbwp[5];        /* cam+0x10..+0x20 (x, y, z, pitch, yaw) as the game placed them, and as written */
    uint32_t kok, kobj, kmask;       /* A4_LIVEKEYS: the key object and mask the whole step's key handler (+25E8) had */
    uint32_t kpeek, ksave[3];        /* a substep built its keys from a copy (kcopy): sp+0xDC, +0xA4, +0x10C to put back */
    uint32_t kcopy[8];               /* the key object as the next whole step's handler will find it (0x1D bytes) */
    uint32_t loop;                   /* the whole step entered the car loop (+14672): the substeps run only then */
    uint32_t meas1;                  /* +22832 measures a wreck: its displacement is a whole step's (x 1) */
    uint32_t wy0, wyset;             /* A4_WRECK3: the wreck's height before this substep's +2292A, and its path set it */
    uint32_t lc[64], ls[64][3], lv[64][3]; /* the orientation's angles a step late: the word, the last three substeps' count and value */
    uint32_t kc[16], ks[16];         /* the wall's first push: the car (+678's address) and the step it hit, bit 31 still to come */
    uint32_t dbg[8];                 /* A4_ENGDBG: the substep's path bits and the player's keys, emitted per substep */
    uint32_t rk[ENG_NREM];           /* the word's address (0: free) */
    int32_t rv[ENG_NREM];            /* its remainder, 0..2 */
};
#ifndef A4_ENG3
/* Round 51: the lifted render functions (the painter, the 3D front end) built without the 3x engine.
 * lifted.c's engine functions are compiled all the same -- static, so the unreferenced ones are dropped --
 * and their edits name the engine's state. Here it is declared and never defined: should a selected
 * function reach one of them, the link fails instead of a step running with no engine behind it. */
extern volatile struct eng a4_eng_absent;
#define ENG (&a4_eng_absent)
#define ENG_SUB (ENG->sub)
#define ENG_KEYS (ENG->keys)
#define ENG_WRECK3 0
#define ENG_SUB_END(car) (ENG_SUB == 2u || (ENG_SUB == 0u && (!ENG->loop || *(volatile uint32_t *)(uintptr_t)((car) + 0x34u) != 0xFFFFFFFFu)))
int32_t eng_sdiv(int32_t n, int32_t d);
uint32_t eng_third(uint32_t ea, uint32_t old, uint32_t nw);
uint32_t eng_ai_slot(uint32_t car);
uint32_t eng_udiv(uint32_t n, uint32_t d);
uint64_t eng_c_1b2d6(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp, uint32_t *r23);
uint32_t eng_alag(uint32_t ea, uint32_t v);
uint32_t eng_kick(uint32_t ea, uint32_t nw);
void eng_kick_mark(uint32_t ea);
uint32_t eng_tmul(uint32_t t, uint32_t speed, uint32_t which);
void eng_cam_save(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp);
void eng_cam_substep(uint32_t sp, uint32_t *r23);
void eng_cam_whole(uint32_t sp, uint32_t *r23);
void eng_cam_unblend(void);
#define ENG_KPEEK(sp) 0
#define ENG_KMERGE(sp) ((void)0)
int32_t eng_thirdv(uint32_t key, int32_t v);
void ENG_BEGIN(uint32_t race);
extern const uint16_t eng_frame_off[10];
#define ENG_NFRAME 10u
#endif
#if !defined(A4_ENG3) && !defined(A4_FASTXF)
uint64_t eng_h_345e4(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp, uint32_t *r23);   /* a4_eng.c, A4_FASTXF */
#endif
#ifdef A4_ENG3
#define A4_ENG_STATE 0x0045D000u
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
#ifdef A4_WRECK3
#define ENG_WRECK3 1
/* round 65: a wreck runs on every substep, so its last substep of the step is 2 like any car's */
#define ENG_SUB_END(car) (ENG_SUB == 2u || (ENG_SUB == 0u && !ENG->loop))
#else
#define ENG_WRECK3 0
#define ENG_SUB_END(car) (ENG_SUB == 2u || (ENG_SUB == 0u && (!ENG->loop || *(volatile uint32_t *)(uintptr_t)((car) + 0x34u) != 0xFFFFFFFFu)))
#endif
static inline void ENG_BEGIN(uint32_t race)
{
    if (ENG->magic != ENG_MAGIC || ENG->race != race) {
        ENG->magic = ENG_MAGIC; ENG->race = race; ENG->cnt = 0; ENG->keys = 0; ENG->nfull = 0; ENG->cbok = 0;
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
 * lists and free their nodes as they go: not made again, their pose waits for the whole step.
 * Round 63 (A4_CAMPROBE, the crash-steer route): the chase camera's offset from its car -- race+0xE4, the
 * sway +4B40 recomputes from cam+0xA4 on every call, the stack argument +12DEC passes -- changes only on the
 * whole step, by up to 63/2048 of yaw at once: the yaw held for two substeps and jumped on the third, the
 * camera stepping at 11.72 Hz in a hard turn (14 one-frame yaw reversals in the crash-steer run). So the
 * chase family (1, 2, 3, 0x2A) is placed from the car on every substep, the whole step's included: the game's
 * pose at each whole step taken as an offset from the car (x, z turned with the car, height, yaw less the
 * heading, pitch), and every substep's pose the car's place and heading now plus the offset moved from the
 * step before's to this one's over the step -- a step late, as round 59's blend (the owner: "the camera looks
 * fine"). The game's own placement still runs on the substeps (the track segment it finds). A new mode, a
 * respawn or a jump in the offset: the step's own offset at once. */
#define ENG_CAMW 56u
static inline int32_t eng_wrap(int32_t d) { d &= 2047; return d >= 1024 ? d - 2048 : d; }
/* a small turn of (ox, oz) by dh /2048 (|dh| < 256), as a4_gl.c's kd_rot (Taylor, Q30) */
static inline void eng_rot(int32_t ox, int32_t oz, int32_t dh, int32_t *rx, int32_t *rz)
{
    int64_t t = -(int64_t)dh * 3294199;
    int64_t t2 = (t * t) >> 30, t3 = (t2 * t) >> 30, t4 = (t2 * t2) >> 30, t5 = (t4 * t) >> 30, t6 = (t4 * t2) >> 30;
    int64_t sn = t - ((t3 * 178956971) >> 30) + ((t5 * 8947849) >> 30);
    int64_t cs = ((int64_t)1 << 30) - (t2 >> 1) + ((t4 * 44739243) >> 30) - ((t6 * 1491308) >> 30);
    *rx = (int32_t)(((int64_t)ox * cs - (int64_t)oz * sn) >> 30);
    *rz = (int32_t)(((int64_t)ox * sn + (int64_t)oz * cs) >> 30);
}
static inline int go_cb_far(const int32_t *c, volatile const int32_t *p)   /* the offset jumped: a cut or a respawn */
{
    int32_t dx = c[0] - p[0], dz = c[1] - p[1], dy = c[2] - p[2], dyaw = eng_wrap(c[3] - p[3]);
    return dx > 1024 || dx < -1024 || dz > 1024 || dz < -1024 || dy > 512 || dy < -512 || dyaw > 128 || dyaw < -128;
}
/* Round 65 (A4_CAMSCRIPT3): the crash and takedown camera (mode 0x18, +554C) on the substeps. Its pose comes from
 * a keyframe script (cam+0x248: +5205A plays it, +3D2A turns its eleven channels and the car into the pose) whose
 * player moves each channel, once a whole step, from its value toward the first keyframe in its list by
 * (keyframe - value) / (keyframe's step - clock), and takes the keyframe and unlinks it once its step is reached --
 * so the next whole step's value of every channel is already fixed by the list. A substep j places the camera with
 * each channel j thirds of the way from its value now to that next value (the script's own straight line), on the
 * car where it is now; the player itself is held off (the script's done byte set for the call) and every channel
 * put back after, so the script, its clock and its lists stay the whole step's. Not when the mode's setup has not
 * run (cam+0xC0 not 0x18: +554C would run +54E8, which allocates). Kept from the substep: the pose +3D2A writes
 * (cam+0xC channel 10, +0x10..+0x24), +554C's cam+0x28, +0x2C, +0x44 -- outputs it makes afresh each call. */
static inline uint32_t eng_udiv(uint32_t n, uint32_t d);
/* a signed divide truncating toward zero, as the game's (+50E8C) */
static inline int32_t eng_sdiv(int32_t n, int32_t d)
{
    const uint32_t q = eng_udiv(n < 0 ? 0u - (uint32_t)n : (uint32_t)n, d < 0 ? 0u - (uint32_t)d : (uint32_t)d);
    return (n < 0) != (d < 0) ? -(int32_t)q : (int32_t)q;
}
#ifdef A4_CAMSCRIPT3
static inline void eng_cam_script(uint32_t sp, uint32_t *r23, uint32_t j, volatile uint32_t *cam)
{
    const uint32_t ob = (uint32_t)(uintptr_t)cam + 0x248u;
    volatile uint32_t *o = (volatile uint32_t *)(uintptr_t)ob;
    volatile uint8_t *done = (volatile uint8_t *)(uintptr_t)(ob + 0x88u);
    const int32_t clk = (int32_t)o[0x8Cu / 4u], end = (int32_t)o[0x90u / 4u];
    int32_t cur[11];
    uint32_t save[ENG_CAMW], k, live = 0;
    if (*done || (end && clk >= end)) return;
    for (k = 0; k < 11u; k++) {
        const uint32_t head = o[0x94u / 4u + k], first = MEM32(head);
        int32_t nx;
        cur[k] = (int32_t)o[0x5Cu / 4u + k];
        if (first == head) continue;
        live = 1;
        {
            const int32_t v = (int32_t)MEM32(first + 8u), t = (int32_t)MEM32(first + 12u);
            /* the player's own arithmetic: 32-bit, (v << 12 - value) / (step - clock) */
            nx = t > clk ? cur[k] + eng_sdiv((int32_t)(((uint32_t)v << 12) - (uint32_t)cur[k]), t - clk) : (int32_t)((uint32_t)v << 12);
        }
        o[0x5Cu / 4u + k] = (uint32_t)(cur[k] + eng_sdiv((nx - cur[k]) * (int32_t)j, 3));
    }
    if (!live) return;
    for (k = 0; k < ENG_CAMW; k++) save[k] = cam[k];
    *done = 1;
    for (k = 0; k < 4u; k++) ((volatile uint32_t *)(uintptr_t)sp)[k] = ENG->cam[4 + k];
    (void)a4_eng_call(GAMEFN(0x5968), ENG->cam[0], ENG->cam[1], ENG->cam[2], ENG->cam[3], sp, r23);
    *done = 0;
    for (k = 0; k < 11u; k++) o[0x5Cu / 4u + k] = (uint32_t)cur[k];
    for (k = 0; k < ENG_CAMW; k++) if (!((k >= 3u && k <= 11u) || k == 17u)) cam[k] = save[k];
#ifdef A4_CAMPROBE
    {
        extern void a4_camp_emit(const void *);
        volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
        uint32_t i = rg[0] & 255u;
        volatile uint32_t *e = rg + 4u + 12u * i;
        volatile int32_t *car = (volatile int32_t *)(uintptr_t)ENG->cam[2];
        e[0] = ENG->cnt; e[1] = j | (0x18u << 8) | 0x10000u; e[2] = (uint32_t)car[0x678 / 4]; e[3] = (uint32_t)car[1];
        e[4] = (uint32_t)car[3]; e[5] = (uint32_t)clk; e[6] = save[8]; e[7] = cam[8];
        e[8] = save[4]; e[9] = cam[4]; e[10] = save[6]; e[11] = cam[6];
        rg[0] = i + 1u;
        if (((i + 1u) & 255u) == 0u) a4_camp_emit((const void *)(rg + 4u));
    }
#endif
}
#endif
static inline void eng_cam_pose(uint32_t sp, uint32_t *r23, uint32_t j)
{
    volatile uint32_t *cam = (volatile uint32_t *)(uintptr_t)ENG->cam[0];
    volatile int32_t *car = (volatile int32_t *)(uintptr_t)ENG->cam[2];
    uint32_t mode, k, save[ENG_CAMW], chase;
    if (!ENG->camv || !cam) return;
    mode = cam[0x4C / 4];
    if (j == 0u) ENG->cbw = 0;
    if (!(mode <= 7u || mode == 0x2Au)) {
        ENG->cbok = 0;
#ifdef A4_CAMSCRIPT3
        if (mode == 0x18u && j != 0u && car && cam[0xC0u / 4u] == 0x18u) eng_cam_script(sp, r23, j, cam);
#endif
        return;
    }
    chase = car && (mode == 1u || mode == 2u || mode == 3u || mode == 0x2Au);
    for (k = 0; k < ENG_CAMW; k++) save[k] = cam[k];
    if (j == 0u) {                                   /* the whole step: the game's pose is the target */
        int32_t c[5], hd;
        if (!chase) { ENG->cbok = 0; return; }
        hd = car[0x678 / 4] & 2047;
        c[0] = (int32_t)save[4] - car[1]; c[1] = (int32_t)save[6] - car[3]; c[2] = (int32_t)save[5] - car[2];
        c[3] = eng_wrap((int32_t)save[8] - hd); c[4] = (int32_t)save[7];
        if (!ENG->cbok || ENG->cbmode != mode || go_cb_far(c, ENG->cbcur) || eng_wrap(hd - ENG->cbhd) > 255 || eng_wrap(hd - ENG->cbhd) < -255) {
            for (k = 0; k < 5u; k++) ENG->cbprev[k] = c[k];
        } else {                                     /* the step before's offset, turned to this step's heading */
            int32_t rx, rz;
            eng_rot(ENG->cbcur[0], ENG->cbcur[1], eng_wrap(hd - ENG->cbhd), &rx, &rz);
            ENG->cbprev[0] = rx; ENG->cbprev[1] = rz;
            for (k = 2; k < 5u; k++) ENG->cbprev[k] = ENG->cbcur[k];
        }
        for (k = 0; k < 5u; k++) { ENG->cbcur[k] = c[k]; ENG->cbgp[k] = (int32_t)save[4 + k]; }
        ENG->cbhd = hd; ENG->cbok = 1; ENG->cbmode = mode;
    } else {
        for (k = 0; k < 4u; k++) ((volatile uint32_t *)(uintptr_t)sp)[k] = ENG->cam[4 + k];
        (void)a4_eng_call(GAMEFN(0x5968), ENG->cam[0], ENG->cam[1], ENG->cam[2], ENG->cam[3], sp, r23);
    }
    if (chase && ENG->cbok && ENG->cbmode == mode) {   /* the pose: the car now, the offset a third further a substep */
        int32_t o[5], hd = car[0x678 / 4] & 2047, dh = eng_wrap(hd - ENG->cbhd), rx, rz;
        for (k = 0; k < 5u; k++) {
            const int32_t a = ENG->cbprev[k], d = k == 3u ? eng_wrap(ENG->cbcur[k] - a) : ENG->cbcur[k] - a;
            o[k] = a + (j == 0u ? 0 : j == 1u ? d / 3 : (2 * d) / 3);
        }
        if (dh < 256 && dh > -256) {
            eng_rot(o[0], o[1], dh, &rx, &rz);
            cam[4] = (uint32_t)(car[1] + rx); cam[6] = (uint32_t)(car[3] + rz);
            cam[5] = (uint32_t)(car[2] + o[2]);
            cam[8] = (uint32_t)((hd + o[3]) & 2047); cam[7] = (uint32_t)o[4];
            for (k = 0; k < 5u; k++) ENG->cbwp[k] = (int32_t)cam[4 + k];
            ENG->cbw = 1;
        }
    }
#ifdef A4_CAMPROBE
    {   /* bench (round 63): the pose this placement inherits (the whole step's, or the substep before's) and the
         * one it makes, with the car's turn terms and the camera's own state -- 12 words a record, a ring of 256 at
         * 0x5A2000 (A4_SCRATCH_SIZE >= 0x1A6000), handed to a4_camp_emit (EKA2L1_PCMEM) when it wraps */
        extern void a4_camp_emit(const void *);
        volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
        uint32_t i = rg[0] & 255u;
        volatile uint32_t *e = rg + 4u + 12u * i;
        e[0] = ENG->cnt; e[1] = j | (mode << 8); e[2] = car ? (uint32_t)car[0x678 / 4] : 0; e[3] = car ? (uint32_t)car[0x808 / 4] : 0;
        e[4] = car ? (uint32_t)car[0x9F8 / 4] : 0; e[5] = car ? (uint32_t)car[0xB28 / 4] : 0; e[6] = save[8]; e[7] = cam[8];
        e[8] = save[47]; e[9] = save[39]; e[10] = save[7]; e[11] = cam[7];
        rg[0] = i + 1u;
        if (((i + 1u) & 255u) == 0u) a4_camp_emit((const void *)(rg + 4u));
    }
#endif
    if (j != 0u) for (k = 0; k < ENG_CAMW; k++) if (k < 4u || k > 11u) cam[k] = save[k];
}
static inline void eng_cam_substep(uint32_t sp, uint32_t *r23) { eng_cam_pose(sp, r23, ENG->sub); }
static inline void eng_cam_whole(uint32_t sp, uint32_t *r23) { eng_cam_pose(sp, r23, 0u); }
/* Before the whole step's camera code (+141B0 at +14CB2): the pose words the blend wrote go back to the
 * game's own last placement, so the game's camera reads its own pose, not the blend's -- unless something
 * else has written them since (a respawn, a mode change), which is then left as it is. */
static inline void eng_cam_unblend(void)
{
    volatile uint32_t *cam = (volatile uint32_t *)(uintptr_t)ENG->cam[0];
    uint32_t k, same = 1;
    if (!ENG->cbw || !ENG->camv || !cam) { ENG->cbw = 0; return; }
    for (k = 0; k < 5u; k++) if ((int32_t)cam[4 + k] != ENG->cbwp[k]) same = 0;
    if (same) for (k = 0; k < 5u; k++) cam[4 + k] = (uint32_t)ENG->cbgp[k];
    ENG->cbw = 0;
}
/* Round 64 (A4_LIVEKEYS): the steering read on every substep. The game's key handler (+25E8, the step loop's
 * +167AE) runs before a whole step only, as the game's (a press it latched on a substep would be gone by the
 * step that acts on it), so a key reached the car up to 85 ms after it went down. A substep now builds the
 * player's key word as the whole step does (+1477C..+14946, the 7/8 view keys skipped) from a copy of the key
 * object with the handler run on the copy -- the state the next whole step's handler will find, the game's
 * own object untouched (its edges, its double-tap count, the releases +25B6 posts) -- and takes from it the
 * steering bits only (1, 2, 0x200, 0x400: actions 2-5, held keys); the presses (4 the start, 0x20, 0x40) and
 * the brake (8, which the auto-throttle's 4 excludes) stay the whole step's, as the race rules and the
 * whole-step decisions (+1BE86, +1B5DA) need them. A key down before substep 1 steers two substeps sooner,
 * before substep 2 one. Not in a race's end (+128's +28 > 12: the key word forced to 8) or when the object
 * is not the one the handler had. */
#ifdef A4_LIVEKEYS
#define ENG_LIVEMASK 0x603u
static inline int eng_keys_peek(uint32_t sp)
{
    const uint32_t ko = MEM32(sp + 0xDCu), rs = MEM32(sp + 0x128u);
    uint32_t k, stk[8] = { 0 }, o23[2];
    if (!ENG->kok || ko != ENG->kobj || (ko & 3u) || !rs || MEM32(rs + 0x28u) > 0xCu) return 0;
    for (k = 0; k < 8u; k++) ENG->kcopy[k] = MEM32(ko + 4u * k);
    (void)a4_eng_call(GAMEFN(0x25E8), (uint32_t)(uintptr_t)ENG->kcopy, ENG->kmask, 0, 0, (uint32_t)(uintptr_t)stk, o23);
    ENG->ksave[0] = ko; ENG->ksave[1] = MEM32(sp + 0xA4u); ENG->ksave[2] = MEM32(sp + 0x10Cu);
    MEM32(sp + 0xDCu) = (uint32_t)(uintptr_t)ENG->kcopy;
    ENG->kpeek = 1;
    return 1;
}
static inline void eng_keys_merge(uint32_t sp)
{
    uint32_t live;
    if (!ENG->kpeek) return;
    ENG->kpeek = 0;
    MEM32(sp + 0xDCu) = ENG->ksave[0]; MEM32(sp + 0xA4u) = ENG->ksave[1]; MEM32(sp + 0x10Cu) = ENG->ksave[2];
    live = MEM32(sp + 0xD0u);
    MEM32(sp + 0xD0u) = (ENG_KEYS & ~ENG_LIVEMASK) | (live & ENG_LIVEMASK);
#ifdef A4_KEYPROBE
    {   /* bench (round 64): per substep, the whole step's key word and the live one -- 4 words a record, a ring
         * of 768 at 0x5A2000 (A4_SCRATCH_SIZE >= 0x1A6000), handed to a4_camp_emit (EKA2L1_PCMEM) when it wraps */
        extern void a4_camp_emit(const void *);
        volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
        uint32_t i = rg[0] % 768u;
        volatile uint32_t *e = rg + 4u + 4u * i;
        e[0] = ENG->cnt; e[1] = ENG->sub; e[2] = ENG_KEYS; e[3] = live;
        rg[0] = i + 1u;
        if (i + 1u == 768u) { rg[0] = 0; a4_camp_emit((const void *)(rg + 4u)); }
    }
#endif
}
#define ENG_KPEEK(sp) eng_keys_peek(sp)
#define ENG_KMERGE(sp) eng_keys_merge(sp)
#else
#define ENG_KPEEK(sp) 0
#define ENG_KMERGE(sp) ((void)0)
#endif
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
