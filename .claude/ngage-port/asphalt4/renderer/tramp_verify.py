"""tramp_verify.py <tramp.bin> <link_hex> <tramp_sym_off> <placeholder_off> --
drive the textured inner-loop TRAMPOLINE (which calls the verified rewrite and
restores the exit state) under unicorn, from the shipped loop's live-register
entry state, and require DST + COV + exit light (r7) + end pointers + preserved
r5/r6 to match the original Thumb loop. Proves the trampoline is a correct
mid-function drop-in, not just that the span pixels match."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import span_harness as H
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,
                               UC_ARM_REG_R7,UC_ARM_REG_R12,UC_ARM_REG_LR,UC_ARM_REG_SP,UC_ARM_REG_PC)
BIN=bytearray(open(sys.argv[1],'rb').read())
LINK=int(sys.argv[2],16); TRAMP=int(sys.argv[3],16); PH=int(sys.argv[4],16)
BLOB,FP_BASE,TEX,DST,COV,STOP=LINK,0x1000000,0x2000000,0x3000000,0x4000000,0x900000
# patch placeholder: r12 = (PH_link) + disp ; want r12 = STOP (ARM, bit0=0)
disp=(STOP-(LINK+PH))&0xFFFFFFFF
struct.pack_into('<I',BIN,PH,disp)

# original loop reference (from exit_verify logic)
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec=importlib.util.spec_from_file_location("ev",os.path.join(os.path.dirname(os.path.abspath(__file__)),"exit_verify.py"))

def orig(light,u,v,ustep,vstep,umask,vmask,vshift,thresh,lightmask,tex,n):
    from unicorn import UC_MODE_THUMB
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB); uc.mem_map(0,0x200000); uc.mem_write(H.CODEBASE,H.CODE)
    for b in (H.STACK,H.TEX,H.DST,H.COV): uc.mem_map(b,0x100000)
    uc.mem_write(H.TEX,b''.join(struct.pack('<H',t) for t in tex))
    SP=H.STACK+0x8000
    def w(o,val): uc.mem_write(SP+o,struct.pack('<I',val&0xFFFFFFFF))
    w(0x14,H.DST+2*n);w(0x18,vstep);w(0x1c,ustep);w(0x20,lightmask);w(0x24,thresh)
    w(0x28,vshift);w(0x2c,vmask);w(0x30,umask);w(0x44,H.TEX)
    uc.reg_write(UC_ARM_REG_SP,SP);uc.reg_write(UC_ARM_REG_R7,light&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R12,u&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_LR,v&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R4,H.DST);uc.reg_write(UC_ARM_REG_R0,H.COV)
    uc.emu_start(H.CODEBASE+H.LOOP+1,H.CODEBASE+H.STOP,count=n*200+1000)
    return list(struct.unpack('<%dH'%n,uc.mem_read(H.DST,2*n))), uc.reg_read(UC_ARM_REG_R7)

def tramp(light,u,v,ustep,vstep,umask,vmask,vshift,thresh,lightmask,tex,n):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for b in (BLOB,FP_BASE,TEX,DST,COV): uc.mem_map(b,0x100000)
    uc.mem_map(STOP,0x1000)
    uc.mem_write(BLOB,bytes(BIN))
    uc.mem_write(TEX,b''.join(struct.pack('<H',t) for t in tex))
    FP=FP_BASE+0x8000
    def w(o,val): uc.mem_write(FP+o,struct.pack('<I',val&0xFFFFFFFF))
    w(0x14,DST+2*n);w(0x18,vstep);w(0x1c,ustep);w(0x20,lightmask);w(0x24,thresh)
    w(0x28,vshift);w(0x2c,vmask);w(0x30,umask);w(0x44,TEX)
    uc.reg_write(UC_ARM_REG_SP,FP);uc.reg_write(UC_ARM_REG_R7,light&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R12,u&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_LR,v&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R4,DST);uc.reg_write(UC_ARM_REG_R0,COV)
    uc.reg_write(UC_ARM_REG_R5,0x55555555);uc.reg_write(UC_ARM_REG_R6,0x66666666)
    uc.emu_start(BLOB+TRAMP,STOP,count=n*400+5000)
    dst=list(struct.unpack('<%dH'%n,uc.mem_read(DST,2*n)))
    cov=uc.mem_read(COV,n)
    return dst,uc.reg_read(UC_ARM_REG_R7),uc.reg_read(UC_ARM_REG_R4),uc.reg_read(UC_ARM_REG_R0),\
           uc.reg_read(UC_ARM_REG_R5),uc.reg_read(UC_ARM_REG_R6),cov

def main(trials=2000):
    rnd=random.Random(13);bad=0
    for t in range(trials):
        shift=rnd.choice([4,5,6]);tw=1<<shift;th=1<<rnd.choice([4,5,6])
        tex=[rnd.getrandbits(16) for _ in range(tw*th)];n=rnd.randint(1,40)
        a=dict(light=rnd.getrandbits(32),u=rnd.getrandbits(32),v=rnd.getrandbits(32),
               ustep=rnd.getrandbits(20),vstep=rnd.getrandbits(20),umask=tw-1,vmask=th-1,
               vshift=shift,thresh=rnd.randint(1,64),lightmask=rnd.choice([0x1f,0x3f,0x7f]),tex=tex,n=n)
        od,ol=orig(**a); rd,rl,r4,r0,r5,r6,cov=tramp(**a)
        ok=(od==rd and ol==rl and r4==DST+2*n and r0==COV+n and r5==0x55555555 and r6==0x66666666 and set(cov)<= {0})
        if not ok:
            bad+=1
            if bad<=3: print("MISMATCH t=%d n=%d dsteq=%s light=%08x/%08x r4=%08x/%08x r0=%08x/%08x r5=%08x r6=%08x cov0=%s"%(
                t,n,od==rd,ol,rl,r4,DST+2*n,r0,COV+n,r5,r6,set(cov)<={0}))
    print("%d/%d trampoline runs match (pixels+light+ptrs+r5/r6+cov)"%(trials-bad,trials))
    return bad==0
if __name__=='__main__': sys.exit(0 if main() else 1)
