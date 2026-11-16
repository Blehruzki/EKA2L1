"""shipchk.py <exe>: a phone candidate's checks -- uncompressed E32, +3A15C holds b510 0004, no 0xDEADBEEF anywhere."""
import sys, struct, hashlib
d = open(sys.argv[1], 'rb').read()
comp, = struct.unpack_from('<I', d, 0x1C); co, = struct.unpack_from('<I', d, 0x64)
w = d[co + 0x3A15C: co + 0x3A160]
dead = d.find(struct.pack('<I', 0xDEADBEEF))
print('sha256', hashlib.sha256(d).hexdigest(), len(d), 'bytes')
print('compression', hex(comp), '(uncompressed)' if comp == 0 else '** compressed **')
print('+3A15C', w.hex(), 'ok' if w.hex() == 'b5100004' or w.hex() == '10b50400' else '** unexpected **')
print('0xDEADBEEF', 'none' if dead < 0 else '** at %x **' % dead)
