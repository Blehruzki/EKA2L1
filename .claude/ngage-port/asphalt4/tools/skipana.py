#!/usr/bin/env python3
# skipana.py <trace.regs> <spec-mark-pc>: round 41. Speculations whose camera update had its
# script player or script setup skipped (A4_SPECCAMSAFE; a4_spec_mark4's third word, SPEC
# word 25), by camera mode (race+0x94, its fourth): how often, in runs of how many, and whether
# the speculated camera moved with the car (its step against the car's) -- a camera left at
# the whole step while the car is drawn ahead is the takedown cameras' "online lag".
import sys, struct, math, collections
d = open(sys.argv[1], 'rb').read(); a0 = int(sys.argv[2], 16)
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
E = []; cur = {}
for i in range(len(d) // 68):
    e = struct.unpack_from('<17I', d, i * 68); k = e[0] - a0
    if k == 0: cur = {'S': (s32(e[1]), s32(e[2])), 'q': e[4] / 65536.0}
    elif k == 4 and 'S' in cur: cur['P'] = (s32(e[1]), s32(e[2]))
    elif k == 8 and 'P' in cur: cur['CS'] = (s32(e[1]), s32(e[2])); cur['CP'] = (s32(e[3]), s32(e[4]))
    elif k == 12 and 'CS' in cur: cur['skip'] = e[3]; cur['mode'] = e[4]; E.append(cur); cur = {}
print('%d speculations; %d with a camera script call skipped' % (len(E), sum(1 for x in E if x['skip'])))
modes = collections.Counter((x['mode'], bool(x['skip'])) for x in E)
for (m, sk), c in sorted(modes.items()): print('  camera mode %3d (0x%02X) %s: %d' % (m, m, 'skipped' if sk else 'played ', c))
runs = []; r = 0
for x in E:
    if x['skip']: r += 1
    elif r: runs.append(r); r = 0
if runs: print('runs of skipped speculations: %d, lengths %s' % (len(runs), sorted(runs)[-12:]))
for sk in (0, 1):
    v = []
    for x in E:
        if bool(x['skip']) != bool(sk): continue
        car = math.hypot(x['P'][0] - x['S'][0], x['P'][1] - x['S'][1])
        cam = math.hypot(x['CP'][0] - x['CS'][0], x['CP'][1] - x['CS'][1])
        if car > 20: v.append(cam / car)
    v.sort()
    if v: print('%s: camera step / car step on moving steps (%d): median %.2f, 10-90%% %.2f..%.2f, under 0.25: %d'
                % ('script skipped' if sk else 'no skip       ', len(v), v[len(v) // 2], v[len(v) // 10], v[9 * len(v) // 10], sum(1 for t in v if t < 0.25)))
