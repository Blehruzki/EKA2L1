    .syntax unified
    .arm
    .section .text,"ax"
    .global _start

    @ Symbian's own startup, as gate 4 established: without this the process has
    @ no heap. Then hand control to the UI framework instead of to our own code.
_start:
    mov  r0, r4                 @ aNotFirst: 0 when the process is starting
    mov  r1, sp                 @ the thread create info the kernel left there
    bl   userheap_setupthreadheap
    cmp  r0, #0
    bne  2f
    bl   user_initprocess
    bl   gate5_main
2:  ldr  r1, =0xBADA0000
    orr  r0, r1, r0
    ldr  r1, [r0]
    b    .

    .ltorg

    .macro IMPORT name, ordinal
    .global \name
\name:
    ldr  pc, [pc, #-4]
    .global \name\()_ord
\name\()_ord:
    .word \ordinal
    .endm

    IMPORT user_panic,        650    @ User::Panic(TDesC16 const&, TInt)
    IMPORT userheap_setupthreadheap, 1360
    IMPORT user_initprocess,  585    @ User::InitProcess()
    IMPORT user_alloc,        646    @ User::Alloc(TInt)
    IMPORT user_allocz,       652    @ User::AllocZ(TInt)
    IMPORT rlibrary_load,    1308    @ RLibrary::Load(TDesC16 const&, TDesC16 const&)
    IMPORT rlibrary_lookup,  1838    @ RLibrary::Lookup(TInt) const
    IMPORT eikstart_runapplication, 394  @ EikStart::RunApplication(TApaApplicationFactory)
    IMPORT eikapplication_ctor,  64  @ CEikApplication::CEikApplication()
    IMPORT eikappui_ctor,       177  @ CEikAppUi::CEikAppUi()
    IMPORT eikappui_baseconstructl, 150  @ CEikAppUi::BaseConstructL(TInt)
    IMPORT coeenv_static,       182  @ CCoeEnv::Static()   [cone]
    IMPORT coeappui_ctor,       245  @ CCoeAppUi::CCoeAppUi()  [cone]
    IMPORT akndocument_ctor,    131  @ CAknDocument::CAknDocument(CEikApplication&)
