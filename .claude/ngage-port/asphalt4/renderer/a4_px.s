@ a4_px.s -- round 50: the two whole-screen pixel loops in ARMv6 (the N95's ARM1136 is an ARMv6
@ core; the rest of the blob is built for ARMv5TE). PKHBT/PKHTB pack one halfword of each of two
@ registers into one, so two pixels go through each mask, shift and OR together: the same pixels
@ as the C loops they replace, in about half the instructions. Each has a check (a4_gl.c).
        .syntax unified
        .arch   armv6
        .arm
        .text

@ void a4_comp_row(uint32_t *d32, const uint32_t *src, const uint32_t *end)
@ The composite's N95 case (composite_rows: BGRA words, alpha bit 31 the drawn-mask): each pixel
@ whose alpha is >= 0x80 written as 565 from the top bits of its channels -- ((p >> 8) & 0xF800)
@ | ((p >> 5) & 0x7E0) | ((p >> 3) & 0x1F) -- into the row at d32 (word aligned), the others left
@ alone. (end - src) a multiple of 4 pixels, not 0. Four pixels a turn, one store pair when all
@ four are drawn (the common case), halfword stores otherwise. Round 83: returns 1 if any turn took
@ the halfword path (a pixel not drawn), else 0 -- kept in bit 31 of the B-lane mask (lr), which
@ the AND with (x >> 3) never sees (its top three bits are clear): one ORR on the slow path only.
        .global a4_comp_row
a4_comp_row:
        push    {r4-r11, lr}
        mov     r3, #0xF800
        orr     r3, r3, #0xF8000000             @ R lanes: 0xF800F800
        mov     r12, #0x7E0
        orr     r12, r12, #0x7E00000            @ G lanes: 0x07E007E0
        mov     lr, #0x1F
        orr     lr, lr, #0x1F0000               @ B lanes: 0x001F001F
1:      ldm     r1!, {r4-r7}                    @ pixels a b c d
        pkhtb   r10, r5, r4, asr #16            @ [Ab Rb Aa Ra]
        pkhbt   r11, r4, r5, lsl #16            @ [Gb Bb Ga Ba]
        and     r8, r3, r10, lsl #8             @ R of a and b at 11-15 of each half
        and     r10, r12, r11, lsr #5           @ G at 5-10
        orr     r8, r8, r10
        and     r11, lr, r11, lsr #3            @ B at 0-4
        orr     r8, r8, r11                     @ 565(a) | 565(b) << 16
        pkhtb   r10, r7, r6, asr #16
        pkhbt   r11, r6, r7, lsl #16
        and     r9, r3, r10, lsl #8
        and     r10, r12, r11, lsr #5
        orr     r9, r9, r10
        and     r11, lr, r11, lsr #3
        orr     r9, r9, r11                     @ 565(c) | 565(d) << 16
        and     r10, r4, r5
        and     r10, r10, r6
        tst     r10, r7                         @ all four drawn?
        bpl     3f
        stmia   r0!, {r8, r9}
2:      cmp     r1, r2
        bne     1b
        mov     r0, lr, lsr #31                 @ round 83: 1 if a pixel was not drawn
        pop     {r4-r11, pc}
