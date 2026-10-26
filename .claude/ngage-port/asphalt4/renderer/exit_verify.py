"""exit_verify.py <rewrite.bin> -- verify the textured rewrite reproduces not
just the span pixels but the EXIT light accumulator (r7 at the shipped loop's
exit +3361C), which the function saves and reuses across spans/polygons. Runs
the original Thumb loop capturing r7 at STOP, runs the ARM rewrite capturing its
return value, and requires both the DST buffer and the final light to match."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import span_harness as H
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
                               UC_ARM_REG_R4,UC_ARM_REG_R7,UC_ARM_REG_R12,UC_ARM_REG_LR,UC_ARM_REG_SP)
BIN=open(sys.argv[1],'rb').read()
FN,STK,TEX,DST,COV,RET=0x400000,0x1000000,0x2000000,0x3000000,0x4000000,0x5000000

def orig_exit(light,u,v,ustep,vstep,umask,vmask,vshift,thresh,lightmask,tex,n):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB); uc.mem_map(0,0x200000); uc.mem_write(H.CODEBASE,H.CODE)
    for b in (H.STACK,H.TEX,H.DST,H.COV): uc.mem_map(b,0x100000)
    uc.mem_write(H.TEX,b''.join(struct.pack('<H',t) for t in tex))
    SP=H.STACK+0x8000
    def w(o,val): uc.mem_write(SP+o,struct.pack('<I',val&0xFFFFFFFF))
    w(0x14,H.DST+2*n); w(0x18,vstep); w(0x1c,ustep); w(0x20,lightmask); w(0x24,thresh)
    w(0x28,vshift); w(0x2c,vmask); w(0x30,umask); w(0x44,H.TEX)
    uc.reg_write(UC_ARM_REG_SP,SP); uc.reg_write(UC_ARM_REG_R7,light&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R12,u&0xFFFFFFFF); uc.reg_write(UC_ARM_REG_LR,v&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R4,H.DST); uc.reg_write(UC_ARM_REG_R0,H.COV)
    uc.emu_start(H.CODEBASE+H.LOOP+1,H.CODEBASE+H.STOP,count=n*200+1000)
    dst=list(struct.unpack('<%dH'%n,uc.mem_read(H.DST,2*n)))
    return dst, uc.reg_read(UC_ARM_REG_R7), uc.reg_read(UC_ARM_REG_R4), uc.reg_read(UC_ARM_REG_R0)

def rewrite_exit(light,u,v,ustep,vstep,umask,vmask,vshift,thresh,lightmask,tex,n):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for b in (FN,STK,TEX,DST,COV): uc.mem_map(b,0x100000)
    uc.mem_map(RET&~0xFFF,0x1000); uc.mem_write(FN,BIN)
    uc.mem_write(TEX,b''.join(struct.pack('<H',t) for t in tex))
    sp=STK+0x8000
    for i,a in enumerate([v,ustep,vstep,umask,vmask,vshift,TEX,light,thresh,lightmask]):
        uc.mem_write(sp+4*i,struct.pack('<I',a&0xFFFFFFFF))
    uc.reg_write(UC_ARM_REG_R0,DST); uc.reg_write(UC_ARM_REG_R1,COV); uc.reg_write(UC_ARM_REG_R2,n)
    uc.reg_write(UC_ARM_REG_R3,u&0xFFFFFFFF); uc.reg_write(UC_ARM_REG_SP,sp); uc.reg_write(UC_ARM_REG_LR,RET)
    uc.emu_start(FN,RET,count=n*400+2000)
    dst=list(struct.unpack('<%dH'%n,uc.mem_read(DST,2*n)))
    return dst, uc.reg_read(UC_ARM_REG_R0)  # AAPCS return = final light

def main(trials=2000):
    rnd=random.Random(11); bad=0
    for t in range(trials):
        shift=rnd.choice([4,5,6]); tw=1<<shift; th=1<<rnd.choice([4,5,6])
        tex=[rnd.getrandbits(16) for _ in range(tw*th)]
        n=rnd.randint(1,40)
        a=dict(light=rnd.getrandbits(32),u=rnd.getrandbits(32),v=rnd.getrandbits(32),
               ustep=rnd.getrandbits(20),vstep=rnd.getrandbits(20),umask=tw-1,vmask=th-1,
               vshift=shift,thresh=rnd.randint(1,64),lightmask=rnd.choice([0x1f,0x3f,0x7f]),tex=tex,n=n)
        od,olight,or4,or0=orig_exit(**a); rd,rlight=rewrite_exit(**a)
        exp_r4=H.DST+2*n; exp_r0=H.COV+n
        if od!=rd or olight!=rlight or or4!=exp_r4 or or0!=exp_r0:
            bad+=1
            if bad<=3: print("MISMATCH t=%d n=%d dst=%s light orig=%08x rw=%08x r4=%08x/%08x r0=%08x/%08x"%(
                t,n,od==rd,olight,rlight,or4,exp_r4,or0,exp_r0))
    print("%d/%d match (pixels + exit light + r4/r0 advance)"%(trials-bad,trials))
    return bad==0
if __name__=='__main__': sys.exit(0 if main() else 1)
