/* a4_eng.h -- round 50: the game's engine functions rewritten in C (renderer/eng/, first drafted by
 * tools/lift.py from the game's own Thumb code, exact to the instruction). Memory, the game's code
 * (literals read where the loader relocated them), calls into the game (a4_eng_call, a4_eng.s: eight
 * stack words passed, r0:r1 returned) and the barrel shifter's carry. */
#ifndef A4_ENG_H
#define A4_ENG_H
#include <stdint.h>
#define MEM32(a)  (*(volatile uint32_t *)(uintptr_t)(a))
#ifdef A4_STKPOISON
void a4_stkpoison(uint32_t *p, uint32_t n);   /* bench: a4_eng.c */
#endif
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
#ifdef A4_FLAREFLUSH
void a4_gl_flare_pre(void);                          /* a4_gl.c, round 88: the frame composited before +37FA4 */
#define A4_FLARE_PRE() a4_gl_flare_pre()
#else
#define A4_FLARE_PRE() ((void)0)
#endif
#ifdef A4_GPUOBJ
int a4_obj_take(uint32_t obj, uint32_t ctx, uint32_t textab, uint32_t r3);   /* a4_gl.c, round 60: 1 = drawn by the GPU */
#define A4_OBJ_TAKE(a, b, c, d) a4_obj_take((a), (b), (c), (d))
#else
#define A4_OBJ_TAKE(a, b, c, d) 0
#endif
#ifdef A4_GPUCAR
int a4_car_take(uint32_t cam, uint32_t verts, uint32_t nv, uint32_t faces, uint32_t nf, uint32_t mode, uint32_t nobf, uint32_t lr);   /* a4_gl.c */
int a4_car_early(uint32_t cam, uint32_t verts, uint32_t nv, uint32_t faces, uint32_t nf, uint32_t mode, uint32_t nobf, uint32_t lr);
void a4_car_earlycheck(uint32_t cam);
#define A4_CAR_TAKE(c, v, nv, f, nf, m, nb, lr) a4_car_take((c), (v), (nv), (f), (nf), (m), (nb), (lr))
#define A4_CAR_EARLY(c, v, nv, f, nf, m, nb, lr) a4_car_early((c), (v), (nv), (f), (nf), (m), (nb), (lr))
#ifdef A4_CAREARLYCHECK
#define A4_CAR_EARLYCHECK(c) a4_car_earlycheck(c)
#else
#define A4_CAR_EARLYCHECK(c) ((void)0)
#endif
#else
#define A4_CAR_TAKE(c, v, nv, f, nf, m, nb, lr) 0
#define A4_CAR_EARLY(c, v, nv, f, nf, m, nb, lr) 0
#define A4_CAR_EARLYCHECK(c) ((void)0)
#endif
#ifdef A4_CARANIM
void a4_car_anim(uint32_t car);                      /* a4_gl.c, round 88: the car +1B084 animates this render */
#define A4_CAR_ANIM(car) a4_car_anim(car)
#else
#define A4_CAR_ANIM(car) ((void)0)
#endif
#ifdef A4_PHTIMERS
/* Round 86 (the owner's heavy scenes, measured on the phone): NTickCount (1 ms) around the scene's calls in the lifted
 * +D3BA, summed from the boot in CRK1's words (a4_gl.c A4_GL_CRK, CRK_N 62): [52] scenes drawn, [53] the scenery walk
 * +CF6E, [54] the cars +34AEA, [55] the shadow's insert +20918, [56] +31A26 and +31A50, [57] the ordering table's draw
 * +31CC6 (our draw hooks and the composite within it); the composite's (a4_gl.c) [58] first readback strip (the GPU's
 * tile render and a strip), [59] the other strips, [60] composites; [61] the timed call's start + 1 (0: none). One
 * tick per call is coarse; summed over a window's ~230 renders the means are not. */
uint32_t a4_ntick(void);
#define ENG_TIM_W ((volatile uint32_t *)0x005C0800u)
static inline void eng_tim0(void) { ENG_TIM_W[61] = a4_ntick() + 1u; }
static inline void eng_tim1(uint32_t k)
{
    const uint32_t t = ENG_TIM_W[61];
    if (t) { ENG_TIM_W[k] += a4_ntick() + 1u - t; ENG_TIM_W[61] = 0; }
}
#define ENG_TIM0() eng_tim0()
#define ENG_TIM1(k) eng_tim1(k)
#define ENG_TIMC() (ENG_TIM_W[52]++)
#else
#define ENG_TIM0() ((void)0)
#define ENG_TIM1(k) ((void)0)
#define ENG_TIMC() ((void)0)
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
#define ENG_NSPIN 16u                /* round 74: the cars a contact turned, spread over a step (eng_spin) */
struct eng {
    uint32_t magic, race;            /* reset when a new race object appears */
    uint32_t cnt;                    /* race steps run (substeps) */
    uint32_t sub;                    /* the substep running: 0 the game's whole step, 1 .. n - 1 motion only */
    uint32_t n;                      /* round 73: this whole step's substeps -- 3; 6 in the game's slow motion (A4_ENG6) */
    uint32_t wst;                    /* round 73: whole steps begun (the step a wall's push belongs to) */
    uint32_t keys;                   /* the player's keys as substep 0 built them, replayed on 1 and 2 */
    uint32_t nfull;                  /* words the remainder table had no room for (kept as plain thirds) */
    uint32_t nsnd;                   /* the step's effect calls (+1B2D6) so far: one per car and effect a step */
    uint32_t flush;                  /* bench (A4_BENCH_INJECT): a car was overwritten -- the lag rings start again */
    uint32_t sndc[64], sndk[64];     /* their car and effect */
    uint32_t tlast[2];               /* the traffic's two clocks (race+0x378, +0x37C) at the last whole step */
    int32_t tdel[2];                 /* and their change over that step */
    uint32_t aic[32], aik[32], aia[32]; /* the AI's keys and argument per car, from the whole step */
    uint32_t sbc[16], sbb[16];       /* the steering rate's last update per car: the car, its branch (1-4) */
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
    uint32_t pc[8], pa[8][3];        /* the pursuers (race slots 8-15) at the whole step's start: car, +0A8 */
    int32_t pw[8][7], pt[8][7];      /* their place, light and heading then (D), and where the game put them (T) */
    int32_t pown[8][3];              /* their own move since (+1E8C0): the rest is the game placing them */
    uint32_t ps[8], pn[8];           /* a pursuer the substeps are carrying from D to T, and how many words (3 or 7) */
    uint32_t wy0, wyset;             /* A4_WRECK3: the wreck's height before this substep's +2292A, and its path set it */
    uint32_t lc[64], ls[64][6], lv[64][6]; /* the orientation's angles a step late: the word, the last six substeps' count and value */
    uint32_t kc[16], ks[16];         /* the wall's first push: the car (+678's address) and the step it hit, bit 31 still to come */
    uint32_t targ, tkp, tkv;         /* round 71: +FB12's first argument; a knocked car it placed; the cars on a line */
    int32_t tsp[16][4], tkt[16][4];  /* the traffic at the whole step before +FB12 (D: x, y, z, +2C) and after (T) */
    uint32_t svc[16], svw[16], svok; /* round 73: the slip check's cars, the whole step each was last checked at, the car
                                      * whose whole-step velocity svv holds */
    int32_t svp[16][3], svv[3];      /* their position then, and the step's displacement (A4_ENG6's sixths) */
    int32_t sclk;                    /* round 73: the crash camera script's clock after the whole step's tick (A4_ENG6) */
    uint32_t cmid;                   /* round 73: this step of sixths' +1300C has run (its camera a second target) */
    uint32_t dbg[8];                 /* A4_ENGDBG: the substep's path bits and the player's keys, emitted per substep */
    uint32_t rk[ENG_NREM];           /* the word's address (0: free) */
    int8_t rv[ENG_NREM];             /* its remainder, 0 .. n - 1 (bytes since round 71: the traffic's lines needed the room) */
    int32_t tkd[16][4];              /* round 74: a knocked car's own drift over its last step, n substeps' worth (x, y, z, +2C) */
    uint32_t tkds[16];               /* the substep count that drift was measured at (valid for the next step's substep 0) */
    uint32_t spc[ENG_NSPIN];         /* round 74: a car a contact turned (0: free) -- eng_spin */
    uint32_t spo[ENG_NSPIN];         /* the car it met (a kick a step for each pair, as the game's response gives) */
    uint32_t sps[ENG_NSPIN];         /* the whole step its kick belongs to */
    int32_t spr[ENG_NSPIN];          /* the kick's part still to turn */
    uint32_t spt[ENG_NSPIN];         /* the substep count it last turned at */
    uint8_t spl[ENG_NSPIN];          /* the substeps the rest is spread over */
};
#ifdef A4_KICKREC
/* bench (round 73): a probe of the engine's address-keyed tables -- each table's evictions (a key that found its
 * table full and took another key's slot) in dbg[0..4]: the slip check's, the orientation lag's, the steering's,
 * the wall push's, the remainders' (as nfull) */
