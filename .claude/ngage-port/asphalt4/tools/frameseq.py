#!/usr/bin/env python3
# frameseq.py <trace.regs> [hold-pc [fwpost-tramp-pc]] (hex): the register ring (EKA2L1_PCTRACE_REGS + _REGRING + _DUMPAT)
# written when A4_DROPHOLD held a frame (a4_drop_mark), as a sequence of named events.
# Round 37: who renders and who posts around the empty frame. PCs for the bench's RM-320
# (code at 0x70000000); the last entry is a4_drop_mark's own (its address per build).
import sys, struct
NAMES = {
    0x70000606: 'FW frame   (+606 the framework frame routine)',
    0x70000662: 'FW state   (+662 state != 0x1E: r0 = state)',
    0x70000672: 'FW draw    (+672 the game draw, vtbl 0x1C)',
    0x7000067A: 'FW drawn   (+67A)',
    0x700006A8: 'FW byteA   (+6A8 r0 = "the game presented" byte)',
    0x700006AC: 'FW POST    (+6AC the framework posts the screen)',
    0x700006B6: 'FW end     (+6B6)',
    0x700001EA: 'DP present (+1EA DoPresent makes its present call)',
    0x700009AA: 'race pres  (+9A6 returned: the new frame)',
    0x70000954: 'race rep1  (+950 returned)',
    0x70000982: 'race rep2  (+97E returned)',
    0x70012992: 'RENDER     (+12992)',
    0x7001670E: 'race upd   (+1670E)',
}
hold = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
if len(sys.argv) > 3: NAMES[int(sys.argv[3], 16)] = 'FW post?   (a4_fwpost_tramp: the framework post asked)'
d = open(sys.argv[1], 'rb').read()
n = len(d) // 68
# the ring is written at the first hold; the trace then goes on into the file (up to 200k)
k = next((i for i in range(n) if struct.unpack_from('<I', d, i * 68)[0] == hold), n - 1)
for i in range(max(0, k - int(sys.argv[4] if len(sys.argv) > 4 else 36)), min(n, k + 40)):
    e = struct.unpack_from('<17I', d, i * 68)
    pc, r = e[0], e[1:]
    name = 'HOLD       (a4_drop_mark: r0 had %d, r1 prev %d)' % (r[0], r[1]) if pc == hold else NAMES.get(pc, '0x%08X' % pc)
    print('%4d  %-52s r0=%08x r1=%08x r4=%08x lr=%08x' % (i - k, name, r[0], r[1], r[4], r[14]))
