#!/usr/bin/env python3
"""Step 3: hand the framework a 9.x object that dispatches into the game's.

    G6CHN 3     the game built application, document and app UI
    G6UID <n>   the game answered AppDllUid through its own vtable
    G6FS/G6MEM/G6HDR/G6IMP/G6LIB  as gate 4
"""
import os
import sys

import buildapp

HERE = os.path.dirname(os.path.abspath(__file__))
GAMES = os.path.join(HERE, 'games')
DEFAULT_GAME = 'asphalt2'

EIKCORE = 'eikcore{000a0000}[10004892].dll'
AVKON = 'avkon{000a0000}[100056c6].dll'
CONE = 'cone{000a0000}[10003a41].dll'
DRTAEABI = 'drtaeabi.dll'
HAL = 'hal.dll'


def game_dir(game):
    return os.path.join(GAMES, game)


def game_setting(game, name):
    """One #define or enum value out of a game's game.h, without a compiler.

    One implementation, in picture.py: this had its own copy, with the same
    `(\S+)` that reads GAME_CAPTION "Asphalt 2" as "Asphalt".
    """
    import picture
    return picture.setting(game, name)


def build(out='.', caption=None, game=DEFAULT_GAME, **kw):
    """The one place the import list lives, so the bench build and the
    release installer cannot drift apart.

    `game` names a directory under `games/`, holding that title's generated
    `shim.cpp` and `gate_imports.h` and its hand-written `game.h`. One source
    tree, one binary per game.
    """
    uid3 = int(game_setting(game, 'GAME_APP_UID3'), 0)
    caption = caption or game_setting(game, 'GAME_CAPTION')
    return buildapp.build('gate6', uid3, caption, out,
               appname=game_setting(game, 'GAME_APP_NAME') or 'gate6',
               imports=[(buildapp.EUSER, ['user_panic', 'userheap_setupthreadheap',
                                          'user_initprocess', 'user_alloc', 'user_allocz', 'user_alloclen',
                                          'user_setexceptionhandler', 'rhandle_close',
                                          'cperiodic_newl', 'cperiodic_start',
                                          'chunk_createlocalcode', 'chunk_base',
                                          'rlibrary_load', 'rlibrary_lookup',
                                          'rdebug_rawprint', 'user_imb_range',
                                          'user_tickcount', 'sem_wait_timeout',
                                          'user_allocator', 'rthread_id', 'rheap_available', 'user_allocsize']),
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
               sources=('gate6.cpp', os.path.join('games', game, 'shim.cpp')),
               incdir=game_dir(game),
               # The N-Gage gave the game 8 KB of stack and that was enough
               # there; on 9.x the framework underneath it is deeper, and a
               # stack that runs out is a fault with nothing to say for itself.
               stack=0x10000,
               heap_max=0x4000000, **kw)


if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else '.',
          game=sys.argv[2] if len(sys.argv) > 2 else DEFAULT_GAME)
