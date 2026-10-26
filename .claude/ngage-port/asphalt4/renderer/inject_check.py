"""inject_check.py <patched.exe> -- exercise the injected image's planted hooks
in place: enter the Thumb veneer at each filler's inner-loop entry, run through
the appended trampoline + rewrite, and require the result + landing to match the
original Thumb loop. This tests the actual injected bytes (veneer, trampoline,
patched return displacement), not just the standalone blob."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import span_harness as H, transp_harness as TH
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import *
d=open(sys.argv[1],'rb').read()
co=struct.unpack_from('<I',d,0x64)[0]; cs=struct.unpack_from('<I',d,0x30)[0]
PCODE=d[co:co+cs]       # patched code section (link base 0x8000)
CB=0x8000

def tex_inplace(light,u,v,ustep,vstep,umask,vmask,vshift,thresh,lightmask,tex,n):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB); uc.mem_map(0,0x200000); uc.mem_write(CB,PCODE)
    for b in (H.STACK,H.TEX,H.DST,H.COV): uc.mem_map(b,0x100000)
    uc.mem_write(H.TEX,b''.join(struct.pack('<H',t) for t in tex))
    SP=H.STACK+0x8000
    def w(o,val): uc.mem_write(SP+o,struct.pack('<I',val&0xFFFFFFFF))
    w(0x14,H.DST+2*n);w(0x18,vstep);w(0x1c,ustep);w(0x20,lightmask);w(0x24,thresh)
    w(0x28,vshift);w(0x2c,vmask);w(0x30,umask);w(0x44,H.TEX)
    uc.reg_write(UC_ARM_REG_SP,SP);uc.reg_write(UC_ARM_REG_R7,light&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R12,u&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_LR,v&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R4,H.DST);uc.reg_write(UC_ARM_REG_R0,H.COV)
    uc.reg_write(UC_ARM_REG_R5,0x55555555);uc.reg_write(UC_ARM_REG_R6,0x66666666)
    uc.emu_start(CB+H.LOOP+1, CB+H.STOP, count=n*600+8000)
    dst=list(struct.unpack('<%dH'%n,uc.mem_read(H.DST,2*n)))
    return dst,uc.reg_read(UC_ARM_REG_R7),uc.reg_read(UC_ARM_REG_R4),uc.reg_read(UC_ARM_REG_R0),\
           uc.reg_read(UC_ARM_REG_R5),uc.reg_read(UC_ARM_REG_R6)

def transp_inplace(u,v,ustep,vstep,umask,vmask,vshift,tex,dst_in,n):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB); uc.mem_map(0,0x200000); uc.mem_write(CB,PCODE)
    for b in (TH.STACK,TH.TEX,TH.DST): uc.mem_map(b,0x100000)
    uc.mem_write(TH.TEX,b''.join(struct.pack('<H',t) for t in tex))
    uc.mem_write(TH.DST,b''.join(struct.pack('<H',x) for x in dst_in))
    SP=TH.STACK+0x8000
    def w(o,val): uc.mem_write(SP+o,struct.pack('<I',val&0xFFFFFFFF))
    w(0x10,vshift);w(0x14,vmask);w(0x18,TH.DST+2*n)
    uc.reg_write(UC_ARM_REG_SP,SP)
    uc.reg_write(UC_ARM_REG_R1,u&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R2,v&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_R6,ustep&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R7,vstep&0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_LR,umask&0xFFFFFFFF);uc.reg_write(UC_ARM_REG_R12,TH.TEX)
    uc.reg_write(UC_ARM_REG_R4,TH.DST)
    uc.emu_start(CB+TH.LOOP+1, CB+TH.STOP, count=n*600+8000)
    return list(struct.unpack('<%dH'%n,uc.mem_read(TH.DST,2*n)))

def main(trials=1500):
    rnd=random.Random(99); bt=bp=0
    for t in range(trials):
        shift=rnd.choice([4,5,6]);tw=1<<shift;th=1<<rnd.choice([4,5,6])
        tex=[rnd.getrandbits(16) for _ in range(tw*th)];n=rnd.randint(1,40)
        a=dict(light=rnd.getrandbits(32),u=rnd.getrandbits(32),v=rnd.getrandbits(32),
               ustep=rnd.getrandbits(20),vstep=rnd.getrandbits(20),umask=tw-1,vmask=th-1,
               vshift=shift,thresh=rnd.randint(1,64),lightmask=rnd.choice([0x1f,0x3f,0x7f]),tex=tex,n=n)
        od,ol=__import__('exit_verify').orig_exit(**a)[:2] if False else (None,None)
        # original textured via span_harness
        od=H.run_original(**a)
        rd,rl,r4,r0,r5,r6=tex_inplace(**a)
        # recompute original exit light with span_model for the light check
        import span_model; ol=span_model.span(**a); # not used; compare pixels + ptrs + preserved
        if rd!=od or r4!=H.DST+2*n or r0!=H.COV+n or r5!=0x55555555 or r6!=0x66666666: bt+=1
        # transparent
        ttex=[0 if rnd.random()<0.25 else rnd.getrandbits(16) for _ in range(tw*th)]
        din=[rnd.getrandbits(16) for _ in range(n)]
        b=dict(u=rnd.getrandbits(32),v=rnd.getrandbits(32),ustep=rnd.getrandbits(20),
               vstep=rnd.getrandbits(20),umask=tw-1,vmask=th-1,vshift=shift,tex=ttex,dst_in=din,n=n)
        if transp_inplace(**b)!=TH.run_original(**b): bp+=1
    print("textured in-image: %d/%d match | transparent in-image: %d/%d match"%(trials-bt,trials,trials-bp,trials))
    return bt==0 and bp==0
if __name__=='__main__': sys.exit(0 if main() else 1)
