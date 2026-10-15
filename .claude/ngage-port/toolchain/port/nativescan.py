#!/usr/bin/env python3
"""Where a framebuffer title bakes its 176x208 into its code (NATIVE240.md, step 1).

    nativescan.py <image.app> [--w 176] [--h 208]

Four kinds of site, each a thing a native-resolution patch would have to
change:
  stride   the row-address idiom  add t,y,y,lsl#2 ; add t,y,t,lsl#1 ; ... lsl #5
           (y*11*32 = y*352: 176 pixels at 16 bits), by function
  imm      data-processing immediates that are the width, the height, the
           stride in bytes, width-1, height-1 or the half width/height, by
           function -- noisy on their own (176 is a common number), so the
           functions that also hold a stride site are marked *
  lit      literal-pool words of width*height*2 (the buffer size) and who
           reads them
  surface  every call of the surface-init routine (the one that stores a
           width and a height as halfwords at +4 and +6 of its object) with
           the immediates the caller moves into r2 and r3

Asphalt 2 (E1443-E1446): 17 stride sites in 12 functions (all below
img+0xc0000: the fills and the span families), the present at img+0xa6c74,
buffers of 73,216 bytes at five literals.
"""
import collections, struct, sys

import capstone

IMM_ROLES = None


def code_of(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 0x64)[0]
    size = struct.unpack_from('<I', d, 0x30)[0]
    return d[off:off + size]


def imm_of(w):
    """The value of a data-processing immediate, or None."""
    if (w >> 25) & 7 != 1 or (w >> 28) == 0xF:
        return None
    rot = (w >> 8) & 0xF
    imm = w & 0xFF
    return ((imm >> (2 * rot)) | (imm << (32 - 2 * rot))) & 0xFFFFFFFF


def func_start(code, x):
    while x > 0:
        w = struct.unpack_from('<I', code, x)[0]
        if (w & 0xFFFF4000) == 0xE92D4000:          # push {..., lr}
            return x
        x -= 4
    return 0


def func_end(code, x):
    while x < len(code) - 4:
        w = struct.unpack_from('<I', code, x)[0]
        if (w & 0xFFFF8000) == 0xE8BD8000 or w == 0xE12FFF1E:   # pop {..., pc} / bx lr
            return x
        x += 4
    return x


def stride_sites(code, md, mult):
    """y*mult*32 written as shift-adds: mult 11 is (y + y<<2) then (y + t<<1)."""
    out = []
    for x in range(0, len(code) - 12, 4):
        a = list(md.disasm(code[x:x + 4], x))
        b = list(md.disasm(code[x + 4:x + 8], x + 4))
        if not a or not b or a[0].mnemonic != 'add' or b[0].mnemonic != 'add':
            continue
        pa = [p.strip() for p in a[0].op_str.split(',')]
        pb = [p.strip() for p in b[0].op_str.split(',')]
        if not (len(pa) == 4 and pa[1] == pa[2] and pa[3] == 'lsl #2' and
                len(pb) == 4 and pb[1] == pa[1] and pb[2] == pa[0] and pb[3] == 'lsl #1'):
            continue
        # The lsl #5 must make an address: `add rd, base, t, lsl #5` with a
        # base that is neither y nor t. E1446: an `add t,t,t,lsl#5` or an
        # `rsb t,y,t,lsl#5` after the same two adds is a constant
        # multiplication (33t, 351y...) in the engine's arithmetic, and the
        # 13 such sites above img+0xc0000 patched as strides killed the boot.
        y, t = pa[1], pb[0]
        for k in range(2, 7):
            c = list(md.disasm(code[x + 4 * k:x + 4 * k + 4], x + 4 * k))
            if not c or c[0].mnemonic != 'add':
                continue
            pc = [p.strip() for p in c[0].op_str.split(',')]
            if len(pc) == 4 and pc[2] == t and pc[3] == 'lsl #5' and pc[1] not in (y, t):
                out.append(x)
                break
    return out


