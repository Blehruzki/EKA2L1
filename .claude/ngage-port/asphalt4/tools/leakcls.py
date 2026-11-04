#!/usr/bin/env python3
# leakcls.py <emulator log> <trace.regs> <spec-ptrs-pc> [ourlo:ourhi]: round 41. The speculation's
# writes outside what it saves. EKA2L1_WATCHGATE (opened by A4_SPECGATE from the keys to the
# camera update) reports each watched word at its first write; a4_spec_ptrs marks what the
# speculation saved (the race object 0x9EC bytes, K 0x40, the cars 0xC7C each). Every gated
# write not into those, not into the writer's own stack (16 KB around its sp) and not from our
# own code is a leak: the speculation changed it and nothing puts it back. Grouped by writer.
import sys, re, struct, collections
log, regs, pp = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
olo, ohi = (int(x, 16) for x in (sys.argv[4] if len(sys.argv) > 4 else '700B9E48:70120000').split(':'))
d = open(regs, 'rb').read(); R = []; nxt = 0
for i in range(len(d) // 68):
    e = struct.unpack_from('<17I', d, i * 68)
    if e[0] != pp: continue
    if nxt == 0:
        R.append((e[1], 0x9EC));
        if e[2]: R.append((e[2], 0x40))
        nxt = (e[3] + 3) // 4
    else:
        for c in e[1:5]:
            if c: R.append((c, 0xC7C))
        nxt -= 1
R = sorted(set(R))
print('%d saved objects seen (race, K, cars)' % len(R))
def saved(a):
    for b, n in R:
        if b <= a < b + n: return True
    return False
rw = re.compile(r'WATCHGATE: write 0x([0-9A-F]+) \((\d+) bytes of 0x([0-9A-F]+)\) from pc 0x([0-9A-F]+) lr 0x([0-9A-F]+) sp 0x([0-9A-F]+)')
by = collections.defaultdict(list); n = stk = sv = our = 0
for line in open(log, errors='replace'):
    m = rw.search(line)
    if not m: continue
    a, pc, lr, spv = int(m.group(3), 16), int(m.group(4), 16), int(m.group(5), 16), int(m.group(6), 16)
    n += 1
    if olo <= pc < ohi: our += 1; continue
    if abs(a - spv) < 0x4000: stk += 1; continue
    if saved(a): sv += 1; continue
    by[(pc, lr)].append(a)
print('%d gated words: %d our code, %d stack, %d saved objects, %d outside (%d writers)' % (n, our, stk, sv, n - our - stk - sv, len(by)))
for (pc, lr), al in sorted(by.items(), key=lambda kv: -len(kv[1])):
    al.sort()
    print('  pc %08X lr %08X: %d words, %08X..%08X' % (pc, lr, len(al), al[0], al[-1]))
