# pacing.py <crec.bin>: how evenly the drawn motion follows the game's clock (round 33). Per
# render ('CRNC' opens it): the game time of its update (race+0x44, 'CRNR') and the player
# car as drawn ('CRNA' slot 0, +004/+00C). Speed on screen = distance drawn / game time between
# consecutive renders; against the median of its 9 neighbours, an even picture reads 1.00 with
# a narrow spread. Real-time runs (no lockstep).
import sys, struct, statistics as st
d = open(sys.argv[1], 'rb').read(); o = 0; hdr = None; part = 0; R = []; cur = None
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20:
        hdr = struct.unpack_from('<8I', b); part = 0
        if hdr[0] == 0x434E5243: cur = {'hs': hdr[1]}; R.append(cur)
        continue
    if cur is None or not hdr: continue
    if hdr[0] == 0x414E5243 and hdr[2] == 0: cur['p'] = struct.unpack_from('<4i', b, 0)
    elif hdr[0] == 0x524E5243 and ln == 0x4000 and part == 0: cur['t'] = struct.unpack_from('<I', b, 0x44)[0]; part = 1
R = [r for r in R if 'p' in r and 't' in r]; R = R[len(R) // 3:]
v = []
for a, b in zip(R, R[1:]):
    dt = (b['t'] - a['t']) & 0xFFFFFFFF
    dist = ((b['p'][1] - a['p'][1]) ** 2 + (b['p'][3] - a['p'][3]) ** 2) ** 0.5
    v.append(dist / dt * 21333 if dt else None)
rat = []
for i in range(4, len(v) - 4):
    if v[i] is None: continue
    nb = [x for x in v[i - 4:i + 5] if x]
    m = st.median(nb)
    if m > 5: rat.append(v[i] / m)
rat.sort()
if rat:
    print('%d renders; drawn speed / local median: median %.2f, quartiles %.2f..%.2f, 10%%..90%% %.2f..%.2f; off by more than 25%%: %.0f%%'
          % (len(rat), st.median(rat), rat[len(rat)//4], rat[3*len(rat)//4], rat[len(rat)//10], rat[9*len(rat)//10], 100.0 * sum(1 for x in rat if abs(x - 1) > 0.25) / len(rat)))
dts = sorted(((b['t'] - a['t']) & 0xFFFFFFFF) / 21333 * 85.33 for a, b in zip(R, R[1:]))
print('game time between renders (ms): median %.1f, 10%%..90%% %.1f..%.1f' % (st.median(dts), dts[len(dts)//10], dts[9*len(dts)//10]))
