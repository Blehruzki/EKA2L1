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

buildapp.build('gate6', 0xE0001006, 'Gate6', sys.argv[1] if len(sys.argv) > 1 else '.',
               imports=[(buildapp.EUSER, ['user_panic', 'userheap_setupthreadheap',
                                          'user_initprocess', 'user_alloc', 'user_allocz',
                                          'chunk_createlocalcode', 'chunk_base',
                                          'rlibrary_load', 'rlibrary_lookup']),
                        (buildapp.EFSRV, ['fs_connect', 'file_open',
                                          'file_size', 'file_read']),
                        (EIKCORE, ['eikstart_runapplication', 'eikapplication_ctor',
                                   'eikappui_ctor', 'eikappui_baseconstructl']),
                        (AVKON, ['akndocument_ctor']),
                        (CONE, ['coeappui_ctor'])],
               sources=('gate6.cpp', 'gate4_shim.cpp'),
               heap_max=0x800000)
