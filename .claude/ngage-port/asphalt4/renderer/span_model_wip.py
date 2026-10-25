"""span_model_wip.py -- WORK IN PROGRESS, NOT yet bit-exact. span_harness.py
shows only a minority of pixels match the original, so the decode below (esp.
the saturating-add) is still wrong somewhere. Kept as the current best decode
and the starting point for single-step fixing, not as a finished model.

Original intent: bit-exact Python reimplementation of Asphalt 4's textured+light
span inner loop (shipped Thumb at code +3357A), transcribed op-for-op from the
disassembly. Used to verify understanding against the original run under unicorn.

Per pixel the original does:
  light = light*K1 + K2                 (K1=0x0019660D, K2=0x3C6EF35F, read from
                                         the code literal pool)
  li = (light>>16) & lightmask ; if li>=thresh: li-=thresh ; li &= 0xffff
  lc = (li<<11)|(li<<6)|li              (grey RGB565 from the light value)
  tu = (u>>16)&umask ; tv=(v>>16)&vmask
  texel = tex[(tv<<vshift)|tu]          (RGB565)
  out = sat565(texel, lc)               (per-channel saturating add, below)
  *dst++ = out ; *cov++ = 0
  u += ustep ; v += vstep
"""
M32 = 0xFFFFFFFF
K1, K2 = 0x0019660D, 0x3C6EF35F


def sat565(texel, lc):
    # transcription of +335C6..+33602 (per-channel saturating RGB565 add)
    s = (texel + lc) & M32                 # +335C6 raw sum
    x = (texel ^ lc) & M32                 # +335C8 texel^lc
    c1 = ((x ^ s) & 0x20) & M32            # +335D0/D2 carry at bit5 boundary
    s = (s - c1) & M32                     # +335D8
    y = (s ^ x) & M32                      # +335DE
    c2 = (y & 0x800) & M32                 # +335E4 carry at bit11 boundary
    s2 = (s - c2) & M32                    # +335E6  (= sum-c1-c2, saved sp+0xC)
    c3 = (s2 & 0x10000) & M32              # +335EE carry at bit16 (overflow)
    carries = (c3 | 0x800 | c1) & M32      # +335F6/F8  combined carry bits
    mask = (carries - ((carries >> 4) & M32)) & M32   # +335FC/FE  r - (r>>4)
    return (s2 | mask) & 0xFFFF            # +33602 output (strh -> 16 bits)


def span(light, u, v, ustep, vstep, umask, vmask, vshift, thresh, lightmask, tex, n):
    out = []
    for _ in range(n):
        light = (light * K1 + K2) & M32
        li = (light >> 16) & lightmask
        if li >= thresh:
            li = (li - thresh) & M32
        li &= 0xFFFF
        lc = ((li << 11) | (li << 6) | li) & M32
        tu = (u >> 16) & umask
        tv = (v >> 16) & vmask
        texel = tex[((tv << vshift) | tu) & (len(tex) - 1)]
        out.append(sat565(texel, lc))
        u = (u + ustep) & M32
        v = (v + vstep) & M32
    return out
