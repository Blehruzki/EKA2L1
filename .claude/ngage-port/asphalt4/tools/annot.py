# annot.py <code.bin> <func hex> [end hex] [names: r0=car,r1=key,...]: a Thumb function's disassembly with
# every load and store named by its base -- the argument it came from plus an offset (car+0x7D0) --
# by following simple register arithmetic (movs/adds/subs/lsls with constants, pc-relative literals,
# stack slots) straight down the code. Branch targets keep the state that fell into them: good for
# the base pointers a function sets up once, wrong after a merge of different values (marked '?').
import sys, capstone, struct, re
code = open(sys.argv[1], 'rb').read(); a0 = int(sys.argv[2], 16)
end = int(sys.argv[3], 16) if len(sys.argv) > 3 and not '=' in sys.argv[3] else None
names = {'r0': 'a0', 'r1': 'a1', 'r2': 'a2', 'r3': 'a3'}
for s in sys.argv[3:]:
    if '=' in s: k, v = s.split('='); names[k] = v
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB); md.detail = True
if end is None:
    o = a0 + 2
    while o < len(code) and (struct.unpack_from('<H', code, o)[0] & 0xFF00) != 0xB500: o += 2
    end = o
R = {k: (v, 0) for k, v in names.items()}   # reg -> (base, offset) or ('#', const)
SL = {}                                      # sp slot -> value
def val(r): return R.get(r)
def fmt(v):
    if v is None: return '?'
    b, o = v
    if b == '#': return '#0x%X' % (o & 0xFFFFFFFF)
    return '%s%+#x' % (b, o) if o else b
def imm(op): return int(op.lstrip('#'), 0)
out = []
for i in md.disasm(code[a0:end], a0):
    m, ops = i.mnemonic, [x.strip() for x in i.op_str.split(',')]
    note = ''
    try:
        if m in ('movs', 'mov') and len(ops) == 2:
            R[ops[0]] = ('#', imm(ops[1])) if ops[1].startswith('#') else val(ops[1])
        elif m in ('adds', 'subs', 'add', 'sub') and len(ops) in (2, 3):
            sgn = -1 if m.startswith('sub') else 1
            d = ops[0]; srcs = ops[1:] if len(ops) == 3 else [ops[0], ops[1]]
            if d == 'sp': pass
            elif srcs[0] == 'sp' and srcs[1].startswith('#'): R[d] = ('sp', imm(srcs[1]))
            elif srcs[1].startswith('#'):
                v = val(srcs[0]); R[d] = (v[0], v[1] + sgn * imm(srcs[1])) if v else None
            else:
                x, y = val(srcs[0]), val(srcs[1])
                if x and y and x[0] == '#' and y[0] != '#': R[d] = (y[0], y[1] + sgn * x[1]) if sgn > 0 else None
                elif x and y and y[0] == '#' and x[0] != '#': R[d] = (x[0], x[1] + sgn * y[1])
                elif x and y and x[0] == '#' and y[0] == '#': R[d] = ('#', x[1] + sgn * y[1])
                else: R[d] = None
        elif m in ('lsls', 'lsl') and len(ops) == 3 and ops[2].startswith('#'):
            v = val(ops[1]); R[ops[0]] = ('#', v[1] << imm(ops[2])) if v and v[0] == '#' else None
        elif m == 'ldr' and '[pc' in i.op_str:
            mm = re.search(r'#(0x[0-9a-f]+|\d+)', i.op_str.split('[')[1]); off = int(mm.group(1), 0) if mm else 0
            la = ((i.address + 4) & ~3) + off; lit = struct.unpack_from('<I', code, la)[0]
            R[ops[0]] = ('#', lit); note = '= #0x%X' % lit
        elif m.startswith('ldr') or m.startswith('str'):
            mm = re.match(r'(\w+), \[(\w+)(?:, (#?-?\w+))?\]', i.op_str)
            if mm:
                rt, rn, off = mm.group(1), mm.group(2), mm.group(3)
                o = imm(off) if off and off.startswith('#') else None
                if rn == 'sp':
                    if m.startswith('str'): SL[o] = val(rt); note = 'sp[%#x] := %s' % (o, fmt(val(rt)))
                    else: R[rt] = SL.get(o); note = '%s := sp[%#x] (%s)' % (rt, o, fmt(SL.get(o)))
                else:
                    b = val(rn)
                    if o is not None and b and b[0] != '#': where = '%s%+#x' % (b[0], b[1] + o) if (b[1] + o) else b[0]
                    elif o is None and b: where = '%s+%s' % (fmt(b), off)
                    else: where = '?'
                    note = ('%s <- [%s]' if m.startswith('ldr') else '[%s] <- %s') % ((rt, where) if m.startswith('ldr') else (where, fmt(val(rt)) if val(rt) and val(rt)[0] == '#' else rt))
                    if m.startswith('ldr'): R[rt] = None
        elif m in ('bl', 'blx'):
            for k in ('r0', 'r1', 'r2', 'r3'): R[k] = None
            note = 'call'
        elif m in ('push', 'pop', 'cmp', 'tst', 'cmn', 'b', 'bx', 'beq', 'bne', 'blt', 'bgt', 'ble', 'bge', 'bhi', 'bls', 'bcc', 'bcs', 'bmi', 'bpl', 'bvs', 'bvc', 'bhs', 'blo', 'stm', 'ldm'):
            if m == 'ldm': 
                for r in re.findall(r'r\d+', i.op_str.split('{')[1]): R[r] = None
        else:
            if ops and re.match(r'r\d+$', ops[0]): R[ops[0]] = None
    except Exception as e:
        if ops and re.match(r'r\d+$', ops[0]): R[ops[0]] = None
    print('  %06x  %-8s %-24s %s' % (i.address, m, i.op_str, ('; ' + note) if note else ''))
