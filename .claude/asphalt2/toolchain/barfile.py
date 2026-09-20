import struct, zlib
class Bar:
    def __init__(s, path):
        b=open(path,'rb').read(); s.path=path
        tsz,dsz=struct.unpack_from('<II',b,0)
        assert 8+tsz+dsz==len(b), (tsz,dsz,len(b))
        o,end=8,8+tsz; base=8+tsz; s.names=[]; s.blobs={}; s.flags={}
        ents=[]
        while o<end:
            e=b.index(b'\0',o); n=b[o:e].decode('latin1'); o=e+1
            raw,=struct.unpack_from('>I',b,o); o+=4
            ents.append([n,raw&0x7fffffff,bool(raw&0x80000000)])
        for i,(n,off,fl) in enumerate(ents):
            nxt=ents[i+1][1] if i+1<len(ents) else dsz
            s.names.append(n); s.flags[n]=fl; s.blobs[n]=b[base+off:base+off+(nxt-off)]
    def get(s,name):
        d=s.blobs[name]
        return zlib.decompress(d[4:]) if s.flags[name] else d
    def put(s,name,data,compress=True):
        s.blobs[name]=struct.pack('>I',len(data))+zlib.compress(data,9) if compress else data
        s.flags[name]=compress
    def save(s,path):
        table=b''; data=[]; cur=0
        for n in s.names:
            d=s.blobs[n]
            table+=n.encode('latin1')+b'\0'+struct.pack('>I',cur|(0x80000000 if s.flags[n] else 0))
            data.append(d); cur+=len(d)
        open(path,'wb').write(struct.pack('<II',len(table),cur)+table+b''.join(data))
