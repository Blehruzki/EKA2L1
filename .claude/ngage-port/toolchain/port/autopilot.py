#!/usr/bin/env python3
"""autopilot.py -- drive Colin McRae's car down a stage from the screen.

    autopilot.py <window id> <seconds> [log path]

A bench instrument, for runs that have to reach the end of a stage (the
split times, type 7, and what follows the finish). It watches the emulator's
window on $DISPLAY and presses the game's own keys, as a player does: 5
throttle, 7 brake and reverse, the arrows steer (E980-E981).

What it sees, in the game's 240x320 picture (sampled out of the window at
whatever scale the window shows it, picture_rect):
  - the road: grey gravel -- low saturation, mid brightness. The car is blue,
    the verges green and brown, the sky yellow-green, so a grey mask is the
    road and little else (shots from E1205).
  - where the road goes: the grey pixels joined to the patch just ahead of
    the car (connected_road), and their middle in a band above the car (FAR)
    and one just above its roof (NEAR). The camera sits behind the car, so
    the car is at x 120 and the error is the road's middle against that.
  - "wrong way", the game's own text (wrong_way): a three-point turn.
  - how far along: the red marker in the strip across the top (rows 4-7),
    x 0 at the start line and 240 at the finish.

  - its speed, read off the HUD's digits (speed: templates from E1230's
    frames), held to a target by bend: throttle under it, brake well over.

What it does, ten times a second: steer toward the road (a dead band, then
held arrow keys); a target speed by bend, 30/20/12 MPH; and when the
speed has read 0 for over a second
with the throttle wanted, or the road has been out of sight for a second
and a half, reverse with the steering turned the other way, then go on.

Every second a line on stdout: time, progress, error, road share, keys.
With a log path, the picture every two seconds goes to <log>.frames/.
"""
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image
from Xlib import X, display

FAR, NEAR = (150, 182), (182, 210)
DEAD, LIFT, SHARP = 7, 26, 52
LOST_AFTER, STUCK_AFTER, REVERSE_FOR, GRACE, TURN_FOR = 1.0, 1.5, 3.0, 1.5, 4.0
NO_PROGRESS = 20.0    # seconds without the progress marker moving: back out and try the other way
# Target speeds (MPH) by bend: E1230's car reached 35 MPH in two seconds and
# missed the first bend, a left-hander just past the start.
V_STRAIGHT, V_BEND, V_SHARP, V_OVER = 30, 20, 12, 8
STEER_FULL = 45       # an error this size (px) holds the arrow the whole tick
# The pace notes' warning triangle comes before the turn and is gone in it:
# 10 MPH from when it shows until 3 s after (E1237 met the hairpin at 25).
V_CAUTION, CAUTION_HOLD = 10, 3.0
# "000" in the HUD: rows 303-317, x 62-89 of the game's picture, bright
# (> 180) pixels as a 15x28 bit pattern, from E1228's stopped cars.
ZERO_HEX = ('00000000000000078f1e1078f1e10ef9f310ef9f310ef9f310870e1108f1e3108f1e31'
            '0ef9f310f9f3e10f9f3e100000000000000')
ZERO_MASK = np.array([[int(c, 16) >> (3 - k) & 1 for c in ZERO_HEX[r * 7:r * 7 + 7] for k in range(4)]
                      for r in range(15)], dtype=bool)


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


def picture_rect(win):
    """Where the game's picture is in the window: under the menu bar (28 px),
    above the status bar (31 px), 3:4, centred. The two bench instances'
    windows are 900x699 and 900x600, so the picture is 2x in one and 1.69x
    in the other (E1228: B's autopilot read the wrong pixels)."""
    geo = win.get_geometry()
    h = geo.height - 28 - 31
    w = h * 3 // 4
    return (geo.width - w) // 2, 28, w, h


