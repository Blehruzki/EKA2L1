#!/usr/bin/env python3
"""Gate 4: load the N-Gage binary on S60v3 and enter it.

The app reports by panicking, since that is the only channel a phone gives us:

    G4FS <err>    the file could not be opened or read
    G4MEM <err>   the heap or the code chunk refused
    G4HDR <n>     the image failed a sanity check (n says which)
    G4IMP <n>     the game's code ran and called import n   <- what we want
    G4RET <p>     it returned without calling anything

Copy 6RBC.APP to the memory card root as 6rbc.app before running it.
"""
import sys
import buildapp

buildapp.build('gate4', 0xE0001004, 'Gate4', sys.argv[1] if len(sys.argv) > 1 else '.',
               imports=[(buildapp.EUSER, ['user_panic', 'userheap_setupthreadheap',
                                          'user_initprocess', 'user_alloc',
                                          'chunk_createlocalcode', 'chunk_base']),
                        (buildapp.EFSRV, ['fs_connect', 'file_open',
                                          'file_size', 'file_read'])],
               heap_max=0x800000)
