@ a4_tramp.s -- inner-loop trampolines that let the verified ARM span rewrites
@ drop into the shipped Thumb filler functions mid-body.
@
@ Each trampoline is reached by a Thumb->ARM veneer planted at the filler's
@ inner-loop entry. It reads the loop's live registers and the function's
@ stack slots, marshals them into the AAPCS rewrite, restores exactly the
@ registers the surrounding code reads after the loop, and branches back into
@ the Thumb function. The branch-back displacement is a .word placeholder
@ (0xDEADBEEF) the injector patches to (return_link|1) - (pc_of_add + 8).
@
@ Build position-independent like the rest of the port code:
@   clang --target=armv5te-none-eabi -marm -O2 -fropi -c a4_tramp.s -o a4_tramp.o
        .syntax unified
        .arm
        .text

@ ---- textured: inner loop +3357A, returns into the per-span loop at +3361C ----
@ Live in:  r0=cov r4=dst r7=light r12=u lr=v  sp=FP (r5,r6 = per-span base, keep)
@ Stack slots off FP: 0x14 dstend, 0x18 vstep, 0x1c ustep, 0x20 lightmask,
@                     0x24 thresh, 0x28 vshift, 0x2c vmask, 0x30 umask, 0x44 tex
@ Exit contract: r7=final light, r4=dst+2n, r0=cov+n, r5/r6 preserved.
        .global a4_tex_tramp
a4_tex_tramp:
        push    {r5, r6}            @ preserve the function's r5,r6; sp -= 8
        add     r6, sp, #8          @ r6 = FP (stable slot base; r6 restored at end)
        mov     r9, r4              @ r9 = dst
        mov     r10, r0             @ r10 = cov
        mov     r11, r7             @ r11 = light
        mov     r8, r12             @ r8 = u
        ldr     r1, [r6, #0x14]     @ dstend
        sub     r1, r1, r4
        lsr     r5, r1, #1          @ r5 = n
        sub     sp, sp, #40         @ 10-word AAPCS stack-arg frame
        str     lr, [sp, #0]        @ v
        ldr     r1, [r6, #0x1c]
        str     r1, [sp, #4]        @ ustep
        ldr     r1, [r6, #0x18]
        str     r1, [sp, #8]        @ vstep
        ldr     r1, [r6, #0x30]
        str     r1, [sp, #12]       @ umask
        ldr     r1, [r6, #0x2c]
        str     r1, [sp, #16]       @ vmask
        ldr     r1, [r6, #0x28]
        str     r1, [sp, #20]       @ vshift
        ldr     r1, [r6, #0x44]
        str     r1, [sp, #24]       @ tex
        str     r11, [sp, #28]      @ light
        ldr     r1, [r6, #0x24]
        str     r1, [sp, #32]       @ thresh
        ldr     r1, [r6, #0x20]
        str     r1, [sp, #36]       @ lightmask
        mov     r0, r9              @ dst
        mov     r1, r10             @ cov
        mov     r2, r5              @ n
        mov     r3, r8              @ u
        bl      a4_span_textured    @ r0 = final light; r4-r11 preserved by callee
        add     sp, sp, #40
        mov     r7, r0              @ exit: final light
        add     r4, r9, r5, lsl #1  @ exit: dst + 2n
        add     r0, r10, r5         @ exit: cov + n
        pop     {r5, r6}            @ restore function's r5,r6
        ldr     r12, a4_tex_ret
        add     r12, pc, r12        @ pc = this + 8
        bx      r12                 @ -> Thumb return site
a4_tex_ret:
        .word   0xDEADBEEF

@ ---- transparent: inner loop +33B16; loop exit falls straight into the
@ function epilogue (+33B42: add sp,#0x34; pop). Nothing is carried, so the
@ contract is pixels only; return to +33B42 and let the epilogue run. ----
@ Live in:  r1=u r2=v r4=dst  ip=texbase lr=? [sp] slots (see transp_harness)
@ The transparent rewrite's arg contract is filled by the injector-side veneer
@ per transp_harness; this trampoline marshals it. Placeholder body patched in
@ once transp_harness entry mapping is transcribed (see a4_transp_tramp build).
@ Live in:  r1=u r2=v r6=ustep r7=vstep lr=umask r12=tex r4=dst  sp=FP
@ Slots off FP: 0x10 vshift, 0x14 vmask, 0x18 dst_end.
@ The epilogue at the return site pops r4-r7 from the frame, so nothing needs
@ restoring except sp=FP; contract is pixels only.
        .global a4_transp_tramp
a4_transp_tramp:
        mov     r8, sp              @ r8 = FP (slot base)
        ldr     r0, [r8, #0x18]     @ dst_end
        sub     r0, r0, r4
        lsr     r5, r0, #1          @ r5 = n
        mov     r9, r4              @ dst
        mov     r10, r1             @ u
        mov     r11, r2             @ v
        sub     sp, sp, #24         @ 6-word AAPCS stack-arg frame (8-aligned)
        str     r6, [sp, #0]        @ ustep
        str     r7, [sp, #4]        @ vstep
        str     lr, [sp, #8]        @ umask
        ldr     r0, [r8, #0x14]
        str     r0, [sp, #12]       @ vmask
        ldr     r0, [r8, #0x10]
        str     r0, [sp, #16]       @ vshift
        str     r12, [sp, #20]      @ tex
        mov     r0, r9              @ dst
        mov     r1, r5              @ n
        mov     r2, r10             @ u
        mov     r3, r11             @ v
        bl      a4_span_transparent
        add     sp, sp, #24         @ sp = FP
        ldr     r12, a4_transp_ret
        add     r12, pc, r12
        bx      r12                 @ -> Thumb epilogue at +33B42
a4_transp_ret:
        .word   0xDEADBEEF