def grab(dpy, win):
    # From the root window at the window's place: the emulator draws the game
    # in a child window, and XGetImage on the top-level window answers
    # BadMatch or black (E1215). Xvfb composites nothing, so the root holds
    # what is shown.
    root = dpy.screen().root
    t = win.translate_coords(root, 0, 0)
    x0, y0, w, h = picture_rect(win)
    raw = root.get_image(x0 - t.x, y0 - t.y, w, h, X.ZPixmap, 0xffffffff).data
    a = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 4)
    ys = ((np.arange(320) + 0.5) * h / 320).astype(int)
    xs = ((np.arange(240) + 0.5) * w / 240).astype(int)
    return a[ys][:, xs, 2::-1].astype(np.int16)        # BGRX -> RGB, 240x320


def road_mask(g):
    mx, mn = g.max(2), g.min(2)
    return ((mx - mn) < 26) & (mx > 85) & (mx < 215)


# The HUD's speed digits, 11x7 bit cells at rows 305-315: tens at x 73-79,
# units at x 80-86 (bright > 180), labelled by eye from E1230's 430 frames.
# Tens seen 0-5; anything else reads as 6 (fast).
TENS = {0: '0f1e67cf9e1c78f9fe7c', 1: '070e3c78f02040810204', 2: '0f1e670e1c39c38e3fff',
        3: '0f1e640810e070e1ffff', 4: '070e1c78f224489fc204', 5: '0f1e20c183f870e1fe7c'}
UNITS = {0: '0f1e66cd9a1468d9be7c', 1: '070e3c78f02040810204', 2: '0f1e660c1831c38e3f7e',
         3: '0f1e640810e060c1bf7e', 4: '070e1c78f224489f8204', 5: '0f1e20c183f060c1be7c',
         6: '0f1e66c183f66cd99e3c', 7: '1fbf063870c1830e1c38', 8: '0f1e66cd99e66cd9bf7e',
         9: '0f1e66850b33e7c1be7c'}


def _bits(h):
    v = int(h, 16)
    return np.array([(v >> (76 - i)) & 1 for i in range(77)], dtype=bool)


TENS_B = {d: _bits(h) for d, h in TENS.items()}
UNITS_B = {d: _bits(h) for d, h in UNITS.items()}


def speed(g):
    """The HUD's MPH: each digit cell against its templates, nearest wins;
    a tens cell matching none closely (6 or more bits off) reads 6."""
    m = g[305:316, 62:90].max(2) > 180
    def read(cell, table):
        best = min(table, key=lambda d: int((table[d] != cell).sum()))
        return best, int((table[best] != cell).sum())
    tens, dt = read(m[:, 11:18].flatten(), TENS_B)
    units, _du = read(m[:, 18:25].flatten(), UNITS_B)
    return (6 if dt >= 6 else tens) * 10 + units


def stopped(g):
    """The HUD's speed reads 000: its bright pixels as in ZERO_MASK, all but a few."""
    m = g[303:318, 62:90].max(2) > 180
    return float((m == ZERO_MASK).mean()) > 0.96   # "000" scores 1.0, "0x2" 0.90 (E1229)


def centre(mask, rows, near_x, widest=12):
    """The road's centre over `rows`: per row, the run of road pixels nearest
    `near_x`, at least `widest` wide -- a birch trunk is grey too, and narrow
    (E1228) -- and the median of their centres."""
    xs = []
    for y in range(*rows):
        r = mask[y].astype(np.int8)
        d = np.diff(np.concatenate(([0], r, [0])))
        best = None
        for a, b in zip(np.where(d == 1)[0], np.where(d == -1)[0]):
            if b - a < widest:
                continue
            c = (a + b) / 2.0
            if best is None or abs(c - near_x) < abs(best - near_x):
                best = c
        if best is not None:
            xs.append(best)
    return float(np.median(xs)) if len(xs) >= 4 else None


def connected_road(mask):
    """The road pixels joined to the patch just ahead of the car (rows
    205-215, x 60-180), grown upward through the mask: white barriers,
    spectator areas and birch trunks are grey too, and are not joined to the
    road under the car (E1229: both cars turned round at the first bend)."""
    m = mask[120:226]
    seed = np.zeros_like(m)
    seed[85:96, 60:180] = m[85:96, 60:180]
    region = seed
    for _ in range(160):
        grown = region.copy()
        grown[:-1] |= region[1:]
        grown[1:] |= region[:-1]
        grown[:, :-1] |= region[:, 1:]
        grown[:, 1:] |= region[:, :-1]
        grown &= m
        if (grown == region).all():
            break
        region = grown
    out = np.zeros_like(mask)
    out[120:226] = region
    return out