def surface_init(code, md, w, h):
    """The routine that stores a width and a height as halfwords at +4/+6:
    strh rX,[rN,#4] ; strh rY,[rN,#6] within a few instructions of a push."""
    for x in range(0, len(code) - 48, 4):
        if (struct.unpack_from('<I', code, x)[0] & 0xFFFF4000) != 0xE92D4000:
            continue
        seen = set()
        for k in range(1, 12):
            i = list(md.disasm(code[x + 4 * k:x + 4 * k + 4], x + 4 * k))
            if i and i[0].mnemonic == 'strh':
                if '#4]' in i[0].op_str:
                    seen.add(4)
                if '#6]' in i[0].op_str:
                    seen.add(6)
        if seen == {4, 6}:
            return x
    return None


def callers(code, target):
    out = []
    for x in range(0, len(code) - 4, 4):
        w = struct.unpack_from('<I', code, x)[0]
        if (w & 0x0F000000) == 0x0B000000:
            off = w & 0xFFFFFF
            if off & 0x800000:
                off -= 0x1000000
            if x + 8 + off * 4 == target:
                out.append(x)
    return out


def main(argv):
    if not argv:
        raise SystemExit(__doc__)
    path = argv[0]
    w = int(argv[argv.index('--w') + 1]) if '--w' in argv else 176
    h = int(argv[argv.index('--h') + 1]) if '--h' in argv else 208
    code = code_of(path)
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    roles = {w: 'w', h: 'h', w * 2: 'stride', w - 1: 'w-1', h - 1: 'h-1', w // 2: 'w/2', h // 2: 'h/2'}

    strides = stride_sites(code, md, 11)
    stride_funcs = collections.OrderedDict()
    for s in strides:
        stride_funcs.setdefault(func_start(code, s), []).append(s)
    print('stride: %d sites in %d functions (y*%d*32 = y*%d)' % (len(strides), len(stride_funcs), w * 2 // 32, w * 2))
    for f, ss in stride_funcs.items():
        print('  func %06x  %s' % (f, ' '.join('%x' % s for s in ss)))

    imms = collections.OrderedDict()
    for x in range(0, len(code) - 4, 4):
        v = imm_of(struct.unpack_from('<I', code, x)[0])
        if v in roles:
            imms.setdefault(func_start(code, x), collections.Counter())[roles[v]] += 1
    print('\nimm: %d functions hold a size immediate; those with a stride site marked *' % len(imms))
    for f, c in imms.items():
        if f in stride_funcs or sum(c.values()) >= 3:
            print('  %sfunc %06x  %s' % ('*' if f in stride_funcs else ' ', f, dict(c)))

    size = w * h * 2
    lits = [x for x in range(0, len(code) - 4, 4) if struct.unpack_from('<I', code, x)[0] == size]
    print('\nlit: %d words of %d (w*h*2)' % (len(lits), size))
    for lit in lits:
        readers = []
        for x in range(max(0, lit - 4096), lit, 4):
            ww = struct.unpack_from('<I', code, x)[0]
            if (ww & 0x0F7F0000) == 0x051F0000:
                off = ww & 0xFFF
                if x + 8 + (off if (ww >> 23) & 1 else -off) == lit:
                    readers.append(x)
        print('  %06x read at %s' % (lit, ' '.join('%x' % r for r in readers)))

    si = surface_init(code, md, w, h)
    if si is not None:
        print('\nsurface init at %06x; its callers and the sizes they move into r2, r3:' % si)
        for c in callers(code, si):
            regs = {}
            for k in range(1, 9):
                y = c - 4 * k
                i = list(md.disasm(code[y:y + 4], y))
                if i and i[0].mnemonic == 'mov' and i[0].op_str[:2] in ('r2', 'r3') and i[0].op_str[:2] not in regs and ', #' in i[0].op_str:
                    regs[i[0].op_str[:2]] = i[0].op_str.split('#')[1]
            print('  %06x  %s' % (c, regs or '(from registers)'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
