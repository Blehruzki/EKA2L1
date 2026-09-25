#!/usr/bin/env python3
"""checkrec.py -- refuse a PORTING.md that grew a section without updating the
head.

The record is a working log, and a log left to itself records what was believed
rather than what is true. The "Read this first" block at the top is the part
that is maintained; it went stale one round after it was written, which is
exactly the failure it exists to prevent. So this is mechanical now: append a
section and the head has to move in the same commit, or this says no.

    python3 checkrec.py          # compares the working tree against HEAD
"""
import re
import subprocess
import sys
import os

DOC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'PORTING.md')
REL = '.claude/asphalt2/PORTING.md'
HEAD_START = '## Read this first'


def head_of(text):
    i = text.index(HEAD_START)
    j = text.index('\n## ', i + len(HEAD_START))
    return text[i:j]


def sections(text):
    return re.findall(r'^## .*$', text, re.M)


ROUNDS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'ROUNDS.md')


def unfinished_runs():
    """Rows emurun.sh wrote and nobody finished.

    The emulator runs went unlogged for a whole session while the hardware
    rounds were written up religiously, because one had a file and a rule and
    the other did not. emurun.sh writes the row; this refuses to let the row
    stay a stub.
    """
    try:
        rows = open(ROUNDS).read().splitlines()
    except OSError:
        return []
    return [r for r in rows if r.startswith('|') and r.rstrip().endswith('TODO |')]


def confirmations():
    """The three the user asked for, printed from the record.

    They were given for rounds 47 to 50 and then quietly stopped, which is the
    same shape as every other lapse in this project: a discipline held while it
    was new and dropped once it was routine. So it is not remembered any more.
    """
    import rules
    print()
    rules.main()


def main():
    bad = unfinished_runs()
    if bad:
        print('checkrec: %d run(s) logged and not written up:' % len(bad))
        for r in bad:
            print('  ' + r.strip())
        print('  Fill in what each settled -- or say it settled nothing, which')
        print('  is also an answer and the one worth recording.')
        return 1
    new = open(DOC).read()
    try:
        old = subprocess.run(['git', 'show', 'HEAD:' + REL], capture_output=True,
                             text=True, check=True).stdout
    except subprocess.CalledProcessError:
        print('checkrec: no committed copy to compare against; nothing to check')
        return 0
    grew = len(sections(new)) - len(sections(old))
    if grew <= 0:
        print('checkrec: no new sections')
        confirmations()
        return 0
    if head_of(new) == head_of(old):
        print('checkrec: %d new section(s) and the head is unchanged.' % grew)
        print('  Settled, Retracted or the rules must move too -- or say in the')
        print('  commit why this one changes nothing at the top.')
        return 1
    print('checkrec: %d new section(s), head updated' % grew)
    confirmations()
    return 0


if __name__ == '__main__':
    sys.exit(main())
