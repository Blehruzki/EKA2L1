"""proffunc.py <code blob> <profile> <image name> <image base hex> [top N] -- an exact profile by function.

Function starts are found by shape: a Thumb `push {..., lr}` (0xB5xx) or an ARM
`stmdb sp!, {..., lr}` (0xE92D4xxx). Each block is credited to the nearest start
at or below it. A heuristic: a function without such a prologue (a leaf) folds
into the one before it.
"""
import bisect, collections, struct, sys
code = open(sys.argv[1], 'rb').read()
prof, img, base = sys.argv[2], sys.argv[3], int(sys.argv[4], 16)
top = int(sys.argv[5]) if len(sys.argv) > 5 else 30
starts = set()
for i in range(0, len(code) - 1, 2):
    if (struct.unpack_from('<H', code, i)[0] & 0xFF00) == 0xB500: starts.add(i)
for i in range(0, len(code) - 3, 4):
    if (struct.unpack_from('<I', code, i)[0] & 0xFFFF4000) == 0xE92D4000: starts.add(i)
starts = sorted(starts)
rows = [l.split() for l in open(prof) if not l.startswith('#')]
total = sum(int(n) for _, n in rows)
fn = collections.Counter(); blocks = collections.defaultdict(list)
for pc, n in rows:
    off = int(pc, 16) - base
    if not (0 <= off < len(code)): continue
    i = bisect.bisect_right(starts, off) - 1
    f = starts[i] if i >= 0 else 0
    fn[f] += int(n); blocks[f].append((int(n), off))
print('%s: %.1f%% of all credited instructions' % (img, 100.0 * sum(fn.values()) / total))
for f, n in fn.most_common(top):
    print('%5.1f%%  %s+%06X  (%d blocks)' % (100.0 * n / total, img, f, len(blocks[f])))
