"""transp_model.py -- bit-exact Python reimplementation of Asphalt 4's
keyed/transparent span inner loop (shipped Thumb at code +33B16), transcribed
op-for-op from the disassembly and confirmed against the original under unicorn
(transp_harness.py).

Per pixel the original does:
  tu = (u>>16) & umask ; tv = (v>>16) & vmask
  texel = tex[(tv<<vshift) | tu]        (RGB565)
  if texel != 0: *dst = texel           (a zero texel is transparent: skip)
  dst += 1 ; u += ustep ; v += vstep
until dst reaches dst_end.

Because a zero texel leaves the destination untouched, the model takes the
destination's prior contents (dst_in) and returns them with the non-zero texels
written over.
"""
M32 = 0xFFFFFFFF


def span(u, v, ustep, vstep, umask, vmask, vshift, tex, dst_in, n):
    dst = list(dst_in)
    for i in range(n):
        tu = (u >> 16) & umask
        tv = (v >> 16) & vmask
        texel = tex[((tv << vshift) | tu) & (len(tex) - 1)]
        if texel != 0:
            dst[i] = texel
        u = (u + ustep) & M32
        v = (v + vstep) & M32
    return dst
