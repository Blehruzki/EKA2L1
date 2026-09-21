"""Write a track as a .znd, the format the game's own sound engine plays.

    u16 sampleRate, then 8-bit signed PCM

The engine honours the rate per sound (its own effects ship at 6000 and 8000),
mixes in a separate server thread, and therefore keeps playing through a race --
which a second media client on the main thread does not.
"""
import struct

RATE = 8000
MAX_SECONDS = 45          # a loop long enough not to notice, small enough to allocate


def build(pcm16, src_rate, rate=RATE, max_seconds=MAX_SECONDS):
    samples = struct.unpack('<%dh' % (len(pcm16) // 2), pcm16)
    step = src_rate / float(rate)
    count = min(int(len(samples) / step), rate * max_seconds)

    out = bytearray()
    peak = max(1, max(abs(s) for s in samples))
    gain = 32767.0 / peak                       # the engine's effects are near full scale

    for i in range(count):
        pos = i * step
        left = int(pos)
        frac = pos - left
        a = samples[left]
        b = samples[left + 1] if left + 1 < len(samples) else a
        v = int((a + (b - a) * frac) * gain) >> 8
        out.append(max(-128, min(127, v)) & 0xff)

    return struct.pack('<H', rate) + bytes(out)
