#!/usr/bin/env python3
"""Compile gate6.cpp for every title in games/ -- syntax only, the way
buildapp.py invokes clang. The shared loader is built for one title at a
time, so a knob read before its default is defined (BUGBOOK 14.n: builds
200-204 could not be built for any title but Asphalt 2) is invisible until
the next title is built. rules.py runs this. Exit 1 on any failure."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
bad = 0
for g in sorted(os.listdir(os.path.join(HERE, 'games'))):
    d = os.path.join(HERE, 'games', g)
    if not os.path.isfile(os.path.join(d, 'game.h')):
        continue
    extra = ['-DGAME_NATIVE_W=240'] if g == 'asphalt2' else []
    r = subprocess.run(['clang', '--target=armv5-none-eabi', '-marm', '-O1', '-fno-exceptions', '-fno-rtti',
                        '-fno-builtin', '-ffreestanding', '-fsyntax-only', '-w'] + extra + ['-I' + d,
                        os.path.join(HERE, 'gate6.cpp')], capture_output=True, text=True)
    ok = r.returncode == 0
    bad += (not ok)
    print('  %-10s %s' % (g, 'compiles' if ok else '** FAILS **'))
    if not ok:
        print('\n'.join('    ' + l for l in r.stderr.splitlines() if 'error' in l)[:800])
sys.exit(1 if bad else 0)
