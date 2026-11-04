#!/usr/bin/env python3
# renderdiff.py <ring file> [render-pc hex]: round 37. An EKA2L1_PCTRACE_RING dump taken at
# a4_drop_mark: the last render call (+12992 entry) in it is the empty one, the one before
# a whole one. Prints where their block sequences first part (the decision), each render's
# block count, and blocks only the whole render ran (first-seen order, with counts).
import sys, struct, collections
d = open(sys.argv[1], 'rb').read()
rp = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x70012992
e = struct.unpack('<%dI' % (len(d) // 4), d)
pcs, sps = e[0::2], e[1::2]
ent = [i for i, p in enumerate(pcs) if p == rp]
print('%d entries, %d render entries in the ring' % (len(pcs), len(ent)))
if len(ent) < 2: sys.exit('need two renders')
def body(i):
    # from the render's entry to its return: the first later block whose sp is back at the entry's (the callee has popped)
    sp0 = sps[i]; j = i + 1
    while j < len(pcs) and sps[j] < sp0: j += 1
    return list(pcs[i:j])
A, B = body(ent[-2]), body(ent[-1])        # whole, empty
print('whole render: %d blocks; empty render: %d blocks' % (len(A), len(B)))
k = 0
while k < min(len(A), len(B)) and A[k] == B[k]: k += 1
print('they part at block %d:' % k)
for j in range(max(0, k - 8), k): print('   both  +%06X' % (A[j] - 0x70000000))
print('   whole +%06X ...  %s' % (A[k] - 0x70000000, ' '.join('+%X' % (p - 0x70000000) for p in A[k + 1:k + 8])))
print('   empty +%06X ...  %s' % (B[k] - 0x70000000, ' '.join('+%X' % (p - 0x70000000) for p in B[k + 1:k + 8])))
ca, cb = collections.Counter(A), collections.Counter(B)
only = [p for p in dict.fromkeys(A) if p not in cb]
print('%d blocks only in the whole render; the first 30 in order:' % len(only))
print('  ' + ' '.join('+%X(%d)' % (p - 0x70000000, ca[p]) for p in only[:30]))
# per-block counts: the blocks whose counts differ most between the two renders
diff = sorted(set(ca) | set(cb), key=lambda p: -abs(ca[p] - cb[p]))
print('blocks by count difference (whole - empty):')
for p in diff[:40]: print('  +%06X  whole %6d  empty %6d  %+d' % (p - 0x70000000, ca[p], cb[p], ca[p] - cb[p]))
