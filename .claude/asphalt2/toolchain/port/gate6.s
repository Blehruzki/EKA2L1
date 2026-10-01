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
    @ First, before the heap is touched: eexe's _E32Startup tests the reason
    @ before anything else, because for reason 4 what sp points at is not a
    @ thread-create info block but the fault frame, and SetupThreadHeap
    @ reading an allocator out of it is a second fault on top of the first.
    cmp  r4, #4
    beq  3f                     @ r4 == 4: the kernel re-entering us for an exception
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
    @ RUN_WORKERS 0 creates and resumes the thread exactly as before and then
    @ ends it without running a single instruction of the game's function. It
    @ is the control for "does the main thread's fault depend on the workers
    @ running at all": everything the kernel sees is identical, and the only
    @ difference is whether the game's own code executes on a second thread.
    .set RUN_WORKERS, 1
2:  mov  r0, #0
.if RUN_WORKERS
    ldr  r12, [r5, #8]          @ iFunction
    ldr  r0,  [r5, #12]         @ iPtr
    mov  lr, pc
    bx   r12
.endif
    bl   user_exit              @ User::Exit(the function's return value)
    b    .

    @ **An exception, and this is how EKA2 delivers one to a user handler.**
    @ kernel/arm/ckernel.cpp, Exc::Dispatch: if the thread has a handler
    @ installed for the fault, the kernel pushes the fault frame on the user
    @ stack -- PushExcInfoOnUserStack: TExcType, iExcCode, iFaultAddress,
    @ iFaultStatus, iCpsr, then r0 to r15, low address first -- and re-enters
    @ the process **at its entry point** with r4 = KModuleEntryReasonException
    @ (4, u32std.h) and sp at the frame. eexe's _E32Startup (uc_exe.cia)
    @ dispatches on r4 and calls User::HandleException(sp), which runs the
    @ installed handler. This entry point dispatched on r4 == 0 only, so an
    @ exception took the thread-start branch below: `ldr r12, [r5, #8]` read
    @ the fault address out of the frame and branched to it -- a second fault
    @ with the last-chance flag set, which the kernel reports as KERN-EXEC 3.
    @ Every KERN-EXEC 3 this port has ever shown went through here.
    @
    @ The handler we installed is the thunk gate6.cpp built around the
    @ context, and User::ExceptionHandler() hands it back; it gets the frame
    @ in r0 and never returns. The kernel clears the Thumb bit before the
    @ re-entry, so this is ARM as the rest of the file is.
3:  mov  r5, sp                 @ the frame: type, code, fault address, status, cpsr, r0-r15
    bl   user_exceptionhandler  @ User::ExceptionHandler(): what we installed, or 0
    cmp  r0, #0
    beq  5f
    mov  r1, r0
    mov  r0, r5
    bx   r1                     @ gate6_fault, through its thunk; does not return
5:  mvn  r0, #0                 @ no handler -- cannot happen, the kernel checked
    bl   user_exit
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

    @ And for two arguments: r0 = the old object, r1 = the slot, r2 and r3 the
    @ arguments. r3 is live here, unlike old_call, so the scratch is r4 -- which
    @ is saved anyway.
    .global old_call2
old_call2:
    push {r4, lr}
    add  r1, r1, #2
    ldr  r4, [r0]
    ldr  r4, [r4, r1, lsl #2]
    mov  r1, r2
    mov  r2, r3
    mov  lr, pc
    bx   r4
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
    IMPORT user_exceptionhandler, 620     @ User::ExceptionHandler()
    IMPORT cactivescheduler_current, 427  @ CActiveScheduler::Current()
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
    IMPORT user_resetinactivity, 634 @ User::ResetInactivityTime()
    IMPORT sem_wait_timeout,   68    @ RSemaphore::Wait(TInt aTimeout) -- the timed one
    IMPORT user_allocator,    665    @ User::Allocator()
    IMPORT rheap_available,  1606    @ RHeap::Available(TInt&) const
    IMPORT user_allocsize,    664    @ User::AllocSize(TInt&)
    IMPORT rthread_id,       1793    @ RThread::Id() const
    @ hal
    IMPORT hal_get,             1    @ HAL::Get(HALData::TAttribute, TInt&)
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
