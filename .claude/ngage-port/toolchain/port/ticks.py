#!/usr/bin/env python3
"""ticks.py <log> -- per-frame durations, from the tick either side of RunL.

`CLOCK_EVERY_FRAME` writes `NOTE_TICK` (878) immediately after `NOTE_FRAME`
(865) and immediately before `NOTE_FRAME_END` (866), so a frame's cost is the
difference between them, in 1/64 s ticks: one tick is 15.6 ms. Anything at
three ticks or more is a frame the phone dropped.

Prints the distribution, the worst frames, and what was inside them -- which
is the whole point: "a long frame" is not an answer, "a long frame with
sixty-three file reads in it" is.
"""
import struct, sys, collections

NAMES = {110: 'RFile::Read', 357: 'SendReceive', 285: 'RMessage::Complete',
         109: 'RFile::Open', 853: 'MDA call', 857: 'MDA callback',
         860: 'slot entered', 835: 'key', 852: 'sound msg'}

def main(path):
    d = open(path, 'rb').read()
    rec = [struct.unpack_from('<II', d, i * 8) for i in range(len(d) // 8)]
    frames, cur = [], None
    for i, (c, v) in enumerate(rec):
        if c == 865:
            cur = [i, v, None, None]
        elif c == 878 and cur is not None:
            if cur[2] is None: cur[2] = v
            else: cur[3] = v
        elif c == 866 and cur is not None:
            if cur[2] is not None and cur[3] is not None:
                frames.append((cur[1], cur[0], i, cur[3] - cur[2]))
            cur = None
    if not frames:
        print('no clocked frames in this log (CLOCK_EVERY_FRAME off?)')
        return
    costs = collections.Counter(t for _f, _s, _e, t in frames)
    print('%d clocked frames' % len(frames))
    for t in sorted(costs):
        print('   %2d tick(s)  %5.1f ms  x%d' % (t, t * 1000 / 64.0, costs[t]))
    dropped = [f for f in frames if f[3] >= 3]
    print('frames of three ticks or more (47 ms+): %d  (%.1f%%)'
          % (len(dropped), 100.0 * len(dropped) / len(frames)))
    for f, s, e, t in sorted(dropped, key=lambda x: -x[3])[:10]:
        inside = collections.Counter(rec[k][0] for k in range(s + 1, e))
        what = ', '.join('%s x%d' % (NAMES.get(c, str(c)), n)
                         for c, n in inside.most_common(5))
        print('   frame %-6d %2d ticks  %s' % (f, t, what or '(nothing traced)'))

if __name__ == '__main__':
    main(sys.argv[1])
