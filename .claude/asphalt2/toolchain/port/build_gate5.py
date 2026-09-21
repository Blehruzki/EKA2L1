#!/usr/bin/env python3
"""Step 1: a real S60v3 GUI application.

    G5DOC 1   the framework took our application object and asked for a document
    G5LIB n   avkon would not load, or its vtable export is not there
    G5RET 0   RunApplication returned, which it should not
    BADA....  the thread heap could not be created
"""
import sys
import buildapp

EIKCORE = 'eikcore{000a0000}[10004892].dll'
AVKON = 'avkon{000a0000}[100056c6].dll'

buildapp.build('gate5', 0xE0001005, 'Gate5', sys.argv[1] if len(sys.argv) > 1 else '.',
               imports=[(buildapp.EUSER, ['user_panic', 'userheap_setupthreadheap',
                                          'user_initprocess', 'user_alloc', 'user_allocz',
                                          'rlibrary_load', 'rlibrary_lookup']),
                        (EIKCORE, ['eikstart_runapplication', 'eikapplication_ctor',
                                   'eikappui_ctor']),
                        (AVKON, ['akndocument_ctor'])],
               heap_max=0x400000)
