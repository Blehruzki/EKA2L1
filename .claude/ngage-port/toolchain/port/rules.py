#!/usr/bin/env python3
"""rules.py -- print the four confirmations, computed from the record.

The user asked for three things, in their words:

  1. each test result must be logged before you proceed with the next step of
     the testing process, and provide me with a confirmation message.
  2. Before coming up with a new test, you must check the logs to make sure
     you're not repeating the same mistakes. Provide me with a confirmation
     of it.
  3. keep track of how far we've gone through with the tests, and which test
     provided the best results so far and why (in a brief manner).

A fourth was added after round 93, in the same spirit and for the same
reason -- the first three are about not repeating a *test*, and nothing was
about not shipping a *guess*:

  4. Review your own code before it goes out. Do not just guess, do not just
     code: read back what you wrote, ask whether it actually makes sense,
     and check the things you assumed.

That one came from noticing, unprompted, that a version bump in a config
format would have silently thrown away a setting every phone already had.
The point is that the noticing should not be luck. So this rule runs what
can be run -- the self-tests, and every hard-coded ordinal against the ROM
that has to answer it -- and then asks the three questions that cannot be
automated.

I followed the first three for rounds 47 to 50 and then stopped, without
noticing and without being asked to. So the confirmations are not written
from memory any more: this reads ROUNDS.md and prints them, and the numbers
in them are whatever the file actually says.

    python3 rules.py            # before shipping anything, and in every reply
                                # that reports a result or asks for a run
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROUNDS = os.path.join(HERE, '..', '..', 'ROUNDS.md')
REL = '.claude/ngage-port/ROUNDS.md'


def rows(text, prefix):
    out = []
    for line in text.splitlines():
        m = re.match(r'\|\s*(%s[0-9]+)\s*\|(.*)\|\s*$' % prefix, line)
        if m:
            cells = [c.strip() for c in m.group(2).split('|')]
            out.append((m.group(1), cells))
    return out


def last_commit_touching():
    try:
        return subprocess.run(
            ['git', 'log', '-1', '--format=%h %s', '--', REL],
            capture_output=True, text=True, check=True,
            cwd=os.path.join(HERE, '..', '..', '..', '..')).stdout.strip()
    except Exception:
        return '(no git)'


def main():
    text = open(ROUNDS).read()
    hw = rows(text, '')
    hw = [(n, c) for n, c in hw if not n.startswith('E')]
    emu = rows(text, 'E')

    pending_hw = [n for n, c in hw if 'pending' in ' '.join(c).lower()]
    unfinished = [n for n, c in emu + hw if c and c[-1].strip() == 'TODO']

    print('RULE 1 -- results logged before the next step')
    done = [n for n, c in hw if n not in pending_hw]
    print('  hardware rounds written up: %d, through round %s' % (len(done), done[-1] if done else '-'))
    print('  emulator runs written up:   %d, through %s' % (len(emu), emu[-1][0] if emu else '-'))
    if unfinished:
        print('  NOT LOGGED: %s  <-- fix before anything else' % ', '.join(unfinished))
    if pending_hw:
        print('  awaiting hardware: round %s' % ', '.join(pending_hw))
    print('  last commit touching the record: %s' % last_commit_touching())

    print()
    print('RULE 2 -- checked against the log, not repeating a test')
    print('  %d hardware rows and %d emulator rows are on file to check a new' %
          (len(hw), len(emu)))
    print('  test against. Name the rows it is not a repeat of.')

    print()
    print('RULE 3 -- how far, and which test was best')
    i = text.find('**Furthest')
    if i >= 0:
        para = ' '.join(text[i:text.index('\n\n', i)].replace('*', '').split())
        print('  ' + para[:320])
    b = re.search(r'\*\*Best round so far: ([^*]+)\*\*', text)
    if b:
        print('  best: %s' % b.group(1).strip(' .'))
    r = re.search(r'\*\*Runner-up: ([^*]+)\*\*', text)
    if r:
        print('  runner-up: %s' % r.group(1).strip(' .'))

    print()
    bad = rule4()
    return 1 if (unfinished or bad) else 0


# ---- rule 4 ---------------------------------------------------------------

SELF_TESTS = ('logringtest.py',)
# Compiled on the host and run. `columntest.cpp` exists because the bench
# cannot reach the geometry the bug lived in: EKA2L1's frame buffer line is
# the screen width and every phone's is wider, so build 008's shift was
# right here and wrong there.
CC_TESTS = ('columntest.cpp', 'fittest.cpp')

# A test that has always failed teaches you to ignore a red line, which is
# the opposite of what rule 4 is for. `fittest.cpp` fails on panels this
# port has never run on -- N80, 5800, E90, all larger than 240x320 -- with
# the scaler's map clamping. That is a real finding and it is not new, so
# the baseline is written down and only a *change* in it is reported.
KNOWN_FAIL = {'fittest.cpp': 8}      # distinct failures, panels > 240x320

# Ordinals this port has confirmed by *measurement*, not by reading a table.
# `epoc9.def` lists 2,184 euser exports and the RM-409 ROM has 2,229, so an
# index into the def is not a proof of an ordinal; and a wrong ordinal for a
# no-argument function does nothing visible, which is the worst kind of
# wrong. E314 to E316 settled 634 by changing how often the port called it
# and watching the emulator's count move by exactly that much.
CONFIRMED = {634: 'User::ResetInactivityTime -- E314-E316, by differential count',
             674: 'User::TickCount -- every frame of every run, values at 64/s',
             650: 'User::Panic -- every G6 panic the phone has ever shown'}


def ordinals():
    """Every IMPORT in gate6.s, and whether the ROM library has that many
    exports. A number past the end is caught here; a number inside the range
    but wrong is not, and only a measurement settles those."""
    import romimg
    src = os.path.join(HERE, 'gate6.s')
    rom = '/root/.local/share/EKA2L1/data/roms/rm-409/SYM.ROM'
    zbin = '/root/.local/share/EKA2L1/data/drives/z/rm-409/sys/bin/'
    lib, out = None, []
    for line in open(src):
        m = re.match(r'\s*@\s*([a-z0-9]+)\s*$', line)
        if m:
            lib = m.group(1)
            continue
        m = re.match(r'\s*IMPORT\s+(\w+),\s*(\d+)', line)
        if m and lib:
            out.append((lib, m.group(1), int(m.group(2))))
    counts = {}
    for l in sorted(set(x[0] for x in out)):
        p = zbin + l + '.dll'
        if not os.path.isfile(p):
            counts[l] = None
            continue
        try:
            d, h = romimg.load(p, rom)
            counts[l] = len(romimg.exports(d, h))
        except Exception:
            counts[l] = None
    return out, counts


def rule4():
    print('RULE 4 -- reviewed, not guessed')
    bad = 0
    for t in SELF_TESTS:
        p = os.path.join(HERE, t)
        if not os.path.isfile(p):
            print('  %-20s MISSING' % t)
            bad += 1
            continue
        r = subprocess.run([sys.executable, p], capture_output=True, text=True)
        print('  %-20s %s' % (t, 'pass' if r.returncode == 0 else '** FAIL **'))
        bad += (r.returncode != 0)
    import tempfile
    for t in CC_TESTS:
        p = os.path.join(HERE, t)
        if not os.path.isfile(p):
            print('  %-20s MISSING' % t)
            bad += 1
            continue
        exe = os.path.join(tempfile.gettempdir(), t.replace('.cpp', '.bin'))
        b = subprocess.run(['c++', '-O2', '-I', HERE, '-o', exe, p],
                           capture_output=True, text=True)
        if b.returncode:
            print('  %-20s ** WILL NOT BUILD **' % t)
            bad += 1
            continue
        r = subprocess.run([exe], capture_output=True, text=True)
        if r.returncode == 0:
            print('  %-20s pass' % t)
            continue
        want = KNOWN_FAIL.get(t)
        m = re.search(r'(\d+) distinct failure', r.stdout)
        got = int(m.group(1)) if m else None
        if want is not None and got == want:
            print('  %-20s %d known failures, unchanged (panels > 240x320)'
                  % (t, got))
            continue
        print('  %-20s ** FAIL ** (%s, baseline %s)' % (t, got, want))
        bad += 1
    try:
        imps, counts = ordinals()
    except Exception as exc:
        print('  ordinals: not checked (%s)' % exc)
        return bad
    over = [(l, n, o) for l, n, o in imps
            if counts.get(l) and o > counts[l]]
    unread = sorted(set(l for l, _n, _o in imps if not counts.get(l)))
    print('  %d imported ordinals across %d libraries; %d past the end of '
          'their export table' % (len(imps), len(set(x[0] for x in imps)), len(over)))
    for l, n, o in over:
        print('    ** %s ordinal %d (%s) -- the ROM has %d' % (l, o, n, counts[l]))
    bad += len(over)
    if unread:
        print('  not checked (no ROM copy): %s' % ', '.join(unread))
    print('  confirmed by measurement: %s'
          % ', '.join(str(k) for k in sorted(CONFIRMED)))
    print()
    print('  And the three that cannot be computed. Answer them in writing')
    print('  before a build goes out, not after:')
    print('    a. What in this change is a guess? Name each one, and how it')
    print('       could be settled on the bench rather than on the phone.')
    print('    b. Which lines of the diff did I read back, and which did I')
    print('       only remember writing?')
    print('    c. What state does this code have that the bench run did not')
    print('       enter? A ring that never wrapped. A config from the last')
    print('       build. A key held down. Those are where the bugs were.')
    return bad


if __name__ == '__main__':
    sys.exit(main())
