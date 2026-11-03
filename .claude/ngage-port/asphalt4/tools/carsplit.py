# carsplit.py <crec.bin>: every car slot's motion on the speculative frames (A4_RENDREC with
# A4_HALFSPEC: 'CRNA' per car slot, 'CRNS' the call log and the speculation's header, in a
# LOCKSTEP 1 / LOCKSTEP_P 10667 run: one half-step per render, hs odd = after a whole step).
# Per slot: the share of the motion between the whole frames either side that the speculative
# frame between them shows (0.5 = half way; 0 = it stayed put; < 0 or > 1 = back and forth),
# measured on the car's own position (+004, +00C), and whether the speculation saved it.
import sys, struct, statistics as st
d = open(sys.argv[1], 'rb').read(); o = 0; hdr = None; part = 0
R = []; cur = None
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: hdr = struct.unpack_from('<8I', b); part = 0; continue
    if not hdr: continue
    tag = hdr[0]
    if tag == 0x434E5243:                                   # 'CRNC' opens a render
        cur = {'hs': hdr[1], 'cars': {}, 'calls': None, 'saved': None}; R.append(cur)
    elif cur is None: continue
    elif tag == 0x414E5243 and ln in (0xA00, 0xC7C):                 # 'CRNA'
        w = struct.unpack_from('<%di' % 64, b)
        cur['cars'][hdr[2]] = (hdr[3], w[1], w[2], w[3], w[0x0B8 // 4], w[0x0C0 // 4])
    elif tag == 0x534E5243 and ln == 0x4000:                # 'CRNS'
        w = struct.unpack_from('<%dI' % 0x200, b)
        if part == 0:
            n = min(w[2], 48); cur['calls'] = [w[8 + 6 * i: 8 + 6 * i + 6] for i in range(n)]; part = 1
        else:
            cur['saved'] = set(w[4:4 + min(w[0], 16)])
R = [r for r in R if r['cars']]
R = R[len(R) // 3:]                                         # the race, not the countdown
print(len(R), 'renders;', 'slots seen:', sorted({s for r in R for s in r['cars']}))
calls = [len(r['calls']) for r in R if r['calls'] is not None and r['hs'] & 1 == 0]
if calls: print('recorded calls per step (at speculative frames): min %d median %d max %d (log holds 48)' % (min(calls), st.median(calls), max(calls)))
kinds = {}
for r in R:
    if r['calls'] and r['hs'] & 1 == 0:
        for c in r['calls']: kinds.setdefault(c[1], set()).add(c[0])
slots = sorted({s for r in R for s in r['cars']})
for s in slots:
    share = []; saved = 0; nspec = 0; moving = 0; car = None; called = set()
    for a, b, c in zip(R, R[1:], R[2:]):
        if not (a['hs'] & 1 and b['hs'] & 1 == 0 and c['hs'] & 1): continue
        if s not in a['cars'] or s not in b['cars'] or s not in c['cars']: continue
        car = b['cars'][s][0]; nspec += 1
        if b['saved'] is not None and car in b['saved']: saved += 1
        pa, pb, pc_ = (a['cars'][s][1], a['cars'][s][3]), (b['cars'][s][1], b['cars'][s][3]), (c['cars'][s][1], c['cars'][s][3])
        dx, dz = pc_[0] - pa[0], pc_[1] - pa[1]; L = dx * dx + dz * dz
        if L < 4: continue
        moving += 1
        share.append(((pb[0] - pa[0]) * dx + (pb[1] - pa[1]) * dz) / L)
    k = sorted(kinds.get(car, ())) if car else []
    if share:
        sh = sorted(share); bad = sum(1 for x in share if x < 0.25 or x > 0.75)
        print('slot %2d car %08x: saved %3d/%3d, calls %s; moving %3d: share median %.2f quartiles %.2f..%.2f, outside 0.25..0.75: %d'
              % (s, car, saved, nspec, k, moving, st.median(sh), sh[len(sh) // 4], sh[3 * len(sh) // 4], bad))
    else:
        print('slot %2d car %08x: saved %3d/%3d, calls %s; not moving' % (s, car or 0, saved, nspec, k))