#define ENG_EVIC(k) (ENG->dbg[(k)]++)
#else
#define ENG_EVIC(k) ((void)0)
#endif
#ifndef A4_ENG3
/* Round 51: the lifted render functions (the painter, the 3D front end) built without the 3x engine.
 * lifted.c's engine functions are compiled all the same -- static, so the unreferenced ones are dropped --
 * and their edits name the engine's state. Here it is declared and never defined: should a selected
 * function reach one of them, the link fails instead of a step running with no engine behind it. */
extern volatile struct eng a4_eng_absent;
#define ENG (&a4_eng_absent)
#define ENG_IS3 0
#define ENG_SUB (ENG->sub)
#define ENG_KEYS (ENG->keys)
#define ENG_WRECK3 0
#define ENG_SUB_END(car) (ENG_SUB + 1u == ENG->n || (ENG_SUB == 0u && (!ENG->loop || *(volatile uint32_t *)(uintptr_t)((car) + 0x34u) != 0xFFFFFFFFu)))
int32_t eng_sdiv(int32_t n, int32_t d);
int32_t eng_divn(int32_t v);
uint32_t eng_third(uint32_t ea, uint32_t old, uint32_t nw);
uint32_t eng_ai_slot(uint32_t car);
void eng_pown(uint32_t car, uint32_t j, int32_t d);
void eng_pfix(uint32_t car);
void eng_pstep(uint32_t car);
uint32_t eng_steer_upd(uint32_t car, uint32_t br);
void eng_slipmark(uint32_t car);
uint32_t eng_slipsrc(uint32_t car);
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
#define ENG_VP(t, car, a, b, c, d, e) ((void)0)
#define ENG_VPA(t, car, a, b, c, d, e) ((void)0)
#define ENG_TDARG(a) ((void)0)
#define ENG_TDREC(race, tag) ((void)0)
#define ENG_SPINREC(s_, c_, o_, n_, a_, d_) ((void)0)
#define ENG_HDREC(a_, ea_, v_) ((void)0)
#define ENG_WPROBE(f_) ((void)0)
#define ENG_TCLK(w) (ENG->tlast[(w)])
void eng_tsnap(uint32_t arg);
void eng_tknock(uint32_t car);
int32_t eng_thirdv(uint32_t key, int32_t v);
int32_t eng_spin(uint32_t car, uint32_t other, int32_t k);
void eng_spin_apply(uint32_t car);
void ENG_BEGIN(uint32_t race);
extern const uint16_t eng_frame_off[10];
#define ENG_NFRAME 10u
#endif
#if !defined(A4_ENG3) && !defined(A4_FASTXF)
uint64_t eng_h_345e4(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp, uint32_t *r23);   /* a4_eng.c, A4_FASTXF */
#endif
/* Phone round 76 (A4_TELMARK, A4_FRC3; a4_gl.c, a4_tel.c): the mark's state -- [0] presses of C, [1] presses
 * handled, [2] marks written, [3] the last one's tick, [4..14] FRC3's counters before the render, [16] the last press's tick; RFile objects for the mark files from
 * +0x80 -- then FRC3 at 0x5B0400 (A4_FREC3_N renders of A4_FREC3_W words), the render's sprite list at 0x5B9800 and the
 * list's tiles at 0x5B9A00 (A4_SCRATCH_SIZE 0x1B9000: the scratch to 0x5BA000) */
#define A4_MARK_STATE 0x005B0000u
#define A4_GL_FREC3   0x005B0400u
#define A4_GL_SPL     0x005B9800u
#define A4_GL_GRD     0x005B9A00u
#define A4_FREC3_N    256u
#define A4_FREC3_W    36u
/* Round 77 (A4_WRECKFRAC): the wrecks whose path rate +2292A gave (8), that rate (1 or 2), and the keyframe record
 * blended for the draw -- in the mark block's spare words (0x5B0200; the engine's block is full) */
struct wreckst { uint32_t wrc[8]; uint8_t wrd[8]; int32_t wkey[16]; uint32_t wcn[8]; };
#define WRK ((volatile struct wreckst *)0x005B0200u)
_Static_assert(sizeof(struct wreckst) <= 0x200u, "the wreck state runs into FRC3 (0x5B0400)");
#ifdef A4_ENG3
#define A4_ENG_STATE 0x0045D000u
_Static_assert(A4_ENG_STATE + sizeof(struct eng) <= 0x00460000u, "the engine state past the scratch block");
#define ENG ((volatile struct eng *)A4_ENG_STATE)
#define ENG_IS3 1
#ifdef A4_TRAFDUMP
#define ENG_TDARG(a) (ENG->dbg[5] = (a))
#else
#define ENG_TDARG(a) ((void)0)
#endif
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
/* round 65: a wreck runs on every substep, so its last substep of the step is n - 1 like any car's */
#define ENG_SUB_END(car) (ENG_SUB + 1u == ENG->n || (ENG_SUB == 0u && !ENG->loop))
#else
#define ENG_WRECK3 0
#define ENG_SUB_END(car) (ENG_SUB + 1u == ENG->n || (ENG_SUB == 0u && (!ENG->loop || *(volatile uint32_t *)(uintptr_t)((car) + 0x34u) != 0xFFFFFFFFu)))
#endif
/* Round 73 (phone round 70: the takedown and crash cameras "in stop motion"): the game's slow motion is its step
 * count at half rate (race+0x84C 512), a substep of a third every 56.9 ms, so frames repeated. With A4_ENG6 a whole
 * step that begins in slow motion runs as six substeps of a sixth, one every 28.44 ms -- every value the engine
 * divided by 3 is divided by the step's n. A value over n, truncated toward zero as v / 3 was (n is 3 or 6). */
static inline int32_t eng_divn(int32_t v)
{
    if (ENG->n == 6u) { int32_t r = v / 6; __asm__("" : "+r"(r)); return r; }   /* the barrier keeps the two constant */
    return v / 3;                                                              /* divides apart (merged: a libgcc call) */
}
/* Phone rounds 69-70 (edits.py at +1E8C0 and +1EA68): a pursuer (+C3C: the helicopter, the cops) moved by the game's
 * own logic on the whole step (+1C48E's placement and wobble, the police's) -- a jump a whole step long. At 3x it was
 * measured as a substep's move (x3: the error flipped and doubled each step) and drawn at 11.72 Hz. r70 spread the
 * jump with the substeps' give-back hidden from their measures; the velocity then read the jump on the whole step
 * and not on the substeps -- a sawtooth, the acceleration +-thousands each substep (hr71b), and the helicopter
 * rocked again (phone 70b). Now the pursuer is drawn on the straight line from where it began the step (D) to where
 * the game put it (T): D + (T - D)/3 after the whole step, 2/3 after substep 1, T after substep 2 (its substeps'
 * own moves overridden, as the game's next placement overrides them); the measure untouched reads T - D a step on
 * every substep, stock's velocity. The helicopter's light aim point (+C50..+C58: the spot on the road ahead of the
 * car it follows, set on the whole step only -- the owner's "freezes on the same spot for two or three frames then
 * updates its position forward") and its heading (+678) move the same way. Not when nothing but its own physics
 * moved it, when +0A8 was set in the step (a respawn, a hit, a set velocity), for a wreck or type 5. */
