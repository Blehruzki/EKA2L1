#!/usr/bin/env python3
"""d2.py <hex start> [count] [image] -- disassemble the game image, exactly.

Same offsets as `dis.py` -- into the loaded chunk, which is what a fault pc
minus the image base and the box log both speak in -- but each word is
disassembled on its own and printed against its own address, and branch
targets are worked out here rather than read off llvm-mc.

`dis.py` prints every mnemonic one instruction below its true address, which
is how round 60's first reading put the `User::Leave` call at the wrong site.
Prefer this one.
"""
import sys, struct, subprocess
F=('uid1 uid2 uid3 check sig cpu pad compression petran timeLo timeHi flags '
   'codeSize dataSize heapMin heapMax stackSize bssSize entryPoint codeBase '
   'dataBase dllRefCount exportDirOffset exportDirCount textSize codeOffset '
   'dataOffset importOffset codeRelocOffset dataRelocOffset').split()
raw=open(sys.argv[3] if len(sys.argv)>3 else '/root/.local/share/EKA2L1/data/drives/e/6rbc.app','rb').read()
h=dict(zip(F,struct.unpack_from('<%dI'%len(F),raw,0)))
code=raw[h['codeOffset']:h['codeOffset']+h['codeSize']]
a0=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 24
for i in range(n):
    a=a0+4*i
    v=struct.unpack_from('<I',code,a)[0]
    p=subprocess.run(['llvm-mc','--disassemble','--triple=armv5te'],
        input=' '.join('0x%02x'%b for b in struct.pack('<I',v)),capture_output=True,text=True)
    t=[x.strip() for x in p.stdout.splitlines() if x.strip() and not x.strip().startswith(('.','#'))]
    t=t[0] if t else '?'
    if (v>>25)&7==5:
        off=v&0xffffff
        if off&0x800000: off-=0x1000000
        t='%s%s -> %08x'%('bl' if (v>>24)&1 else 'b', '' if (v>>28)==0xe else '{cc%x}'%(v>>28), a+8+off*4)
    print('%08x  %08x  %s'%(a,v,t))
