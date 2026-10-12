#!/usr/bin/env python3
"""waitmenu.py <window id> [timeout s] -- return once Colin McRae's main menu is up.

A fixed-time key script went wrong on a cold boot 20 s slower than usual
(E1264), and the splashes are too brief to time from (E1265-E1266), so this
waits for the main menu itself: its red "2005" under the title, rows 255-290,
x 140-235 of the game's picture -- 100 or more red pixels there and the page
white above them.
"""
import os
import sys
import time

from Xlib import display

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from autopilot import grab


def main_menu(g):
    t = g[255:291, 140:236]
    red = (t[:, :, 0] > 190) & (t[:, :, 1] < 90) & (t[:, :, 2] < 90)
    page = float((g[220:250, 20:220].min(2) > 230).mean())
    return int(red.sum()) >= 100 and page > 0.9


def main(wid, timeout=240):
    dpy = display.Display(os.environ.get('DISPLAY'))
    win = dpy.create_resource_object('window', int(wid))
    t0 = time.time()
    while time.time() - t0 < timeout:
        if main_menu(grab(dpy, win)):
            time.sleep(1)
            print('main menu at %.1f s' % (time.time() - t0))
            return 0
        time.sleep(0.5)
    print('waitmenu: no main menu in %d s' % timeout)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 240))