static const uint16_t eng_pxo[7] = { 4u, 8u, 0xCu, 0xC50u, 0xC54u, 0xC58u, 0x678u };
static inline void eng_psnap(uint32_t race)
{
    uint32_t k, j;
    for (k = 0; k < 8u; k++) {                         /* the police slots: race slots 8-15 */
        const uint32_t car = *(volatile uint32_t *)(uintptr_t)(race + 0x394u + 4u * (k + 8u));
        volatile int32_t *c = (volatile int32_t *)(uintptr_t)car;
        if (ENG->ps[k] && ENG->ps[k] == car)              /* a line its substeps did not finish: finished now, */
            for (j = 0; j < ENG->pn[k]; j++) {            /* the position's move out of the measure's sight */
                const int32_t d = ENG->pt[k][j] - c[eng_pxo[j] / 4u];
                c[eng_pxo[j] / 4u] = ENG->pt[k][j];
                if (j < 3u) c[0xA8 / 4 + j] = (int32_t)((uint32_t)c[0xA8 / 4 + j] + (uint32_t)d);
            }
        ENG->ps[k] = 0; ENG->pc[k] = 0;
        if (!car || !*(volatile uint8_t *)(uintptr_t)(car + 0xC3Cu)) continue;
        ENG->pc[k] = car;
        for (j = 0; j < 7u; j++) ENG->pw[k][j] = c[eng_pxo[j] / 4u];
        for (j = 0; j < 3u; j++) { ENG->pa[k][j] = (uint32_t)c[0xA8 / 4 + j]; ENG->pown[k][j] = 0; }
    }
}
static inline void eng_pown(uint32_t car, uint32_t j, int32_t d)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) if (ENG->pc[k] == car) { ENG->pown[k][j] += d; return; }
}
static inline int32_t eng_sdiv(int32_t n, int32_t d);
static inline int32_t eng_wrap(int32_t d);
/* the point j n-ths of the way from a to b (word 6, the heading, the short way round: 2048 a turn) */
static inline int32_t eng_pline(uint32_t w, int32_t a, int32_t b, uint32_t j)
{
    const int32_t d = w == 6u ? eng_wrap(b - a) : b - a;
    const int32_t v = a + eng_sdiv((int32_t)j * d, (int32_t)ENG->n);
    return w == 6u ? (v & 2047) : v;
}
/* the whole step, at the pursuer's move (+1EA64's entry, before its measure) */
static inline void eng_pfix(uint32_t car)
{
    uint32_t k, j, n;
    for (k = 0; k < 8u; k++) {
        volatile int32_t *c;
        uint32_t moved = 0;
        if (ENG->pc[k] != car) continue;
        ENG->pc[k] = 0;
        c = (volatile int32_t *)(uintptr_t)car;
        if ((uint32_t)c[0x34 / 4] != 0xFFFFFFFFu) return;                       /* a wreck: its measure is a step's */
        if (c[0xC70 / 4] == 5) return;                                          /* type 5: +1EA64 does not move it */
        for (j = 0; j < 3u; j++) if ((uint32_t)c[0xA8 / 4 + j] != ENG->pa[k][j]) return;   /* +0A8 set this step */
        for (j = 0; j < 3u; j++) {
            const int32_t w = (int32_t)((uint32_t)c[1 + j] - (uint32_t)ENG->pw[k][j]) - ENG->pown[k][j];
            if (w <= -0x40000 || w >= 0x40000) return;                          /* not a step's jump: a respawn */
            if (w) moved = 1;
        }
        if (!moved) return;                                                     /* its own physics alone */
        n = c[0xC70 / 4] == 4 ? 7u : 3u;                                        /* the helicopter: its light and heading */
        for (j = 0; j < n; j++) ENG->pt[k][j] = c[eng_pxo[j] / 4u];
        for (j = 0; j < n; j++) c[eng_pxo[j] / 4u] = eng_pline(j, ENG->pw[k][j], ENG->pt[k][j], 1u);
        ENG->ps[k] = car; ENG->pn[k] = n;
        return;
    }
}
/* substeps 1 .. n - 1, at the pursuer's move: (sub + 1) n-ths of the way, then (the last) the game's own place */
static inline void eng_pstep(uint32_t car)
{
    uint32_t k, j;
    for (k = 0; k < 8u; k++) {
        volatile int32_t *c;
        if (ENG->ps[k] != car) continue;
        c = (volatile int32_t *)(uintptr_t)car;
        for (j = 0; j < ENG->pn[k]; j++)                  /* only the words the line carries (a cop's heading is its own) */
            c[eng_pxo[j] / 4u] = ENG_SUB + 1u < ENG->n ? eng_pline(j, ENG->pw[k][j], ENG->pt[k][j], ENG_SUB + 1u) : ENG->pt[k][j];
        if (ENG_SUB + 1u >= ENG->n) ENG->ps[k] = 0;
        return;
    }
}
/* Round 71 (phone round 70: a traffic car hit from behind "moves around a little ... like passing through a ghost
 * car"; yt73): a hit traffic car plays a knock -- +FB12's state word bit 8, a table of entries (+39FE of its +3C)
 * stepped by a counter the whole step alone advances (+FC90, gated) -- and +FB12 places it absolutely from that entry
 * every call. So the whole step moved it a whole entry, ~110 x and ~75 z, and the substeps placed it on the same
 * entry again (their lane drift only): 11.72 Hz jumps. Before +FB12 on the whole step every traffic car's place and
 * spin (+2C, 4096 a turn) is kept (D); a car +FB12 then places through the knock branch (+FF7E) is drawn on the
 * line from D to where the game put it (T): a third of the way, two thirds after substep 1 (plus the substep's own
 * drift), the game's own after substep 2. Its counters, velocity (+2202C) and measure (+22832) stay the game's:
 * only the place drawn and collided with between whole steps moves. A jump of more than 0x2000 is not drawn over.
 * Round 74 (phone round 73, "the cars are still looking stop-motion-ish during takedown cams"; tk74): the line ran
 * to T, which holds substep 0's own drift but not the next substeps' (~21 x each), and those were added on top -- a
 * knocked car moved 32, 53, 54 a substep, the same jolt every whole step. Now the line runs to the step's end,
 * estimated: T plus the car's drift over the rest of the step (at substep 0 its last step's, kept at that step's
 * end; after, its own drift since T, scaled to the step) -- 46, 46, 46. The last substep is the game's own place. */
