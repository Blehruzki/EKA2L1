# turnfit.py: the player's next-step turn fitted from what is known at the step (bench recordings)
import sys, struct, numpy as np
s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
wrap = lambda a: ((a + 1024) % 2048) - 1024
rows=[]
for fn in sys.argv[1:]:
    d=open(fn,'rb').read(); o=0; cur=None; recs=[]
    while o+12<=len(d):
        pc,addr,ln=struct.unpack_from('<3I',d,o); o+=12; b=d[o:o+ln]; o+=ln
        if ln==0x20: cur=[b]; recs.append(cur)
        elif cur is not None: cur.append(b)
    st={}
    for r in recs:
        h=struct.unpack_from('<8I',r[0])
        if len(r)<3 or h[0]!=0x444B5246 or not (h[2]>>31) or ((h[2]>>24)&0x7F)!=0: continue
        g=lambda off: s32(struct.unpack_from('<I',r[1],off)[0])
        st[h[1]]=dict(h=s32(h[7])&2047, s=g(0x7FC), b8=g(0xB8), c0=g(0xC0), c4=g(0xC4), cc=g(0xCC), w81c=g(0x81C), a7c=g(0x9F8))
    for k in st:
        if all(j in st for j in (k-2,k-1,k+1)):
            t=wrap(st[k]['h']-st[k-1]['h']); tp=wrap(st[k-1]['h']-st[k-2]['h']); tn=wrap(st[k+1]['h']-st[k]['h'])
            if max(abs(t),abs(tp),abs(tn))>=128: continue
            rows.append((tn, t, tp, st[k]['s'], st[k-1]['s'], st[k]['b8'], st[k]['c0'], st[k]['c4'], st[k]['cc'], st[k]['w81c']))
A=np.array(rows, dtype=float); y=A[:,0]; X=A[:,1:]
names=['t','tp','s','sp','b8','c0','c4','cc','81c']
def ev(pred, name):
    e=np.abs(pred-y); over=np.sum((np.abs(pred)>=15)&(pred*y<=0)); lag=np.sum((np.abs(y)>=15)&(np.abs(pred-y)>=15)&(pred*y>=0)&(np.abs(pred)<np.abs(y)))
    print('%-34s mean %.2f 99%% %.0f over %d lag %d' % (name, e.mean(), np.quantile(e,.99), over, lag))
print(len(y), 'steps')
ev(X[:,0], 'base')
t, tp, s, sp = X[:,0], X[:,1], X[:,2], X[:,3]
ev(np.where(np.abs(s)<np.abs(sp), 0, t), 'ease0')
ev(np.where((np.abs(s)<np.abs(sp)) | (t*s<0), 0, t), 'ease0 + against steering')
ev(np.where((np.abs(s)<np.abs(sp)) | (t*s<0) & (np.abs(s)>=1024), 0, t), 'ease0 + against steering>=1024')
for cols in ([0,2],[0,2,3],[0,2,3,4,5],list(range(9))):
    M=np.c_[X[:,cols], np.ones(len(X))]
    c,*_=np.linalg.lstsq(M,y,rcond=None)
    ev(np.round(M@c), 'lin '+','.join(names[i] for i in cols))
    print('   coef', ' '.join('%s=%.4g'%(names[i],c[j]) for j,i in enumerate(cols)))
print('turn against the steering (t*s<0, |t|>=8):')
import collections
B=collections.defaultdict(lambda: [0,0,0])
for tn_, t_, tp_, s_, sp_ in zip(y, t, tp, s, sp):
    if t_*s_ < 0 and abs(t_) >= 8:
        key=(min(4, int(abs(s_))//1024), 'grow' if abs(s_)>abs(sp_) else 'ease' if abs(s_)<abs(sp_) else 'hold', 'same' if sp_*s_>0 else 'flip' if sp_*s_<0 else 'from0')
        B[key][0 if tn_*t_>0 else 1 if tn_==0 else 2]+=1
for k in sorted(B): print('  |s|>=%d*1024 %-4s %-5s  next turn same way %3d, none %3d, other way %3d' % (k+tuple(B[k])))
kick = (t*tp < 0) & (t*s <= 0)
ease = np.abs(s) < np.abs(sp)
rev = (t*s < 0) & (np.abs(s) >= 1024) & (np.abs(s) > np.abs(sp)) & (s*sp > 0)
ev(np.where(ease | kick, 0, t), 'r58a: ease, kick')
ev(np.where(ease | kick | rev, 0, t), 'r58b: ease, kick, reversing steer')
pa = np.where(ease | kick, 0, t); pb = np.where(ease | kick | rev, 0, t)
lagb = (np.abs(y)>=15)&(np.abs(pb-y)>=15)&(pb*y>=0)&(np.abs(pb)<np.abs(y)); laga = (np.abs(y)>=15)&(np.abs(pa-y)>=15)&(pa*y>=0)&(np.abs(pa)<np.abs(y))
idx = np.where(lagb & ~laga)[0]
print(len(idx), 'new lags; first:', [(int(y[i]), int(t[i]), int(tp[i]), int(s[i]), int(sp[i])) for i in idx[:12]])
