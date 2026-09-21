#!/usr/bin/env python3
"""Gate 3: call a clang-built C++ class through a GCC98r2 vtable.

The app reports its result by panicking with category GATE3 and the mask as the
reason, because a phone shows a panic on screen and never shows a fault address.
All seven checks passing reads "GATE3 127".
"""
import sys
import buildapp

buildapp.build('gate3', 0xE0001003, 'Gate3', sys.argv[1] if len(sys.argv) > 1 else '.',
               imports=[(buildapp.EUSER, ['user_panic'])])
