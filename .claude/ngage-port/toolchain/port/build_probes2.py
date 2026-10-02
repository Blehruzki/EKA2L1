#!/usr/bin/env python3
"""build_probes2.py -- is the executable refused for its name or its bytes?

Round 81 settled that the installer refuses `6rbc.app`: probes 1 to 3 went
in and probe 4, which carried nothing but that file into
`\\system\\apps\\6rbc\\`, did not. Symbian will not install an E32 image
anywhere but `\\sys\\bin`.

Which leaves one question, and the answer decides how much work the fix is.

  p5   the same bytes under the name 6rbc.bin
  p6   the same, with the first 32 bytes -- the UID triple, the checksum
       and the 'EPOC' signature -- XORed, so nothing at the front of the
       file reads as an image header

p5 installs  -> the check is on the name, and the fix is a rename: the
                bytes never change, the game reads what it always read
p5 fails and p6 installs
             -> the check is on the content, and the loader has to put
                those 32 bytes back after reading, and hide the change
                from the game's own read of the file
neither      -> the file cannot travel in a SIS at all
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mksis

GAME = '/root/.local/share/EKA2L1/data/drives/e/system/apps/6rbc'
SCRAMBLE = 0xA5
SCRAMBLE_BYTES = 32


def build(out='.'):
    os.makedirs(out, exist_ok=True)
    p = lambda n: os.path.join(out, n)
    src = open(os.path.join(GAME, '6rbc.app'), 'rb').read()

    plain = p('6rbc.bin')
    open(plain, 'wb').write(src)

    head = bytes(b ^ SCRAMBLE for b in src[:SCRAMBLE_BYTES])
    hidden = p('6rbc_x.bin')
    open(hidden, 'wb').write(head + src[SCRAMBLE_BYTES:])

    for fn, uid, title, local in (
            ('probe5-renamed.sis', 0xE0001105, 'Probe 5 renamed', plain),
            ('probe6-scrambled.sis', 0xE0001106, 'Probe 6 scrambled', hidden)):
        mksis.build(p(fn), uid, title, 'DeltaCharlie',
                    [(local, '!:\\system\\apps\\6rbc\\6rbc.bin')])
        print('%-24s %8d bytes' % (fn, os.path.getsize(p(fn))))


if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else '.')