static inline int32_t eng_wrap12(int32_t d) { d &= 4095; return d >= 2048 ? d - 4096 : d; }
static inline void eng_tsnap(uint32_t arg)
{
    uint32_t k, ta;
    ENG->targ = arg;
    if (ENG_SUB || !arg) return;
    ta = arg + GAME32(0xFEB4);
    for (k = 0; k < 16u; k++) {
        const uint32_t t = ta + 0x148u * k;
        ENG->tsp[k][0] = (int32_t)MEM32(t + 4u); ENG->tsp[k][1] = (int32_t)MEM32(t + 8u);
        ENG->tsp[k][2] = (int32_t)MEM32(t + 0xCu); ENG->tsp[k][3] = (int32_t)MEM32(t + 0x2Cu);
    }
}
static inline int32_t eng_tline(int32_t a, int32_t b, uint32_t j, uint32_t ang)
{
    const int32_t d = ang ? eng_wrap12(b - a) : b - a;
    const int32_t v = a + eng_sdiv((int32_t)j * d, (int32_t)ENG->n);
    return ang ? (v & 4095) : v;
}
static inline void eng_tknock(uint32_t car)
{
    const uint32_t ta = ENG->targ + GAME32(0xFEB4), off = car - ta, k = off / 0x148u;
    volatile int32_t *c = (volatile int32_t *)(uintptr_t)car;
    const int32_t n = (int32_t)ENG->n;
    uint32_t j;
    if (!ENG->targ || off % 0x148u || k >= 16u) return;
    if (ENG_SUB == 0u) {
        const int known = ENG->cnt - ENG->tkds[k] == 1u;   /* its drift measured over the step just ended */
        ENG->tkv &= ~(1u << k);
        for (j = 0; j < 3u; j++) {
            const int32_t d = c[1u + j] - ENG->tsp[k][j];
            if (d > 0x2000 || d < -0x2000) return;                      /* a respawn, not a knock's step */
        }
        for (j = 0; j < 4u; j++) {
            volatile int32_t *w = j < 3u ? &c[1u + j] : &c[0x2C / 4];
            const int32_t f = *w + (known ? eng_sdiv(ENG->tkd[k][j] * (n - 1), n) : 0);   /* the step's end, estimated */
            ENG->tkt[k][j] = *w;
            *w = eng_tline(ENG->tsp[k][j], f, 1u, j == 3u);
        }
        ENG->tkv |= 1u << k;
    } else if (ENG->tkv & (1u << k)) {
        const int32_t s = (int32_t)ENG_SUB;
        for (j = 0; j < 4u; j++) {
            volatile int32_t *w = j < 3u ? &c[1u + j] : &c[0x2C / 4];
            const int32_t dl = j < 3u ? *w - ENG->tkt[k][j] : eng_wrap12(*w - ENG->tkt[k][j]);   /* its own drift since T */
            if (ENG_SUB + 1u < ENG->n)                                  /* (sub + 1) n-ths of the way to the step's end, */
                *w = eng_tline(ENG->tsp[k][j], ENG->tkt[k][j] + eng_sdiv(dl * (n - 1), s), ENG_SUB + 1u, j == 3u);
            else ENG->tkd[k][j] = eng_sdiv(dl * n, n - 1);              /* the last: the game's own place; the drift kept */
        }
        if (ENG_SUB + 1u >= ENG->n) { ENG->tkv &= ~(1u << k); ENG->tkds[k] = ENG->cnt; }
    }
}
#ifdef A4_TRAFDUMP
/* bench (phone round 70, a hit traffic car's response): every traffic car within 1,500 of the player, all its 0x148
 * bytes -- a record: the substep count, the player's x and z, the cars' count | the substep << 8 | the point << 12
 * (round 71: 0 the substep's start, 1 before the traffic update +FB12, 2 after it), then per car its address << 4 |
 * its slot and 82 words; 4 cars at most, 1,344 bytes a record, a ring of 12 at 0x5A2000 handed to a4_camp_emit when
 * it wraps. The traffic array: +FB12's first argument (ENG->dbg[5], at +14574) + the game's +FEB4. Never a phone
 * build. */
