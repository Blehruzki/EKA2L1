#!/usr/bin/env python3
"""wavgaps.py <capture.wav> [min ms] -- the dropouts in a stream EKA2L1 captured.

EKA2L1_AUDIO_CAPTURE_DIR puts each stream in a .wav, pumped in real time, and
writes zeros for whatever the stream did not have ready: an underrun is a run of
exact zeros. Music and tone are almost never exactly zero for more than a few
samples, so a run of at least <min ms> (default 3) between non-silent audio is
counted as a dropout -- the bench's measure of a stutter. Leading and trailing
silence is not counted. A stream the title keeps open and silent -- One's
effects stream between effects -- is all "dropout" by this measure: read it
against a baseline run (E690 against E691), not on its own."""
import os, struct, sys, wave
p = sys.argv[1]
min_ms = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0
if os.path.getsize(p) < 44:
    print('%s: empty (the stream was made and never started)' % os.path.basename(p))
    sys.exit(0)
w = wave.open(p, 'rb')
rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
raw = w.readframes(n)
s = struct.unpack('<%dh' % (len(raw) // 2), raw)
if ch > 1:
    s = s[::ch]
min_run = int(rate * min_ms / 1000)
first = next((i for i, v in enumerate(s) if v), None)
last = max((i for i, v in enumerate(s) if v), default=None) if first is not None else None
gaps = []
if first is not None:
    i = first
    while i < last:
        if s[i] == 0:
            j = i
            while j < last and s[j] == 0:
                j += 1
            if j - i >= min_run:
                gaps.append((i, j - i))
            i = j
        else:
            i += 1
dur = (last - first) / rate if first is not None else 0
tot = sum(g for _, g in gaps) / rate * 1000
print('%s: %d Hz, %.1f s of audio, %d dropouts of %g ms or more, %.0f ms silent in all'
      % (p.split('/')[-1], rate, dur, len(gaps), min_ms, tot))
for i, g in gaps[:12]:
    print('   at %.2f s: %.0f ms' % ((i - first) / rate, g * 1000.0 / rate))
