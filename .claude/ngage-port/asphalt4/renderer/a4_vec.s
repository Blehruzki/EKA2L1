@ a4_vec.s -- hand-tuned ARM rewrites of two hot fixed-point vector leaves,
@ forcing ldmia/stmia so the four elements move as bursts instead of eight
@ scattered ldr/str (what clang would not commit to). Pure, position
@ independent, AAPCS; verified bit-exact by renderer/a4_vec_verify.py.
@
@   +51D96  a4_vec_add(int a[4], const int b[4])         : a[i] += b[i]
@   +51DB8  a4_vec_mulsub(int a[4], int s, const int b[4]): a[i] -= s*b[i]
        .syntax unified
        .arm
        .text

        .global a4_vec_add
a4_vec_add:                     @ r0=a, r1=b
        push    {r4-r8}
        ldmia   r0, {r2, r3, r4, r5}        @ a[0..3]  (one burst)
        ldmia   r1, {r6, r7, r8, r12}       @ b[0..3]  (one burst)
        add     r2, r2, r6
        add     r3, r3, r7
        add     r4, r4, r8
        add     r5, r5, r12
        stmia   r0, {r2, r3, r4, r5}        @ a[0..3]  (one burst)
        pop     {r4-r8}
        bx      lr

        .global a4_vec_mulsub
a4_vec_mulsub:                  @ r0=a, r1=s, r2=b
        push    {r4-r9}
        ldmia   r2, {r6, r7, r8, r9}        @ b[0..3]  (read before r2 is reused)
        ldmia   r0, {r2, r3, r4, r5}        @ a[0..3]
        mul     r6, r1, r6
        sub     r2, r2, r6
        mul     r7, r1, r7
        sub     r3, r3, r7
        mul     r8, r1, r8
        sub     r4, r4, r8
        mul     r9, r1, r9
        sub     r5, r5, r9
        stmia   r0, {r2, r3, r4, r5}
        pop     {r4-r9}
        bx      lr
