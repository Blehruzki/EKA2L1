#!/usr/bin/env python3
# skychk.py <capture files or dirs>: round 88, A4_GPUSKY's census words (a4_gl.c 0x412D00, written after the
# framebuffer under A4_BBPROBE: header 256 + FPS 560 + framebuffer 153,600 bytes in) from the latest capture --
# with A4_SKYCHECK: [50] frames checked, [51] backdrop pixels differing from the routine's own stores, [52] frames
# with any, [53] the first one's x | y << 16, [54] backdrop pixels without the drawn mask, [55] rows software
# changed between the first triangle and the composite, [56] frames with any, [57] the check's own mutant flips;
# always: [58] sky texture uploads, [59] of them the image's pixels changed, [62] the image, [63] the surface,
# [67] frames with the GPU's backdrop, [68] frames whose sky stayed software, [69] composites forced by a triangle
import sys, os, glob, struct
OFF = 256 + 560 + 153600
fs = []
for a in sys.argv[1:]: fs += sorted(glob.glob(os.path.join(a, '*.dat'))) if os.path.isdir(a) else [a]
best = None
for f in fs:
    d = open(f, 'rb').read()
    if len(d) < OFF + 122 * 4: continue
    w = struct.unpack_from('<122I', d, OFF)
    fr = struct.unpack_from('<I', d, 12)[0]          # the header's frame count (fcount)
    if best is None or fr > best[0]: best = (fr, os.path.basename(f), w)
if not best: print('no census block'); sys.exit()
fr, fn, c = best
print('%s (frame %d):' % (fn, fr))
print('  sky check: frames %d, backdrop pixels differing %d (frames with any %d, first at %d,%d), without the drawn mask %d; rows software changed before the composite %d (frames %d); mutant flips %d'
      % (c[50], c[51], c[52], c[53] & 0xFFFF, (c[53] >> 16) & 0x7FFF, c[54], c[55], c[56], c[57]))
print('  frames with the GPU backdrop %d, software sky %d, composites forced by a triangle %d; uploads %d (changed %d)' % (c[67], c[68], c[69], c[58], c[59]))
if c[40]:
    K = {1: 'draw hook entry', 2: 'draw hook exit', 3: 'billboard', 5: 'fill', 6: 'flush'}
    print('  trace: intervals with a change %d, rows %d; the first after %s (info %06x) and before %s (info %06x), first row %d, rows %d'
          % (c[40], c[44], K.get(c[41] >> 24, c[41] >> 24), c[41] & 0xFFFFFF, K.get(c[42] >> 24, c[42] >> 24), c[42] & 0xFFFFFF, c[43] & 0xFFFF, c[43] >> 16))
    print('  by the hook before: entry %d, exit %d, billboard %d, fill %d, flush %d; first event box segments %d-%d rows %d-%d (%d segments), union segments %d-%d rows %d-%d, %d segments in all'
          % (c[45], c[46], c[47], c[48], c[49], c[36] & 255, (c[36] >> 8) & 255, (c[36] >> 16) & 255, c[36] >> 24, c[37], c[38] & 255, (c[38] >> 8) & 255, (c[38] >> 16) & 255, c[38] >> 24, c[39]))
