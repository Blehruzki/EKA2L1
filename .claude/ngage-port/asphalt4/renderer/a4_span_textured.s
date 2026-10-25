@ a4_span_textured.s -- hand-tuned ARM rewrite of Asphalt 4's textured + light
@ span filler. Same semantics as the shipped Thumb loop at code +3357A (verified
@ bit-exact by span_model.py / span_harness.py), and the same AAPCS signature as
@ a4_span_textured.c:
@
@   void a4_span_textured(uint16_t *dst, uint8_t *cov, int n,
@       uint32_t u, uint32_t v, uint32_t ustep, uint32_t vstep,
@       uint32_t umask, uint32_t vmask, uint32_t vshift,
@       const uint16_t *tex, uint32_t light, uint32_t thresh, uint32_t lightmask)
@
@ Why by hand: the loop has 16 values that must live across the whole loop
@ (6 advancing: dst, cov, u, v, light, counter; 10 invariant: K1, K2, tex,
@ umask, vmask, vshift, ustep, vstep, thresh, lightmask), and the saturating
@ RGB565 blend needs 4 scratch registers at its peak. ARM has 14 usable
@ registers, so 16 + 4 > 14: the floor is 6 invariant reloads per pixel. clang
@ -O2 on the C version leaves 10 (it also reloads K1/K2 from the literal pool
@ every pixel). This keeps the 6 hottest values resident (dst, cov, u, v, light,
@ counter + K1, K2, tex, umask) and reloads only the 6 cold invariants
@ (vmask, vshift, ustep, vstep, thresh, lightmask) straight from the incoming
@ stack args -- no spill stores, no literal-pool reloads.
@
@ Build (position independent, no data relocations):
@   clang --target=armv5te-none-eabi -marm -c a4_span_textured.s -o a4_span_textured.o
@   llvm-objcopy -O binary --only-section=.text a4_span_textured.o a4_span_textured.bin

        .syntax unified
        .arm
        .text
        .globl  a4_span_textured
        .type   a4_span_textured, %function
a4_span_textured:
        cmp     r2, #1
        bxlt    lr                      @ n < 1: nothing to do
        push    {r4-r11, lr}            @ 9 words = 36 bytes; stack args at sp+36+
        @ resident set
        ldr     r4,  [sp, #36]          @ r4  = v
        ldr     r5,  [sp, #64]          @ r5  = light
        ldr     r8,  [sp, #60]          @ r8  = tex
        ldr     r9,  [sp, #48]          @ r9  = umask
        ldr     r6,  .LK1               @ r6  = K1 = 0x0019660D
        ldr     r7,  .LK2               @ r7  = K2 = 0x3C6EF35F
        @ r0=dst  r1=cov  r2=counter(n)  r3=u  r4=v  r5=light
        @ r6=K1   r7=K2   r8=tex  r9=umask   scratch: r10 r11 r12 lr
.Lloop:
        @ --- light gradient:  light = light*K1 + K2 ---
        mla     r5, r5, r6, r7          @ light = light*K1 + K2
        ldr     r10, [sp, #72]          @ lightmask
        ldr     r11, [sp, #68]          @ thresh
        and     r10, r10, r5, lsr #16   @ li = (light>>16) & lightmask
        subs    r12, r10, r11           @ li - thresh  (C set iff li >= thresh)
        movcs   r10, r12                @ if li >= thresh: li -= thresh
        lsl     r11, r10, #6            @ li<<6
        orr     r11, r11, r10, lsl #11  @ (li<<11)|(li<<6)
        orr     r10, r11, r10           @ lc = (li<<11)|(li<<6)|li
        @ --- affine texel lookup ---
        and     r11, r9, r3, lsr #16    @ tu = (u>>16) & umask
        ldr     r12, [sp, #52]          @ vmask
        and     r12, r12, r4, lsr #16   @ tv = (v>>16) & vmask
        ldr     lr,  [sp, #56]          @ vshift
        orr     r11, r11, r12, lsl lr   @ idx = (tv<<vshift) | tu
        add     r11, r8, r11, lsl #1    @ &tex[idx]   (ldrh has no scaled offset)
        ldrh    r11, [r11]              @ texel (RGB565)
        @ --- per-channel saturating RGB565 add (lc=r10, texel=r11) ---
        add     r12, r11, r10           @ s  = texel + lc
        eor     lr,  r11, r10           @ x  = texel ^ lc
        eor     r11, lr, r12            @ x ^ s
        and     r11, r11, #0x20         @ c1 = (x^s) & 0x20
        sub     r12, r12, r11           @ sa = s - c1
        eor     r10, r12, lr            @ sa ^ x
        and     r10, r10, #0x800        @ c2 = (sa^x) & 0x800
        sub     r12, r12, r10           @ s2 = sa - c2
        orr     r11, r11, r10           @ c1 | c2
        and     r10, r12, #0x10000      @ c3 = s2 & 0x10000
        orr     r11, r11, r10           @ carries = c3 | c2 | c1
        sub     r11, r11, r11, lsr #4   @ mask = carries - (carries>>4)
        orr     r12, r12, r11           @ out = s2 | mask
        @ --- store + advance ---
        strh    r12, [r0], #2           @ *dst++ = out
        mov     r12, #0
        strb    r12, [r1], #1           @ *cov++ = 0
        ldr     r10, [sp, #40]          @ ustep
        ldr     r11, [sp, #44]          @ vstep
        add     r3, r3, r10             @ u += ustep
        add     r4, r4, r11             @ v += vstep
        subs    r2, r2, #1              @ counter--
        bne     .Lloop
        pop     {r4-r11, lr}
        bx      lr
        .align  2
.LK1:   .word   0x0019660D
.LK2:   .word   0x3C6EF35F
        .size   a4_span_textured, . - a4_span_textured
