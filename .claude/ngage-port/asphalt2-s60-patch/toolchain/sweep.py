import scan, collections
from capstone import *
img=scan.img
md=Cs(CS_ARCH_ARM, CS_MODE_THUMB); md.detail=False
def sweep(lo=0x8000, hi=None):
    """resilient linear THUMB sweep; returns list of instructions"""
    if hi is None: hi=0x8000+scan.CODE_SIZE
    out=[]; addr=lo
    base=scan.fo(lo); blob=img[base:base+(hi-lo)]
    while addr<hi:
        got=False
        for ins in md.disasm(blob[addr-lo:], addr):
            out.append(ins); addr=ins.address+ins.size; got=True
            if addr>=hi: break
        if not got:
            addr+=2
    return out
