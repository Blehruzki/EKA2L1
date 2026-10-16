#!/usr/bin/env python3
"""The code patches that move a framebuffer title's renderer to a wider buffer
(NATIVE240.md, step 2). Asphalt 2 only, so far; the sites are nativescan.py's.

    nativepatch.py <image.app> <out.h> [--w 240]

Writes a header with GAME_CODE_PATCHES / GAME_CODE_PATCH_COUNT for the
loader's patch loop, every row carrying the word it expects to find, so a
different image refuses to start rather than running patched nonsense.
Width only: the height stays 208 and the 3D view 176 rows tall (stage 2b of
the plan); the rows come in these kinds:

  stride   the y*11<<5 idiom (y*352) becomes y*15<<5 (y*480): the first add's
           shift 2 -> 1 (t = 3y), the second add reads t twice with shift 2
           (t = 15y); the lsl #5 stays
  imm      a data-processing immediate in a renderer function: 176 -> 240,
           352 -> 480, 175 -> 239, and in the present and the screen object's
           constructor the band offsets 0x1600 (16 rows) -> 0x1e00 and
           0x10800 (192 rows) -> 0x16800; 88 -> 120 in the perspective spans
  lit      the five literal 73,216s (176*208*2) -> 99,840, and the screen
           buffer's allocation of 73,216 + 16 KB (img+0x8434) likewise
  surface  `mov r2, #176` before each surface_init call -> 240
  site     single immediates in functions whose other 176s and 88s are 2D
           layout: the scene renderer's projection centre and clips, the
           screen descriptors {flags, 176, 208, pixels} the text and sprite
           blits draw through, the column walker's x clips (SITES below)
  by hand  the present's `lsl r2, r3, #5` (16 rows from the 176 it also uses
           as a row count) -> `mov r2, #0x1e00`; the sky walker's row clip

Everything here is a guess until E-rows say the picture is right (rule 4):
an immediate 176 in a renderer function could be something else, and a
site this does not know about shears the picture (E133-E135). E1446-E1452
and E1453-E1462 are the record: 192 words, the attract race drawn 240
wide about column 120 with the HUD in its place.
"""
import struct, sys, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capstone
import nativescan as ns

# Immediates are patched only where 176 means the width: the renderer's own
# functions (the stride functions below img+0xc0000: the fills, the span
# families) and these -- the present, the screen object's constructor, the
# perspective span, the descriptor builder. The engine's big functions above
# 0xc0000 hold stride sites too, but their 0x160s and 0xb0s are stack offsets
# and constant arithmetic (`add r7, sp, #0x160`, `add r8, r8, #0x160` in a
# seed), read back in the first table and taken out.
RENDERER_BELOW = 0xc0000
# E1448 added the sky: its row walker (0xb9df0: 176 as the pitch factor, a
# hard 352 row step) and span (0xb9d18: the x clip), the second blit
# descriptor builder (0xba1ac) and the four text row loops (0x506d0, 0x5072c,
# 0x50a84, 0x50ae0: the first row from the descriptor's width, then a hard
# 352 a row -- the sheared strips).
EXTRA_FUNCS = [0xa6c74, 0x83f0, 0xb9008, 0xba110,
               0xb9df0, 0xb9d18, 0xba1ac, 0x506d0, 0x5072c, 0x50a84, 0x50ae0]
HALF_WIDTH_FUNCS = [0xb9008, 0xb92d8, 0xbe848]
# The present's `mov r3, #0xb0` at 0xa6f70 is a row count (the overlay's 176
# rows) as well as the width: left alone, and the shift that made 16 rows of
# it is replaced by the new band offset.
LEAVE = {0xa6f70, 0x8584, 0x8658,      # and the line drawers' row clips (E1455)
         # Round 183 (E1513): the 3D view is 176 rows tall as well as 176
         # wide, so a 175/176 in a row test is the view's height and stays.
         # The width rule had widened these to 239/240, and the sprite
         # blits drew down to row 239 -- 32 rows past the picture, through
         # the margin, into the triangle array in the next heap cell; the
         # N95 died on the first car low in the frame. Read off the code:
         # each is a test on the y of the pair (x at +0, y at +4).
         0xb800, 0xb8b8, 0xb8bc,            # sprite blit 0xb714: y0 test, y1 clamp
         0xbc4c, 0xbcc8, 0xbccc,            # scaled sprite blit 0xbb84: y0 test, y1 clamp
         0xc0d4, 0xc1bc,                    # line drawer 0xbf90: the y test in each branch
         0xbe998, 0xbea08,                  # 0xbe848: y test, y-3..y+3 test
         0xb9460, 0xb9464, 0xb94b8, 0xb94bc,  # row routine 0xb92d8: row clamps (r4 feeds the row stride)
         0xb91f8, 0xb91fc}                  # column routine 0xb9008: rows per column