def road_centre(region, rows):
    """The middle of the joined road over `rows`: per row with 8 or more
    road pixels, halfway between its first and last; the median of those."""
    xs = []
    for y in range(*rows):
        cols = np.where(region[y])[0]
        if len(cols) >= 8:
            xs.append((cols[0] + cols[-1]) / 2.0)
    return float(np.median(xs)) if len(xs) >= 4 else None


def caution(g):
    """The pace notes' yellow warning triangle, shown before a hairpin or a
    sharp turn: 56 yellow pixels at rows 8-39, x 40-199 when it is up, none
    otherwise (E1237's frames)."""
    t = g[8:40, 40:200]
    return int(((t[:, :, 0] > 200) & (t[:, :, 1] > 170) & (t[:, :, 2] < 90)).sum()) > 25


def wrong_way(g):
    """The game's "wrong way": seven or more rows of white text at rows
    70-84, x 70-170 (E1229's frames: 8-10 with it, 0-3 without)."""
    w = g[70:84, 70:170].min(2) > 215
    return int((w.sum(1) > 6).sum()) >= 7


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
    last_seen, reverse_until, grace_until, last_err, track_x = t0, 0.0, 0.0, 0.0, 120.0
    zero_since, next_log, best, reverses = None, t0, -1, 0
    ww_since, turn_until, turns, next_frame = None, 0.0, 0, t0
    road_side, v, steer = 0, 0, None
    wall_since, best_prog, best_at = None, -1, t0
    caution_until = 0.0
    frames = (argv[2] + '.frames') if len(argv) > 2 else None
    if frames:
        os.makedirs(frames, exist_ok=True)
    try:
        while time.time() - t0 < seconds:
            now = time.time()
            g = grab(dpy, win)
            p_now = progress(g)
            m = road_mask(g)
            region = connected_road(m)
            far = road_centre(region, FAR)
            near = road_centre(region, NEAR)
            if frames and now >= next_frame:
                next_frame = now + 2.0
                Image.fromarray(g.astype(np.uint8)).save(os.path.join(frames, '%05d.png' % int((now - t0) * 10)))
            backwards = wrong_way(g)
            if caution(g):
                caution_until = now + CAUTION_HOLD
            # A wall is grey too, and joins the road where it stands on it --
            # but it stands up: road never reaches rows 120-145 and fills the
            # far band to ~1250 px; a building ahead puts 900-1950 px up there
            # and ~3400 in the far band (E1236: both cars drove into one for a
            # minute, wheels spinning, the HUD reading 12-30 MPH).
            # ...but a crest does too, thinly (E1238: a hill read as a wall
            # for 40 s): the far band over 2,600, or 24 of rows 120-145 dense
            # with road and the far band over 1,800 (E1236's walls; E1238's
            # crest at most 10 rows and 1,748).
            far_px = int(region[FAR[0]:FAR[1]].sum())
            dense = int((region[120:146].sum(1) >= 40).sum())
            wall = far_px > 2600 or (dense >= 24 and far_px > 1800)
            if wall and wall_since is None:
                wall_since = now
            elif not wall:
                wall_since = None
            if p_now > best_prog:
                best_prog, best_at = p_now, now
            if not backwards:
                ww_since = None
            elif ww_since is None:
                ww_since = now
            share = float(m[FAR[0]:NEAR[1]].mean())
            v = speed(g)
            # Crawling with the throttle down is stuck too: E1231's cars sat at
            # 0-6 MPH against the verge for 40 s and a test for exactly 0 never fired.
            still = v <= 6 and '5' in keys.down
            loose = None
            if far is None and near is None:
                # Off the road (on grass, nothing grey joined to the car): head
                # for the most grey column in the lower picture, slowly, rather
                # than reversing in place (E1237's B: 108 reverses in a field).
                cols = np.convolve(m[175:215].sum(0), np.ones(9), 'same')
                if cols.max() > 60:
                    loose = float(np.argmax(cols))
            if far is not None or near is not None or loose is not None:
                last_seen = now
            if not still or now < grace_until or now < reverse_until:
                zero_since = None
            elif zero_since is None:
                zero_since = now
            if now < turn_until:
                # A three-point turn: back up on full lock, then forward on
                # the other lock, until the game stops saying "wrong way".
                want = ['7', 'Left'] if turn_until - now > TURN_FOR / 2 else ['5', 'Right']
            elif ww_since is not None and now - ww_since > 1.0:
                turn_until = now + TURN_FOR
                grace_until = turn_until + GRACE
                ww_since = None
                turns += 1
                want = ['7', 'Left']
            elif now < reverse_until:
                # Backing up with Right swings the nose left: toward the road.
                want = ['7', 'Right' if road_side < 0 else 'Left']
            elif ((zero_since is not None and now - zero_since > STUCK_AFTER) or
                  now - last_seen > LOST_AFTER or
                  (wall_since is not None and now - wall_since > 0.8 and now > grace_until) or
                  (now - best_at > NO_PROGRESS and now > grace_until)):
                if now - best_at > NO_PROGRESS:
                    best_at = now               # one escape per stall, then wait again
                    road_side = -road_side or 1  # and try the other way out
                reverse_until = now + REVERSE_FOR
                grace_until = reverse_until + GRACE
                last_seen = reverse_until
                zero_since = None
                reverses += 1
                want = ['7', 'Right' if road_side < 0 else 'Left']
            else:
                aim = far if far is not None else near if near is not None else loose
                if far is not None and near is not None:
                    aim = 0.4 * far + 0.6 * near    # nearer: aiming far cuts the bend into the trees (E1233)
                    if abs(far - near) > 40:
                        # In a hairpin the far band takes in whatever grey is
                        # straight ahead -- a building, the verge -- while the
                        # road under the car has turned (E1237: far 128, near
                        # 65, the car on into the house).
                        aim = near
                err = (aim - 120) if aim is not None else last_err
                last_err = err
                track_x = far if far is not None else 120.0
                want = []
                # Proportional steering: the arrow held for a share of the
                # tick that grows with the error (pulse below), not full lock
                # for the whole tick -- that overshot both ways (E1233).
                steer = 'Right' if err > DEAD else 'Left' if err < -DEAD else None
                pulse = min(1.0, abs(err) / STEER_FULL) if steer else 0.0
                if abs(err) > 10 and (far is not None):
                    road_side = 1 if err > 0 else -1
                target = V_STRAIGHT if abs(err) < LIFT else V_BEND if abs(err) < SHARP else V_SHARP
                if now < caution_until or (loose is not None and far is None and near is None):
                    target = min(target, V_CAUTION)
                if v < target:
                    want.append('5')
                elif v > target + V_OVER and v > V_SHARP:
                    want.append('7')            # brake: 7 slows a car going forward
            if steer and pulse > 0 and not any(k in want for k in ('Left', 'Right')):
                keys.hold(want + [steer])
                time.sleep(0.09 * pulse)
            keys.hold(want)
            steer = None
            p = progress(g)
            best = max(best, p)
            if now >= next_log:
                next_log = now + 1.0
                log.write('%6.1f prog %3d err %6.1f share %.2f %2d mph%s%s keys %s\n'
                          % (now - t0, p, last_err, share, v,
                             ' WRONGWAY' if backwards else '', ' WALL' if wall else '',
                             '+'.join(sorted(want)) or '-'))
                log.flush()
            time.sleep(max(0.0, 0.1 - (time.time() - now)))
    finally:
        keys.hold([])
        log.write('end: furthest progress %d of 240, %d reverses, %d turnarounds\n' % (best, reverses, turns))
        log.flush()


if __name__ == '__main__':
    main(sys.argv[1:])
