    .syntax unified
    .arm
    .section .text,"ax"
    .global _start
_start:
    ldr r0, =0xDEAD0000        @ an address nothing can be mapped at
    ldr r1, [r0]               @ the emulator logs the fault with this exact address
    b   .
    .ltorg