static inline void eng_tdrec(uint32_t race, uint32_t tag)
{
    extern void a4_camp_emit(const void *);
    volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
    const uint32_t pl = *(volatile uint32_t *)(race + 0x390u);
    const uint32_t pi = pl ? *(volatile uint32_t *)(pl + 0xCu) : 16u;
    const uint32_t pc = pi < 16u ? *(volatile uint32_t *)(race + 0x394u + 4u * pi) : 0u;
    if (pc && ENG->dbg[5]) {
        const uint32_t ta = ENG->dbg[5] + GAME32(0xFEB4);
        uint32_t i = rg[0] % 12u, k, j, n = 0;
        volatile uint32_t *q = rg + 4u + (1344u / 4u) * i;
        const int32_t px = (int32_t)MEM32(pc + 4u), pz = (int32_t)MEM32(pc + 0xCu);
        q[0] = tag ? ENG->cnt - 1u : ENG->cnt; q[1] = (uint32_t)px; q[2] = (uint32_t)pz;
        for (k = 0; k < 16u && n < 4u; k++) {
            const uint32_t t = ta + 0x148u * k;
            const int32_t dx = (int32_t)MEM32(t + 4u) - px, dz = (int32_t)MEM32(t + 0xCu) - pz;
            if (dx > 1500 || dx < -1500 || dz > 1500 || dz < -1500) continue;
            q[4u + 83u * n] = t << 4 | k;
            for (j = 0; j < 82u; j++) q[5u + 83u * n + j] = MEM32(t + 4u * j);
            n++;
        }
        q[3] = n | ENG->sub << 8 | tag << 12;
        rg[0] = i + 1u;
        if (i + 1u == 12u) { rg[0] = 0; a4_camp_emit((const void *)(rg + 4u)); }
    }
}
#define ENG_TDREC(race, tag) eng_tdrec((race), (tag))
#else
#define ENG_TDREC(race, tag) ((void)0)
#endif
static inline void ENG_BEGIN(uint32_t race)
{
    /* Round 85 (phone round 84b, KERN-EXEC 3 in a session's second race; the bench's at84c, ny2 to ny5): the next race,
     * or a restart, runs in the last race's own object -- the game initialises it again in place, at the same address
     * -- so the engine's state, keyed by that address, ran on from the last race into the new one. A new race at the
     * last one's address is told by its traffic clock (race+0x378, the r2 +FB12 is given), which starts again (0x3000)
     * below the last whole step's (0x8500 after 45 s of LA). Kept, the last race's clock made the first step's change
     * negative (-0x5500), substep 2's traffic product went below zero, and on a lane it counts down +FB12 indexes
     * past the lane's end (+FBC2: wrapped only when negative): the word there, inflated track data, named traffic slot
     * 150 and +F1B0 called through a vtable 150 slots on. */
    const int again = ENG->magic == ENG_MAGIC && ENG->race == race && ENG->tlast[0] != 0u &&
                      (int32_t)(MEM32(race + 0x378u) - ENG->tlast[0]) < 0;
    if (ENG->magic != ENG_MAGIC || ENG->race != race || again) {
        ENG->magic = ENG_MAGIC; ENG->race = race; ENG->cnt = 0; ENG->keys = 0; ENG->nfull = 0; ENG->cbok = 0;
        ENG->sub = 0; ENG->n = 3u; ENG->wst = 0;
        for (uint32_t i = 0; i < ENG_NREM; i++) { ENG->rk[i] = 0; ENG->rv[i] = 0; }
        for (uint32_t i = 0; i < 32u; i++) ENG->aic[i] = 0;
        for (uint32_t i = 0; i < 16u; i++) { ENG->kc[i] = 0; ENG->ks[i] = 0; }
        for (uint32_t i = 0; i < 16u; i++) ENG->tkds[i] = 0x80000000u;   /* round 74: no knock drift measured yet */
        for (uint32_t i = 0; i < ENG_NSPIN; i++) { ENG->spc[i] = 0; ENG->spo[i] = 0; ENG->spl[i] = 0; ENG->spr[i] = 0; }   /* no spin spread */
        for (uint32_t i = 0; i < 64u; i++) ENG->lc[i] = 0;
        ENG->flush = 0;
        ENG->tlast[0] = ENG->tlast[1] = 0; ENG->tdel[0] = ENG->tdel[1] = 0;
        for (uint32_t i = 0; i < 8u; i++) { ENG->pc[i] = 0; ENG->ps[i] = 0; }
        for (uint32_t i = 0; i < 16u; i++) { ENG->sbc[i] = 0; ENG->svc[i] = 0; }
        ENG->targ = 0; ENG->tkp = 0; ENG->tkv = 0; ENG->svok = 0;
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
#ifdef A4_KICKREC
    {   /* bench (round 73): the player car at every substep's start, in the push ring -- the whole step, the substep |
         * n << 8, +7D0 (the speed scalar), B4 | +7FC << 16, x, z, +010 (vx), 'SUBS' */
        extern void a4_camp_emit(const void *);
        volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
        const uint32_t pl = *(volatile uint32_t *)(race + 0x390u), pi = pl ? *(volatile uint32_t *)(pl + 0xCu) : 16u;
        const uint32_t pc = pi < 16u ? *(volatile uint32_t *)(race + 0x394u + 4u * pi) : 0u;
        if (pc && ENG->magic == ENG_MAGIC) {
            const uint32_t i = rg[0] & 255u;
            volatile uint32_t *e = rg + 4u + 8u * i;
            e[0] = ENG->wst; e[1] = ENG->sub | ENG->n << 8; e[2] = MEM32(pc + 0x7D0u);
            e[3] = (MEM32(pc + 0xB4u) & 0xFFFFu) | MEM32(pc + 0x7FCu) << 16; e[4] = MEM32(pc + 0x7BCu); e[5] = MEM32(pc + 0x7C4u);
            e[6] = MEM32(pc + 0x10u); e[7] = 0x53425553u;
            rg[0] = i + 1u;
            if (((i + 1u) & 255u) == 0u) a4_camp_emit((const void *)(rg + 4u));
        }
        if (ENG->magic == ENG_MAGIC && ENG->sub == 0u) {   /* every whole step's start: the race cars and the traffic */
            volatile uint32_t *z = (volatile uint32_t *)0x005A4400u;   /* 0x2000 bytes, below 0x5A7000 */
            static const uint16_t cw[] = { 4, 8, 12, 16, 20, 24, 0xA8, 0xB4, 0x678, 0x7D0, 0x7D8, 0x7FC, 0x34 };
            const uint32_t ta = ENG->targ ? ENG->targ + GAME32(0xFEB4) : 0u;
            uint32_t q, w;
            z[0] = 0x50414E53u; z[1] = ENG->wst; z[2] = ENG->cnt; z[3] = ENG->n | MEM32(race + 0x84Cu) << 16;
            z[4] = MEM32(race + 0x378u); z[5] = MEM32(race + 0x37Cu); z[6] = ENG->targ; z[7] = ta;
            for (q = 0; q < 16u; q++) {
                const uint32_t c = *(volatile uint32_t *)(race + 0x394u + 4u * q);
                z[8u + 14u * q] = c;
                for (w = 0; w < 13u; w++) z[9u + 14u * q + w] = c ? MEM32(c + cw[w]) : 0u;
            }
            for (q = 0; q < 16u; q++) for (w = 0; w < 82u; w++) z[240u + 82u * q + w] = ta ? MEM32(ta + 0x148u * q + 4u * w) : 0u;
            a4_camp_emit((const void *)z);
        }
    }
#endif
#ifdef A4_VELPROBE
    {   /* the player car, for the probe */
        uint32_t pl = *(volatile uint32_t *)(race + 0x390), idx = pl ? *(volatile uint32_t *)(pl + 0xC) : 0u;
        ENG->dbg[6] = idx < 16u ? *(volatile uint32_t *)(race + 0x394 + 4u * idx) : 0u;
    }
#endif
#ifdef A4_TRAFDUMP
    eng_tdrec(race, 0u);
#endif
#ifdef A4_HELIREC
    {   /* bench (phone round 70, the helicopter's light): while police slot 2 (race slot 10) holds the helicopter
         * (+C70 4) and it has moved since the last record, the substep count, the player car's x, y, z and the
         * helicopter's first 0xD00 bytes -- 0xD10 a record, a ring of 4 at 0x5A2000 (A4_SCRATCH_SIZE >= 0x1A6000)
         * handed to a4_camp_emit when it wraps. Never a phone build. */
        extern void a4_camp_emit(const void *);
        volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
        const uint32_t h = *(volatile uint32_t *)(race + 0x394u + 4u * 10u);
        const uint32_t pl = *(volatile uint32_t *)(race + 0x390u);
        const uint32_t pi = pl ? *(volatile uint32_t *)(pl + 0xCu) : 16u;
        const uint32_t pc = pi < 16u ? *(volatile uint32_t *)(race + 0x394u + 4u * pi) : 0u;
        if (h && MEM32(h + 0xC70u) == 4u && pc && MEM32(h + 4u) != rg[1]) {
            uint32_t i = rg[0] % 4u, k;
            volatile uint32_t *q = rg + 4u + (0xD10u / 4u) * i;
            rg[1] = MEM32(h + 4u);
            q[0] = ENG->cnt; q[1] = MEM32(pc + 4u); q[2] = MEM32(pc + 8u); q[3] = MEM32(pc + 0xCu);
            for (k = 0; k < 0xD00u / 4u; k++) q[4u + k] = MEM32(h + 4u * k);
            rg[0] = i + 1u;
            if (i + 1u == 4u) { rg[0] = 0; a4_camp_emit((const void *)(rg + 4u)); }
        }
    }
#endif
#ifdef A4_CARSUB
    {   /* bench (phone round 69): every race slot's car at each substep's start -- the counter, then per slot its
         * address, x, y, z, type (+C70) -- 81 words a record, a ring of 48 at 0x5A2000 (A4_SCRATCH_SIZE >= 0x1A6000)
         * handed to a4_camp_emit when it wraps. Never a phone build. */
        extern void a4_camp_emit(const void *);
        volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
        uint32_t i = rg[0] % 48u, k;
        volatile uint32_t *q = rg + 4u + 81u * i;
        q[0] = ENG->cnt;
        for (k = 0; k < 16u; k++) {
            uint32_t car = *(volatile uint32_t *)(race + 0x394u + 4u * k);
            volatile uint32_t *e = q + 1u + 5u * k;
            e[0] = car;
            if (car) { e[1] = MEM32(car + 4u); e[2] = MEM32(car + 8u); e[3] = MEM32(car + 0xCu); e[4] = MEM32(car + 0xC70u); }
        }
        rg[0] = i + 1u;
        if (i + 1u == 48u) { rg[0] = 0; a4_camp_emit((const void *)(rg + 4u)); }
    }
#endif
    if (ENG->cnt == 0u || ENG->sub + 1u >= ENG->n) {          /* a whole step: its substep count chosen at its start */
        ENG->sub = 0u; ENG->wst++;
#ifdef A4_ENG6
        ENG->n = *(volatile uint32_t *)(race + 0x84Cu) == 512u ? 6u : 3u;   /* the game's slow motion: sixths */
#else
        ENG->n = 3u;
#endif
    } else ENG->sub++;
    ENG->cnt++;
    if (ENG->sub == 0u) { ENG->nsnd = 0; ENG->camv = 0; ENG->loop = 0; eng_psnap(race); }
}
/* bench (A4_VELPROBE, phone round 68): per substep, the player car's control -- its entry, the velocity's change
 * toward its target before and after the substep's scaling, the vector the move adds -- 8 words a record, a ring of
 * 384 at 0x5A2000 (A4_SCRATCH_SIZE >= 0x1A6000) handed to a4_camp_emit (EKA2L1_PCMEM) when it wraps. Never a phone
 * build. */
#ifdef A4_VELPROBE
static inline void eng_vp(uint32_t tag, uint32_t car, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    extern void a4_camp_emit(const void *);
    volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
    uint32_t i;
    volatile uint32_t *q;
    if (car != ENG->dbg[6]) return;
    i = rg[0] % 384u; q = rg + 4u + 8u * i;
    q[0] = tag | (ENG->sub << 8); q[1] = ENG->cnt; q[2] = a; q[3] = b; q[4] = c; q[5] = d; q[6] = e; q[7] = car;
    rg[0] = i + 1u;
    if (i + 1u == 384u) { rg[0] = 0; a4_camp_emit((const void *)(rg + 4u)); }
}
#define ENG_VP(t, car, a, b, c, d, e) eng_vp((t), (car), (a), (b), (c), (d), (e))
/* any car (phone round 69: the cars on rails, +1F1F8's first branch) */
static inline void eng_vpa(uint32_t tag, uint32_t car, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t keep = ENG->dbg[6];
    ENG->dbg[6] = car; eng_vp(tag, car, a, b, c, d, e); ENG->dbg[6] = keep;
}
#define ENG_VPA(t, car, a, b, c, d, e) eng_vpa((t), (car), (a), (b), (c), (d), (e))
#else
#define ENG_VP(t, car, a, b, c, d, e) ((void)0)
#define ENG_VPA(t, car, a, b, c, d, e) ((void)0)
#endif
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
    /* Round 73 (A4_ENG6): in a step of sixths the script ticks twice -- at the whole step, and in the race update's
     * +1300C, which a4_eng6_count runs after the step's substep 3 (stock's slow motion: the camera at its normal rate,
     * one tick a real 85 ms, while the cars run at half). So a tick spans three substeps here, as at normal speed:
     * substeps 1-3 a third, two thirds and all of the way to the next value from the whole step's, 4-5 a third and
     * two thirds on from the value the mid tick left (the clock moved since the whole step's: ENG->sclk). Before,
     * the substeps spread one tick over six and the two ticks landed whole -- a jump at each step's end. */
    int32_t jj = (int32_t)j, den = (int32_t)ENG->n;
    if (ENG->n == 6u) { den = 3; jj = clk != ENG->sclk ? (j >= 3u ? (int32_t)j - 3 : 0) : (j > 3u ? 3 : (int32_t)j); }   /* no tick yet: hold */
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
        o[0x5Cu / 4u + k] = (uint32_t)(cur[k] + eng_sdiv((nx - cur[k]) * jj, den));
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
    if (j == 0u) { ENG->cbw = 0; ENG->cmid = 0; }
    if (!(mode <= 7u || mode == 0x2Au)) {
        ENG->cbok = 0;
#ifdef A4_CAMSCRIPT3
        if (mode == 0x18u && j == 0u) ENG->sclk = (int32_t)MEM32((uint32_t)(uintptr_t)cam + 0x248u + 0x8Cu);   /* after its tick */
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
        /* round 73 (A4_ENG6): in a step of sixths the game updates the camera twice -- the whole step, and +1300C
         * after substep 3 (eng_cam_mid makes its pose the next target) -- so a target spans three substeps, j
         * counted from the last one: 1-3 toward the whole step's, 4-5 toward +1300C's */
        int32_t jj = (int32_t)j;
        if (ENG->n == 6u) jj = ENG->cmid ? (j >= 3u ? (int32_t)j - 3 : 0) : (j > 3u ? 3 : (int32_t)j);   /* no +1300C: hold */
        for (k = 0; k < 5u; k++) {
            const int32_t a = ENG->cbprev[k], d = k == 3u ? eng_wrap(ENG->cbcur[k] - a) : ENG->cbcur[k] - a;
            o[k] = a + (jj == 0 ? 0 : ENG->n == 6u ? eng_sdiv(jj * d, 3) : eng_divn(jj * d));
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
/* Round 73 (A4_ENG6): +1300C, the race update's between-steps fill, runs in a step of sixths once, after substep 3
 * (a4_eng6_count), and updates the camera as stock's slow motion does between its logic steps. Before it the chase
 * pose goes back to the game's own (eng_cam_unblend, as before a whole step), so its camera code reads its own
 * pose; after it that pose is the chase blend's next target -- the blend's offset turned to the car's heading the
 * last target, as at a whole step -- and the substep that ran last is placed again on the new pair (j - 3 of
 * three), so the frame shows the blend and not +1300C's raw pose (smd73: the offset hopped and came back at every
 * such frame). The crash camera's script ticked in +1300C; its substep is placed again the same way. */
static inline void eng_cam_mid(void)
{
    volatile uint32_t *cam = (volatile uint32_t *)(uintptr_t)ENG->cam[0];
    volatile int32_t *car = (volatile int32_t *)(uintptr_t)ENG->cam[2];
    uint32_t stk[32] = { 0 }, r23[2], mode, k;
    if (ENG->n != 6u || !ENG->camv || !cam || !car) return;
    mode = cam[0x4C / 4];
    ENG->cmid = 1;
    if ((mode == 1u || mode == 2u || mode == 3u || mode == 0x2Au) && ENG->cbok && ENG->cbmode == mode) {
        int32_t c[5], rx, rz, hd = car[0x678 / 4] & 2047;
        c[0] = (int32_t)cam[4] - car[1]; c[1] = (int32_t)cam[6] - car[3]; c[2] = (int32_t)cam[5] - car[2];
        c[3] = eng_wrap((int32_t)cam[8] - hd); c[4] = (int32_t)cam[7];
        if (go_cb_far(c, ENG->cbcur) || eng_wrap(hd - ENG->cbhd) > 255 || eng_wrap(hd - ENG->cbhd) < -255) {
            for (k = 0; k < 5u; k++) ENG->cbprev[k] = c[k];
        } else {
            eng_rot(ENG->cbcur[0], ENG->cbcur[1], eng_wrap(hd - ENG->cbhd), &rx, &rz);
            ENG->cbprev[0] = rx; ENG->cbprev[1] = rz;
            for (k = 2; k < 5u; k++) ENG->cbprev[k] = ENG->cbcur[k];
        }
        for (k = 0; k < 5u; k++) { ENG->cbcur[k] = c[k]; ENG->cbgp[k] = (int32_t)cam[4 + k]; }
        ENG->cbhd = hd;
        if (ENG->sub >= 3u) eng_cam_pose((uint32_t)(uintptr_t)(stk + 8), r23, ENG->sub);
    }
#ifdef A4_CAMSCRIPT3
    else if (mode == 0x18u && cam[0xC0u / 4u] == 0x18u && ENG->sub > 3u) eng_cam_script((uint32_t)(uintptr_t)(stk + 8), r23, ENG->sub, cam);
#endif
}
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
/* A rate's store on a substep: the word keeps an n-th (a third; a sixth in slow motion) of the change the game's
 * whole step would make, floor-divided, the remainder (0 .. n - 1) carried in the word's own slot, so the step's n
 * substeps of the same change add up to the whole step's exactly. */
static inline uint32_t eng_third(uint32_t ea, uint32_t old, uint32_t nw)
{
    uint32_t h = (ea * 2654435761u) >> 22, n;
    int32_t t, q;
    for (n = 0; n < 16u; n++, h = (h + 1u) & (ENG_NREM - 1u)) {
        if (ENG->rk[h] == ea) break;
        if (ENG->rk[h] == 0u) { ENG->rk[h] = ea; ENG->rv[h] = 0; break; }
    }
    t = (int32_t)(nw - old);
    if (n == 16u) { ENG->nfull++; return old + (uint32_t)eng_divn(t); }
    t += ENG->rv[h];
    q = t >= 0 ? eng_divn(t) : -eng_divn((int32_t)ENG->n - 1 - t);
    ENG->rv[h] = (int8_t)(t - (int32_t)ENG->n * q);
    return old + (uint32_t)q;
}
/* A third of a value with its remainder carried under a key of its own (an odd address, never a word's):
 * a per-step change the game applies through code that is not a store (a vector add, a call). */
static inline int32_t eng_thirdv(uint32_t key, int32_t v) { return (int32_t)eng_third(key | 1u, 0u, (uint32_t)v); }
/* Round 74 (phone round 73: "the cars are still looking stop-motion-ish during takedown cams"): a contact's spin.
 * +239B6, the car-car response, turns each car by clamp(torque >> 4, -128, 128) about y through its rotate (+197B8,
 * the car's vtable +0x2C: angles += the vector, then +1971E). The game does it once a step, so a takedown's wreck
 * turns up to a sixteenth of a turn a step, drawn at 11.72 Hz. Run every substep, the response turned the car the
 * whole kick on each substep that found the contact and not at all on the others (tv1, the watch on a wreck's
 * heading: +34, +29, +112, +128, +128, -126, -101 on single substeps, held between) -- the heading in sixteenths.
 * Now a pair's first kick in a step is the step's kick for that car, as the game computes one a pair; it is turned a
 * share now and the rest evenly at the next n - 1 substeps' orientation (+1971E's entry); the step's later kicks of
 * the same pair are dropped, and a spread still unfinished at the pair's next kick is finished with it. */
static inline int32_t eng_spin(uint32_t car, uint32_t other, int32_t k)
{
    uint32_t i, f = ENG_NSPIN;
    const int32_t n = (int32_t)ENG->n;
    int32_t s, q;
    for (i = 0; i < ENG_NSPIN; i++) {
        if (ENG->spc[i] == car && ENG->spo[i] == other) { f = i; break; }
        if (f == ENG_NSPIN && (ENG->spc[i] == 0u || (ENG->spl[i] == 0u && ENG->sps[i] != ENG->wst))) f = i;
    }
    if (f == ENG_NSPIN) {                             /* full: the oldest finished slot, else one by the address */
        f = (car >> 4) % ENG_NSPIN; ENG_EVIC(5);
        if (ENG->spl[f] && ENG->spc[f]) MEM32(ENG->spc[f] + 0x678u) += (uint32_t)ENG->spr[f];   /* its rest turned now */
        ENG->spl[f] = 0; ENG->spr[f] = 0;
    }
    if (ENG->spc[f] != car || ENG->spo[f] != other) { ENG->spc[f] = car; ENG->spo[f] = other; ENG->sps[f] = ENG->wst - 1u; ENG->spr[f] = 0; ENG->spl[f] = 0; }
    if (ENG->sps[f] == ENG->wst) return 0;            /* the step's kick is already the car's */
    s = ENG->spr[f];                                  /* the last kick's unfinished part, turned now */
    q = eng_sdiv(k, n);                               /* toward zero: a share of each sign alike */
    s += k - q * (n - 1);
    ENG->sps[f] = ENG->wst; ENG->spr[f] = q * (n - 1); ENG->spl[f] = (uint8_t)(n - 1); ENG->spt[f] = ENG->cnt;
    return s;
}
/* Round 77 (phone round 76: "whatever violent movement the car as an object does around its own axis feels choppy"):
 * the wreck's draw at the substep's time. A wreck's path (+2292A) moves its frame +034 and step counter +048 by 1 --
 * 2 at the path's double rate -- on the step's last substep (edits.py GE at +22BBA..+22BEA), and the car draw +D3BA
 * poses it from them alone: the keyframe record of frame +034 (+32042 / +31F2E), the turns -42 x +034 and
 * -min(85 x +034, 512), the ease of +048 x +044 that lifts, rolls (up to 512) and spins (up to 1,024) it, its arc,
 * and the damage morph every 4 of +048. So the wreck's pose moved one render in n, its position every one (A4_WRECK3).
 * The draw now reads them at the substep's time: the counter (x 256) plus the rate x the share of the step run since
 * they moved, (substeps since the move) / n -- the counters' own next value, reached at the step's end; 0 on the
 * substep they move, so the game's pose exactly there. eng_wrate: the rate, recorded where +2292A adds it, and the
 * substep count at the move (wcn); no share once a whole step has passed without a move (the game stopped moving
 * them: a pose must not creep ahead of the counters and fall back), nor before a car's first recorded move. */
static inline void eng_wrate_set(uint32_t car, uint32_t d)
{
#ifdef A4_WRECKFRAC
    uint32_t i, f = 8u;
    for (i = 0; i < 8u; i++) { if (WRK->wrc[i] == car) break; if (f == 8u && WRK->wrc[i] == 0u) f = i; }
    if (i == 8u) {
        if (f == 8u) { f = (car >> 4) & 7u; ENG_EVIC(6); }
        i = f; WRK->wrc[i] = car; WRK->wcn[i] = ENG->cnt - 64u;      /* no move recorded yet */
    }
    WRK->wrd[i] = (uint8_t)d;
    if (ENG_SUB_END(car)) WRK->wcn[i] = ENG->cnt;                  /* the substep the counters move on */
#else
    (void)car; (void)d;
#endif
}
static inline uint32_t eng_wfrac8(uint32_t car)       /* the share of the step since the counters moved, x 256 x rate */
{
#ifdef A4_WRECKFRAC
    const uint32_t n = ENG->n;
    static const uint16_t q8[7][6] = { {0}, {0}, {0, 128}, {0, 85, 171}, {0, 64, 128, 192}, {0, 51, 102, 154, 205},
                                       {0, 43, 85, 128, 171, 213} };
    uint32_t i, k;
    if (ENG->magic != ENG_MAGIC || n < 2u || n > 6u) return 0u;
    for (i = 0; i < 8u; i++) if (WRK->wrc[i] == car) break;
    if (i == 8u) return 0u;                                        /* no move recorded: the game's pose */
    k = ENG->cnt - WRK->wcn[i];                                    /* substeps since the counters moved */
    if (k == 0u || k >= n) return 0u;                              /* on the move, or a step without one */
    return (uint32_t)q8[n][k] * WRK->wrd[i];
#else
    (void)car; return 0u;
#endif
}
static inline uint32_t eng_wt8(uint32_t car, uint32_t off) { return (MEM32(car + off) << 8) + eng_wfrac8(car); }
/* the keyframe record +D3BA hands +32042 / +31F2E: frame +034's (rec, clamped to last by the game), blended toward the
 * next by the substep's share; the record itself when there is no share or no next frame. 15 words a record. */
static inline uint32_t eng_wkey(uint32_t car, uint32_t rec, uint32_t path, int32_t last)
{
    const int32_t f = (int32_t)MEM32(car + 0x34u);
    uint32_t t8 = ((uint32_t)f << 8) + eng_wfrac8(car), i, w, a, b;
    if (f < 0 || f >= last || (!(t8 & 0xFFu) && (int32_t)(t8 >> 8) == f)) return rec;
    if ((int32_t)(t8 >> 8) >= last) t8 = (uint32_t)last << 8;
    a = path + 4u + 60u * (t8 >> 8); b = (int32_t)(t8 >> 8) + 1 <= last ? a + 60u : a; w = t8 & 0xFFu;
    for (i = 0; i < 15u; i++) {
        const int32_t va = (int32_t)MEM32(a + 4u * i), vb = (int32_t)MEM32(b + 4u * i);
        WRK->wkey[i] = va + (int32_t)(((int64_t)(vb - va) * (int32_t)w) >> 8);
    }
    return (uint32_t)(uintptr_t)WRK->wkey;
}
/* the turns +D3BA gives by the frame: unit x the frame (unit -42 or +42), -min(85 x frame, 512) -- at the share */
static inline uint32_t eng_wt8c(uint32_t car, int32_t last)
{
    const int32_t f = (int32_t)MEM32(car + 0x34u);
    uint32_t t8 = ((uint32_t)(f < 0 ? 0 : f) << 8) + (f >= 0 && f < last ? eng_wfrac8(car) : 0u);
    return (int32_t)(t8 >> 8) > last ? (uint32_t)(last < 0 ? 0 : last) << 8 : t8;
}
static inline void eng_spin_apply(uint32_t car)
{
    uint32_t i;
    int32_t d;
    for (i = 0; i < ENG_NSPIN; i++) {                 /* each pair's spread of this car, a share a substep */
        if (ENG->spc[i] != car || ENG->spl[i] == 0u || ENG->spt[i] == ENG->cnt) continue;
        d = eng_sdiv(ENG->spr[i], (int32_t)ENG->spl[i]);
        ENG->spr[i] -= d; ENG->spl[i]--; ENG->spt[i] = ENG->cnt;
        MEM32(car + 0x678u) += (uint32_t)d;           /* the heading; +1971E builds the matrix from it next */
    }
}
/* The wall's first push (+1ED4C: the heading turned off the wall by half the angle met, clamped 20..64,
 * while +B18 counts the contact's 5 steps). The game turns the car the whole push at the step the contact
 * starts; three thirds, each on the angle the last one left, add up to less (30 against stock's 49 at a
 * wide hit, so a hit cost less speed). The substep where +B18 leaves 0 marks the car; its push is then
 * the game's whole one and the step's later substeps add none; later steps are thirded as before. */
static inline void eng_kick_mark(uint32_t ea)
{
    uint32_t st = ENG->wst, i, f = 16u;
    for (i = 0; i < 16u; i++) {
        if (ENG->kc[i] == ea) { f = i; break; }
        if (f == 16u && (ENG->kc[i] == 0u || (ENG->ks[i] & 0x7FFFFFFFu) != st)) f = i;
    }
    if (f == 16u) { f = (ea >> 4) & 15u; ENG_EVIC(3); }
    ENG->kc[f] = ea; ENG->ks[f] = st | 0x80000000u;
}
#ifdef A4_SPINREC
/* bench (round 74, the takedown's wreck turning in steps): every store of a wrecked car's heading +678 the control
 * (+1DE78: += / -= A20, += the drift 9F8) and the wall (+1F132/+1F13A) make -- the substep count, the substep | n << 8
 * | the site << 16, the car, the heading before, the game's new value, A20, the drift, +034: 8 words, a ring of 256
 * at 0x5A2000 handed to a4_camp_emit */
static inline void eng_spinrec(uint32_t site, uint32_t car, uint32_t old, uint32_t nw, uint32_t a20, uint32_t drift)
{
    extern void a4_camp_emit(const void *);
    volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
    uint32_t i;
    volatile uint32_t *e;
    if (MEM32(car + 0x34u) == 0xFFFFFFFFu) return;
    i = rg[0] & 255u; e = rg + 4u + 8u * i;
    e[0] = ENG->cnt; e[1] = ENG->sub | ENG->n << 8 | site << 16; e[2] = car; e[3] = old; e[4] = nw; e[5] = a20; e[6] = drift;
    e[7] = MEM32(car + 0x34u);
    rg[0] = i + 1u;
    if (((i + 1u) & 255u) == 0u) a4_camp_emit((const void *)(rg + 4u));
}
#define ENG_SPINREC(s_, c_, o_, n_, a_, d_) eng_spinrec((s_), (c_), (o_), (n_), (a_), (d_))
/* every other store at +0x38 off a base in the lifted engine: if the word is a wreck's heading (the base - 0x640 a
 * car whose +034 is a path frame), recorded as a site 0x10000 + the store's address */
static inline void eng_hdrec(uint32_t a, uint32_t ea, uint32_t v)
{
    const uint32_t car = ea - 0x678u;
    if (ea < 0x02000000u || ea >= 0x03000000u || MEM32(car + 0x34u) >= 1024u || MEM32(car + 0xC70u) > 16u) return;
    eng_spinrec(0x100u | (a & 0xFFFFu) >> 0, car, MEM32(ea), v, a, 0u);
}
#define ENG_HDREC(a_, ea_, v_) eng_hdrec((a_), (ea_), (v_))
/* at every lifted function's entry: the wreck TKREC tracks (0x5A8008) -- its heading changed since the last probe:
 * recorded as site 0xFF with the function's address in the A20 word */
static inline void eng_wprobe(uint32_t fn)
{
    volatile uint32_t *w = (volatile uint32_t *)0x005A8008u;
    const uint32_t wc = w[0];
    if (wc < 0x02000000u || wc >= 0x03000000u) return;
    const uint32_t h = MEM32(wc + 0x678u);
    if (h != w[1]) { eng_spinrec(0xFFu, wc, w[1], h, fn, 0u); w[1] = h; }
}
#define ENG_WPROBE(f_) eng_wprobe((f_))
#else
#define ENG_SPINREC(s_, c_, o_, n_, a_, d_) ((void)0)
#define ENG_HDREC(a_, ea_, v_) ((void)0)
#define ENG_WPROBE(f_) ((void)0)
#endif
#ifdef A4_KICKREC
/* bench (round 73): every wall push -- the whole step, the substep | n << 8, the word, its value before, the game's
 * new value, the value kept -- 6 words a record, a ring of 256 at 0x5A2000 handed to a4_camp_emit when it wraps */
static inline uint32_t eng_kick_in(uint32_t ea, uint32_t nw);
static inline uint32_t eng_kick(uint32_t ea, uint32_t nw)
{
    extern void a4_camp_emit(const void *);
    volatile uint32_t *rg = (volatile uint32_t *)0x005A2000u;
    const uint32_t old = MEM32(ea), r = eng_kick_in(ea, nw), i = rg[0] & 255u;
    volatile uint32_t *e = rg + 4u + 8u * i;
    e[0] = ENG->wst; e[1] = ENG->sub | ENG->n << 8; e[2] = ea; e[3] = old; e[4] = nw; e[5] = r; e[6] = ENG->cnt; e[7] = 0x4B49434Bu;
    rg[0] = i + 1u;
    if (((i + 1u) & 255u) == 0u) a4_camp_emit((const void *)(rg + 4u));
    return r;
}
static inline uint32_t eng_kick_in(uint32_t ea, uint32_t nw)
#else
static inline uint32_t eng_kick(uint32_t ea, uint32_t nw)
#endif
{
    uint32_t st = ENG->wst, i;
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
    const uint32_t now = ENG->cnt - 1u, L = ENG->n - 1u;     /* the lag: the step's substeps less this one */
    uint32_t i, f = 64u, k = now % 6u, o = (now - L) % 6u, r = v;
    for (i = 0; i < 64u; i++) {
        if (ENG->lc[i] == ea) break;
        if (f == 64u && ENG->lc[i] == 0u) f = i;
    }
    if (i == 64u) {
        if (f == 64u) { f = (ea >> 2) & 63u; ENG_EVIC(1); }
        i = f; ENG->lc[i] = ea;
        for (uint32_t q = 0; q < 6u; q++) ENG->ls[i][q] = 0xFFFFFFFFu;
    }
    if (now >= L && ENG->ls[i][o] == now - L) r = ENG->lv[i][o];
    ENG->ls[i][k] = now; ENG->lv[i][k] = v;
    return r;
}
/* Phone round 70 (+1B9E0, edits.py): whether the steering rate A1C is computed on this call -- always on the whole
 * step; on a substep only when the car's key branch (1 left, 2 right, 3/4 back toward centre) differs from the one
 * the rate was last computed under (a live key changed mid-step); else the substep keeps the step's rate */
static inline uint32_t eng_steer_upd(uint32_t car, uint32_t br)
{
    uint32_t i, f = 16u;
    for (i = 0; i < 16u; i++) { if (ENG->sbc[i] == car) break; if (ENG->sbc[i] == 0u && f == 16u) f = i; }
    if (i == 16u) { if (f == 16u) ENG_EVIC(2); i = f == 16u ? (car >> 4) & 15u : f; ENG->sbc[i] = car; ENG->sbb[i] = 0u; }
    if (ENG_SUB && ENG->sbb[i] == br) return 0u;
    ENG->sbb[i] = br;
    return 1u;
}
/* Round 73 (k6): the slip check (+1BB98, the whole step's: dot(+010, +704) >> 12 against the speed +7D0, more than 10
 * under it an rpm penalty and a downshift) reads +010, the last substep's displacement x n. In a step of sixths that
 * is one sixth, and at a wall a sixth spent sliding read 126 against a speed of 139: the penalty and a downshift (the
 * speed scalar 139 -> 92, gear 2 -> 1) that neither stock (its step's displacement) nor thirds (the last third's,
 * 132) took. So in a step of sixths the check reads the car's displacement over the whole step before -- its position
 * less where the last whole step's check found it, stock's own measure of a step -- when that check was the step
 * just before; else +010. Positions are kept at every whole step for every car (thirds read +010 as before). */
static inline void eng_slipmark(uint32_t car)
{
    uint32_t i, f = 16u, k;
    for (i = 0; i < 16u; i++) { if (ENG->svc[i] == car) break; if (f == 16u && ENG->svc[i] == 0u) f = i; }
    if (i == 16u) { if (f == 16u) ENG_EVIC(0); i = f < 16u ? f : (car >> 4) & 15u; ENG->svc[i] = car; ENG->svw[i] = 0u; }
    ENG->svok = 0u;
    if (ENG->svw[i] + 1u == ENG->wst) {
        for (k = 0; k < 3u; k++) ENG->svv[k] = (int32_t)MEM32(car + 4u + 4u * k) - ENG->svp[i][k];
        ENG->svok = car;
    }
    for (k = 0; k < 3u; k++) ENG->svp[i][k] = (int32_t)MEM32(car + 4u + 4u * k);
    ENG->svw[i] = ENG->wst;
}
static inline uint32_t eng_slipsrc(uint32_t car)
{
    return ENG->n == 6u && ENG->svok == car ? (uint32_t)(uintptr_t)ENG->svv : car + 0x10u;
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
#ifdef A4_TCLK2
/* bench (phone round 70, a hit traffic car's knock on the whole step only): the substeps' clocks the step's own plus
 * the substep's third of its change, and +FB12's products from them alone -- whatever else the traffic does by the
 * clocks' advance (a knock) then sees a third of it each substep, not 0, 0 and all of it */
#define ENG_TCLK(w) (ENG->tlast[(w)] + (uint32_t)eng_sdiv((int32_t)ENG->sub * ENG->tdel[(w)], (int32_t)ENG->n))
static inline uint32_t eng_tmul(uint32_t t, uint32_t speed, uint32_t which) { (void)which; return t * speed; }
#else
#define ENG_TCLK(w) (ENG->tlast[(w)])
static inline uint32_t eng_tmul(uint32_t t, uint32_t speed, uint32_t which)
{
    int64_t f = (int64_t)ENG->sub * ENG->tdel[which & 1u] * (int32_t)speed;   /* the substep's share, x n */
    if (f > 0x7FFFFFFFll || f < -0x7FFFFFFFll) f = 0;                         /* (not a clock: its whole step) */
    return t * speed + (uint32_t)eng_divn((int32_t)f);                        /* the game's own product, wrapped */
}
#endif
#endif
#endif
