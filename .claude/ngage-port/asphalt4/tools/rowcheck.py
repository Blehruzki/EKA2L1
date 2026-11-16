# rowcheck.py <tel dir>: A4_ROWCHECK's FRC1 -- per render the composite's top row and the GPU items' top row
import sys, struct, glob, os
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384 + 0x680
E = {}
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'a4tel*.dat')) + glob.glob(os.path.join(sys.argv[1], 'a4mark*.dat'))):
    b = open(f, 'rb').read()
    if len(b) < OFF + 16 or struct.unpack_from('<I', b, OFF)[0] != 0x31435246: continue
    n = struct.unpack_from('<I', b, OFF + 4)[0]
    for k in range(max(0, n - 256), n):
        e = struct.unpack_from('<8I', b, OFF + 16 + (k % 256) * 32); E[e[0]] = e
lost = []; n = 0
for t in sorted(E):
    w = E[t][6]; mn, it = w & 0xFFFF, w >> 16
    if ((E[t][2] >> 20) & 0xFF) not in (1, 2, 3): continue
    n += 1
    if it < mn and mn <= 240: lost.append(mn - it)
lost.sort()
print('%d chase renders; the items began above the composite in %d (%.0f%%); rows lost: median %d, max %d'
      % (n, len(lost), 100.0 * len(lost) / max(1, n), lost[len(lost) // 2] if lost else 0, lost[-1] if lost else 0))
