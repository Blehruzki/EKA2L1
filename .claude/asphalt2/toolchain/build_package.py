import struct, zlib, hashlib, os, sisrw, barfile, rleraw, compose7, fireboost, sishash
SRC='z_c5e5a984-Asphalt23D_nokiaN76_N93_ML_IGP_v1_0_0_Signed_N73_1/Asphalt23D_nokiaN76_N93_ML_IGP_v1_0_0_Signed_N73.sisx'
DST='Asphalt23D_N73_newhud.sisx'
EXE_IDX, LB_IDX = 0, 5
head, contents, orig = sisrw.load(SRC)
ctrl=[k for k in contents.kids if k.t==sisrw.COMPRESSED][0]
fds=list(sisrw.walk(contents, sisrw.FILEDATA))
def replace(idx,payload):
    comp=fds[idx].kids[0]; alg,=struct.unpack_from('<I',comp.raw,0)
    packed=zlib.compress(payload,9)
    comp.raw=struct.pack('<IQ',alg,len(payload))+packed
    return len(packed),len(payload)

exe=open('asphalt2_full_patched.exe','rb').read()
e_c,e_u=replace(EXE_IDX, exe)

stage=zlib.decompress(fds[LB_IDX].kids[0].raw[12:]); open('_s6.bar','wb').write(stage)
b=barfile.Bar('_s6.bar')
_, empty, goldbar, squares, (eh,gh,sh) = compose7.build()
blank=rleraw.encode(1,1,1,[0xFF0F],[0])
H=r'Textures\interf\hud'
# The km/h | mph label sheet ships as type 0 (raw 4bpp) and, like every font sheet
# in this archive, is stored bottom-up for the font blitter.  The dashboard path
# (0x1f49a) walks rows top-down, so flip it here and re-encode as type 1 RLE.
_t,uw,uh,upal,ulo,_uhi = rleraw.decode0(b.get(H+r'\kmhmph.RLE'))
ulo = [ulo[(uh-1-y)*uw+x] for y in range(uh) for x in range(uw)]
units = rleraw.encode(1,uw,uh,upal,ulo)
# The speed readout's font sheet: swap the narrow 6x7 speed_digit art for the 8x7
# digits.RLE the original HUD uses (the exe patch widens the font object to match).
b.put(H+r'\speed_digit.RLE', b.get(H+r'\digits.RLE'))
# The x1/x2/x3 nitro starbursts only ship in the S60v2 archive.  The S60v3 code
# already loads three boost textures and picks one by index, so dropping them into
# those slots needs no code change -- only a 2x scale to match the 64x64 they expect.
for _i, (_blob, _w, _h) in fireboost.build().items():
    b.put(r'Textures\boost%db.RLE' % _i, _blob)
for n,d in ((H+r'\rpm.RLE',empty), (H+r'\boost-slice.RLE',goldbar), (H+r'\larrow.RLE',squares),
            (H+r'\rpm_gradient.RLE',units), (H+r'\boost-stick.RLE',blank)):
    b.put(n,d)
b.save('_o6.bar'); bar=open('_o6.bar','rb').read()
b_c,b_u=replace(LB_IDX, bar)

cbuf=bytearray(zlib.decompress(ctrl.raw[12:]))
CONT={2,12,13,14,15,16,17,18,19,20,21,22,23,24,26,27,28,29,30,31,32,33,36,38,39,40}
found=[]
def walk(buf,off,end):
    while off+8<=end:
        t,l=struct.unpack_from('<II',buf,off); off+=8
        if l==0xFFFFFFFF: l,=struct.unpack_from('<Q',buf,off); off+=8
        if l>end-off: return
        if t==24: found.append((off,off+l))
        if t==2:
            e,=struct.unpack_from('<I',buf,off); o=off+4
            while o+4<=off+l:
                el,=struct.unpack_from('<I',buf,o); o+=4
                if el>off+l-o: break
                if e==24: found.append((o,o+el))
                elif e in CONT: walk(buf,o,o+el)
                o+=el+(-el%4)
        elif t in CONT: walk(buf,off,off+l)
        off+=l+(-l%4)
walk(cbuf,0,len(cbuf))
for idx,(c,u) in ((EXE_IDX,(e_c,e_u)),(LB_IDX,(b_c,b_u))):
    struct.pack_into('<QQ',cbuf,found[idx][1]-28+8,c,u)

# Each SISFileDescription carries a SHA-1 of the file's UNCOMPRESSED bytes, and a real
# device re-hashes what it extracts and compares.  EKA2L1 never checks it, so a stale
# hash installs cleanly in the emulator and blows up on hardware.  Rewrite both.
_des = sishash.find_filedes(cbuf)
assert len(_des) == len(found), 'file-description walk disagrees (%d vs %d)' % (len(_des), len(found))
for _idx, _payload in ((EXE_IDX, exe), (LB_IDX, bar)):
    _off, _len = sishash.hash_slice(cbuf, _des[_idx][0])
    _new = hashlib.sha1(_payload).digest()
    assert _len == len(_new), 'hash field is %d bytes, SHA-1 is %d' % (_len, len(_new))
    cbuf[_off:_off + _len] = _new
cbuf,_removed = sishash.strip_signature(cbuf)
print('signature block stripped: %d bytes, controller now %d'%(_removed,len(cbuf)))
ctrl.raw=struct.pack('<IQ',1,len(cbuf))+zlib.compress(bytes(cbuf),9)
sisrw.save(DST,head,contents)
print('units %dx%d (%d bytes) ; '%(uw,uh,len(units)), end='')
print('layers %dx%d / %dx%d / %dx%d ; light.bar %d -> %d ; wrote %s (%d bytes)'%(
      240,eh,240,gh,240,sh,len(stage),b_u,DST,os.path.getsize(DST)))
