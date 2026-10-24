"""callcount.py <e32 image> -- count BL/BLX call sites to each import stub of an EABI E32 image."""
import sys, struct, collections
sys.path.insert(0, '/home/user/EKA2L1/.claude/ngage-port/toolchain/port')
import e32imports as e
d = open(sys.argv[1], 'rb').read()
h = e.header(d)
co = h['code_offset']; code = d[co:co + h['code_size']]; tsz = h['text_size']
names = {}
D = '/home/user/symbiansource/kernelhwsrv/kernel/eka/compsupp/eabi/'
for lib, f in (('dfpaeabi', 'dfpaeabiu.def'), ('drtaeabi', 'drtaeabiu.def')):
    for l in open(D + f):
        p = l.split()
        if '@' in p: names[(lib, int(p[p.index('@') + 1]))] = p[0]
# import words: offsets into code; find stubs that reference them
slot = {}
o = h['import_offset'] + 4
for _ in range(h['dll_ref_count']):
    name_off, count = struct.unpack_from('<II', d, o); o += 8
    words = struct.unpack_from('<%dI' % count, d, o) if count else (); o += 4 * count
    nm = d[h['import_offset'] + name_off:d.index(b'\0', h['import_offset'] + name_off)].decode('latin1')
    for w in words:
        slot[w] = (nm.split('{')[0], struct.unpack_from('<I', code, w)[0] & 0xffff)
if not slot:
    print('imports() gives no offsets; raw:', list(imp.items())[:1]); sys.exit()
# stubs: ARM 'ldr pc,[pc,#-4]' (e51ff004) followed by the word
stub = {}
for off, v in slot.items():
    if off >= 4 and struct.unpack_from('<I', code, off - 4)[0] == 0xe51ff004:
        stub[off - 4] = v
    else:
        stub.setdefault(('raw', off), v)
cnt = collections.Counter()
arm = thumb = 0
for i in range(0, tsz - 3, 4):            # ARM BL / BLX imm
    w = struct.unpack_from('<I', code, i)[0]
    if (w & 0x0f000000) == 0x0b000000 and (w >> 28) != 0xf:
        off = ((w & 0xffffff) ^ 0x800000) - 0x800000
        t = i + 8 + off * 4
        if t in stub: cnt[stub[t]] += 1; arm += 1
for i in range(0, tsz - 3, 2):            # Thumb BL/BLX pair
    a, b = struct.unpack_from('<HH', code, i)
    if (a & 0xf800) == 0xf000 and (b & 0xe800) == 0xe800:
        off = ((a & 0x7ff) << 12) | ((b & 0x7ff) << 1)
        if off & 0x400000: off -= 0x800000
        t = i + 4 + off
        if (b & 0x1000) == 0: t &= ~3
        if t in stub: cnt[stub[t]] += 1; thumb += 1
print('stubs found', sum(1 for k in stub if not isinstance(k, tuple)), 'of', len(slot), '; arm calls', arm, 'thumb calls', thumb)
for (lib, o), n in cnt.most_common(40):
    print('%5d  %-12s %4d %s' % (n, lib, o, names.get((lib, o), '')))
