import os, random, struct, sys, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_SP,UC_ARM_REG_LR
CODE=open(os.environ['A4_CODE'],'rb').read(); CB=0x8000
BIN=open(sys.argv[1],'rb').read(); FN=0x400000; A,B,STK,STOP=0x600000,0x610000,0x700000,0x7F0000
syms={}
for l in subprocess.check_output(['llvm-nm',sys.argv[2]],text=True).splitlines():
    p=l.split()
    if len(p)==3: syms[p[2]]=int(p[0],16)
def orig(off,a,s,b):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB);uc.mem_map(0,0x200000);uc.mem_write(CB,CODE)
    for m in (A,B,STK,STOP): uc.mem_map(m,0x1000)
    uc.mem_write(A,struct.pack('<4i',*a));uc.mem_write(B,struct.pack('<4i',*b))
    uc.reg_write(UC_ARM_REG_R0,A)
    if off==0x51db8: uc.reg_write(UC_ARM_REG_R1,s&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R2,B)
    else: uc.reg_write(UC_ARM_REG_R1,B)
    uc.reg_write(UC_ARM_REG_SP,STK+0x800);uc.reg_write(UC_ARM_REG_LR,STOP)
    uc.emu_start(CB+off+1,STOP,count=200)
    return list(struct.unpack('<4i',uc.mem_read(A,16)))
def rw(sym,a,s,b):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);uc.mem_map(FN,0x10000);uc.mem_write(FN,BIN)
    for m in (A,B,STK,STOP): uc.mem_map(m,0x1000)
    uc.mem_write(A,struct.pack('<4i',*a));uc.mem_write(B,struct.pack('<4i',*b))
    uc.reg_write(UC_ARM_REG_R0,A)
    if sym=='a4_vec_mulsub': uc.reg_write(UC_ARM_REG_R1,s&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R2,B)
    else: uc.reg_write(UC_ARM_REG_R1,B)
    uc.reg_write(UC_ARM_REG_SP,STK+0x800);uc.reg_write(UC_ARM_REG_LR,STOP)
    uc.emu_start(FN+syms[sym],STOP,count=200)
    return list(struct.unpack('<4i',uc.mem_read(A,16)))
def main(trials=4000):
    rnd=random.Random(5);bad=0
    for t in range(trials):
        a=[rnd.getrandbits(32)-2**31 for _ in range(4)];b=[rnd.getrandbits(32)-2**31 for _ in range(4)];s=rnd.getrandbits(32)-2**31
        if orig(0x51d96,a,0,b)!=rw('a4_vec_add',a,0,b): bad+=1; (print("ADD mismatch",t) if bad<=3 else 0); continue
        if orig(0x51db8,a,s,b)!=rw('a4_vec_mulsub',a,s,b): bad+=1; (print("MULSUB mismatch",t) if bad<=3 else 0)
    print("%d/%d vec rewrites match (add + mulsub)"%(trials-bad,trials)); return bad==0
if __name__=='__main__': sys.exit(0 if main() else 1)
