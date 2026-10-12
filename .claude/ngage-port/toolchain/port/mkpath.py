#!/usr/bin/env python3
"""mkpath.py <pose log> <out.npy> [t_from t_to] -- a stage's path from a drive that finished.

The autopilot's <log>.pose holds, ten times a second, `t x z heading progress`
(metres, radians; autopilot.car_pose). A drive that reached the finish went
along the whole stage, with detours: reverses, three-point turns, minutes in a
field. This keeps the stage and drops the detours, and writes the path as an
(n, 2) array of (x, z) points one metre apart, start to finish, for the
autopilot to follow (AP_PATH).

  - The race only: from the first point the car has moved 2 m from where it
    sat, to the last point before it stopped for good (or t_from..t_to).
  - Loops erased: walking the points in order, a point within LOOP_R of a
    kept point at least LOOP_GAP points back cuts the list back to that
    point -- whatever the car did in between went nowhere.
  - Then resampled to one point a metre.
"""
import sys

import numpy as np

LOOP_R, LOOP_GAP = 4.0, 20


def load(path):
    rows = [l.split() for l in open(path) if l.strip()]
    a = np.array([[float(v) for v in r[:4]] for r in rows if len(r) >= 5])
    return a[:, 0], a[:, 1], a[:, 2]


def race_span(t, x, z):
    p = np.stack([x, z], 1)
    moved = np.where(np.hypot(*(p - p[0]).T) > 2.0)[0]
    first = int(moved[0]) if len(moved) else 0
    step = np.r_[0, np.hypot(*np.diff(p, axis=0).T)]
    going = np.where(step > 0.01)[0]
    last = int(going[-1]) if len(going) else len(p) - 1
    return first, last


def erase_loops(p):
    kept = []
    for q in p:
        if kept:
            k = np.array(kept)
            d = np.hypot(*(k[:-LOOP_GAP] - q).T) if len(k) > LOOP_GAP else np.array([])
            hit = np.where(d < LOOP_R)[0]
            if len(hit):
                del kept[int(hit[0]) + 1:]
                continue
        if not kept or np.hypot(*(np.array(kept[-1]) - q)) > 0.2:
            kept.append(tuple(q))
    return np.array(kept)


def resample(p, step=1.0):
    seg = np.hypot(*np.diff(p, axis=0).T)
    s = np.r_[0, np.cumsum(seg)]
    u = np.arange(0, s[-1], step)
    return np.stack([np.interp(u, s, p[:, 0]), np.interp(u, s, p[:, 1])], 1)


def main(argv):
    t, x, z = load(argv[0])
    if len(argv) >= 4:
        i0, i1 = np.searchsorted(t, float(argv[2])), np.searchsorted(t, float(argv[3]))
    else:
        i0, i1 = race_span(t, x, z)
    raw = np.stack([x[i0:i1 + 1], z[i0:i1 + 1]], 1)
    path = resample(erase_loops(raw))
    np.save(argv[1], path)
    drove = float(np.hypot(*np.diff(raw, axis=0).T).sum())
    print('race %.1f-%.1f s, driven %.0f m, path %d m (%d points)' % (t[i0], t[i1], drove, len(path) - 1, len(path)))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1:])
