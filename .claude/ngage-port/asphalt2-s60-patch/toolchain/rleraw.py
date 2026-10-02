"""Codec for the dashboard blit path (0x1f458): it copies forward in x and y,
so stored row order IS screen row order -- no flip anywhere."""
import struct
def decode(d):
    t,w,h,ds=struct.unpack_from('<HHHH',d,0)
    pn=(len(d)-8-ds)//2
    pal=[struct.unpack_from('<H',d,8+i*2)[0] for i in range(pn)]
    dat=d[8+pn*2:]; px=[]
    for b in dat: px+=([b&63]*((b>>6)+1) if t==2 else [b&15]*((b>>4)+1))
    return t,w,h,pal,px[:w*h]
def encode(t,w,h,pal,px):
    assert len(px)==w*h
    maxrun = 4 if t==2 else 16
    shift  = 6 if t==2 else 4
    out=bytearray(); i=0
    while i<len(px):
        v=px[i]; n=1
        while i+n<len(px) and px[i+n]==v and n<maxrun: n+=1
        out.append(((n-1)<<shift)|v); i+=n
    npal=64 if t==2 else 16
    pal=(list(pal)+[0]*npal)[:npal]
    return struct.pack('<HHHH',t,w,h,len(out))+b''.join(struct.pack('<H',c) for c in pal)+bytes(out)

def decode0(d):
    """type 0: uncompressed 4bpp, 16-colour palette."""
    t,w,h,ds=struct.unpack_from('<HHHH',d,0)
    assert t==0
    pn=(len(d)-8-ds)//2
    pal=[struct.unpack_from('<H',d,8+i*2)[0] for i in range(pn)]
    dat=d[8+pn*2:]
    lo=[]; hi=[]
    for byte in dat:
        lo += [byte&15, byte>>4]
        hi += [byte>>4, byte&15]
    return t,w,h,pal,lo[:w*h],hi[:w*h]
