# atlasim.py ldump: what a texture atlas would do to the list's draws (rule A grouping, keys replaced)
import struct, numpy as np, collections, sys
exec(open('/home/user/EKA2L1/.claude/ngage-port/asphalt4/tools/listcmp.py').read().replace('\nmain()\n','\n'))
fs=load(sys.argv[1]); tot=collections.Counter(); nfr=0; areas=[]; sizes=collections.Counter()
for h,V,T,Kb,Zb,ext in fs:
    _,n,F,cx,cy,zbits,frame,nk=h
    if n*36>len(V) or len(ext)<256: continue
    lw=np.frombuffer(ext[:128],np.uint8); lh=np.frombuffer(ext[128:256],np.uint8)
    Pv=np.frombuffer(V[:n*36],dtype='<i2').reshape(n,18).astype(np.int64)
    K=np.frombuffer(Kb[:n],np.uint8).astype(np.int64)
    Tw=struct.unpack_from('<%dI'%n,T,0); Bx=[struct.unpack_from('<4h',T,0x2000+8*i) for i in range(n)]
    ORD=struct.unpack_from('<%dH'%n,T,0x7000); Zz=[struct.unpack_from('<2H',Zb,4*i) for i in range(n)]
    u=Pv[:,[3,9,15]]; v=Pv[:,[4,10,16]]
    Su=16*(1<<lw[K].astype(np.int64)); Sv=16*(1<<lh[K].astype(np.int64))
    def fitm(M): # the face's span, shifted by whole tiles, within M tiles from a tile's start
        return ((u.max(1)-1 - np.floor_divide(u.min(1),Su)*Su) < M*Su) & ((v.max(1)-1 - np.floor_divide(v.min(1),Sv)*Sv) < M*Sv)
    fit=fitm(1); fit2=fitm(2); fit3=fitm(3)
    areas.append(sum((1<<int(lw[k]))*(1<<int(lh[k])) for k in range(nk)))
    for k in range(nk): sizes[(1<<int(lw[k]), 1<<int(lh[k]))]+=1
    tot['faces']+=n; tot['fit']+=int(fit.sum()); tot['fit2']+=int(fit2.sum()); tot['fit3']+=int(fit3.sum())
    # keys: a fitting face -> key 200 (one atlas page); others keep their own
    for name, Kn in (('now', K), ('atlas', np.where(fit, 200, K)), ('atlas2', np.where(fit2, 200, K)), ('atlas3', np.where(fit3, 200, K)), ('all', np.full(n, 200))):
        Kb2=bytes(Kn.astype(np.uint8).tolist())
        bt=group(n,'A',Kb2,Tw,Bx,ORD,Zz,zbits>=24)
        tot[name]+=sum(len(r) for r in bt)
    nfr+=1
print('%d frames: faces fitting 1 / 2x2 / 3x3 tiles of their texture %.1f%% / %.2f%% / %.2f%%; list draws a frame now %.1f, atlas (1) %.1f, (2x2) %.1f, (3x3) %.1f, every face %.1f'
      % (nfr, 100*tot['fit']/tot['faces'], 100*tot['fit2']/tot['faces'], 100*tot['fit3']/tot['faces'], tot['now']/nfr, tot['atlas']/nfr, tot['atlas2']/nfr, tot['atlas3']/nfr, tot['all']/nfr))
print('texel area of a frame\'s list textures: median %d, max %d (a 1024x1024 page: 1048576)' % (int(np.median(areas)), max(areas)))
print('texture sizes (keys over frames):', sorted(sizes.items(), key=lambda x:-x[1])[:12])
