    .syntax unified
    .arm
    .section .text,"ax"
    .global _start
    .global old_call
    .global old_call_slot1

_start:
    bl   gate3_main
    ldr  r1, =0xBEEF0000
    orr  r0, r1, r0            @ the fault address carries the result mask
    ldr  r1, [r0]
    b    .

    @ this in r0, slot index in r1.  Two header words sit before slot 0, so the
    @ word index is slot + 2.
old_call:
    push {r4, lr}
    add  r1, r1, #2
    ldr  r3, [r0]
    ldr  r3, [r3, r1, lsl #2]
    mov  lr, pc
    bx   r3
    pop  {r4, lr}
    bx   lr

    @ The exact shape read out of the game's code, for slot 1: 8 + 4*1 = 12.
old_call_slot1:
    push {r4, lr}
    ldr  r3, [r0]
    ldr  r3, [r3, #12]
    mov  lr, pc
    bx   r3
    pop  {r4, lr}
    bx   lr

    .ltorg
