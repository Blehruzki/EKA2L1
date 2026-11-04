#!/usr/bin/env python3
"""autodrive2.py <seconds> [window-id]: round 41, autodrive.py for the current bench layout -- the
emulator window at 900x720, the game shown turned 180 degrees (landscape 270: the car at the
top, the road ahead between it and the horizon at y ~395, screen left = the car's right). Holds
'5' (what moves the car on the bench) and steers with the game's own keys (8 left, 2 right)
held while the grey road's centroid in the band is off centre: continuous held steering at
speed, as a player drives. Stuck (the picture not changing ~0.8 s): brake '6', then a short turn
toward more road. Prints, every 5 s, how much of the time a steering key was held.
"""
import sys, time, subprocess
from Xlib import display, X

BX0, BX1, BY0, BY1 = 40, 860, 330, 388      # the road ahead (y between the car and the horizon)
CENTRE = (450 - BX0) / (BX1 - BX0)
DEADZONE = 0.04


def road(buf, w, h, step=3):
    sumx = n = total = left = right = 0
    rb = w * 4
    for y in range(0, h, step):
        for x in range(0, w, step):
            o = y * rb + x * 4
            b, g, r = buf[o], buf[o + 1], buf[o + 2]
            total += 1
            if max(r, g, b) - min(r, g, b) < 40 and 60 < (r + g + b) // 3 < 200:
                sumx += x; n += 1
                if x < w // 3: left += 1
                elif x >= 2 * w // 3: right += 1
    return (None if n < 15 else (sumx / n) / w), left, right


def run(dur, wid):
    d = display.Display(':99'); root = d.screen().root
    win = ['--window', wid] if wid else []
    def key(a, k): subprocess.call(['xdotool', a] + win + [k])
    held = None
    def steer(want):
        nonlocal held
        if want != held:
            if held: key('keyup', held)
            if want: key('keydown', want)
            held = want
    key('keydown', '5')
    last = None; stuck = 0; t0 = time.time(); t_end = t0 + dur; on = tot = 0; rep = t0 + 5
    while time.time() < t_end:
        raw = root.get_image(BX0, BY0, BX1 - BX0, BY1 - BY0, X.ZPixmap, 0xffffffff).data
        c, left, right = road(raw, BX1 - BX0, BY1 - BY0)
        m = sum(raw[::97]) & 0xffffff
        stuck = stuck + 1 if last is not None and abs(m - last) < 300 else 0
        last = m
        if stuck >= 16:
            steer(None); key('keyup', '5')
            key('keydown', '6'); time.sleep(0.5); key('keyup', '6')
            side = '2' if left >= right else '8'       # screen-left is the car's right
            key('keydown', side); time.sleep(0.35); key('keyup', side)
            key('keydown', '5'); stuck = 0; last = None
            continue
        if c is None: want = '8'
        else:
            e = c - CENTRE                              # road to screen-right = to the car's left
            want = '8' if e > DEADZONE else '2' if e < -DEADZONE else None
        steer(want)
        tot += 1; on += want is not None
        if time.time() > rep:
            print('t %3.0f s: steering held %2.0f%% of ticks' % (time.time() - t0, 100.0 * on / max(tot, 1)), flush=True)
            rep += 5; on = tot = 0
        time.sleep(0.04)
    steer(None); key('keyup', '5')


if __name__ == '__main__':
    run(float(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else None)
