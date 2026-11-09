/* a4_memo.h -- round 53: where A4_MESHMEMO keeps its corner table and its generation word, for a4_gl.c (the
 * table) and a4_eng.h (MESHMEMO_BUMP, the lifted mesh routine's entry). Round 50 put them at 0x451FFC-0x453000,
 * free only without the half-step machinery; a half-step build keeps them past its scratch, 0x460000-0x461004,
 * which the injector then sizes to (A4_SCRATCH_SIZE >= 0x62000) -- where the bench-only checks keep their
 * buffers too (A4_LIFTCHECK, A4_CECHECK, A4_BBCHECK, A4_PAINTCHECK_BIG: a4_gl.c refuses the pairing). */
#ifndef A4_MEMO_H
#define A4_MEMO_H
#ifdef A4_HALFSPEC
#define A4_MEMO_TABLE 0x00460000u
#define A4_MEMO_STAMP 0x00461000u
#else
#define A4_MEMO_TABLE 0x00452000u
#define A4_MEMO_STAMP 0x00451FFCu
#endif
#endif
