@ a4_eng.s -- round 50: the engine rewrite's way back into the game (a4_eng.h)
        .syntax unified
        .arm
        .text
@ uint64_t a4_eng_call(fn, a0, a1, a2, [sp] a3, [sp+4] emulated sp, [sp+8] &r2r3): a call into the game from a lifted
@ function -- the eight words at the emulated stack pointer copied to the real stack as the callee's
@ stack arguments, r0:r1 returned and r2, r3 stored (the game's code reads them after some calls)
        .global a4_eng_call
a4_eng_call:
        push    {r4-r8, lr}
        ldr     r4, [sp, #24]
        ldr     r5, [sp, #28]
        mov     r12, r0
        sub     sp, sp, #32
        ldr     r6, [r5, #0]
        ldr     r7, [r5, #4]
        ldr     r8, [r5, #8]
        str     r6, [sp, #0]
        str     r7, [sp, #4]
        str     r8, [sp, #8]
        ldr     r6, [r5, #12]
        ldr     r7, [r5, #16]
        ldr     r8, [r5, #20]
        str     r6, [sp, #12]
        str     r7, [sp, #16]
        str     r8, [sp, #20]
        ldr     r6, [r5, #24]
        ldr     r7, [r5, #28]
        str     r6, [sp, #24]
        str     r7, [sp, #28]
        mov     r0, r1
        mov     r1, r2
        mov     r2, r3
        mov     r3, r4
        blx     r12
        ldr     r12, [sp, #64]      @ where r2, r3 go (the callee's, as the game's code may read them)
        str     r2, [r12, #0]
        str     r3, [r12, #4]
        add     sp, sp, #32
        pop     {r4-r8, pc}
@ the game's HUD sprite painter (+29736) as it was, its prologue (overwritten by the engine's veneer)
@ replayed and then on at +29740 (Thumb): A4_PAINTCHECK runs it beside the lifted one. Its epilogue
@ (add sp, #0x5c; pop {r4-r7, pc}) returns to our caller through the lr pushed here.
        .global a4_paint_orig
a4_paint_orig:
        push    {r0-r7, lr}         @ +29736: push {r0-r7, lr}
        sub     sp, sp, #0x4c       @ +29738
        ldr     r4, [sp, #0x70]     @ +2973A
        mov     r7, r0              @ +2973C (flags dead: +29760 sets them)
        mov     r3, #0              @ +2973E
        adr     r12, a4_g_base
        ldr     lr, [r12]
        add     r12, r12, lr        @ the game's code
        add     r12, r12, #0x29000
        add     r12, r12, #0x740
        add     r12, r12, #1        @ +29740, Thumb
        bx      r12
        .global a4_g_base
a4_g_base:
        .word   0xDEADBEEF          @ patched: (code + 0) - (this word): the game's code, where it runs
