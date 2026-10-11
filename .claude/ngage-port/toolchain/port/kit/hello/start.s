    .syntax unified
    .arm
    .section .text._start,"ax"
    .global _start

    @ The entry point. An SDK build gets this from eexe.lib; without the SDK we
    @ write it. Symbian's own startup (kernel/eka/euser/epoc/arm/uc_exe.cpp) is:
    @   UserHeap::SetupThreadHeap(aNotFirst, cinfo);
    @   User::InitProcess();
    @   E32Main();
    @ The kernel passes the start reason in r4 and leaves SStdEpocThreadCreateInfo
    @ at the initial stack pointer. Without SetupThreadHeap the process has no
    @ heap at all.
_start:
    cmp  r4, #4
    beq  9f                     @ 4: the kernel re-entering for an exception
    mov  r5, sp                 @ SStdEpocThreadCreateInfo
    mov  r0, r4                 @ aNotFirst: 0 for the process's first thread
    mov  r1, sp
    bl   userheap_setupthreadheap
    cmp  r4, #0
    bne  1f
    bl   user_initprocess
    bl   hello_main
    mov  r0, #0
    bl   user_exit

    @ Every thread of the process starts here too (RThread::Create points it at
    @ the image's entry point): run its function, iFunction at +8, iPtr at +12.
1:  ldr  r12, [r5, #8]
    ldr  r0,  [r5, #12]
    blx  r12
    bl   user_exit
9:  mvn  r0, #0
    bl   user_exit

    @ An import stub, as Symbian's linker emits one: `ldr pc,[pc,#-4]` jumps
    @ through the word after it, and the loader overwrites that word, which
    @ starts out holding the function's ordinal, with its address. build.py
    @ finds the word by its `<name>_ord` label. The ordinals are euser.dll's
    @ on S60 3rd Edition (Symbian 9.x).
    .macro IMPORT name, ordinal
    .global \name
\name:
    ldr  pc, [pc, #-4]
    .global \name\()_ord
\name\()_ord:
    .word \ordinal
    .endm

    IMPORT userheap_setupthreadheap, 1360  @ UserHeap::SetupThreadHeap(TBool, SStdEpocThreadCreateInfo&)
    IMPORT user_initprocess,  585          @ User::InitProcess()
    IMPORT user_exit,         641          @ User::Exit(TInt)
    IMPORT user_panic,        650          @ User::Panic(TDesC16 const&, TInt)
    IMPORT rdebug_rawprint,   916          @ RDebug::RawPrint(TDesC16 const&)