BY_HAND = {0xa6f74: ('lsl r2, r3, #5 -> mov r2, #0x1e00 (16 rows of 480)', 0xE3A02C1E),
           # the sky walker clips its row count with the same r2 it multiplies
           # the pitch by: the clip stays 176 rows, the factor becomes 240
           0xba070: ('movge r3, r2 -> movge r3, #0xb0 (row clip kept at 176)', 0xA3A030B0),
           # the vertex projection (0x11baec and its twin 0x11bb60) starts
           # both screen coordinates at 88 from one register; x's read-back
           # before the add becomes the new centre, y keeps the 88 (E1455)
           0x11bb34: ('ldr r3, [r4] -> mov r3, #0x78 (projection centre x 88 -> 120)', 0xE3A03078),
           0x11bbac: ('ldr r3, [r4] -> mov r3, #0x78 (projection centre x 88 -> 120, the twin)', 0xE3A03078),
           # the perspective column routine's y*11<<5 whose base register is
           # the row register reused (E1458): the same two words as a stride
           0xb91c8: ('add sl, r2, r2, lsl #2 -> add sl, r2, r2, lsl #1 (stride, base reg reused)', 0xE082A082),
           0xb91cc: ('add sl, r2, sl, lsl #1 -> add sl, sl, sl, lsl #2 (stride, base reg reused)', 0xE08AA10A)}


# Single sites in functions whose other immediates mean something else
# (E1449): the scene renderer (0xa1d78) projects with the screen's centre at
# 88 and tests points against 175, and also lays 2D text out at x 88, which
# stays; the big engine function 0x9e920 builds three more {0, 176, 208}
# blit descriptors. Each: image offset -> (new immediate, why).
SITES = {
    0xa1390: (240, 'blit descriptor width in 0x9e920'),
    0xa1440: (240, 'blit descriptor width in 0x9e920'),
    0xa1590: (240, 'blit descriptor width in 0x9e920'),
    0xa2388: (239, 'scene renderer: x clip 175'),
    0xa2c7c: (239, 'scene renderer: on-screen test x <= 175'),
    0xa2cac: (120, 'scene renderer: 88 - w/2, a sprite centred on the screen'),
    0xa2ccc: (120, 'scene renderer: x - 88'),
    0xa2fa8: (120, 'scene renderer: the projection centre, x += 88'),
    # E1450: the strips were text blitted through {flags, 176, 208, pixels}
    # descriptors of the screen built in nine more places (the text row
    # loops step by the descriptor's width); every one below wraps the
    # screen's own pixels ([x+0x1c] of the screen object, or the global
    # chain to it). 0x3942c stores 176/208 into a menu object and is left.
    0x4aa18: (240, 'screen wrapper object built over the screen pixels'),
    0x4c008: (240, 'screen descriptor on the stack'),
    0x4ee88: (240, 'screen descriptor on the stack'),
    0xa0210: (240, 'screen descriptor on the stack (0x9e920)'),
    0xa1db4: (240, 'screen descriptor on the stack (0xa1d78)'),
    0xaea20: (240, 'screen descriptor on the stack (0xae940), also passed as a width'),
    0xaf154: (240, 'screen descriptor on the stack (0xae940)'),
    0xbcd90: (240, 'screen descriptor on the stack (0xbcd44)'),
    0xbd67c: (240, 'screen descriptor on the stack (0xbd648)'),
    # E1455: the car is polygons through the triangle clipper (0xaa28) and
    # the rasteriser family 0x84f4-0xac64, and the projection that places
    # every vertex is 0x11baec/0x11bb60 (BY_HAND below). The clipper rejects
    # a triangle whose least x is past 175 (three tests on [v+0]); its six
    # tests on [v+4] are rows and stay. The two line drawers (0x84f4,
    # 0x85c8) are stride functions now, so the immediate rule widens their
    # x clips; the row they interpolate is clipped to 175 too, in LEAVE.
    0xaa78: (239, 'triangle clipper: least x past the right edge'),
    0xaabc: (239, 'triangle clipper: least x past the right edge'),
    0xaac8: (239, 'triangle clipper: least x past the right edge'),
    # E1458: the perspective column routine (0xb9008: the sky and ground
    # under a tilted camera) and its row sibling (0xb92d8). 176 rows of the
    # new pitch, 0x14a00, is not an immediate: 0x15000 bounds the copy at
    # 179 rows (the overrun is in the band the present clears), and the
    # clamp subtracts 0x7e0 so the clamped row is still row 175.
    0xb920c: (0x76, 'column routine: row step (480-8)/4'),
    0xb9278: (0x76, 'column routine: row step (480-8)/4'),
    0xb92bc: (0x3b, 'column routine: last column, 60 columns of 4'),
    0xb91d0: (0x1e, 'column routine: centre column 22 -> 30'),
    0xb913c: (0x15000, 'column routine: copy bound, 176 rows (179 for want of an immediate)'),
    0xb926c: (0x7e0, 'column routine: clamp to row 175 from the 179-row bound'),
    0xb9534: (0x77, 'row sibling: a full row is 120 words'),
    0xb9554: (0x77, 'row sibling: a full row is 120 words'),
    0xb95f4: (0x15000, 'row sibling: copy bound, 176 rows (179)'),
    0x88c4: (0x15000, 'view clear (0x88c0): 176 rows (179)'),
    # E1461 (the user: triangles missing at the right): each rasteriser
    # emits a scanline only if its left x is at most 175 (`cmp r1, #0xaf`
    # before the span call, with `cmp ip, #0` on the right x), so a span
    # starting past the old edge was dropped. Nineteen tests, five
    # rasterisers; their `cmp r2/r3, #0xaf` on [vertex+4] are rows and stay.
    0x8e8c: (239, 'rasteriser 88e4: span left x <= 175'),
    0x8f4c: (239, 'rasteriser 88e4: span left x <= 175'),
    0x9004: (239, 'rasteriser 88e4: span left x <= 175'),
    0x90a4: (239, 'rasteriser 88e4: span left x <= 175'),
    0x96e0: (239, 'rasteriser 9138: span left x <= 175'),
    0x97a0: (239, 'rasteriser 9138: span left x <= 175'),
    0x9858: (239, 'rasteriser 9138: span left x <= 175'),
    0x98f8: (239, 'rasteriser 9138: span left x <= 175'),
    0x9f34: (239, 'rasteriser 998c: span left x <= 175'),
    0x9ff4: (239, 'rasteriser 998c: span left x <= 175'),
    0xa0ac: (239, 'rasteriser 998c: span left x <= 175'),
    0xa14c: (239, 'rasteriser 998c: span left x <= 175'),
    0xa658: (239, 'rasteriser a1e0: span left x <= 175'),
    0xa764: (239, 'rasteriser a1e0: span left x <= 175'),
    0xa864: (239, 'rasteriser a1e0: span left x <= 175'),
    0xa94c: (239, 'rasteriser a1e0: span left x <= 175'),
    0xb4fc: (239, 'rasteriser ac64: span left x <= 175'),
    0xb5c0: (239, 'rasteriser ac64: span left x <= 175'),
    0xb670: (239, 'rasteriser ac64: span left x <= 175'),
    # E1451: the column walker (0x11bbfc, and its twin 0x11bd44) clips x to
    # [0, 175] and the row it computes to [0, 175]; the x bounds widen, the
    # row bound (0x11bca0, 0x11bdf0: the view's 176 rows) stays.
    0x11bc18: (239, 'column walker: x0 clip'),
    0x11bc80: (240, 'column walker: x1 clip'),
    0x11bc84: (240, 'column walker: x1 clip'),
    0x11bd68: (239, 'column walker twin: x0 clip'),
    0x11bdd0: (240, 'column walker twin: x1 clip'),
    0x11bdd4: (240, 'column walker twin: x1 clip'),
}


