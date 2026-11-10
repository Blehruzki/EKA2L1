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

@ ---- fps instrumentation: detour planted just BEFORE the present call (+0x1E6),
@ so the overlay a4_fps_tick draws into the back buffer lands on the finished
@ frame and is carried by the present copy (hooking after the present would be
@ overwritten by the next frame's render). Reached by a Thumb BLX, lr = +0x1EA.
@ Reads User::TickCount (euser ord 674, IAT +0x50CE8, pc-relative so portable),
@ calls a4_fps_tick(tick), then re-executes the two displaced loads
@ (ldr r0,[r4,#0x20] ; ldr r3,[r0]) and returns into the present call. ----
@ measurement trampolines (fps detour + back-buffer cache); omitted in --clean builds
#ifndef A4_CLEAN
        .global a4_fps_tramp
a4_fps_tramp:
        @ Save ALL caller-saved regs: the present call just past the return site
        @ takes arguments in r1/r2 that the surrounding code computed before +0x1E6,
        @ and the tick/draw calls would clobber them. r4 is callee-saved by them.
        push    {r0, r1, r2, r3, r12, lr}
#if defined(A4_LRCAP) || defined(A4_SKIPREPRES)
        @ stash DoPresent's caller return address (its push {r4,lr} frame sits just
        @ above our 6-word push: [sp+28]) so a4_gl_frame can histogram which call site
        @ presents on a real-render vs an empty frame. r2/r3 are restored by the pop
        @ below before the present, so clobbering them here is safe.
        ldr     r2, [sp, #28]
        ldr     r3, a4_lrcap_slot_addr
        str     r2, [r3]
        b       9f
a4_lrcap_slot_addr:
        .word   0x00400840
9:
#endif
        @ Resolve two euser imports pc-relatively and pass them to a4_fps_tick,
        @ which calls them: User::TickCount (ord 674, monotonic, for the ring) and
        @ TTime::HomeTime (ord 859, real microseconds, so the window is one real
        @ second and the frame count is true fps).
        ldr     r0, a4_fps_tick_iat
        add     r0, pc, r0          @ pc = a4_fps_tick_iat addr
        b       2f
        .global a4_fps_tick_iat
a4_fps_tick_iat:
        .word   0xDEADBEEF          @ patched: (code+0x50CE8) - (this word's addr)
2:      ldr     r0, [r0]            @ r0 = &User::TickCount (arg0)
        ldr     r1, a4_fps_home_iat
        add     r1, pc, r1          @ pc = a4_fps_home_iat addr
        b       3f
        .global a4_fps_home_iat
a4_fps_home_iat:
        .word   0xDEADBEEF          @ patched: (code+0x50CF0) - (this word's addr)
        .global a4_rep1, a4_rep2
a4_rep1:
        .word   0xDEADBEEF          @ patched: (code+0x955) - (this word's addr): DoPresent's return from +950
a4_rep2:
        .word   0xDEADBEEF          @ patched: (code+0x983) - (this word's addr): DoPresent's return from +97E
3:      ldr     r1, [r1]            @ r1 = &TTime::HomeTime (arg1)
        bl      a4_fps_tick         @ a4_fps_tick(tickcount_fn, hometime_fn); preserves r4-r11
        cmp     r0, #0              @ 1 = the GPU presented this frame (A4_WINDOW)
        pop     {r0, r1, r2, r3, r12, lr}   @ restore the pre-hook r0-r3,r12 (flags kept)
        ldr     r0, [r4, #0x20]     @ displaced 0x1E6: ldr r0,[r4,#0x20]
        ldr     r3, [r0]            @ displaced 0x1E8: ldr r3,[r0]
        addne   lr, lr, #4          @ GPU presented: skip +0x1EA/+0x1EC (ldr r3,[r3,#0x14]; blx r3), the game's present
        bx      lr                  @ -> +0x1EA (the present call) or +0x1EE (past it)

@ ---- cache the back-buffer base: inserted at the textured filler's +0x33412,
@ where r2 = ctx[0x4c] (the surface the 3D renders into). Stashes it at a fixed
@ scratch word so the fps overlay can draw into the frame. Re-executes the two
@ displaced loads first (sp unchanged, so [sp,#0x60] is valid), then stashes;
@ uses only r12 as scratch (dead here -- the inner loop's u-accum is set later). ----
        .global a4_cachebb_tramp
a4_cachebb_tramp:
        ldr     r1, [sp, #0x60]     @ displaced: ldr r1,[sp,#0x60]
        ldr     r1, [r1, #0x10]     @ displaced: ldr r1,[r1,#0x10]
        ldr     r12, a4_bb_addr
        str     r2, [r12]           @ *(a4_bb_addr) = back-buffer base
        bx      lr
        .global a4_bb_addr
a4_bb_addr:
        .word   0x00400700          @ A4_FPS_BB (absolute; data loads at its linked base)

@ ---- MBX stage 3a: primitive capture, inserted at draw_primitive +0x300B2
@ (just after the prologue push/sub, so lr is already saved on the stack and
@ r0/r1/r2 still hold ctx/desc/flags). Records the triangle via a4_prim_capture,
@ then re-executes the two displaced Thumb moves (movs r5,r0 ; movs r4,r2) and
@ returns to +0x300B6. lr is free to clobber here -- the real return was saved
@ by the push at +0x300AE and is restored by the function's pop {r4,r5,pc}. ----
        .global a4_prim_capture_tramp
a4_prim_capture_tramp:
        push    {r0, r1, r2, r3, r12, lr}
        bl      a4_prim_capture     @ ret r0: 1 = GPU drew it (skip software), 0 = continue
        cmp     r0, #0
        bne     1f
        pop     {r0, r1, r2, r3, r12, lr}
        mov     r5, r0              @ displaced +300B2: movs r5, r0
        mov     r4, r2              @ displaced +300B4: movs r4, r2
        bx      lr                  @ -> +0x300B6 (Thumb; software rasterizer runs)
1:      @ GPU drew it: return from draw_primitive via its own epilogue (+0x300E0:
        @ add sp,#0x14 ; pop {r4,r5,pc}), with r0=0 as the cull path returns.
        pop     {r0, r1, r2, r3, r12, lr}   @ balance the push (sp back to post-prologue)
        mov     r0, #0
        ldr     r12, a4_skip_ret
        add     r12, pc, r12        @ pc(add)+8 == a4_skip_ret addr
        bx      r12                 @ -> +0x300E0
        .global a4_skip_ret
a4_skip_ret:
        .word   0xDEADBEEF          @ patched: (code+0x300E0 | 1) - this word's addr

@ ---- the HUD sprite painter (+29736), after its prologue (+2973A). A race frame draws
@ every 3D triangle, then ~17 painter calls for the HUD (MBX.md, the draw order), so the
@ first painter call of a frame is the end of the 3D pass: a4_gl_hud composites the
@ frame's own GPU image there, before the HUD and the tags are painted over it. The two
@ displaced instructions come first, with the painter's own sp (the BLX left it alone). ----
        .global a4_hud_tramp
a4_hud_tramp:
        ldr     r4, [sp, #0x70]     @ displaced +2973A: ldr r4, [sp, #0x70]
        mov     r7, r0              @ displaced +2973C: movs r7, r0 (flags dead: +2973E sets them)
        push    {r0, r1, r2, r3, r12, lr}
        bl      a4_gl_hud
        pop     {r0, r1, r2, r3, r12, lr}
        bx      lr                  @ -> +0x2973E (Thumb)

@ ---- after the game's present call (+1EC blx r3), at +1EE: times it (a4_gl_presented).
@ The displaced pair runs after the pop; flags are dead (+1F2 is pop {r4, pc}). ----
        .global a4_pres_tramp
a4_pres_tramp:
        push    {r0, r1, r2, r3, r12, lr}
        bl      a4_gl_presented
        pop     {r0, r1, r2, r3, r12, lr}
        mov     r0, #0              @ displaced +1EE: movs r0, #0
        str     r0, [r4, #0x6c]     @ displaced +1F0: str r0, [r4, #0x6c]
        bx      lr                  @ -> +0x1F2 (Thumb)

@ ---- the scaled billboard blit (+3015E): the glows, the nitro flame, the pickups --
@ 2D sprites the game draws straight into its surface, outside draw_primitive, among
@ the last triangles of the 3D pass (and in the middle of it in the city; round 12's
@ order trace). Hooked at +30252, after its clip and before its mode switch (lr is
@ dead there: the divides at +30208 clobbered it); a4_gl_bb gets r0..r7 and the
@ blit's sp, and either queues the sprite as a GPU quad in draw order and returns 1
@ (straight to the blit's epilogue) or returns 0 (the software blit runs). ----
        .global a4_bbq_tramp
a4_bbq_tramp:
        ldr     r3, [r7, #0x10]     @ displaced +30252: ldr r3, [r7, #0x10]
        ldr     r2, [r7, #4]        @ displaced +30254: ldr r2, [r7, #4]
        push    {r0, r1, r2, r3, r12, lr}
        push    {r0, r1, r2, r3, r4, r5, r6, r7}
        mov     r0, sp              @ r0..r7 at the hook
        add     r1, sp, #56         @ the blit's own sp (14 words pushed here)
        bl      a4_gl_bb
        add     sp, sp, #32
        cmp     r0, #0
        pop     {r0, r1, r2, r3, r12, lr}
        bxeq    lr                  @ software -> +0x30256 (Thumb)
        ldr     r12, a4_bbq_skip
        add     r12, pc, r12        @ pc(add)+8 == a4_bbq_skip addr
        bx      r12                 @ GPU -> +0x3031C: add sp,#0x7c ; pop {r4-r7,pc}
        .global a4_bbq_skip
a4_bbq_skip:
        .word   0xDEADBEEF          @ patched: (code+0x3031C | 1) - this word's addr

@ ---- the rectangle fill (+2AB08), after its prologue (+2AB0C): the letterbox bars and
@ other solid fills; a4_gl_fill gets the fill's sp and queues a solid fill into the game's
@ surface as a flat GPU quad (1: straight to the epilogue) or lets it run (0). The
@ displaced pair, add r1,sp,#0x28 ; ldm r1,{r1,r5,r6}, as three loads (sp is the fill's
@ again after the pop). ----
        .global a4_fill_tramp
a4_fill_tramp:
        push    {r0, r1, r2, r3, r12, lr}
        add     r0, sp, #24         @ the fill's own sp
        bl      a4_gl_fill
        cmp     r0, #0
        pop     {r0, r1, r2, r3, r12, lr}
        bne     1f
        ldr     r5, [sp, #0x2c]     @ displaced +2AB0C/+2AB0E: r1, r5, r6 = h, colour, border
        ldr     r6, [sp, #0x30]
        ldr     r1, [sp, #0x28]
        bx      lr                  @ software -> +0x2AB10 (Thumb)
1:      ldr     r12, a4_fill_skip
        add     r12, pc, r12        @ pc(add)+8 == a4_fill_skip addr
        bx      r12                 @ GPU -> +0x2AB4C: add sp,#0x14 ; pop {r4-r7,pc}
        .global a4_fill_skip
a4_fill_skip:
        .word   0xDEADBEEF          @ patched: (code+0x2AB4C | 1) - this word's addr

@ ---- its sibling (+3059E, a two-depth variant; never called in the bench race): not
@ reproduced, so the frame is composited at its first call and what follows is
@ software, in order (a4_gl_bb2 counts the calls). Hooked after the prologue; the
@ displaced pair runs after the pop, which would undo r12. ----
        .global a4_bb2_tramp
a4_bb2_tramp:
        push    {r0, r1, r2, r3, r12, lr}
        bl      a4_gl_bb2
        pop     {r0, r1, r2, r3, r12, lr}
        mov     r12, r3             @ displaced +305A2: mov ip, r3
        mov     r5, r1              @ displaced +305A4: movs r5, r1 (flags dead: cmp at +305B6)
        bx      lr                  @ -> +0x305A6 (Thumb)
#endif  /* A4_CLEAN */

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

#ifdef A4_HALFLOGIC
@ Round 31, the faster step: the race update's step loop head +167A6 (ldr r0,[r4,#0x38];
@ adds r0,r0,r7 -- r4 the race, r7 0x1100) becomes a BLX here. a4_halfstep(race) says
@ whether this half-step runs the game's whole step (0: the displaced pair, back to
@ +167AA) or not (1: what it runs instead it has run itself; on to the loop's tail +16926,
@ subs r5,#1 ...). lr is +167AA|1, so +16926|1 is lr + 0x17C.
        .arm
        .align  2
        .global a4_hstep_tramp
a4_hstep_tramp:
        push    {r0, r1, r2, r3, r12, lr}
        mov     r0, r4
        bl      a4_halfstep
        cmp     r0, #0
        pop     {r0, r1, r2, r3, r12, lr}
        bne     1f
        ldr     r0, [r4, #0x38]
        adds    r0, r0, r7
        bx      lr
1:      add     lr, lr, #0x17C
        bx      lr
#endif

#ifdef A4_DROPHOLD
@ Round 37: the framework's own post. The race loop calls the framework frame routine
@ +606 after its present (+9BE), and +606 ends with [r4+0x3C]->vtbl[0x34](this, 0) at
@ +6B2/+6B4 (movs r1,#0 ; blx r2), which puts the back buffer on the screen whenever
@ DoPresent's present did not (the bench's trace at a held frame: no +1EA, then +6AC).
@ Those 4 bytes become a BLX here. a4_fw_hold() says whether a frame is being held
@ (A4_DROPHOLD in a4_gl.c): if so the call is skipped -- +606 returns at +6B6 and its
@ caller does not read r0 (+9C2: ldrb r0,[r7,#8]) -- else it is made as the game made
@ it, a tail call with lr = +6B6|1. r0 (this) and r2 (the method) survive our call.
        .arm
        .align  2
        .global a4_fwpost_tramp
a4_fwpost_tramp:
        push    {r0, r2, r3, lr}
        bl      a4_fw_hold
        cmp     r0, #0
        pop     {r0, r2, r3, lr}
        bxne    lr                  @ held: back to +6B6, the post not made
        movs    r1, #0              @ displaced +6B2: movs r1,#0
#ifdef A4_RBPROBE
        @ round 56: the post timed -- made as a call, then a4_fw_done (which returns r0 as given); +6B6
        @ only pops {r4-r6,pc} and its caller reads no register, so r1-r3 may be clobbered
        push    {r4, lr}
        blx     r2                  @ displaced +6B4: blx r2
        bl      a4_fw_done
        pop     {r4, pc}            @ to +6B6
#else
        bx      r2                  @ displaced +6B4: blx r2, as a tail call (returns to +6B6)
#endif
#endif

#ifdef A4_SPECCAMSAFE
@ Round 40: the camera scripts are not advanced by the speculation. +5205A plays a camera
@ script one tick (its clock [obj+0x8C]++, eleven channels interpolated) and unlinks and
@ deletes each keyframe node passed; +3D2A sets a script up (allocates). Both under the
@ camera update +12DEC, which the speculation runs; their state is race words, which the
@ restore puts back, but the deleted nodes stay freed -- the race then points at freed
@ keyframes (the bench's gated watch: nodes freed and allocated by the speculative update,
@ its addresses climbing, round 40). The four bl's (+556E, +5912 to +5205A; +5578, +5926 to
@ +3D2A) become BLX's here: skipped while the speculation runs the camera update (SPEC word
@ 24, 0x447060, set around it), else entered as the bl would have -- registers, stack and
@ lr untouched. No caller reads the result (each reloads r0-r2 after).
        .arm
        .align  2
        .global a4_cs_5205a_tramp
a4_cs_5205a_tramp:
        ldr     r12, a4_cs_flag1
        ldr     r12, [r12]
        cmp     r12, #0
        beq     1f
        ldr     r12, a4_cs_flag1           @ speculating: the script not played, and counted in SPEC
        ldr     r1, [r12, #4]       @ word 25 (round 41: the frame is then drawn at the whole
        add     r1, r1, #1          @ step, A4_SPECSCRIPTHOLD); r1 is the callee's to clobber,
        str     r1, [r12, #4]       @ and the call is not made
        bx      lr
1:      ldr     r12, a4_hc_5205a
        add     r12, pc, r12        @ pc reads as a4_hc_5205a
        bx      r12
        .global a4_hc_5205a
a4_hc_5205a:
        .word   0xDEADBEEF          @ patched: (code+0x5205A | 1) - (this word)
a4_cs_flag1:
        .word   0x00447060
        .global a4_cs_3d2a_tramp
a4_cs_3d2a_tramp:
        ldr     r12, a4_cs_flag2
        ldr     r12, [r12]
        cmp     r12, #0
        beq     1f
        ldr     r12, a4_cs_flag2           @ speculating: no script set up, and counted in SPEC
        ldr     r1, [r12, #4]       @ word 25 (round 41: the frame is then drawn at the whole
        add     r1, r1, #1          @ step, A4_SPECSCRIPTHOLD); r1 is the callee's to clobber,
        str     r1, [r12, #4]       @ and the call is not made
        bx      lr
1:      ldr     r12, a4_hc_3d2a
        add     r12, pc, r12        @ pc reads as a4_hc_3d2a
        bx      r12
        .global a4_hc_3d2a
a4_hc_3d2a:
        .word   0xDEADBEEF          @ patched: (code+0x3D2A | 1) - (this word)
a4_cs_flag2:
        .word   0x00447060
@ Round 41: the rest of the camera scripts' heap -- the keyframe add +51FEC (a node linked into
@ a channel's list, whose sentinel is a heap node outside the saved race object) and the script
@ init +5202C (every channel's list reset, +537BE). Run by the speculation (+554C starts a
@ scripted camera: +5202C at +5618/+5620, then seven +51FEC), they survived the restore: the
@ race's words rewound, the lists not -- the real camera script then held the speculation's
@ keyframes. Skipped and counted like the two above (every call site).
        .global a4_cs_51fec_tramp
a4_cs_51fec_tramp:
        ldr     r12, a4_cs_flag3
        ldr     r12, [r12]
        cmp     r12, #0
        beq     1f
        ldr     r12, a4_cs_flag3    @ speculating: no keyframe added, and counted
        ldr     r1, [r12, #4]
        add     r1, r1, #1
        str     r1, [r12, #4]
        bx      lr
1:      ldr     r12, a4_hc_51fec
        add     r12, pc, r12        @ pc reads as a4_hc_51fec
        bx      r12
        .global a4_hc_51fec
a4_hc_51fec:
        .word   0xDEADBEEF          @ patched: (code+0x51FEC | 1) - (this word)
a4_cs_flag3:
        .word   0x00447060
        .global a4_cs_5202c_tramp
a4_cs_5202c_tramp:
        ldr     r12, a4_cs_flag4
        ldr     r12, [r12]
        cmp     r12, #0
        beq     1f
        ldr     r12, a4_cs_flag4    @ speculating: no script reset, and counted
        ldr     r1, [r12, #4]
        add     r1, r1, #1
        str     r1, [r12, #4]
        bx      lr
1:      ldr     r12, a4_hc_5202c
        add     r12, pc, r12        @ pc reads as a4_hc_5202c
        bx      r12
        .global a4_hc_5202c
a4_hc_5202c:
        .word   0xDEADBEEF          @ patched: (code+0x5202C | 1) - (this word)
a4_cs_flag4:
        .word   0x00447060
#endif

#ifdef A4_ZSKIP
@ Round 51: the game's begin-frame (+31C70) zero-fills its per-pixel 1/w buffer every frame (+2DEF4: 300 KB,
@ euser's memset -- ~5 ms a render on the round-50 model, its missed stores). Only the perspective span
@ (+333F4) reads or writes that buffer (bench watch zw2), and the span runs only for the perspective
@ triangles our draw hook leaves to software. a4_zclr_skip (a4_gl.c) answers whether the buffer is still
@ all zero since this context's last clear; if it may not be, the game's clear runs as it always did.
        .global a4_zclr_tramp
a4_zclr_tramp:
        push    {r0, lr}
        bl      a4_zclr_skip
        cmp     r0, #0
        pop     {r0, lr}
        bxne    lr                  @ known clean: nothing to clear
        ldr     r12, a4_hc_2def4
        add     r12, pc, r12        @ pc reads as a4_hc_2def4
        bx      r12                 @ the game's clear, returning to the game
        .global a4_hc_2def4
a4_hc_2def4:
        .word   0xDEADBEEF          @ patched: (code+0x2DEF4 | 1) - (this word)
#endif

#if defined(A4_SKYCEN) || defined(A4_SKYSKIP)
@ Round 54: the sky routine +35FDA draws the backdrop before the frame's first triangle -- a top fill,
@ the band (a memmove a row), then the bottom fill when its flag (the stacked argument at [sp, #0x70])
@ is set. Its band loops all join at +3616A, where r4 points past the band's last row. The BLX there
@ replaces `ldr r0, [sp, #0x70]; cmp r0, #0`, done here with the flags it leaves; r1-r3 are dead at
@ that point (reloaded on the fill's path, unused on the other). Noted at A4_GL_SKY (0x412800, a4_gl.c):
@ [0] r4, [1] the framebuffer (ctx [0x4c], ctx the routine's stacked r2 at [sp, #0x54]), [2] calls,
@ [3] the sky image's header (the stacked r0's +8: w, h halfwords, pixels at +0x10), [4] the fill flag.
        .global a4_sky_tramp
a4_sky_tramp:
        ldr     r1, [sp, #0x54]
        ldr     r1, [r1, #0x4c]
        ldr     r2, a4_sky_w
        str     r4, [r2]
        str     r1, [r2, #4]
        ldr     r3, [r2, #8]
        add     r3, r3, #1
        str     r3, [r2, #8]
        ldr     r3, [sp, #0x4c]
        ldr     r3, [r3, #8]
        str     r3, [r2, #12]
        ldr     r0, [sp, #0x70]
        str     r0, [r2, #16]
        cmp     r0, #0
        bx      lr
a4_sky_w:
        .word   0x00412800
#endif

#ifdef A4_GPUSKY
@ Round 54: the backdrop on the GPU. The sky routine +35FDA's stores -- the top fill (stm loop +360A2),
@ the band (a memmove a row: +36120, or +3614E/+3615A where it wraps) and the bottom fill (stm loop
@ +361A4) -- are recorded by a4_gl.c (a4_sky_mv/top/bot), which draws them on the GPU at the frame's
@ first triangle; each returns 1 when it recorded the store (the routine skips it) or 0 (it runs).
@ Each tramp pushes r0-r5, r12, lr (8 words) and passes that block; the routine's sp is block + 32.
        .global a4_skymv_tramp
a4_skymv_tramp:                     @ at the band's BLX to the memmove veneer (+50EAC, ARM)
        push    {r0, r1, r2, r3, r4, r5, r12, lr}
        mov     r0, sp
        bl      a4_sky_mv
        cmp     r0, #0
        pop     {r0, r1, r2, r3, r4, r5, r12, lr}
        bxne    lr                  @ recorded: the row is the GPU's
        ldr     r12, a4_hc_50eac
        add     r12, pc, r12        @ pc reads as a4_hc_50eac
        bx      r12                 @ the game's memmove, returning to the game
        .global a4_hc_50eac
a4_hc_50eac:
        .word   0xDEADBEEF          @ patched: (code+0x50EAC) - (this word); ARM, no Thumb bit

        .global a4_skytop_tramp
a4_skytop_tramp:                    @ at +3609C: adds r0, r0, r4 ; cmp r4, r0 (the bhs after reads it)
        add     r0, r0, r4          @ the fill's end
        push    {r0, r1, r2, r3, r4, r5, r12, lr}
        mov     r0, sp
        bl      a4_sky_top
        cmp     r0, #0
        pop     {r0, r1, r2, r3, r4, r5, r12, lr}
        movne   r4, r0              @ recorded: the loop starts at its end
        cmp     r4, r0
        bx      lr

        .global a4_skybot_tramp
a4_skybot_tramp:                    @ at +3619C: lsls r2, r1, #16 ; orrs r1, r2 (a cmp follows)
        lsl     r2, r1, #16
        orr     r1, r1, r2          @ the colour pair
        push    {r0, r1, r2, r3, r4, r5, r12, lr}
        mov     r0, sp
        bl      a4_sky_bot
        cmp     r0, #0
        pop     {r0, r1, r2, r3, r4, r5, r12, lr}
        movne   r4, r0              @ recorded: the loop starts at its end
        bx      lr
#endif
