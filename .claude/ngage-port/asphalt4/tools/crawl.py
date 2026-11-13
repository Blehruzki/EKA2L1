#!/usr/bin/env python3
# crawl.py <crec.bin> ...: round 65, a race's driving from the car records (A4_CARREC, one a game step): from the
# start (car+0xE0 reaching 1) to the end of the record -- the speed (+7D0) median and mean, the steps at a crawl
# (|speed| < 30), the longest crawl, and the wrecks (+034 going from -1 to anything else).
import sys, os, importlib.util
spec = importlib.util.spec_from_file_location('crec_load', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'crec.py'))
src = open(spec.origin).read().split('\nA = load(')[0]
ns = {}; exec(compile(src, spec.origin, 'exec'), ns); load = ns['load']
for fn in sys.argv[1:]:
    A = load(fn)
    i0 = next((i for i, (h, w) in enumerate(A) if w[0xE0 // 4] >= 1), 0)
    S = [w[0x7D0 // 4] for h, w in A[i0:]]; W = [w[0x34 // 4] for h, w in A[i0:]]
    if not S: print(fn, 'no steps'); continue
    crawl = [abs(s) < 30 for s in S]; run = best = 0
    for c in crawl: run = run + 1 if c else 0; best = max(best, run)
    wrecks = sum(1 for a, b in zip(W, W[1:]) if a == -1 and b != -1)
    ss = sorted(S)
    print('%-16s %4d steps: speed median %3d mean %5.1f; crawling %4d steps (%2.0f%%), longest %3d; wrecks %d'
          % (os.path.basename(fn), len(S), ss[len(ss) // 2], sum(S) / len(S), sum(crawl), 100.0 * sum(crawl) / len(S), best, wrecks))
