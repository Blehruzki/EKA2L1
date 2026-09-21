    .syntax unified
    .arm
    .section .text,"ax"
    .global _start
    .global old_call
    .global old_call_slot1

_start:
    bl   gate3_main
    @ gate3_main panics with the result, so reaching here means the import
    @ never resolved.  Fall back to the fault the emulator can still report.
    ldr  r1, =0xBEEF0000
    orr  r0, r1, r0
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

    @ An import stub, exactly as the Symbian linker emits it: `ldr pc,[pc,#-4]`
    @ reads pc as its own address plus 8, so it jumps through the word that
    @ follows, and the loader overwrites that word with the resolved address.
    .macro IMPORT name, ordinal
    .global \name
\name:
    ldr  pc, [pc, #-4]
    .global \name\()_ord
\name\()_ord:
    .word \ordinal
    .endm

    IMPORT user_panic, 650          @ User::Panic(TDesC16 const&, TInt)
