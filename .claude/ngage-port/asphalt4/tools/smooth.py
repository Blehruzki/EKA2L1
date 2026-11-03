# smooth.py <crec.bin>: per render, how far the camera (race+0x58/+0x60) and the player car
# (+004/+00C) moved since the render before, split by renders after whole steps and after
# speculative half-steps. Even motion: the two kinds of step about the same.
import sys, struct, statistics as st
d = open(sys.argv[1], 'rb').read(); o = 0; hdr = None; cur = {}; R = []; part = 0
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: hdr = struct.unpack_from('<8I', b); part = 0; continue
    if not hdr: continue
    if hdr[0] == 0x504E5243: cur = {'hs': hdr[1], 'car': struct.unpack_from('<4i', b, 0)}
    elif hdr[0] == 0x524E5243:
        if part == 0:
            w = struct.unpack_from('<%di' % (ln // 4), b); cur['cam'] = (w[0x58 // 4], w[0x5C // 4], w[0x60 // 4], w[0x68 // 4]); part = 1
        else:
            if 'car' in cur: R.append(cur)
            cur = {}; hdr = None
R = R[len(R) // 3:]
def dist(a, b): return ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5
for key, name, ix in (('cam', 'camera', (0, 1, 2)), ('car', 'player car', (1, 2, 3))):
    W = []; S = []
    for a, b in zip(R, R[1:]):
        pa = tuple(a[key][i] for i in ix); pb = tuple(b[key][i] for i in ix)
        dd = dist(pa, pb)
        if dd > 0 or True: (W if b['hs'] & 1 else S).append(dd)
    Wm = [x for x in W if x > 0]; Sm = [x for x in S if x > 0]
    pairs = [(w, s2) for w, s2 in zip(W, S) if w + s2 > 0]
    if pairs:
        share = [s2 / (w + s2) for w, s2 in pairs]
        print('%s: the speculative frame\'s share of each whole step\'s motion: median %.2f (0.5 = even), quartiles %.2f..%.2f over %d steps'
              % (name, st.median(share), sorted(share)[len(share)//4], sorted(share)[3*len(share)//4], len(share)))
    print('%s: moves into renders after whole steps: median %.1f mean %.1f (n %d); after speculative: median %.1f mean %.1f (n %d)'
          % (name, st.median(W), st.mean(W), len(W), st.median(S), st.mean(S), len(S)))
yaw = [r['cam'][3] for r in R]
print('camera +0x68 over the last 12 renders:', yaw[-12:], ' half-step parity:', [r['hs'] & 1 for r in R[-12:]])
