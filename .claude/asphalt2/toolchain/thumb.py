import struct
def movs(rd,imm): assert 0<=imm<=255; return 0x2000|(rd<<8)|imm
def adds_i(rd,imm): assert 0<=imm<=255; return 0x3000|(rd<<8)|imm
def lsls(rd,rs,n): return (n<<6)|(rs<<3)|rd
def muls(rd,rm): return 0x4340|(rm<<3)|rd
def str_sp(rd,off): assert off%4==0 and off//4<=255; return 0x9000|(rd<<8)|(off//4)
def ldr_sp(rd,off): assert off%4==0 and off//4<=255; return 0x9800|(rd<<8)|(off//4)
def mov_ip_r0(): return 0x4684
def nop(): return 0x46c0
def bl(frm,to):
    off=to-(frm+4)
    return struct.pack('<HH', 0xF000|((off>>12)&0x7ff), 0xF800|((off>>1)&0x7ff))
def asm(*hws): return b''.join(struct.pack('<H',h) for h in hws)
def asrs(rd,rs,n): return 0x1000|(n<<6)|(rs<<3)|rd
def subs_r(rd,rn,rm): return 0x1A00|(rm<<6)|(rn<<3)|rd
def cmp_r(rn,rm): return 0x4280|(rm<<3)|rn
def mov_r(rd,rm): return (rm<<3)|rd
def bge(frm,to): return 0xDA00|(((to-(frm+4))>>1)&0xff)
def ble(frm,to): return 0xDD00|(((to-(frm+4))>>1)&0xff)
def ldr_r(rd,rn,off): return 0x6800|((off//4)<<6)|(rn<<3)|rd
def ldrb_r(rd,rn,off): return 0x7800|(off<<6)|(rn<<3)|rd
def adds_r(rd,rn,rm): return 0x1800|(rm<<6)|(rn<<3)|rd
def cmp_i(rn,imm): return 0x2800|(rn<<8)|imm
def beq(frm,to): return 0xD000|(((to-(frm+4))>>1)&0xff)
def mov_r2_ip(): return 0x4662
def str_r(rd,rn,off): return 0x6000|((off//4)<<6)|(rn<<3)|rd
def add_sp(rd,off): assert off%4==0; return 0xA800|(rd<<8)|(off//4)
def stmia(rn,regs): return 0xC000|(rn<<8)|sum(1<<r for r in regs)
def mov_lr_r7(): return 0x46BE
def subs_i1(rd,rn): return 0x1E00|(1<<6)|(rn<<3)|rd
def mov_r_lr(rd): return 0x4600|0x40|((14&7)<<3)|rd
