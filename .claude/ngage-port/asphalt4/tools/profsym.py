# profsym.py <EKA2L1_PCPROF output> <A4_SYMFILE> <group>... : instructions (or cycles) credited to named code,
# summed per group. A group is name=item,item,...; an item is a blob symbol (its range runs to the next
# symbol) or a game range +lo-hi (hex code offsets). Code at 0x70000000 (the bench's RM-320). For the
# in-run checks (A4_PAINTCHECK, A4_LIFTCHECK), which run the game's version and ours on the same inputs:
# the two sums are the gain on the race's own data, whatever the CPI split.
import sys, re, bisect
prof, symf = sys.argv[1], sys.argv[2]
S = []
for l in open(symf):
    m = re.match(r'\+([0-9A-Fa-f]+)\s+(\S+)', l)
    if m: S.append((int(m.group(1), 16), m.group(2)))
S.sort(); addrs = [a for a, _ in S]
rng = {}
for k, (a, n) in enumerate(S):
    rng.setdefault(n, (a, S[k + 1][0] if k + 1 < len(S) else a + 0x10000))
rows = []
for l in open(prof):
    if l.startswith('#'): continue
    pc, n = l.split(); rows.append((int(pc, 16) - 0x70000000, int(n)))
tot = sum(n for _, n in rows)
for g in sys.argv[3:]:
    name, items = g.split('=', 1); s = 0
    for it in items.split(','):
        if it.startswith('+'):
            lo, hi = [int(x, 16) for x in it[1:].split('-')]
        else:
            if it not in rng: print('  (no symbol %s)' % it); continue
            lo, hi = rng[it]
        s += sum(n for pc, n in rows if lo <= pc < hi)
    print('%-12s %14d  %6.2f%%' % (name, s, 100.0 * s / tot))
