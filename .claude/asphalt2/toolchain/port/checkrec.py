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


def main():
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
        return 0
    if head_of(new) == head_of(old):
        print('checkrec: %d new section(s) and the head is unchanged.' % grew)
        print('  Settled, Retracted or the rules must move too -- or say in the')
        print('  commit why this one changes nothing at the top.')
        return 1
    print('checkrec: %d new section(s), head updated' % grew)
    return 0


if __name__ == '__main__':
    sys.exit(main())
