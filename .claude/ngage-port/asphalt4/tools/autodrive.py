"""autodrive.py <seconds> -- a visual autopilot for the Asphalt 4 bench race.

Holds accelerate (key '5') and steers with the d-pad (Left/Right) to keep the
grey road centred ahead of the car. Fast path: grabs a sub-rectangle of the
screen with Xlib (~5 ms) rather than spawning a capture tool, so the control
loop runs at ~15 Hz and can actually react at racing speed. Detects a stuck car
(the frame stops changing) and reverses out. The caller runs FRAMESTAT and/or
PCPROF around it.

Geometry is for the emulator window at 900x720 with A4 rendering landscape
(calibrated from a4/cal.png): car lane centre x~450; the road surface ahead
sits in a band at y 355-445; the minimap (top-left) and speedo (bottom-right)
are kept out of the sampled band.

The road detector (road_info) is importable without driving.
"""
import sys, time

# sampled road band (avoids minimap and speedo HUD)
BX0, BX1, BY0, BY1 = 185, 760, 355, 450
TARGET = (450 - BX0) / (BX1 - BX0)   # where the car's lane centre falls in the band
DEADZONE = 0.05


def _grab(root, X):
    raw = root.get_image(BX0, BY0, BX1 - BX0, BY1 - BY0, X.ZPixmap, 0xffffffff)
    return raw.data, BX1 - BX0, BY1 - BY0


def road_info_from_rgba(buf, w, h, step=3):
    """-> (centroid 0..1 or None, road fraction, left-third mass, right-third mass).
    buf is BGRX bytes. The left/right masses are road pixel counts in the outer
    thirds of the band, for deciding which way a junction turns."""
    sumx = n = total = left = right = 0
    rowbytes = w * 4
    lcut, rcut = w // 3, 2 * w // 3
    for y in range(0, h, step):
        base = y * rowbytes
        for x in range(0, w, step):
            o = base + x * 4
            b, g, r = buf[o], buf[o + 1], buf[o + 2]
            total += 1
            if max(r, g, b) - min(r, g, b) < 40 and 60 < (r + g + b) // 3 < 190:
                sumx += x; n += 1
                if x < lcut: left += 1
                elif x >= rcut: right += 1
    if total == 0:
        return None, 0.0, 0, 0
    return (None if n < 15 else (sumx / n) / w), n / total, left, right


def road_info(im):
    """PIL image cropped to the band -> (centroid, fraction). For offline tests."""
    im = im.convert("RGB")
    px = im.load(); w, h = im.size
    sumx = n = total = 0
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            r, g, b = px[x, y]; total += 1
            if max(r, g, b) - min(r, g, b) < 40 and 60 < (r + g + b) // 3 < 190:
                sumx += x; n += 1
    return (None if n < 15 else (sumx / n) / w), (n / total if total else 0.0)


def run(dur):
    import subprocess
    from Xlib import display, X
    d = display.Display(':99'); root = d.screen().root

    def press(k): subprocess.call(['xdotool', 'keydown', k])
    def release(k): subprocess.call(['xdotool', 'keyup', k])

    held = None                 # which steer key is currently down
    def steer(want):
        nonlocal held
        if want != held:
            if held: release(held)
            if want: press(want)
            held = want

    press('5')                  # accelerate, held throughout
    last_mean = None; stuck_ticks = 0; noroad_ticks = 0
    recover_n = 0; last_recover = 0.0   # escalate when stuck repeatedly in one spot
    t_end = time.time() + dur
    while time.time() < t_end:
        buf, w, h = _grab(root, X)
        centroid, frac, left, right = road_info_from_rgba(buf, w, h)
        mean = sum(buf[::97]) & 0xffffff          # a cheap frame fingerprint
        moving = last_mean is None or abs(mean - last_mean) >= 300
        if not moving:
            stuck_ticks += 1
        else:
            stuck_ticks = 0
        last_mean = mean

        # Finish/results screen: the road is gone from the band for a sustained
        # stretch while the picture still changes (an animation, not a stall).
        # A wall or tunnel is brief and is cleared by stuck-recovery, so a long
        # no-road-but-moving stretch is the race having ended.
        noroad_ticks = noroad_ticks + 1 if (frac < 0.08 and moving) else 0
        if noroad_ticks >= 40:                    # ~3 s
            print('FINISH detected', flush=True)
            steer(None); release('5')
            return

        if stuck_ticks >= 12:                     # ~0.8 s with no change: nudge out
            # Keep the nudge SHORT. A long turn here over-rotates the car at a
            # junction until it faces back down the track; the game then shows
            # "WRONG WAY / REVERSE" and stops counting progress, and the lap
            # never completes (runs H/G). A brief reverse plus a short turn
            # toward the side with more road unwedges it without spinning it.
            now = time.time()
            recover_n = recover_n + 1 if (now - last_recover) < 10 else 1
            last_recover = now
            side = 'Left' if left >= right else 'Right'
            steer(None); release('5')
            if recover_n >= 6:
                # Thrashing in one place: likely turned around and now facing a
                # wall or driving wrong-way. One committed ~U-turn to face back
                # the other way, then resume; done rarely, not every nudge.
                press(side); time.sleep(1.4); release(side)
                recover_n = 0
            else:
                press('Down'); time.sleep(0.5); release('Down')
                press(side); time.sleep(0.35); release(side)
            press('5')
            held = None; stuck_ticks = 0; last_mean = None
            continue

        if centroid is None:                      # no road in band: sweep toward more road
            steer('Right')                        # default sweep; recovery handles true stalls
        else:
            e = centroid - TARGET
            steer('Left' if e < -DEADZONE else 'Right' if e > DEADZONE else None)
        time.sleep(0.05)

    steer(None); release('5')


if __name__ == '__main__':
    run(float(sys.argv[1]) if len(sys.argv) > 1 else 180)
