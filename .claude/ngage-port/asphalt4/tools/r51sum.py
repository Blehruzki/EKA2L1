# r51sum.py <dir>: round 51 telemetry (aggregates 'AG28': slot 0 the 1/w clear run, 1 skipped while clean; round 53's
# 'AG29': slot 0 the game's 3D front end, 1 the lifted mesh routine, 2 the game's again, 3 lifted with the corner memo;
# round 50's 'AG27' files read as before): per A/B slot over the race -- slot bit 0 AB_PX6
# (the ARMv6 pixel loops and the runtime's convert ours), bit 1 AB_PXPLD (those loops with a prefetch; slot
# 2, the prefetch without the loops, is the C loops again: a second baseline). ms a render is the whole
# window over its renders (the present's convert included); comp is the composite (readback and loop).
# Then the converter's census words: [90] installs, [91] fast converts, [92] handed back, [95] baseline slots,
# [89] why it refused (0 none), [88] the hash a different runtime's convert gave, [87] presents whose 3D target was
# not the screen's buffer (the garage's offscreen car; tried again, not a refusal -- 2 in [89] is the old latch).
import sys, struct, glob
AGG = 256 + 560 + 153600 + 122 * 4
rows = []
for f in glob.glob(sys.argv[1] + '/a4tel*.dat'):
    d = open(f, 'rb').read(); h = struct.unpack_from('<64I', d, 0); rows.append((h[2], h, d))
rows.sort()
NAME27 = {0: 'C loops', 1: 'ARMv6', 2: 'C loops (2)', 3: 'ARMv6 + PLD'}
NAME28 = {0: 'clear', 1: 'skip', 2: '-', 3: '-'}
NAME29 = {0: 'game front', 1: 'lifted', 2: 'game (2)', 3: 'lifted+memo'}   # round 53: AB_FRONT x AB_MEMO
fmt0 = struct.unpack_from('<I', rows[-1][2], AGG + 0x600)[0] if rows else 0
NAME, SHIFT, MASK = {0x41473238: (NAME28, 14, 1), 0x41473239: (NAME29, 15, 3)}.get(fmt0, (NAME27, 12, 3))
print('capidx mode   slot          rend  ms/rend | 3D   comp  read  2D   pres')
for c, h, d in rows:
    r, w = h[33], h[34]
    if not r or not w: continue
    print('%5d %6d  %-12s %5d %7.1f | %5.1f %5.1f %5.1f %4.1f %4.1f' % (c, h[32], NAME[(h[32] >> SHIFT) & MASK], r, w / r,
          h[13] / r, h[18] / r, h[36] / r, h[16] / r, h[17] / r))
c, h, d = rows[-1]
fmt = struct.unpack_from('<I', d, AGG + 0x600)[0]
print('aggregates format %08x%s' % (fmt, '' if fmt in (0x41473237, 0x41473238, 0x41473239) else '  (not AG27-AG29)'))
for s in range(3):
    for m in range(4):
        a = struct.unpack_from('<32I', d, AGG + (s * 4 + m) * 128)
        if not a[0]: continue
        print('seg%d slot%d %-12s: %5d renders in %3d windows, %.1f ms a render | 3D %.1f comp %.1f (read %.1f) 2D %.1f pres %.1f'
              % (s, m, NAME[m], a[0], a[15], a[1] / a[0], a[2] / a[0], a[4] / a[0], a[5] / a[0], a[7] / a[0], a[8] / a[0]))
o = 256 + 560 + struct.unpack_from('<I', d, 28)[0]
if len(d) >= o + 122 * 4:
    ck = struct.unpack_from('<122I', d, o)
    why = {0: 'none recorded', 2: 'the buffer\'s object not where expected (bb[8..10])', 3: 'the converter not linked back (conv[1])',
           4: 'no vtable', 5: 'no convert entry', 6: 'the runtime\'s convert differs (hash %08x)' % ck[88], 7: 'its size stub differs'}
    print('converter: installs %d, fast %d, handed back %d, baseline-slot converts %d, other 3D targets %d; refusal: %s'
          % (ck[90], ck[91], ck[92], ck[95], ck[87], why.get(ck[89], str(ck[89]))))
    print('1/w clear: skipped %d, run %d' % (ck[79], ck[99]))
