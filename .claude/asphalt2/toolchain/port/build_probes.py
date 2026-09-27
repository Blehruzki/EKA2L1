#!/usr/bin/env python3
"""build_probes.py -- two one-kilobyte SIS files that isolate one thing each.

Build 167 failed on the phone at about nine tenths of the progress bar, on
both drives. That rules out a good deal on its own: the SD card had 13.8 GB
free, so it is not space, and SWI checks a package's whole file list before
it copies anything, so the paths and the executables in it were accepted.

What is left that is new since build 166, which installs: the package now
writes to `\\system\\apps\\6rbc\\`, and it carries a display-text entry.
These two probes take one each. Neither shares a UID with anything
installed, so neither can be confused with an upgrade.

  p1   one small file into \\system\\apps\\6rbc\\, nothing else
  p2   the same, plus the display text
  p3   forty small files into the same directory
  p4   the game's own 6rbc.app, which is an E32 executable image, into a
       directory that is not \\sys\\bin

The first of these to fail is the answer. If all four install, what is left
is the size, and build 168 -- the whole package with the text entry taken
out -- tests that by itself.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mksis

GAME = '/root/.local/share/EKA2L1/data/drives/e/system/apps/6rbc'

TEXT = 'Asphalt 2 N-Gage version, ported to S60v3 by DeltaCharlie.'


def build(out='.'):
    os.makedirs(out, exist_ok=True)
    p = lambda n: os.path.join(out, n)

    payload = p('probe.txt')
    open(payload, 'wb').write(b'probe\r\n')
    note = p('probe_note.txt')
    open(note, 'wb').write(b'\xff\xfe' + TEXT.encode('utf-16-le'))

    common = [(payload, '!:\\system\\apps\\6rbc\\probe.txt')]

    many = []
    for i in range(40):
        f = p('probe%02d.txt' % i)
        open(f, 'wb').write(b'probe %d\r\n' % i)
        many.append((f, '!:\\system\\apps\\6rbc\\probe%02d.txt' % i))

    made = [
        ('probe1-onefile.sis',   0xE0001101, 'Probe 1 one file', list(common)),
        ('probe2-withtext.sis',  0xE0001102, 'Probe 2 text', list(common) + [(note, None)]),
        ('probe3-fortyfiles.sis', 0xE0001103, 'Probe 3 forty files', many),
        ('probe4-executable.sis', 0xE0001104, 'Probe 4 executable',
         [(os.path.join(GAME, '6rbc.app'), '!:\\system\\apps\\6rbc\\6rbc.app')]),
    ]
    for fn, uid, title, files in made:
        mksis.build(p(fn), uid, title, 'DeltaCharlie', files)
        print('%-26s %9d bytes  %d file(s)' % (fn, os.path.getsize(p(fn)), len(files)))


if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else '.')
