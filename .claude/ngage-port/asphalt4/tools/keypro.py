#!/usr/bin/env python3
# keypro.py <pcmem dump>: round 64, the A4_KEYPROBE ring (eng_keys_merge, a4_eng.h): per substep (1, 2) the
# whole step's key word (ENG_KEYS) and the one built live from the key object's copy. Prints how often the
# live steering (0x603) differs from the whole step's, and where each steering change reached the car: on
# substep 1 (two substeps, 57 ms, before the game's whole step would have read it), on 2 (one, 28 ms), or on
# the whole step itself (no record: a change the whole step's word shows with no live change before it).
import sys, struct
d = open(sys.argv[1], 'rb').read()
E = {}
o = 0
while o + 12 <= len(d):                         # EKA2L1_PCMEM blocks: pc, address, size, then the bytes
    pc, ad, n = struct.unpack_from('<3I', d, o); o += 12
    for k in range(0, n - 15, 16):
        c, sub, kw, live = struct.unpack_from('<4I', d, o + k)
        if sub in (1, 2): E[c] = (sub, kw, live)
    o += n
R = sorted(E.items())
M = 0x603
if not R: print('no records'); sys.exit()
diff = sum(1 for c, (s, kw, lv) in R if (kw ^ lv) & M)
other = sum(1 for c, (s, kw, lv) in R if (kw ^ lv) & ~M & 0xFFFF)
print('%d substep records (cnt %d..%d); live steering differs from the whole step\'s on %d; other bits differ on %d (not taken)'
      % (len(R), R[0][0], R[-1][0], diff, other))
at = {1: 0, 2: 0, 0: 0}
seen = dict(R)
for c, (s, kw, lv) in R:
    p = seen.get(c - 1)
    if s == 2 and p and (p[2] ^ lv) & M: at[2] += 1                        # changed between substeps 1 and 2
    if s == 1:
        q = seen.get(c - 2)                                                 # substep 2 of the step before
        if q and (q[2] ^ lv) & M:
            if (lv ^ kw) & M: at[1] += 1                                    # live ahead of the whole step just run
            else: at[0] += 1                                                # the whole step had it too
print('steering changes reaching the car: on substep 1 (57 ms sooner) %d, on substep 2 (28 ms sooner) %d, with the whole step (as before) %d'
      % (at[1], at[2], at[0]))
