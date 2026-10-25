"""palconv_model.py -- bit-exact Python reimplementation of Asphalt 4's sprite
painter's per-pixel palette->RGB565 conversion (shipped Thumb at code
+298D8..+2991E, inside the sprite blitter at +29736), confirmed against the
original under unicorn (palconv_harness.py).

The sprite painter re-does this conversion for every pixel, even though the
result depends only on the pixel's palette index and the sprite's palette --
both constant for the whole blit. Proving it is a pure function of (palette,
index) is what makes the optimization (hoist it to a per-blit lookup table)
valid: the inner loop then reads lut[index] instead of walking the palette
struct and converting 3 colour bytes every pixel.

Per pixel the original does, for palette index i and the sprite's 3-bytes-per-
entry colour table:
  R = colortab[i*3 + 0]; G = colortab[i*3 + 1]; B = colortab[i*3 + 2]
  rgb565 = ((B >> 3) << 11) | ((G >> 2) << 5) | (R >> 3)
  if palette.colorkey == i: the pixel is transparent (coverage r5 := 0)
(the channel that lands in bits 11-15 is colortab[+2]; naming aside, this is
transcribed from the instructions, not assumed.)
"""


def convert(colortab, i):
    R = colortab[i * 3 + 0]
    G = colortab[i * 3 + 1]
    B = colortab[i * 3 + 2]
    return (((B >> 3) << 11) | ((G >> 2) << 5) | (R >> 3)) & 0xFFFF


def build_lut(colortab, ncolors):
    """The per-blit hoist: one RGB565 value per palette index."""
    return [convert(colortab, i) for i in range(ncolors)]
