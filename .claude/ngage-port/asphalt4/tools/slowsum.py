# slowsum.py <dir>: round 34's slowest frames and lossy frames (A4_SLOWREC), from the latest
# capture: each frame's ms split by phase (ticks are NTickCount, 1 ms on the N95).
import sys, struct, glob
OFF = 256 + 560 + 153600 + 122 * 4 + 0x700 + 384
best = None
for f in glob.glob(sys.argv[1] + '/a4tel*.dat'):
    d = open(f, 'rb').read()
    if len(d) < OFF + 0x680: continue
    cap = struct.unpack_from('<I', d, 8)[0]
    if best is None or cap > best[0]: best = (cap, d, f)
if not best: sys.exit('no slow-frame block')
cap, d, f = best
S = struct.unpack_from('<416I', d, OFF)
print('%s capidx %d: magic %08x, 3D frames seen %d' % (f.split('/')[-1], cap, S[0], S[1]))
print('presents: real %d (no render since the last real one %d, no 3D %d), skipped %d; renders %d, skipped (half-step) %d' % tuple(S[20:26]))
# words 30-31 were a third caller up to round 36; from round 37 A4_DROPHOLD's frames held back and framework posts held
if S[26]: print('  callers of the no-render real presents:', ['0x%08X x%d' % (S[26 + 2*k], S[27 + 2*k]) for k in range(2) if S[26 + 2*k]])
print('frames held back (A4_DROPHOLD: 3D under a half (r38 a quarter, r37 a sixth) of the last frame that went out, the camera where it was; 0 in a build without it): %d, and %d no-render presents and %d framework posts after them' % (S[30] & 0xFFFF, S[30] >> 16, S[31]))
# word 12 of an entry: list triangles lost (low 16 bits); from round 39 (A4_DROPHOLD) the
# camera's offset from the player car against the last frame shown, most on one axis
# (bits 16-30, 32767 = no race render), and bit 31 = the frame was held
cols = 'ms 3D hook comp 2D pres logic spec rcall pre tris list lost camdev held mode nodraw stale frame prevtri'.split()
def show(rows, title):
    print(title); print('  ' + ' '.join('%6s' % c for c in cols))
    for e in rows:
        if e[0]:
            v = list(e[:12]) + [e[12] & 0xFFFF, (e[12] >> 16) & 0x7FFF, e[12] >> 31] + [e[13] & 0xFFFF, (e[13] >> 16) & 0x3FF, e[13] >> 26] + list(e[14:16])
            print('  ' + ' '.join('%6d' % x for x in v))
R = sorted([S[32 + i * 16: 48 + i * 16] for i in range(16)], key=lambda e: -e[0])
show(R, 'the 16 slowest 3D frames (ms; 3D = first triangle to composite, comp = the flush/composite, rcall = the game\'s render call, pre = its part before the first triangle)')
L = [S[288 + i * 16: 304 + i * 16] for i in range(8)]
print('drop frames (triangles halved or worse) in the race: %d' % S[18])
print('speculative frames drawn at the whole step for a camera script (A4_SPECSCRIPTHOLD, round 41): %d; speculations in slow motion (A4_SLOWMOQ; mod 65536): %d' % (S[14] & 0xFFFF, S[14] >> 16))
show(sorted(L, key=lambda e: e[14]), 'the last 8 frames with list triangles lost or the triangles halved (prevtri: the frame before)')
# round 71: word 7 of a drop entry (bit 31 set) -- the camera's track segment (4095: no race render), the GPU's objects
# taken / drawn / taken this frame, its cars taken / this frame; from round 71 an entry is also a frame whose objects
# were taken and not drawn, or fell to under half of the frame before's
W = [e for e in sorted(L, key=lambda e: e[14]) if e[0] and e[7] >> 31]
if W:
    print('  drop entries (round 71 word 7): frame, camera segment, objects taken, drawn, this frame, cars taken, this frame, direction set, substep')
    for e in W:
        w = e[7]
        print('  %6d %6d %6d %6d %6d %6d %6d %6d %6d' % (e[14], w & 0xFFF, (w >> 12) & 0xFF, (w >> 20) & 1, (w >> 21) & 1, (w >> 22) & 0x1F, (w >> 27) & 1, (w >> 28) & 1, (w >> 29) & 3))
