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
NAME30 = {0: 'sky software', 1: 'sky GPU', 2: '-', 3: '-'}               # round 54: AB_GPUSKY
NAME31 = {0: 'v0 runtime', 1: 'v1 EGL', 2: 'v2 EGL', 3: 'v3 runtime'}   # rounds 55-56: route B's probe (AB_RBVAR)
fmt0 = struct.unpack_from('<I', rows[-1][2], AGG + 0x600)[0] if rows else 0
NAME, SHIFT, MASK = {0x41473238: (NAME28, 14, 1), 0x41473239: (NAME29, 15, 3), 0x41473330: (NAME30, 17, 1), 0x41473331: (NAME31, 18, 3)}.get(fmt0, (NAME27, 12, 3))
print('capidx mode   slot          rend  ms/rend | 3D   comp  read  2D   pres')
for c, h, d in rows:
    r, w = h[33], h[34]
    if not r or not w: continue
    print('%5d %6d  %-12s %5d %7.1f | %5.1f %5.1f %5.1f %4.1f %4.1f' % (c, h[32], NAME[(h[32] >> SHIFT) & MASK], r, w / r,
          h[13] / r, h[18] / r, h[36] / r, h[16] / r, h[17] / r))
c, h, d = rows[-1]
fmt = struct.unpack_from('<I', d, AGG + 0x600)[0]
print('aggregates format %08x%s' % (fmt, '' if fmt in (0x41473237, 0x41473238, 0x41473239, 0x41473330, 0x41473331) else '  (not AG27-AG31)'))
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
    if ck[67] or ck[68]:
        print('GPU sky: frames with the GPU backdrop %d, sky left to software %d, composites forced %d' % (ck[67], ck[68], ck[69]))
# rounds 55-56: route B's probe record (a4_gl.c struct rbp), after FRC2 in each capture: 'RBP1' (round 55) or 'RBP2'
RBO = AGG + 0x700 + 3 * 32 * 4 + 0x680 + 16 + 256 * 32 + 16 + 255 * 16
STEP = {0: 'untried', 1: 'up', 2: 'refused', 3: 'X10 not a pointer', 4: 'no X10 vtable', 5: 'the observer vtable not after it',
        6: 'the runtime entries elsewhere', 7: 'stop code differs', 8: 'restart code differs', 9: 'another X10',
        10: 'no frame EGL objects', 11: 'no 565 window config', 12: 'no context', 13: 'no window surface',
        14: 'make-current failed', 15: 'GL error at set-up'}
mg = struct.unpack_from('<I', d, RBO)[0] if len(d) >= RBO + 0x200 else 0
if mg == 0x31504252:
    r = struct.unpack_from('<128I', d, RBO)
    print('route B probe (RBP1): state %s (detail %08x), variant applied %d, owns %d; stops %d, restarts %d, make-currents failed %d'
          % (STEP.get(r[1], str(r[1])), r[2], r[3], r[13], r[17], r[18], r[19]))
    for k in range(3):
        v = r[32 + 16 * k: 48 + 16 * k]
        if not (v[13] or v[6]): continue
        e = max(v[0], 1)
        print('  %-12s: 3D %5d, EGL %5d (failed %d), runtime %5d, empty held %d, posts skipped %d | upload %.2f ms (max %d), '
              'draw+swap %.2f (max %d), make-currents %.2f | taken %d, given %d' % (NAME31[k], v[13], v[0], v[1], v[6], v[7], v[8],
              v[4] / e, v[14], v[2] / e, v[3], v[5] / e, v[9], v[10]))
elif mg == 0x32504252:
    r = struct.unpack_from('<128I', d, RBO)
    NM = {0: 'v0 runtime', 1: 'v1 EGL si1', 2: 'v2 EGL si0', 3: 'v3 no reposts'}
    si = lambda x: '-' if x == 0xFFFFFFFF else str(x)
    print('route B probe (RBP2): state %s (detail %08x), variant %d, owns %d; window config %08x (%d matched), swap interval '
          'range %s..%s (attributes refused %d), interval set %s, make-currents failed %d; X10 %08x, runtime text %08x'
          % (STEP.get(r[1], str(r[1])), r[2], r[3], r[13], r[7], r[21], si(r[17]), si(r[18]), r[20], si(r[15]), r[19], r[11], r[12]))
    if r[26] or r[27]:   # A4_LONGPRESS (round 56): the pause key's long-press count (a4_lp_count)
        print('pause key long press: drops counted %d, held back %d (within %s ms of the last), run out %d (the N-Gage application)'
              % (r[26], r[27], 'A4_LP_MS', r[28]))
    for k in range(4):
        v = r[32 + 24 * k: 56 + 24 * k]
        if not (v[11] or v[12] or v[8]): continue
        e = max(v[0], 1); pm = max(v[12], 1)
        print('  %-13s: 3D presents %5d; EGL %5d (swaps failed %d), new frames to the runtime %5d; posts made %6d (%.2f ms each, '
              'max %d), skipped %6d (check: %d same frame, %d different)' % (NM[k], v[11], v[0], v[1], v[7], v[12], v[13] / pm, v[14],
              v[8], v[17], v[18]))
        if v[0]:
            print('  %13s  upload %.2f ms (max %d), draw+swap %.2f ms (max %d), make-currents %.2f; interval set %d / refused %d; '
                  'taken %d, given %d (%d for no 3D); owned frames with its DSA off %d' % ('', v[4] / e, v[5], v[2] / e, v[3],
                  v[6] / e, v[15], v[16], v[9], v[10], v[21], v[20]))
