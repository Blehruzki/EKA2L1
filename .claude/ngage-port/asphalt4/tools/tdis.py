"""tdis.py <code file> <start hex> <end hex> [arm] -- disassemble a raw code blob (Thumb by default)."""
import sys, capstone
code = open(sys.argv[1], 'rb').read()
a, b = int(sys.argv[2], 16), int(sys.argv[3], 16)
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM if len(sys.argv) > 4 else capstone.CS_MODE_THUMB)
for i in md.disasm(code[a:b], a):
    print('  %06x  %-10s %s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
