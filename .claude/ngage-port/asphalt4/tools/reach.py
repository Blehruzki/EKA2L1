# reach.py <code.bin> <root>[:<stop>] ... : every function the given code can reach by BL, statically, with
# what each one writes. A root is a hex start, optionally with the address its flow stops at (a part of a
# function: 14672:14CB2 is +141B0's car loop). Flow is followed the way lift.py follows it (branches,
# Thumb byte jump tables), a function ending at a return; BLs inside the function's own body to a label
# without a prologue are its local subroutines, followed as part of it. For each function: its size, its
# callees, its stores whose base is not sp (the state it changes), and its indirect calls (blx rX,
# mov pc) -- calls this cannot follow, listed so they are looked at by hand.
import sys, struct, capstone
from capstone import arm as A
code = open(sys.argv[1], 'rb').read()
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB); md.detail = True
BLT = set()
for o in range(0, len(code) - 3, 2):
    h1, h2 = struct.unpack_from('<HH', code, o)
    if (h1 & 0xF800) == 0xF000 and (h2 & 0xF800) == 0xF800:
        off = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
        if off & (1 << 22): off -= 1 << 23
        BLT.add(o + 4 + off)
cache = {}
def ins(a):
    if a not in cache:
        cache[a] = next(md.disasm(code[a:a + 4], a, count=1), None)
    return cache[a]
def isbr(i): return i.mnemonic == 'b' or (i.mnemonic.startswith('b') and len(i.mnemonic) == 3 and i.mnemonic not in ('bic', 'blx'))
def walk(start, stop=None):
    nextpush = start + 2
    while nextpush < len(code) - 1 and (struct.unpack_from('<H', code, nextpush)[0] & 0xFF00) != 0xB500: nextpush += 2
    def local(t): return start < t < nextpush and (struct.unpack_from('<H', code, t)[0] & 0xFF00) != 0xB500
    reach, work, calls, stores, ind = set(), [start], set(), [], []
    while work:
        a = work.pop()
        while a not in reach and (stop is None or a != stop):
            i = ins(a)
            if i is None: break
            reach.add(a); m = i.mnemonic
            if m == 'add' and i.op_str.startswith('pc,'):
                back = [ins(a - 2 * k) for k in range(1, 12) if ins(a - 2 * k)]
                n = next((b.operands[1].imm for b in back if b.mnemonic == 'cmp' and b.operands[1].type == A.ARM_OP_IMM), None)
                tb = next((((b.address + 4) & ~3) + b.operands[1].imm for b in back if b.mnemonic == 'adr'), None)
                if n is None or tb is None: ind.append('%06x jump through pc' % a); break
                work += [a + 4 + 2 * code[tb + k] for k in range(n)]; break
            if m == 'bl' and i.operands[0].type == A.ARM_OP_IMM:
                t = i.operands[0].imm
                if local(t): work.append(t)
                else: calls.add(t)
            if m == 'blx' or (m == 'mov' and i.op_str.startswith('pc')) or (m == 'bx' and 'lr' not in i.op_str):
                ind.append('%06x %s %s' % (a, m, i.op_str))
            if m.startswith('str') or m.startswith('stm') or m == 'push':
                if m != 'push':
                    base = i.reg_name(i.operands[1].mem.base) if m.startswith('str') else i.reg_name(i.operands[0].reg)
                    if base != 'sp': stores.append('%06x %s %s' % (a, m, i.op_str))
            if isbr(i):
                t = i.operands[0].imm
                if t not in BLT or local(t) or t == start: work.append(t)
                if m == 'b': break
            if m == 'bx' or (m == 'pop' and 'pc' in i.op_str) or (m == 'mov' and i.op_str.startswith('pc')): break
            a += i.size
    return reach, calls, stores, ind
seen, order, q = {}, [], []
for r in sys.argv[2:]:
    s, _, e = r.partition(':')
    q.append((int(s, 16), int(e, 16) if e else None))
while q:
    s, e = q.pop(0)
    if s in seen: continue
    reach, calls, stores, ind = walk(s, e)
    seen[s] = (reach, calls, stores, ind); order.append(s)
    q += [(c, None) for c in sorted(calls) if c not in seen]
for s in order:
    reach, calls, stores, ind = seen[s]
    print('+%05X  %4d ins  calls: %s' % (s, len(reach), ' '.join('+%X' % c for c in sorted(calls)) or '-'))
    for x in stores: print('      st  ' + x)
    for x in ind: print('      ind ' + x)
