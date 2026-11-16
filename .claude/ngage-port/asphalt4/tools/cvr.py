# cvr.py <files or dirs>: the cover redraw's counters ('CVR1' + 8 words) in each capture
import sys, glob, os, struct
fs = []
for a in sys.argv[1:]: fs += sorted(glob.glob(os.path.join(a, '*.dat'))) if os.path.isdir(a) else [a]
names = 'tri-covered tri/quad-kept flushes sprite-cells redraws redraw-draws redraw-ticks -'.split()
for f in fs:
    d = open(f, 'rb').read(); i = d.rfind(b'CVR1')
    if i < 0: print(os.path.basename(f), 'no CVR1'); continue
    k = struct.unpack_from('<8I', d, i + 4)
    print(os.path.basename(f), ' '.join('%s=%d' % (n, v) for n, v in zip(names, k) if n != '-'))
