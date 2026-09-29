#!/usr/bin/env python3
"""screenfit.py <log> -- what the port made of the screen it was given.

Reads the NOTE_SCREEN_* records one run writes at setup and prints the
layout decision in one block: the panel as HAL reported it, the buffer shape
the picker chose, where the picture landed and how big, and the source
geometry it was read with. This is the block to check first when the port is
pointed at a panel it has not seen before.

Two traps, both hit on the first use of this script:

- **Note codes are reused.** 840 is `NOTE_SCREEN_DST` *and* `NOTE_THREAD_ARG`,
  841 is `NOTE_NGAGE` *and* `NOTE_THREAD_NAME`. Taking "the last record with
  code 840" reads a thread argument and reports a top inset of 1. So the
  records are matched as the consecutive run `screen_layout` writes them --
  three FIT, one SRC, one DST -- and anything else with those codes is ignored.
- **The heights are measured from different origins.** The FIT record's height
  is the panel *minus* the top inset, while `offY` is measured from the top of
  the whole screen and already includes it. Comparing them directly reports a
  picture hanging off the bottom of every panel, which is wrong.
"""
import struct, sys

FIT, SRC, DST, FMT = 838, 839, 840, 837


def main(path):
    d = open(path, 'rb').read()
    ev = [struct.unpack_from('<II', d, 8 * i) for i in range(len(d) // 8)]
    # The exact run screen_layout writes, so a reused code cannot be mistaken
    # for part of it.
    groups = []
    for i in range(len(ev) - 4):
        if ([c for c, _v in ev[i:i + 5]] == [FIT, FIT, FIT, SRC, DST]):
            groups.append([v for _c, v in ev[i:i + 5]])
    if not groups:
        print('no complete layout record -- did the run reach the screen?')
        return
    bwbh, dwdh, oxoy, src, dst = groups[-1]
    bw, bh = bwbh >> 16, bwbh & 0xFFFF
    dw, dh = dwdh >> 16, dwdh & 0xFFFF
    ox, oy = oxoy >> 16, oxoy & 0xFFFF
    inset, mode, first = dst >> 16, (dst >> 12) & 0xF, dst & 0xFFF
    panel_h = bh + inset
    names = {0: '1:1', 1: 'aspect', 2: 'fill'}
    print('layout decisions recorded: %d' % len(groups))
    print('  panel             %d x %d   (usable %d x %d below a %d-row inset)'
          % (bw, panel_h, bw, bh, inset))
    print('  mode              %d (%s)' % (mode, names.get(mode, '?')))
    print('  picture drawn     %d x %d at (%d, %d)' % (dw, dh, ox, oy))
    fits = ox + dw <= bw and oy + dh <= panel_h
    print('  right/bottom edge %d / %d of %d / %d   %s'
          % (ox + dw, oy + dh, bw, panel_h, 'inside' if fits else '*** PAST THE PANEL ***'))
    if dw and dh:
        print('  aspect            %.4f vs source %.4f   (%+.1f%%)'
              % (dw / dh, 176 / 208, 100.0 * ((dw / dh) / (176 / 208) - 1)))
    print('  screen used       %.1f%%' % (100.0 * dw * dh / (bw * panel_h)))
    if dw >= 320 or dh >= 320:
        print('  *** at or past the 320-entry limit of mapX/mapY: clamped ***')
    print('  source origin/pitch %d / %d' % (src >> 16, src & 0xFFFF))
    print('  first pixel offset  %d' % first)


if __name__ == '__main__':
    main(sys.argv[1])
