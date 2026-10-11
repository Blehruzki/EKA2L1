#!/usr/bin/env python3
"""autopilot.py -- drive Colin McRae's car down a stage from the screen.

    autopilot.py <window id> <seconds> [log path]

A bench instrument, for runs that have to reach the end of a stage (the
split times, type 7, and what follows the finish). It watches the emulator's
window on $DISPLAY and presses the game's own keys, as a player does: 5
throttle, 7 brake and reverse, the arrows steer (E980-E981).

What it sees, in the game's 240x320 picture (the window shows it 2x at
X0, Y0):
  - the road: grey gravel -- low saturation, mid brightness. The car is blue,
    the verges green and brown, the sky yellow-green, so a grey mask is the
    road and little else (shots from E1205).
  - where the road goes: the centre of the road run nearest the last one,
    in a band above the car (FAR) and one just above its roof (NEAR). The
    camera sits behind the car, so the car is at x 120 and the error is the
    road's centre against that.
  - how far along: the red marker in the strip across the top (rows 4-7),
    x 0 at the start line and 240 at the finish.

What it does, ten times a second: steer toward the road (a dead band, then
held arrow keys); throttle while the bend is gentle, off while it is sharp,
a brake tap when it is very sharp; and when the road has been out of sight
for over a second and a half -- off into the trees -- reverse with the
steering turned the other way, then go on.

Every second a line on stdout: time, progress, error, road share, keys.
"""
import os
import subprocess
import sys
import time

import numpy as np
from Xlib import X, display

X0, Y0, W, H = 210, 28, 480, 640
FAR, NEAR = (150, 182), (182, 210)
DEAD, LIFT, BRAKE = 7, 26, 52
LOST_AFTER, REVERSE_FOR = 1.5, 1.6


class Keys:
    def __init__(self, win):
        self.win, self.down = win, set()

    def _x(self, verb, key):
        subprocess.run(['xdotool', verb, '--window', self.win, key],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3)

    def hold(self, keys):
        for k in sorted(self.down - set(keys)):
            self._x('keyup', k)
        for k in sorted(set(keys) - self.down):
            self._x('keydown', k)
        self.down = set(keys)


def grab(dpy, win):
    # From the root window at the window's place: the emulator draws the game
    # in a child window, and XGetImage on the top-level window answers
    # BadMatch or black (E1215). Xvfb composites nothing, so the root holds
    # what is shown.
    root = dpy.screen().root
    t = win.translate_coords(root, 0, 0)
    raw = root.get_image(X0 - t.x, Y0 - t.y, W, H, X.ZPixmap, 0xffffffff).data
    a = np.frombuffer(raw, dtype=np.uint8).reshape(H, W, 4)
    return a[::2, ::2, 2::-1].astype(np.int16)        # BGRX -> RGB, 240x320


def road_mask(g):
    mx, mn = g.max(2), g.min(2)
    return ((mx - mn) < 26) & (mx > 85) & (mx < 215)


def centre(mask, rows, near_x):
    """The road's centre over `rows`: per row, the run of road pixels nearest
    `near_x` (at least 6 wide), and the median of their centres."""
    xs = []
    for y in range(*rows):
        r = mask[y].astype(np.int8)
        d = np.diff(np.concatenate(([0], r, [0])))
        best = None
        for a, b in zip(np.where(d == 1)[0], np.where(d == -1)[0]):
            if b - a < 6:
                continue
            c = (a + b) / 2.0
            if best is None or abs(c - near_x) < abs(best - near_x):
                best = c
        if best is not None:
            xs.append(best)
    return float(np.median(xs)) if len(xs) >= 4 else None


def progress(g):
    top = g[4:8]
    red = (top[:, :, 0] > 150) & (top[:, :, 1] < 90) & (top[:, :, 2] < 90)
    xs = np.where(red)[1]
    return int(xs.max()) if len(xs) else -1


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    wid, seconds = argv[0], float(argv[1])
    log = open(argv[2], 'w') if len(argv) > 2 else sys.stdout
    dpy = display.Display(os.environ.get('DISPLAY'))
    win = dpy.create_resource_object('window', int(wid))
    subprocess.run(['xdotool', 'windowactivate', '--sync', wid],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    keys = Keys(wid)
    t0 = time.time()
    last_seen, reverse_until, last_err, track_x = t0, 0.0, 0.0, 120.0
    next_log, best = t0, -1
    try:
        while time.time() - t0 < seconds:
            now = time.time()
            g = grab(dpy, win)
            m = road_mask(g)
            far = centre(m, FAR, track_x)
            near = centre(m, NEAR, 120)
            share = float(m[FAR[0]:NEAR[1]].mean())
            if far is not None or near is not None:
                last_seen = now
            if now < reverse_until:
                want = ['7', 'Right' if last_err < 0 else 'Left']
            elif now - last_seen > LOST_AFTER:
                reverse_until = now + REVERSE_FOR
                last_seen = now + REVERSE_FOR
                want = ['7']
            else:
                aim = far if far is not None else near
                if far is not None and near is not None:
                    aim = 0.7 * far + 0.3 * near
                err = (aim - 120) if aim is not None else last_err
                last_err = err
                track_x = far if far is not None else 120.0
                want = []
                if err > DEAD:
                    want.append('Right')
                elif err < -DEAD:
                    want.append('Left')
                if abs(err) < LIFT:
                    want.append('5')
                elif abs(err) > BRAKE and int(now * 4) % 3 == 0:
                    want.append('7')
            keys.hold(want)
            p = progress(g)
            best = max(best, p)
            if now >= next_log:
                next_log = now + 1.0
                log.write('%6.1f prog %3d err %6.1f share %.2f keys %s\n'
                          % (now - t0, p, last_err, share, '+'.join(sorted(want)) or '-'))
                log.flush()
            time.sleep(max(0.0, 0.1 - (time.time() - now)))
    finally:
        keys.hold([])
        log.write('end: furthest progress %d of 240\n' % best)
        log.flush()


if __name__ == '__main__':
    main(sys.argv[1:])
