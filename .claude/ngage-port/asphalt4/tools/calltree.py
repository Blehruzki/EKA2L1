# calltree.py <trace.bin> <code.bin> [depth]: the call tree of the race step (+141B0) from an
# EKA2L1_PCTRACE of (pc, sp) pairs over the game code: a block entered at a function start (a Thumb
# push {.., lr}) is a call; the stack pointer at entry says which frames have returned. Per step:
# each function's callers and calls (counts per step, averaged over the steps traced).
import sys, struct, collections
import numpy as np
BASE = 0x70000000
t = np.frombuffer(open(sys.argv[1], 'rb').read(), dtype='<u4'); c = open(sys.argv[2], 'rb').read()
DEPTH = int(sys.argv[3]) if len(sys.argv) > 3 else 6
pcs = (t[0::2].astype(np.int64) - BASE); sps = t[1::2].astype(np.int64)
isstart = lambda o: 0 <= o < len(c) - 1 and (struct.unpack_from('<H', c, o & ~1)[0] & 0xFF00) == 0xB500
STEP = 0x141B0
steps = 0; edges = collections.Counter(); calls = collections.Counter(); stack = []
inside = False; base_sp = None; order = []; seen_first = set()
for o, s in zip(pcs.tolist(), sps.tolist()):
    o &= ~1
    if o == STEP:
        inside = True; steps += 1; stack = [(STEP, s)]; base_sp = s; calls[STEP] += 1; continue
    if not inside: continue
    if s > base_sp + 16:     # left the step function
        inside = False; continue
    if isstart(o):
        while stack and stack[-1][1] <= s and len(stack) > 1: stack.pop()
        parent = stack[-1][0] if stack else None
        edges[(parent, o)] += 1; calls[o] += 1
        if steps == 2 and o not in seen_first: seen_first.add(o); order.append((len(stack), parent, o))
        stack.append((o, s))
print('steps traced: %d' % steps)
kids = collections.defaultdict(list)
for (p, k), n in edges.items(): kids[p].append((k, n))
def show(f, d, path):
    if d > DEPTH: return
    for k, n in sorted(kids.get(f, []), key=lambda x: x[0]):
        print('%s+%05X  x%.1f' % ('  ' * d, k, n / max(1, steps)) + ('  (recursion)' if k in path else ''))
        if k not in path: show(k, d + 1, path | {k})
print('+141B0'); show(STEP, 1, {STEP})
