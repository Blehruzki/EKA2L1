import sys, struct, capstone, unicorn as U
from unicorn.arm_const import *
base=open(sys.argv[1],'rb').read(); new=open(sys.argv[2],'rb').read()
assert len(base)==len(new)
diff=[i for i in range(len(base)) if base[i]!=new[i]]
words=sorted(set(i&~3 for i in diff))
print('changed words (file offsets):',[hex(x) for x in words])
CODE_OFF,CODE_LEN,BASE=0x7C,0x1615B0,0x10000000
ck=sum(struct.unpack_from('<%dI'%(CODE_LEN//4),new,CODE_OFF))&0xffffffff
print('checksum field %08x computed %08x'%(struct.unpack_from('<I',new,0x18)[0],ck))
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM)
code=new[CODE_OFF:CODE_OFF+CODE_LEN]
for va,n in ((0x100ade90,6),(0x10023510,4)):
    for i in md.disasm(code[va-BASE:va-BASE+4*n],va): print('  %08x: %s %s'%(i.address,i.mnemonic,i.op_str))
# ---- emulation ----
mu=U.Uc(U.UC_ARCH_ARM,U.UC_MODE_ARM)
mu.mem_map(BASE,0x170000); mu.mem_write(BASE,code)
HEAP=0x20000000; mu.mem_map(HEAP,0x100000)
STK=0x30000000; mu.mem_map(STK,0x10000)
GAME=HEAP+0x40000   # fake game object (needs > 0x12400+0x30c)
TM=HEAP+0x80000     # text table
GLOB=HEAP+0x1000
mu.mem_write(GLOB,struct.pack('<I',GLOB+0x10)); mu.mem_write(GLOB+0x10,struct.pack('<I',GAME))
mu.mem_write(GAME+0x1270c,struct.pack('<I',TM))
mu.mem_write(TM+0x728,struct.pack('<I',HEAP+0x90000)); mu.mem_write(HEAP+0x90000,b'CHEATS\0')
mu.mem_write(GAME+0x12334,struct.pack('<I',0)); mu.mem_write(GAME+0x7438,b'\0')
bump=[HEAP+0xA0000]
calls=[]
RET=0x100adea4
def hook(mu,addr,size,ud):
    r=lambda x: mu.reg_read(x)
    if addr==0x100bdbfc:   # global accessor (Dll::Tls wrapper)
        mu.reg_write(UC_ARM_REG_R0,GLOB); mu.reg_write(UC_ARM_REG_PC,r(UC_ARM_REG_LR))
    elif addr==0x10067864: # operator new
        n=r(UC_ARM_REG_R0); p=bump[0]; bump[0]+=(n+7)&~7
        mu.reg_write(UC_ARM_REG_R0,p); mu.reg_write(UC_ARM_REG_PC,r(UC_ARM_REG_LR))
    elif addr==0x100f48f4: # std::string(const char*) ctor -> emulate
        d=r(UC_ARM_REG_R0); src=r(UC_ARM_REG_R1)
        t=bytes(mu.mem_read(src,64)).split(b'\0')[0]; p=bump[0]; bump[0]+=(len(t)+8)&~7
        mu.mem_write(p,t+b'\0'); mu.mem_write(d,struct.pack('<III',p,p+len(t),0))
        mu.reg_write(UC_ARM_REG_R0,d); mu.reg_write(UC_ARM_REG_PC,r(UC_ARM_REG_LR))
    elif addr==0x100cd9f0: # addItem(menu, std::string*, id) -> record
        s=r(UC_ARM_REG_R1); b=struct.unpack('<I',mu.mem_read(s,4))[0]
        txt=bytes(mu.mem_read(b,32)).split(b'\0')[0] if b else b'?'
        calls.append((hex(r(UC_ARM_REG_R0)),txt,r(UC_ARM_REG_R2),hex(r(UC_ARM_REG_SP))))
        mu.reg_write(UC_ARM_REG_R0,0x1234); mu.reg_write(UC_ARM_REG_PC,r(UC_ARM_REG_LR))
    elif addr==RET: mu.emu_stop()
mu.hook_add(U.UC_HOOK_CODE,hook)
MENU=HEAP+0x5000
sp=STK+0x8000
mu.mem_write(sp+0x20,struct.pack('<I',MENU))
EXITSTR=HEAP+0x6000; mu.mem_write(EXITSTR,b'EXIT GAME\0')
STRCELL=sp+0xd4; mu.mem_write(STRCELL,struct.pack('<III',EXITSTR,EXITSTR+9,0))
regs={UC_ARM_REG_SP:sp,UC_ARM_REG_R4:STRCELL,UC_ARM_REG_R5:0x55555555,UC_ARM_REG_R6:MENU,UC_ARM_REG_R7:0x77777777,
      UC_ARM_REG_R8:0x88888888,UC_ARM_REG_R9:0x99999999,UC_ARM_REG_R10:0xAAAAAAAA,UC_ARM_REG_R11:0xBBBBBBBB,UC_ARM_REG_LR:0xDEAD0000}
for k,v in regs.items(): mu.reg_write(k,v)
mu.emu_start(0x100ade94,0,count=5000)
print('addItem calls (menu, text, id, sp):',calls)
ok=mu.reg_read(UC_ARM_REG_PC)==RET and mu.reg_read(UC_ARM_REG_SP)==sp
for k,v in regs.items():
    if k in (UC_ARM_REG_SP,UC_ARM_REG_LR): continue
    if mu.reg_read(k)!=v: ok=False; print('reg clobbered',k)
print('returned to 0x100adea4 with sp/callee-saved intact:',ok)

def run(mp, mode):
    calls.clear()
    mu.mem_write(GAME+0x7438,bytes([mp])); mu.mem_write(GAME+0x12334,struct.pack('<I',mode))
    for k,v in regs.items(): mu.reg_write(k,v)
    mu.emu_start(0x100ade94,0,count=5000)
    return [c[1:3] for c in calls], mu.reg_read(UC_ARM_REG_SP)==sp
print('multiplayer flag=1:',run(1,0))
print('mode 0x14:',run(0,0x14))
print('single-player again:',run(0,0))
