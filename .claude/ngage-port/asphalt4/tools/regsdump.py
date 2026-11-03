# regsdump.py <file.regs> [pc]: EKA2L1_PCTRACE_REGS records (pc, r0..r15), grouped by pc and lr
import sys, struct, collections
d = open(sys.argv[1], 'rb').read(); n = len(d) // 68
want = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
recs = [struct.unpack_from('<17I', d, i * 68) for i in range(n)]
print('%d records' % n)
by = collections.Counter((r[0], r[15]) for r in recs)
for (pc, lr), k in by.most_common(40):
    print('  pc %08x lr %08x (+%06X)  x%d' % (pc, lr, (lr - 0x70000000) & ~1, k))
if want:
    for r in [r for r in recs if r[0] == want][-12:]:
        print(' '.join('%08x' % v for v in r))
