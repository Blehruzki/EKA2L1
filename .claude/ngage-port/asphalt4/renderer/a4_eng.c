/* a4_eng.c -- round 50: the game's engine in C. Each function here replaces the game's own (a veneer
 * at its entry, tools/injector.py: every symbol a4_engv_XXXXX), first as tools/lift.py drafts it --
 * exact, so a lockstep race against the game's own code must match it word for word -- and only then
 * changed (the step at 3x). Nothing here is compiled without A4_ENGINE. */
#ifdef A4_ENGINE
#include "a4_eng.h"
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
