    .syntax unified
    .arm
    .section .text,"ax"
    .global _start

    @ Symbian's own startup, from kernel/eka/euser/epoc/arm/uc_exe.cpp:
    @   UserHeap::SetupThreadHeap(aNotFirst, cinfo);
    @   User::InitProcess();          // statics for implicitly linked DLLs
    @   E32Main();
    @ We are not linked against eexe.lib, so nothing does this for us, and
    @ without it the process has no heap at all -- User::Alloc walks a null
    @ allocator and jumps to zero. The kernel leaves SStdEpocThreadCreateInfo
    @ at the initial stack pointer and passes the startup reason in r4, which
    @ is what a real entry point forwards.
_start:
    mov  r0, r4                 @ aNotFirst: 0 when this is the process starting
    mov  r1, sp                 @ the thread create info the kernel left there
    bl   userheap_setupthreadheap
    cmp  r0, #0
    bne  1f
    bl   user_initprocess
    bl   gate4_main
1:  ldr  r1, =0xBADA0000        @ the heap could not be created
    orr  r0, r1, r0
    @ gate4_main always panics, so reaching here means an import never resolved.
    ldr  r1, =0xBEEF0000
    orr  r0, r1, r0
    ldr  r1, [r0]
    b    .

    .ltorg

    @ An import stub, as the Symbian linker emits it: `ldr pc,[pc,#-4]` reads pc
    @ as its own address plus 8, so it jumps through the word that follows, and
    @ the loader overwrites that word with the resolved address.
    .macro IMPORT name, ordinal
    .global \name
\name:
    ldr  pc, [pc, #-4]
    .global \name\()_ord
\name\()_ord:
    .word \ordinal
    .endm

    @ euser
    IMPORT user_panic,        650    @ User::Panic(TDesC16 const&, TInt)
    IMPORT userheap_setupthreadheap, 1360  @ UserHeap::SetupThreadHeap(TBool, SStdEpocThreadCreateInfo&)
    IMPORT user_initprocess,  585    @ User::InitProcess()
    IMPORT user_alloc,        646    @ User::Alloc(TInt)
    IMPORT chunk_createlocalcode, 905  @ RChunk::CreateLocalCode(TInt, TInt, TOwnerType)
    IMPORT chunk_base,       1702    @ RChunk::Base() const
    @ efsrv
    IMPORT fs_connect,         68    @ RFs::Connect(TInt)
    IMPORT file_open,          93    @ RFile::Open(RFs&, TDesC16 const&, TUint)
    IMPORT file_size,         264    @ RFile::Size(TInt&) const
    IMPORT file_read,         255    @ RFile::Read(TDes8&) const
