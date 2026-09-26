#!/usr/bin/env python3
"""Step 3: hand the framework a 9.x object that dispatches into the game's.

    G6CHN 3     the game built application, document and app UI
    G6UID <n>   the game answered AppDllUid through its own vtable
    G6FS/G6MEM/G6HDR/G6IMP/G6LIB  as gate 4
"""
import sys
import buildapp

EIKCORE = 'eikcore{000a0000}[10004892].dll'
AVKON = 'avkon{000a0000}[100056c6].dll'
CONE = 'cone{000a0000}[10003a41].dll'
DRTAEABI = 'drtaeabi.dll'
HAL = 'hal.dll'

buildapp.build('gate6', 0xE0001006, 'Gate6', sys.argv[1] if len(sys.argv) > 1 else '.',
               imports=[(buildapp.EUSER, ['user_panic', 'userheap_setupthreadheap',
                                          'user_initprocess', 'user_alloc', 'user_allocz', 'user_alloclen',
                                          'user_setexceptionhandler', 'rhandle_close',
                                          'cperiodic_newl', 'cperiodic_start',
                                          'chunk_createlocalcode', 'chunk_base',
                                          'rlibrary_load', 'rlibrary_lookup',
                                          'rdebug_rawprint', 'user_imb_range',
                                          'user_tickcount', 'sem_wait_timeout',
                                          'user_allocator', 'rthread_id']),
                        (buildapp.EFSRV, ['fs_connect', 'file_open', 'file_close',
                                          'file_size', 'file_read',
                                          'file_replace', 'file_write_at',
                                          'file_flush', 'fs_delete']),
                        (EIKCORE, ['eikstart_runapplication', 'eikapplication_ctor',
                                   'eikappui_ctor', 'eikappui_baseconstructl']),
                        (AVKON, ['akndocument_ctor']),
                        (CONE, ['coeappui_ctor', 'coeenv_static', 'coecontrol_ctor',
                                 'coecontrol_createwindowl']),
                        (DRTAEABI, ['drtaeabi_pure_virtual', 'cpprt_globals_ctor']),
                        (HAL, ['hal_get'])],
               sources=('gate6.cpp', 'gate4_shim.cpp'),
               # The N-Gage gave the game 8 KB of stack and that was enough
               # there; on 9.x the framework underneath it is deeper, and a
               # stack that runs out is a fault with nothing to say for itself.
               stack=0x10000,
               heap_max=0x4000000)
