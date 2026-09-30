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

import picture

FIT, SRC, DST, FMT = 838, 839, 840, 837


def map_max():
    """MAP_MAX from gate6.cpp, so this cannot drift from what ships."""
    import os, re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gate6.cpp')
    try:
        m = re.search(r'enum \{ MAP_MAX = (\d+) \}', open(src).read())
        return int(m.group(1)) if m else 320
    except Exception:
        return 320


def main(path, g):
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
    names = {0: '1:1', 1: 'aspect', 2: 'fill', 3: 'integer', 4: 'full'}
    print('layout decisions recorded: %d' % len(groups))
    print('  panel             %d x %d   (usable %d x %d below a %d-row inset)'
          % (bw, panel_h, bw, bh, inset))
    print('  mode              %d (%s)' % (mode, names.get(mode, '?')))
    print('  picture drawn     %d x %d at (%d, %d)' % (dw, dh, ox, oy))
    fits = ox + dw <= bw and oy + dh <= panel_h
    print('  right/bottom edge %d / %d of %d / %d   %s'
          % (ox + dw, oy + dh, bw, panel_h, 'inside' if fits else '*** PAST THE PANEL ***'))
    if dw and dh:
        # The source aspect comes from the game, not from 176/208 written
        # down here: that is Asphalt 2's shape and reporting it for another
        # game is the same class of mistake as rendering with its origin.
        sa = g['width'] / g['height']
        print('  aspect            %.4f vs source %.4f   (%+.1f%%)'
              % (dw / dh, sa, 100.0 * ((dw / dh) / sa - 1)))
    print('  screen used       %.1f%%' % (100.0 * dw * dh / (bw * panel_h)))
    # MAP_MAX, which phase 3 raised from 320 to 1024. Read it out of the
    # source rather than repeating it here: this check went stale the moment
    # the constant moved and reported a clamp that no longer existed.
    if dw >= map_max() or dh >= map_max():
        print('  *** at or past the %d-entry limit of mapX/mapY: clamped ***' % map_max())
    print('  source origin/pitch %d / %d' % (src >> 16, src & 0xFFFF))
    print('  first pixel offset  %d' % first)


if __name__ == '__main__':
    game, rest = picture.take_game_arg(sys.argv[1:])
    g = picture.geometry(game)
    picture.banner(g)
    main(rest[0], g)