3:      orr     lr, lr, #0x80000000             @ round 83: the row had an undrawn pixel
        tst     r4, #0x80000000                 @ some pixel not drawn: one at a time
        strhne  r8, [r0]
        tst     r5, #0x80000000
        movne   r10, r8, lsr #16
        strhne  r10, [r0, #2]
        tst     r6, #0x80000000
        strhne  r9, [r0, #4]
        tst     r7, #0x80000000
        movne   r10, r9, lsr #16
        strhne  r10, [r0, #6]
        add     r0, r0, #8
        b       2b

@ void a4_cvt565_run(uint32_t *d, const uint32_t *s, const uint32_t *e)
@ The runtime's 565 -> 32-bit convert (ngibackbuffer +CB70's inner loop): each 565 pixel p of the
@ pairs [s, e) to the word ((p & 0xF800) << 8) | ((p & 0x7E0) << 5) | ((p & 0x1F) << 3), in order
@ at d. (e - s) a multiple of two words (four pixels), not 0. The runtime's loop takes 20
@ instructions a pair (each source word loaded twice, a store a pixel); this one 16 for four.
        .global a4_cvt565_run
a4_cvt565_run:
        push    {r4-r8, lr}
        mov     r3, #0xF8
        orr     r3, r3, #0xF80000               @ 0x00F800F8: B (<< 3) and R (>> 8) of both halves
        mov     r12, #0xFC
        orr     r12, r12, #0xFC0000             @ 0x00FC00FC: G (>> 3)
1:      ldm     r1!, {r5, r7}                   @ pixels p0 p1 | p2 p3
        and     r8, r3, r5, lsl #3              @ [0 B1 0 B0]
        and     lr, r12, r5, lsr #3             @ [0 G1 0 G0]
        orr     r8, r8, lr, lsl #8              @ [G1 B1 G0 B0]
        and     lr, r3, r5, lsr #8              @ [0 R1 0 R0]
        pkhbt   r4, r8, lr, lsl #16             @ [0 R0 G0 B0]
        pkhtb   r5, lr, r8, asr #16             @ [0 R1 G1 B1]
        and     r8, r3, r7, lsl #3
        and     lr, r12, r7, lsr #3
        orr     r8, r8, lr, lsl #8
        and     lr, r3, r7, lsr #8
        pkhbt   r6, r8, lr, lsl #16
        pkhtb   r7, lr, r8, asr #16
        stmia   r0!, {r4-r7}
        cmp     r1, r2
        bne     1b
        pop     {r4-r8, pc}

@ round 50, for the phone A/B (A4_ABCYCLE11, AB_PXPLD): the same two loops with a PLD of the source a few
@ lines ahead each turn. The bench's model treats PLD as nothing (no fill); only the phone can say whether the
@ streaming reads' misses are hidden.
        .global a4_comp_row_pld
a4_comp_row_pld:
        push    {r4-r11, lr}
        mov     r3, #0xF800
        orr     r3, r3, #0xF8000000             @ R lanes: 0xF800F800
        mov     r12, #0x7E0
        orr     r12, r12, #0x7E00000            @ G lanes: 0x07E007E0
        mov     lr, #0x1F
        orr     lr, lr, #0x1F0000               @ B lanes: 0x001F001F
1:      pld     [r1, #128]                      @ four lines ahead (16 bytes a turn: every line twice)
        ldm     r1!, {r4-r7}                    @ pixels a b c d
        pkhtb   r10, r5, r4, asr #16            @ [Ab Rb Aa Ra]
        pkhbt   r11, r4, r5, lsl #16            @ [Gb Bb Ga Ba]
        and     r8, r3, r10, lsl #8             @ R of a and b at 11-15 of each half
        and     r10, r12, r11, lsr #5           @ G at 5-10
        orr     r8, r8, r10
        and     r11, lr, r11, lsr #3            @ B at 0-4
        orr     r8, r8, r11                     @ 565(a) | 565(b) << 16
        pkhtb   r10, r7, r6, asr #16
        pkhbt   r11, r6, r7, lsl #16
        and     r9, r3, r10, lsl #8
        and     r10, r12, r11, lsr #5
        orr     r9, r9, r10
        and     r11, lr, r11, lsr #3
        orr     r9, r9, r11                     @ 565(c) | 565(d) << 16
        and     r10, r4, r5
        and     r10, r10, r6
        tst     r10, r7                         @ all four drawn?
        bpl     3f
        stmia   r0!, {r8, r9}
2:      cmp     r1, r2
        bne     1b
        mov     r0, lr, lsr #31                 @ round 83: 1 if a pixel was not drawn
        pop     {r4-r11, pc}
3:      orr     lr, lr, #0x80000000             @ round 83: the row had an undrawn pixel
        tst     r4, #0x80000000                 @ some pixel not drawn: one at a time
        strhne  r8, [r0]
        tst     r5, #0x80000000
        movne   r10, r8, lsr #16
        strhne  r10, [r0, #2]
        tst     r6, #0x80000000
        strhne  r9, [r0, #4]
        tst     r7, #0x80000000
        movne   r10, r9, lsr #16
        strhne  r10, [r0, #6]
        add     r0, r0, #8
        b       2b

        .global a4_cvt565_run_pld
a4_cvt565_run_pld:
        push    {r4-r8, lr}
        mov     r3, #0xF8
        orr     r3, r3, #0xF80000               @ 0x00F800F8: B (<< 3) and R (>> 8) of both halves
        mov     r12, #0xFC
        orr     r12, r12, #0xFC0000             @ 0x00FC00FC: G (>> 3)
1:      pld     [r1, #96]                       @ three lines ahead (8 bytes a turn)
        ldm     r1!, {r5, r7}                   @ pixels p0 p1 | p2 p3
        and     r8, r3, r5, lsl #3              @ [0 B1 0 B0]
        and     lr, r12, r5, lsr #3             @ [0 G1 0 G0]
        orr     r8, r8, lr, lsl #8              @ [G1 B1 G0 B0]
        and     lr, r3, r5, lsr #8              @ [0 R1 0 R0]
        pkhbt   r4, r8, lr, lsl #16             @ [0 R0 G0 B0]
        pkhtb   r5, lr, r8, asr #16             @ [0 R1 G1 B1]
        and     r8, r3, r7, lsl #3
        and     lr, r12, r7, lsr #3
        orr     r8, r8, lr, lsl #8
        and     lr, r3, r7, lsr #8
        pkhbt   r6, r8, lr, lsl #16
        pkhtb   r7, lr, r8, asr #16
        stmia   r0!, {r4-r7}
        cmp     r1, r2
        bne     1b
        pop     {r4-r8, pc}

@ void a4_gobj_copy(uint32_t *dst, const uint32_t *src, uint32_t n, uint32_t dxy, uint32_t dz)
@ Round 67: the GPU scenery's staging copy (gobj_flush). n corners (a multiple of 3, not 0) of 12-byte records
@ [x y] [z u] [v -] (int16 halves): x += dx and y += dy (dxy = dx | dy << 16), z += dz (dz in the low half, 0
@ above), each half wrapping on its own as the C loop's masks do (SADD16), v and the pad copied. Three corners
@ (nine words) a turn: one LDM, six SADD16, one STM -- against ~15 instructions a corner in C.
        .global a4_gobj_copy
a4_gobj_copy:
        push    {r4-r11, lr}
        ldr     r12, [sp, #36]                  @ dz
1:      ldm     r1!, {r4-r11, lr}               @ corners a b c: [xy zu v-] x 3
        sadd16  r4, r4, r3
        sadd16  r5, r5, r12
        sadd16  r7, r7, r3
        sadd16  r8, r8, r12
        sadd16  r10, r10, r3
        sadd16  r11, r11, r12
        stm     r0!, {r4-r11, lr}
        subs    r2, r2, #3
        bgt     1b
        pop     {r4-r11, pc}

@ void a4_blur_row(uint16_t *p6, uint16_t *p7, uint16_t *p5, uint16_t *p4, const uint8_t *kbuf, uint32_t cols,
@                  uint32_t w2, uint32_t odd)
@ Round 88: one row of the nitro blur (a4_gl.c blur_run, the game's +1054C): for each column step, k = kbuf[i] (0:
@ skipped), the four pixels in the game's order -- p6 (up-left, moving left), p7 (down-left, left), p5 (up-right,
@ right), p4 (down-right, right) -- each blended toward its neighbour nearer the centre: across (p6, p7 from the
@ right, p5, p4 from the left) when the step is odd, along the column (p6, p5 from the row below, p7, p4 from the row
@ above; w2 the row in bytes) when even; odd is the first step's parity, flipped every step. A pixel: P = (p | p << 16)
@ & 0x07E0F81F, Q likewise, p = low half of o | o >> 16 with o = (P + ((Q - P) k >> 5)) & 0x07E0F81F -- the game's
@ arithmetic, its 32-bit wrap and logical shift. State in registers: ~12 instructions a pixel against the game's ~30.
        .macro  BLURPX ptr
        orr     r9, r9, r9, lsl #16
        and     r9, r9, r7
        orr     r10, r10, r10, lsl #16
        and     r10, r10, r7
        sub     r10, r10, r9
        mul     r10, r10, r8
        add     r9, r9, r10, lsr #5
        and     r9, r9, r7
        orr     r9, r9, r9, lsr #16
        strh    r9, [\ptr]
        .endm
        .global a4_blur_row
a4_blur_row:
        push    {r4-r11, lr}
        ldr     r4, [sp, #36]                   @ kbuf
        ldr     r5, [sp, #40]                   @ cols
        ldr     r6, [sp, #44]                   @ w2: a row in bytes
        ldr     r11, [sp, #48]                  @ the first step's parity
        mov     r7, #0x1F
        orr     r7, r7, #0xF800
        orr     r7, r7, #0x7E00000              @ the two lanes: 0x07E0F81F (ARMv6: no MOVW/MOVT)
        cmp     r5, #0
        beq     9f
1:      ldrb    r8, [r4], #1                    @ k (0: the step skipped)
        cmp     r8, #0
        beq     4f
        cmp     r11, #0
        bne     3f
        ldrh    r9, [r0]                        @ even: along the column
        ldrh    r10, [r0, r6]
        BLURPX  r0
        ldrh    r9, [r1]
        ldrh    r10, [r1, -r6]
        BLURPX  r1
        ldrh    r9, [r2]
        ldrh    r10, [r2, r6]
        BLURPX  r2
        ldrh    r9, [r3]
        ldrh    r10, [r3, -r6]
        BLURPX  r3
        b       4f
3:      ldrh    r9, [r0]                        @ odd: across
        ldrh    r10, [r0, #2]
        BLURPX  r0
        ldrh    r9, [r1]
        ldrh    r10, [r1, #2]
        BLURPX  r1
        ldrh    r9, [r2]
        ldrh    r10, [r2, #-2]
        BLURPX  r2
        ldrh    r9, [r3]
        ldrh    r10, [r3, #-2]
        BLURPX  r3
4:      sub     r0, r0, #2
        sub     r1, r1, #2
        add     r2, r2, #2
        add     r3, r3, #2
        eor     r11, r11, #1
        subs    r5, r5, #1
        bne     1b
9:      pop     {r4-r11, pc}
