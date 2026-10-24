"""ngsplit.py <file.n-gage> <outdir> -- split an N-Gage 2.0 install file (MIME multipart) into its parts."""
import sys, os
b = open(sys.argv[1], 'rb').read()
out = sys.argv[2]
bound = b'--KBoundary'
pos = [i for i in range(len(b)) if b.startswith(bound, i)] if False else []
i = 0
while True:
    j = b.find(bound, i)
    if j < 0: break
    pos.append(j); i = j + len(bound)
for n, (s, e) in enumerate(zip(pos, pos[1:] + [len(b)])):
    part = b[s + len(bound):e]
    if part.startswith(b'--'): continue
    h, sep, body = part.partition(b'\r\n\r\n')
    if not sep:
        h, sep, body = part.partition(b'\n\n')
    body = body[:-2] if body.endswith(b'\r\n') else body
    hdr = h.decode('latin1').strip()
    cid = [l.split(':', 1)[1].strip().strip('<>') for l in hdr.splitlines() if l.lower().startswith('content-id')]
    ct = [l.split(':', 1)[1].strip() for l in hdr.splitlines() if l.lower().startswith('content-type')]
    name = '%02d_%s' % (n, (cid[0] if cid else 'part').replace('/', '_'))
    open(os.path.join(out, name), 'wb').write(body)
    print('%-40s %10d  %s' % (name, len(body), ct[0] if ct else ''))
