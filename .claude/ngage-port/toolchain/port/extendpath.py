#!/usr/bin/env python3
"""extendpath.py <path.npy> <ap log> <pose log> <out.npy> -- grow a stage path by one run.

A run that follows a partial path (AP_PATH) hands over to the screen at its
end and goes on recording the car's pose. This appends what the screen drove
after the handover, up to the last moment the car was on the road before its
furthest progress -- "on the road" meaning the screen saw road in at least
ROAD_SHARE of its bands (the log's `share`) -- so a stretch that ended in a
field is not taught as the stage (E1268: the bar jumped 111 -> 124 while the
car cut across a field by a lake, the share near 0).
"""
import sys

import numpy as np

sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from mkpath import erase_loops, resample

ROAD_SHARE = 0.25


def main(argv):
    path = np.load(argv[0])
    log = [l.split() for l in open(argv[1])]
    hand = [float(l[0]) for l in log if 'path' in l and 'ends' in l]
    if not hand:
        sys.exit('extendpath: the run never reached the end of the path')
    t_hand = hand[0]
    rows = [(float(l[0]), int(l[2]), float(l[l.index('share') + 1])) for l in log
            if len(l) > 3 and l[1] == 'prog' and 'share' in l and float(l[0]) >= t_hand]
    if not rows:
        sys.exit('extendpath: nothing after the handover')
    best = max(r[1] for r in rows)
    t_best = min(r[0] for r in rows if r[1] == best)
    good = [r[0] for r in rows if r[0] <= t_best and r[2] >= ROAD_SHARE]
    if not good:
        sys.exit('extendpath: no time on the road after the handover')
    t_cut = max(good)
    pose = np.array([[float(v) for v in l.split()[:3]] for l in open(argv[2]) if l.strip()])
    seg = pose[(pose[:, 0] >= t_hand) & (pose[:, 0] <= t_cut), 1:3]
    joined = erase_loops(np.vstack([path, seg]))
    out = resample(joined)
    np.save(argv[3], out)
    print('handover %.1f s, furthest progress %d at %.1f s, on the road until %.1f s: path %d -> %d m'
          % (t_hand, best, t_best, t_cut, len(path) - 1, len(out) - 1))


if __name__ == '__main__':
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(sys.argv[1:])
