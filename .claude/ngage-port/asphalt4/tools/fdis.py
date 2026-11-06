# fdis.py <code> <addr hex> [n]: disassemble the Thumb function containing addr (from the nearest
# push {...,lr} at or before it) for n instructions; marks bl targets
import sys, capstone, struct
code = open(sys.argv[1], 'rb').read()
a = int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv) > 3 else 200
s = a & ~1
while s > 0:
    h = struct.unpack_from('<H', code, s)[0]
    if (h & 0xFF00) == 0xB500: break
    s -= 2
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
k = 0; o = s
while k < n and o < len(code):
    got = False
    for i in md.disasm(code[o:o + 4], o, count=1):
        print('  %06x  %-10s %s %s%s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str, '   <==' if i.address <= a < i.address + i.size else ''))
        o += i.size; got = True
    if not got: print('  %06x  .word?' % o); o += 2
    k += 1
