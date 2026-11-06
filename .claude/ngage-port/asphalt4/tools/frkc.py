# frkc.py crec.bin [...]: the camera mode and the slow-motion scale (race+0x84C) of every frame
# in the FRKC records (a4_gl.c A4_YREC), counted and in sequence: mode/scale:frames@step
import sys, struct, collections
for fn in sys.argv[1:]:
    d = open(fn,'rb').read(); o = 0; c = collections.Counter(); seq = []
    while o + 12 <= len(d):
        pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o+ln]; o += ln
        if ln == 0x20:
            h = struct.unpack_from('<8I', b)
            if h[0] == 0x434B5246:
                k = (h[3], h[4]); c[k] += 1
                if not seq or seq[-1][0] != k: seq.append([k, 1, h[1]])
                else: seq[-1][1] += 1
    print(fn.split('/')[-1], ' '.join('%x/%d:%d' % (k[0], k[1], n) for k, n in sorted(c.items())))
    print('   seq', ' '.join('%x/%d:%d@%d' % (k[0], k[1], n, s) for k, n, s in seq[:30]))
