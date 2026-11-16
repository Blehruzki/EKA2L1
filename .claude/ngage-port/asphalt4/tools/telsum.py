"""telsum.py <dir>: a run's a4tel windows -- ms a render (the window's ticks over its renders) and the phases a render
(3D, hook, comp, read, 2D, present), summed over the windows; errc."""
import sys, struct, glob
names = {'w_renders': 33, 'w_nticks': 34, 'w_comp': 35, 'w_read': 36, 't_3d': 13, 't_hook': 14, 't_2d': 16, 't_pres': 17, 'errc': 12, 'capidx': 2}
for d in sys.argv[1:]:
    tot = dict.fromkeys(names, 0); n = 0; per = []
    for f in sorted(glob.glob(d + '/a4tel*.dat')):
        b = open(f, 'rb').read()
        h = struct.unpack_from('<64I', b, 0)
        if h[33] == 0: continue
        n += 1
        for k, i in names.items(): tot[k] += h[i]
        per.append(h[34] / h[33])
    r = tot['w_renders'] or 1
    print('%-8s windows %d renders %d  ms a render %.1f (windows %s)  3D %.1f hook %.1f comp %.1f read %.1f 2D %.1f pres %.1f  errc %d' % (
        d.split('/')[-2] if d.endswith('/') else d.split('/')[-2], n, tot['w_renders'], tot['w_nticks'] / r, ' '.join('%.0f' % x for x in per),
        tot['t_3d'] / r, tot['t_hook'] / r, tot['w_comp'] / r, tot['w_read'] / r, tot['t_2d'] / r, tot['t_pres'] / r, tot['errc']))
