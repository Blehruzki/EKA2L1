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
    mov  r5, sp                 @ SStdEpocThreadCreateInfo, before anything moves sp
    mov  r0, r4                 @ aNotFirst: 0 when this is the process starting
    mov  r1, sp                 @ the thread create info the kernel left there
    bl   userheap_setupthreadheap
    cmp  r0, #0
    bne  1f
    cmp  r4, #0
    bne  2f                     @ r4 != 0: a thread starting, not the process
    bl   user_initprocess
    bl   gate6_main
1:  ldr  r1, =0xBADA0000        @ the heap could not be created
    orr  r0, r1, r0
    @ gate6_main always panics, so reaching here means an import never resolved.
    ldr  r1, =0xBEEF0000
    orr  r0, r1, r0
    ldr  r1, [r0]
    b    .

    @ A thread starting, not the process. Every entry point is shared between
    @ the two, and without this branch RThread::Create re-ran the whole
    @ application in the new thread: it loaded a second copy of the image, got
    @ as far as creating another worker, and so on. The kernel points a new
    @ thread at the process entry point by design -- a real EXE is linked
    @ against eexe.lib, whose _E32Startup dispatches on r4 -- so a hand-built
    @ image has to do the dispatch itself. SStdEpocThreadCreateInfo holds the
    @ function at +8 and its argument at +12, and the thread ends with whatever
    @ the function returns.
2:  ldr  r12, [r5, #8]          @ iFunction
    ldr  r0,  [r5, #12]         @ iPtr
    mov  lr, pc
    bx   r12
    bl   user_exit              @ User::Exit(the function's return value)
    b    .

    @ A GCC98r2 virtual call, as gate 3 measured it in the game's own code: the
    @ vptr sits at object offset 0 and points eight bytes before slot 0.
    @ r0 = the old object (and `this` for the call), r1 = slot index.
    .global old_call
old_call:
    push {r4, lr}
    add  r1, r1, #2
    ldr  r3, [r0]
    ldr  r3, [r3, r1, lsl #2]
    mov  lr, pc
    bx   r3
    pop  {r4, lr}
    bx   lr

    @ The same, for a call that takes one argument as well as the object.
    @ r0 = the old object, r1 = the slot, r2 = the argument.
    .global old_call1
old_call1:
    push {r4, lr}
    add  r1, r1, #2
    ldr  r3, [r0]
    ldr  r3, [r3, r1, lsl #2]
    mov  r1, r2
    mov  lr, pc
    bx   r3
    pop  {r4, lr}
    bx   lr

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
    IMPORT user_exit,         641    @ User::Exit(TInt)
    IMPORT user_alloc,        646    @ User::Alloc(TInt)
    IMPORT user_allocz,       652    @ User::AllocZ(TInt)
    IMPORT user_alloclen,     660    @ User::AllocLen(TAny const*)
    IMPORT user_setexceptionhandler, 635  @ User::SetExceptionHandler(TExceptionHandler, TUint32)
    IMPORT rhandle_close,     120    @ RHandleBase::Close()
    IMPORT cperiodic_newl,   1379    @ CPeriodic::NewL(TInt)
    IMPORT cperiodic_start,  1381    @ CPeriodic::Start(...)
    IMPORT chunk_createlocalcode, 905  @ RChunk::CreateLocalCode(TInt, TInt, TOwnerType)
    IMPORT chunk_base,       1702    @ RChunk::Base() const
    IMPORT rlibrary_load,    1308    @ RLibrary::Load(TDesC16 const&, TDesC16 const&)
    IMPORT rlibrary_lookup,  1838    @ RLibrary::Lookup(TInt) const
    IMPORT rdebug_rawprint,   916    @ RDebug::RawPrint(TDesC16 const&)
    IMPORT user_imb_range,    667    @ User::IMB_Range(TAny*, TAny*)
    IMPORT user_tickcount,    674    @ User::TickCount()
    @ efsrv
    IMPORT fs_connect,         68    @ RFs::Connect(TInt)
    IMPORT file_open,          93    @ RFile::Open(RFs&, TDesC16 const&, TUint)
    IMPORT file_close,        300    @ RFile::Close()
    IMPORT file_size,         264    @ RFile::Size(TInt&) const
    IMPORT file_read,         255    @ RFile::Read(TDes8&)
    IMPORT file_replace,      108    @ RFile::Replace(RFs&, TDesC16 const&, TUint)
    IMPORT file_write_at,     101    @ RFile::Write(TInt, TDesC8 const&)
    IMPORT file_flush,         96    @ RFile::Flush()
    IMPORT fs_delete,          65    @ RFs::Delete(TDesC16 const&)
    @ eikcore
    IMPORT eikstart_runapplication, 394  @ EikStart::RunApplication(TApaApplicationFactory)
    IMPORT eikapplication_ctor,  64  @ CEikApplication::CEikApplication()
    IMPORT eikappui_ctor,       177  @ CEikAppUi::CEikAppUi()
    IMPORT eikappui_baseconstructl, 150  @ CEikAppUi::BaseConstructL(TInt)

    @ avkon
    IMPORT akndocument_ctor,    131  @ CAknDocument::CAknDocument(CEikApplication&)

    @ cone
    IMPORT coeappui_ctor,       245  @ CCoeAppUi::CCoeAppUi()

    @ drtaeabi -- referenced so the C++ runtime is a real dependency
    IMPORT drtaeabi_pure_virtual, 189  @ __cxa_pure_virtual
    IMPORT cpprt_globals_ctor,   204  @ TCppRTExceptionsGlobals::TCppRTExceptionsGlobals()
    IMPORT coeenv_static,       182  @ CCoeEnv::Static()
    IMPORT coecontrol_ctor,      64  @ CCoeControl::CCoeControl()
    IMPORT coecontrol_createwindowl, 25  @ CCoeControl::CreateWindowL()
