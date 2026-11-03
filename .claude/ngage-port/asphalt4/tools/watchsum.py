# watchsum.py <log> <base hex> [from_post]: EKA2L1_WATCH writes into an object, by writing PC:
# how many, which fields, the function (nearest Thumb push {.., lr}), after the given post count.
import sys, re, struct, collections
log, base = sys.argv[1], int(sys.argv[2], 16)
frm = int(sys.argv[3]) if len(sys.argv) > 3 else 0
c = open('/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad/a4code.bin', 'rb').read()
def fstart(o):
    a = o & ~1
    while a > 0 and (struct.unpack_from('<H', c, a)[0] & 0xFF00) != 0xB500: a -= 2
    return a
rx = re.compile(r'WATCH: (\d+) byte write of 0x([0-9A-F]+) to 0x([0-9A-F]+) from pc 0x([0-9A-F]+) lr 0x([0-9A-F]+)')
posts = 0; by = collections.defaultdict(lambda: [0, collections.Counter()]); n = 0
for line in open(log, errors='replace'):
    if 'Framestat' in line: posts += 1; continue
    m = rx.search(line)
    if not m or posts < frm: continue
    w, v, a, pc, lr = int(m[1]), int(m[2], 16), int(m[3], 16), int(m[4], 16), int(m[5], 16)
    e = by[pc]; e[0] += 1; e[1][a - base] += 1; n += 1
print('%d writes after post %d, %d writing PCs' % (n, frm, len(by)))
fn = collections.defaultdict(list)
for pc, (k, offs) in by.items(): fn[fstart(pc - 0x70000000)].append((pc - 0x70000000, k, offs))
print('%d functions' % len(fn))
for f, lst in sorted(fn.items(), key=lambda x: -sum(k for _, k, _ in x[1])):
    tot = sum(k for _, k, _ in lst)
    fields = sorted(set(o for _, _, offs in lst for o in offs))
    print('  fn +%06X  %6d writes, %2d sites, fields %s' % (f, tot, len(lst), ' '.join('%X' % o for o in fields[:24]) + (' ...' if len(fields) > 24 else '')))
