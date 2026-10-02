import struct
img=open('n73_full.img','rb').read()
CODE_OFF,CODE_BASE,CODE_SIZE=156,0x8000,342716
def va(o): return o-CODE_OFF+CODE_BASE
def fo(v): return v-CODE_BASE+CODE_OFF
def ror(v,n): n&=31; return ((v>>n)|(v<<(32-n)))&0xffffffff

def arm_refs():
    """yield (insn_va, kind, target_va) for ADR-style and LDR-literal in ARM mode"""
    for o in range(CODE_OFF, CODE_OFF+CODE_SIZE-4, 4):
        i,=struct.unpack_from('<I',img,o)
        a=va(o)
        if (i & 0x0FFF0000)==0x028F0000:          # ADD Rd, PC, #imm
            imm=ror(i&0xff, 2*((i>>8)&0xf)); yield a,'adr+',(a+8+imm)&0xffffffff,(i>>12)&0xf
        elif (i & 0x0FFF0000)==0x024F0000:        # SUB Rd, PC, #imm
            imm=ror(i&0xff, 2*((i>>8)&0xf)); yield a,'adr-',(a+8-imm)&0xffffffff,(i>>12)&0xf
        elif (i & 0x0F7F0000)==0x059F0000:        # LDR Rd,[PC,#+imm]
            lit=a+8+(i&0xfff); f=fo(lit)
            if 0<=f<=len(img)-4: yield a,'ldr+',struct.unpack_from('<I',img,f)[0],(i>>12)&0xf
        elif (i & 0x0F7F0000)==0x051F0000:        # LDR Rd,[PC,#-imm]
            lit=a+8-(i&0xfff); f=fo(lit)
            if 0<=f<=len(img)-4: yield a,'ldr-',struct.unpack_from('<I',img,f)[0],(i>>12)&0xf

if __name__=='__main__':
    names=['rpm.rle','rpm_gradient.rle','boost-stick.rle','boost-slice.rle','wantedblue.rle','speed_digit.rle']
    tgt={}
    for n in names:
        s=img.find(('textures\\interf\\hud\\'+n+'\x00').encode())
        if s>=0: tgt[va(s)]=n
    print('targets:',{hex(k):v for k,v in tgt.items()})
    refs=list(arm_refs())
    print('ARM pc-relative refs found: %d'%len(refs))
    hit=[(a,k,t,rd) for a,k,t,rd in refs if t in tgt]
    for a,k,t,rd in hit:
        print('  VA 0x%08x  %s r%d -> 0x%08x  %s'%(a,k,rd,t,tgt[t]))
    print('matches:',len(hit))

def thumb_refs():
    for o in range(CODE_OFF, CODE_OFF+CODE_SIZE-2, 2):
        h,=struct.unpack_from('<H',img,o)
        a=va(o)
        if (h & 0xF800)==0xA000:                  # ADD Rd, PC, #imm8*4  (ADR)
            rd=(h>>8)&7; t=((a+4)&~3)+((h&0xff)<<2); yield a,'t_adr',t,rd
        elif (h & 0xF800)==0x4800:                # LDR Rd,[PC,#imm8*4]
            rd=(h>>8)&7; lit=((a+4)&~3)+((h&0xff)<<2); f=fo(lit)
            if 0<=f<=len(img)-4: yield a,'t_ldr',struct.unpack_from('<I',img,f)[0],rd
