# rendrec.py <crec.bin>: the per-render records (A4_RENDREC): for each render the half-step
# count, the mesh context ('CRNC') and the player car ('CRNP'). Which words of each move on
# odd-count renders (the speculative frames) and which only on even ones.
import sys, struct, collections
d = open(sys.argv[1], 'rb').read(); o = 0; R = []; hdr = None
while o + 12 <= len(d):
    pc, addr, ln = struct.unpack_from('<3I', d, o); o += 12; b = d[o:o + ln]; o += ln
    if ln == 0x20: hdr = struct.unpack_from('<8I', b); continue
    if hdr and hdr[0] in (0x434E5243, 0x504E5243):
        R.append((hdr[0], hdr[1], struct.unpack_from('<%di' % (ln // 4), b)))
    hdr = None
for magic, name in ((0x434E5243, 'mesh context'), (0x504E5243, 'player car')):
    S = [(hs, w) for m, hs, w in R if m == magic]
    if len(S) < 4: print(name, 'records', len(S)); continue
    S = S[len(S) // 3:]                      # the race, not the countdown
    odd = collections.Counter(); even = collections.Counter(); n_o = n_e = 0
    for (h0, w0), (h1, w1) in zip(S, S[1:]):
        par = h1 & 1    # count after the render's last half-step: odd = a speculative frame follows a whole step's? see below
        ch = [j for j in range(len(w0)) if w0[j] != w1[j] and abs(w1[j]) < 10**7]
        if par: odd.update(ch); n_o += 1
        else: even.update(ch); n_e += 1
    print('%s: %d renders; words changing into odd-count renders (%d) / even-count renders (%d):' % (name, len(S), n_o, n_e))
    allw = sorted(set(odd) | set(even))
    for j in allw[:60]:
        print('   +%03X  odd %4d  even %4d' % (j * 4, odd[j], even[j]))
