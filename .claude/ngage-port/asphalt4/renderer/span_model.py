"""span_model.py -- bit-exact Python reimplementation of Asphalt 4's textured+light
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
    # per-channel saturating RGB565 add, derived from a single-step trace of the
    # original (+335C6..+33602). NB: an earlier version used a constant 0x800 for
    # the green carry, which was a disassembler operand-order misread; the real
    # green carry is computed (c2 below), confirmed bit-exact by span_harness.
    s = (texel + lc) & M32                 # sum
    x = (texel ^ lc) & M32
    c1 = ((x ^ s) & 0x20) & M32            # carry at the blue/green boundary (bit5)
    s_a = (s - c1) & M32
    c2 = (((s_a ^ x) & 0x800)) & M32       # carry at the green/red boundary (bit11)
    s2 = (s_a - c2) & M32
    c3 = (s2 & 0x10000) & M32              # carry out of red (bit16)
    carries = (c3 | c2 | c1) & M32         # the carries that actually occurred
    mask = (carries - ((carries >> 4) & M32)) & M32
    return (s2 | mask) & 0xFFFF


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
