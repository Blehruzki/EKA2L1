"""autodrive.py <seconds> -- a crude visual autopilot for the A4 bench race.

Holds accelerate (key '5'); each tick grabs the screen, finds the grey road's
horizontal centroid in a strip ahead of the car, and steers toward it with
Left/Right (the d-pad). The caller runs FRAMESTAT. Needs DISPLAY=:99 set and the
emulator already in a race. The road detector is importable without driving.
"""
import io, subprocess, sys, time
from PIL import Image

# the phone screen inside the emulator window (benchplay.sh crop: 210,40 490x660)
X0, Y0, W, H = 210, 40, 490, 660


def grab():
    png = subprocess.check_output(['import', '-display', ':99', '-window', 'root', 'png:-'])
    return Image.open(io.BytesIO(png)).convert('RGB')


def road_centroid(im):
    sx, sy, sw, sh = X0 + 40, Y0 + 300, W - 80, 90
    strip = im.crop((sx, sy, sx + sw, sy + sh))
    px = strip.load(); w, h = strip.size
    sumx = n = 0
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            r, g, b = px[x, y]
            if max(r, g, b) - min(r, g, b) < 40 and 60 < (r + g + b) // 3 < 190:
                sumx += x; n += 1
    return None if n < 20 else (sumx / n) / w


def tap(k, t=0.18):
    subprocess.call(['xdotool', 'keydown', k]); time.sleep(t); subprocess.call(['xdotool', 'keyup', k])


def run(dur):
    subprocess.call(['xdotool', 'keydown', '5'])
    t_end = time.time() + dur
    while time.time() < t_end:
        try:
            c = road_centroid(grab())
        except Exception:
            c = None
        if c is None:
            tap('Right', 0.25)
        elif c < 0.42:
            tap('Left', 0.12 + (0.42 - c) * 0.5)
        elif c > 0.58:
            tap('Right', 0.12 + (c - 0.58) * 0.5)
        time.sleep(0.12)
    subprocess.call(['xdotool', 'keyup', '5'])


if __name__ == '__main__':
    run(float(sys.argv[1]) if len(sys.argv) > 1 else 180)
