#!/usr/bin/env python3
# gatelog.py <emulator log> [from-present]: round 40. EKA2L1_WATCHGATE opened around the
# speculative camera update with the present count + 1 (A4_SPECGATECAM): each word of the
# watched range reported at its first write, with the PC. Prints, per present (after
# from-present, default 2000), the words first written then, the game thread's stack left
# out (within 16 KB of the writer's sp), grouped into runs.
import sys, re
fn = sys.argv[1]; frm = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
cur = None; per = {}
ro = re.compile(r'WATCHGATE: open at pc 0x([0-9A-F]+) value 0x([0-9A-F]+)')
rw = re.compile(r'WATCHGATE: write 0x([0-9A-F]+) \((\d+) bytes of 0x([0-9A-F]+)\) from pc 0x([0-9A-F]+) lr 0x([0-9A-F]+) sp 0x([0-9A-F]+)')
for line in open(fn, errors='replace'):
    m = ro.search(line)
    if m: cur = int(m.group(2), 16) - 1; continue
    m = rw.search(line)
    if m and cur is not None:
        a, sp, pc = int(m.group(1), 16), int(m.group(6), 16), int(m.group(4), 16)
        if abs(a - sp) < 0x4000: continue
        per.setdefault(cur, []).append((a, pc))
for f in sorted(per):
    if f < frm: continue
    ws = sorted(per[f]); runs = []
    for a, pc in ws:
        if runs and a <= runs[-1][1] + 8: runs[-1][1] = a; runs[-1][2].add(pc)
        else: runs.append([a, a, {pc}])
    print('present %d: %d words: %s' % (f, len(ws), '; '.join('%08X-%08X (pc %s)' % (r[0], r[1] + 3, ','.join('%X' % (p - 0x70000000) for p in sorted(r[2])[:3])) for r in runs[:6])))
