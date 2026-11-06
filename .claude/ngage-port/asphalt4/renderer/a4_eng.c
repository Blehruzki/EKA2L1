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
#include "eng/lifted.c"
#pragma clang diagnostic pop
#endif
