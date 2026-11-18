/* a4_prim.c -- MBX stage 4: forward every primitive from the dispatcher
 * (+0x300B2) to the GL renderer (a4_gl.c). Returns 1 if the GPU drew the
 * primitive (the trampoline then returns from the dispatcher, skipping the
 * software rasterizer -- the CPU win), 0 to fall through to software.
 */
#include <stdint.h>

int a4_gl_draw_prim(uint32_t *ctx, uint32_t *desc, uint32_t flags);

#ifdef A4_FORCE_SKIP
/* Diagnostic build (no GL at all): skip the software rasteriser for every
 * textured primitive past the intro, and do nothing else. It isolates the skip
 * mechanism from the GL path. On the device:
 *   - 3D goes blank + fps rises  -> the skip works and the software rasteriser
 *     was the cost (so the GPU path can win, once it stops double-drawing).
 *   - 3D still renders           -> the skip jump (+0x300B2 -> +0x300E0) has no
 *     effect on real ARM; that is the bug, not GL.
 * The counter lives at glr.seen (A4_GL_SCRATCH + 0x14, bss-zeroed), reused so
 * no writable global is needed. */
int a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    (void)ctx; (void)desc;
    if (!(flags & 1)) return 0;                      /* non-textured: leave to software */
    volatile uint32_t *seen = (volatile uint32_t *)0x00401014u;
    if (++(*seen) < 50000u) return 0;                /* past the intro video + menu */
    return 1;                                        /* skip software; no GL */
}
#else
#ifdef A4_STREAMCEN
/* Bench (round 83, step 2's scoping): the per-face stream by its source. Each draw_primitive call in a race frame (the
 * frame record's last entry in the chase camera) counted by the game's call site (the trampoline's r3: the caller's
 * return address) and its flags: 'SCN2' at 0x5C0900 (CUT1's place: lean_tail records no cuts in this build) -- [1]
 * calls, [2] renders seen (A4_PERF[PF_RENDERS] summed by window is not at hand: the frame record's count), [3] spare;
 * then 20 entries x 16 words: [0] call site, [1] flags, [2] calls, [3] calls the GPU path took, [4..11] calls whose face
 * record lies in a car-mesh draw call of this frame, by its mode 0..7 ([12] of those a taken draw, [13] a refused one),
 * [14] a sample face record, [15] its first vertex. The frame's car-mesh draws: a4_gl.c's car take records each one
 * (faces, faces + 52 nf, mode, taken) at 0x5C0E10. */
int a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags, uint32_t caller)
{
    const int r = a4_gl_draw_prim(ctx, desc, flags);
    const volatile uint32_t *FR = (const volatile uint32_t *)0x00456000u;   /* A4_GL_FREC */
    const uint32_t frames = ((const volatile uint32_t *)0x00401000u)[1];   /* A4_GL_SCRATCH: struct glr frames */
    if (FR[0] == 0x31435246u && FR[1]) {
        const volatile uint32_t *fe = FR + 4u + ((FR[1] - 1u) & 255u) * 8u;
        if (((fe[2] >> 20) & 0xFFu) == 2u) {
            volatile uint32_t *C = (volatile uint32_t *)0x005C0900u;
            if (C[0] != 0x324E4353u) { for (uint32_t k = 1; k < 4u + 20u * 16u; k++) C[k] = 0; C[0] = 0x324E4353u; }
            C[1]++;
            if (C[3] != FR[1]) { C[3] = FR[1]; C[2]++; }   /* a new render (the frame record's count moved) */
            volatile uint32_t *e = C + 4;
            uint32_t k = 0;
            for (; k < 20u; k++, e += 16) if ((e[0] == caller && e[1] == flags) || e[2] == 0u) break;
            if (k < 20u) {
                if (!e[2]) { e[0] = caller; e[1] = flags; e[14] = (uint32_t)(uintptr_t)desc; e[15] = desc ? desc[3] : 0u; }
                e[2]++; if (r) e[3]++;
                const volatile uint32_t *L = (const volatile uint32_t *)0x005C0E10u;
                if (L[0] == frames) {
                    const uint32_t a = (uint32_t)(uintptr_t)desc;
                    for (uint32_t q = 0; q < L[1] && q < 24u; q++) {
                        const volatile uint32_t *d = L + 2u + q * 4u;
                        if (a >= d[0] && a < d[1]) { e[4u + (d[2] & 7u)]++; e[d[3] ? 12u : 13u]++; break; }
                    }
                }
            }
        }
    }
    return r;
}
#else
int a4_prim_capture(uint32_t *ctx, uint32_t *desc, uint32_t flags)
{
    return a4_gl_draw_prim(ctx, desc, flags);
}
#endif
#endif