def encode_imm(v):
    for rot in range(16):
        imm8 = ((v << (2 * rot)) | (v >> (32 - 2 * rot))) & 0xFFFFFFFF
        if imm8 < 256:
            return imm8 | (rot << 8)
    return None


def with_imm(w, v):
    e = encode_imm(v)
    assert e is not None, 'not an ARM immediate: %#x' % v
    return (w & ~0xFFF) | e


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    path, out = argv[0], argv[1]
    w_new = int(argv[argv.index('--w') + 1]) if '--w' in argv else 240
    w_old, h = 176, 208
    code = ns.code_of(path)
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    word = lambda x: struct.unpack_from('<I', code, x)[0]
    rows = []                                   # (at, expect, replace, why)

    # stride: y*11<<5 -> y*15<<5
    strides = ns.stride_sites(code, md, 11)
    funcs = set(f for f in (ns.func_start(code, s) for s in strides) if f < RENDERER_BELOW) | set(EXTRA_FUNCS)
    assert w_new * 2 == 15 * 32, 'the stride idiom below is for 480'
    for s in strides:
        a, b = word(s), word(s + 4)
        t = (a >> 12) & 0xF
        a2 = (a & ~(0x1F << 7)) | (1 << 7)                       # add t, y, y, lsl #1
        b2 = (b & ~(0xF << 16) & ~(0x1F << 7)) | (t << 16) | (2 << 7)   # add t, t, t, lsl #2
        rows.append((s, a, a2, 'stride: add t,y,y,lsl#1'))
        rows.append((s + 4, b, b2, 'stride: add t,t,t,lsl#2'))

    # immediates in the renderer functions
    imm_map = {w_old: w_new, w_old * 2: w_new * 2, w_old - 1: w_new - 1,
               0x1600: 16 * w_new * 2, 0x10800: (h - 16) * w_new * 2}
    half = {w_old // 2: w_new // 2}
    for f in sorted(funcs):
        e = ns.func_end(code, f)
        m = dict(imm_map)
        if f in HALF_WIDTH_FUNCS:
            m.update(half)
        for x in range(f, e + 4, 4):
            if x in LEAVE or x in BY_HAND:
                continue
            w = word(x)
            v = ns.imm_of(w)
            if ((w >> 16) & 0xF) == 13:         # add/sub/cmp against sp: a stack offset
                continue
            if v in m:
                rows.append((x, w, with_imm(w, m[v]), 'imm %d -> %d in func %x' % (v, m[v], f)))
    for x, (why, new) in BY_HAND.items():
        rows.append((x, word(x), new, why))
    for x, (v, why) in SITES.items():
        rows.append((x, word(x), with_imm(word(x), v), 'site %d -> %d: %s' % (ns.imm_of(word(x)), v, why)))

    # literals: the buffer size, and the screen object's own allocation of
    # it plus 16 KB (img+0x8434, E1447: the memset of the patched size ran
    # off the end of a heap chunk sized by the unpatched one). The 16 KB is
    # overdraw margin in rows -- 16 above the picture (the band offset
    # 0x1600) and 30.5 below -- so it scales with the row: kept at 16 KB,
    # the 240-wide buffer had 18 rows below, and on the N95 the renderer's
    # spill below the picture reached the next heap cell, a triangle array,
    # 0x3c bytes past the buffer's end (round 182, BUGBOOK 14.l). 47 rows
    # of the new width keeps both margins.
    slack_old = 0x4000
    slack_new = -(-slack_old // (w_old * 2)) * (w_new * 2)
    for x in range(0, len(code) - 4, 4):
        if word(x) == w_old * h * 2:
            rows.append((x, word(x), w_new * h * 2, 'lit w*h*2'))
        elif word(x) == w_old * h * 2 + slack_old:
            rows.append((x, word(x), w_new * h * 2 + slack_new, 'lit w*h*2 + slack (the screen buffer allocation: %d rows of margin)' % (slack_new // (w_new * 2))))

    # surfaces: mov r2, #176 before each surface_init call -- those that
    # wrap the screen's pixels. E1476: 0x5ad68 and 0x11c894 wrap the title's
    # own 176-wide pictures (decoded into [obj+0x368] and [obj+0x33c]); made
    # 240 wide they showed the decode's leftovers beside every menu.
    SURFACE_IS_PICTURE = set()      # E1477: both are compose buffers and must be 240; left as a hook
    si = ns.surface_init(code, md, w_old, h)
    for c in ns.callers(code, si):
        if any(c - 4 * k in SURFACE_IS_PICTURE for k in range(1, 9)):
            continue
        for k in range(1, 9):
            y = c - 4 * k
            i = list(md.disasm(code[y:y + 4], y))
            if i and i[0].mnemonic == 'mov' and i[0].op_str == 'r2, #0x%x' % w_old:
                if not any(r[0] == y for r in rows):
                    rows.append((y, word(y), with_imm(word(y), w_new), 'surface width'))
                break

    # a SITES or BY_HAND word outranks the rule that also matched it
    named = set(SITES) | set(BY_HAND)
    byhand = [why for why, new in BY_HAND.values()]
    rows = [r for r in rows if r[0] not in named or r[3].startswith('site ') or r[3] in byhand]
    rows.sort()
    seen = set()
    for r in rows:
        assert r[0] not in seen, 'two patches at %x' % r[0]
        seen.add(r[0])
    with open(out, 'w') as f:
        f.write('// Generated by nativepatch.py from %s: width %d -> %d, height %d.\n' % (os.path.basename(path), w_old, w_new, h))
        f.write('// %d words. Every row: image offset, the word expected, the word written.\n' % len(rows))
        f.write('#define GAME_CODE_PATCHES \\\n')
        for k, (at, exp, new, why) in enumerate(rows):
            f.write('    { 0x%08x, 0x%08X, 0x%08X }%s /* %s */ \\\n' % (at, exp, new, ',' if k + 1 < len(rows) else ' ', why))
        f.write('\n#define GAME_CODE_PATCH_COUNT %d\n' % len(rows))
        f.write('// The back buffer is allocated w*h*2 plus this (gate6_result, gate6_mem_copy read it).\n')
        f.write('#define GAME_NATIVE_SLACK 0x%X\n' % slack_new)
    kinds = {}
    for r in rows:
        kinds[r[3].split(':')[0].split(' ')[0]] = kinds.get(r[3].split(':')[0].split(' ')[0], 0) + 1
    print('%d patch words -> %s  %s' % (len(rows), out, kinds))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
