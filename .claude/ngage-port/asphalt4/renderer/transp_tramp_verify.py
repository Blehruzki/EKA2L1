"""transp_tramp_verify.py <tramp.bin> <link_hex> <tramp_off_hex> <ph_off_hex> --
drive the transparent inner-loop trampoline vs the original Thumb loop; require
the DST buffer (including skipped/transparent pixels left as-is) to match, and
sp restored (so the epilogue pops correctly)."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import transp_harness as TH
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R4,UC_ARM_REG_R6,
                               UC_ARM_REG_R7,UC_ARM_REG_R12,UC_ARM_REG_LR,UC_ARM_REG_SP)
BIN=bytearray(open(sys.argv[1],'rb').read())
LINK=int(sys.argv[2],16);TRAMP=int(sys.argv[3],16);PH=int(sys.argv[4],16)
BLOB,FPB,TEX,DST,STOP=LINK,0x1000000,0x2000000,0x3000000,0x900000
struct.pack_into('<I',BIN,PH,(STOP-(LINK+PH))&0xFFFFFFFF)

def tramp(u,v,ustep,vstep,umask,vmask,vshift,tex,dst_in,n):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for b in (BLOB,FPB,TEX,DST): uc.mem_map(b,0x100000)
    uc.mem_map(STOP,0x1000); uc.mem_write(BLOB,bytes(BIN))
    uc.mem_write(TEX,b''.join(struct.pack('<H',t) for t in tex))
    uc.mem_write(DST,b''.join(struct.pack('<H',d) for d in dst_in))
    FP=FPB+0x8000
    def w(o,val): uc.mem_write(FP+o,struct.pack('<I',val&0xFFFFFFFF))
    w(0x10,vshift);w(0x14,vmask);w(0x18,DST+2*n)
    uc.reg_write(UC_ARM_REG_SP,FP)
    uc.reg_write(UC_ARM_REG_R1,u&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R2,v&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R6,ustep&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R7,vstep&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_LR,umask&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R12,TEX)
    uc.reg_write(UC_ARM_REG_R4,DST)
    uc.emu_start(BLOB+TRAMP,STOP,count=n*400+5000)
    dst=list(struct.unpack('<%dH'%n,uc.mem_read(DST,2*n)))
    return dst, uc.reg_read(UC_ARM_REG_SP)

def main(trials=2000):
    rnd=random.Random(21);bad=0
    for t in range(trials):
        shift=rnd.choice([4,5,6]);tw=1<<shift;th=1<<rnd.choice([4,5,6])
        tex=[0 if rnd.random()<0.25 else rnd.getrandbits(16) for _ in range(tw*th)]
        n=rnd.randint(1,40); dst_in=[rnd.getrandbits(16) for _ in range(n)]
        a=dict(u=rnd.getrandbits(32),v=rnd.getrandbits(32),ustep=rnd.getrandbits(20),
               vstep=rnd.getrandbits(20),umask=tw-1,vmask=th-1,vshift=shift,tex=tex,dst_in=dst_in,n=n)
        od=TH.run_original(**a); rd,sp=tramp(**a)
        if od!=rd or sp!=FPB+0x8000:
            bad+=1
            if bad<=3: print("MISMATCH t=%d n=%d dsteq=%s sp=%08x/%08x"%(t,n,od==rd,sp,FPB+0x8000))
    print("%d/%d transparent trampoline runs match (pixels incl skips + sp restored)"%(trials-bad,trials))
    return bad==0
if __name__=='__main__': sys.exit(0 if main() else 1)
