#!/usr/bin/env python3
"""rules.py -- print the three confirmations, computed from the record.

The user asked for three things, in their words:

  1. each test result must be logged before you proceed with the next step of
     the testing process, and provide me with a confirmation message.
  2. Before coming up with a new test, you must check the logs to make sure
     you're not repeating the same mistakes. Provide me with a confirmation
     of it.
  3. keep track of how far we've gone through with the tests, and which test
     provided the best results so far and why (in a brief manner).

I followed them for rounds 47 to 50 and then stopped, without noticing and
without being asked to. So the confirmations are not written from memory any
more: this reads ROUNDS.md and prints them, and the numbers in them are
whatever the file actually says.

    python3 rules.py            # before shipping anything, and in every reply
                                # that reports a result or asks for a run
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROUNDS = os.path.join(HERE, '..', '..', 'ROUNDS.md')
REL = '.claude/asphalt2/ROUNDS.md'


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
    return 1 if unfinished else 0


if __name__ == '__main__':
    sys.exit(main())
