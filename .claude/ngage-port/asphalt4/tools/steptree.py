# steptree.py <trace.bin> <code.bin>: the game code one logic step runs. Segments of an
# EKA2L1_PCTRACE (pc, sp words) from the step loop's head +167A6 to its exit +1692E (the n
# steps of one race update); blocks mapped to functions by the nearest Thumb `push {..., lr}`
# at or before them (a heuristic: a function without one is merged into the one above).
import sys, struct, collections
BASE, GAME_END = 0x70000000, 0xB9E48
t = open(sys.argv[1], 'rb').read(); c = open(sys.argv[2], 'rb').read()
import numpy as np
arr = np.frombuffer(t, dtype='<u4')[0::2].astype(np.int64) - BASE
n = len(arr)
print('trace entries %d, first %06x, last %06x' % (n, arr[0], arr[-1]))
heads = np.nonzero(arr == 0x167A6)[0]; exits = np.nonzero(arr == 0x1692E)[0]
# keep only the slices from a head to the next exit (the logic), as a short Python list
keep = []
for e in exits:
    h = heads[heads < e]
    if len(h) == 0: continue
    # the first head after the previous exit
    prev = exits[exits < e]
    h0 = h[h > prev[-1]][0] if len(prev) and (h > prev[-1]).any() else (h[0] if not len(prev) else None)
    if h0 is None: continue
    keep.append((int(h0), int(e)))
pcs = {}
for (i0, i1) in keep: pcs[i0] = arr[i0:i1 + 1].tolist()
fcache = {}
def func_of(o):
    o &= ~1
    if o in fcache: return fcache[o]
    a = o
    while a >= 0 and o - a < 8192:
        if (struct.unpack_from('<H', c, a)[0] & 0xFF00) == 0xB500: break
        a -= 2
    fcache[o] = a
    return a
segs = keep
print('logic segments (step loop head to exit): %d' % len(segs))
allb, allf = collections.Counter(), collections.Counter()
for k, (i0, i1) in enumerate(segs):
    body = [o for o in pcs[i0] if o < GAME_END]
    steps = sum(1 for o in body if o == 0x167A6)
    fs = set(func_of(o) for o in body)
    allb.update(set(body)); allf.update(fs)
    if k < 12: print('  seg %2d: %d steps, %6d block entries, %5d distinct blocks, %4d functions' % (k, steps, len(body), len(set(body)), len(fs)))
print('union over %d segments: %d distinct blocks, %d functions' % (len(segs), len(allb), len(allf)))
# function sizes: up to the next function start among all pushes in the image
starts = sorted(set(a for a in range(0, GAME_END, 2) if (struct.unpack_from('<H', c, a)[0] & 0xFF00) == 0xB500))
import bisect
def fsize(a):
    j = bisect.bisect_right(starts, a)
    return (starts[j] - a) if j < len(starts) else 0
tot = sum(fsize(a) for a in allf)
print('their code: %d bytes (%.1f%% of the game code); functions in every segment: %d'
      % (tot, 100.0 * tot / GAME_END, sum(1 for a, v in allf.items() if v == len(segs))))
# heaviest functions by block entries over all segments
ent = collections.Counter()
for (i0, i1) in segs:
    for o in pcs[i0]:
        if o < GAME_END: ent[func_of(o)] += 1
print('top functions by block entries:')
for a, v in ent.most_common(25): print('   +%06X  %7d entries  %5d bytes' % (a, v, fsize(a)))
