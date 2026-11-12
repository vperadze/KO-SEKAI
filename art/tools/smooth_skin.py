"""Widen the skin-weight blend zones of a rigged GLB by Laplacian smoothing, writing a new GLB.
usage: smooth_skin.py in.glb out.glb [iterations]"""
import json,struct,sys,numpy as np
src,dst=sys.argv[1],sys.argv[2]; ITER=int(sys.argv[3]) if len(sys.argv)>3 else 30
raw=open(src,'rb').read()
n,_=struct.unpack('<I4s',raw[12:20]); j=json.loads(raw[20:20+n]); bo=20+n; bl,_=struct.unpack('<I4s',raw[bo:bo+8]); B=bytearray(raw[bo+8:bo+8+bl])
CT={5121:np.uint8,5123:np.uint16,5125:np.uint32,5126:np.float32}; NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}
def loc(i):
    a=j['accessors'][i]; v=j['bufferViews'][a['bufferView']]; c=NC[a['type']]; dt=CT[a['componentType']]
    st=v.get('byteStride'); assert not st or st==np.dtype(dt).itemsize*c
    return v.get('byteOffset',0)+a.get('byteOffset',0), dt, a['count'], c
def acc(i):
    o,dt,cnt,c=loc(i); return np.frombuffer(bytes(B),dtype=dt,count=cnt*c,offset=o).reshape(-1,c)
total=0
for mesh in j['meshes']:
    for pr in mesh['primitives']:
        A=pr['attributes']
        if 'JOINTS_0' not in A: continue
        P=acc(A['POSITION']); J=acc(A['JOINTS_0']); W=acc(A['WEIGHTS_0']).astype(np.float64); I=acc(pr['indices']).reshape(-1,3)
        nj=int(J.max())+1; nv=len(P)
        D=np.zeros((nv,nj))
        for k in range(4): np.add.at(D,(np.arange(nv),J[:,k]),W[:,k])
        D/=np.maximum(D.sum(1,keepdims=True),1e-9)
        key=np.round(P/(np.ptp(P,0).max()*1e-5)).astype(np.int64); _,inv=np.unique(key,axis=0,return_inverse=True); inv=inv.ravel(); nw=inv.max()+1
        Wd=np.zeros((nw,nj)); cnt=np.zeros(nw); np.add.at(Wd,inv,D); np.add.at(cnt,inv,1); Wd/=cnt[:,None]
        E=np.unique(np.sort(np.concatenate([inv[I[:,[0,1]]],inv[I[:,[1,2]]],inv[I[:,[2,0]]]]),axis=1),axis=0); E=E[E[:,0]!=E[:,1]]
        E=np.concatenate([E,E[:,::-1]]); deg=np.bincount(E[:,0],minlength=nw).astype(np.float64)
        for it in range(ITER):
            N=np.zeros_like(Wd); np.add.at(N,E[:,0],Wd[E[:,1]]); N/=np.maximum(deg,1)[:,None]
            Wd=0.5*Wd+0.5*N
        S=Wd[inv]
        top=np.argsort(-S,axis=1)[:,:4]; tw=np.take_along_axis(S,top,axis=1); tw[tw<0.01]=0; tw/=tw.sum(1,keepdims=True); top[tw==0]=0
        oj,dtj,_,_=loc(A['JOINTS_0']); ow,dtw,_,_=loc(A['WEIGHTS_0'])
        assert dtw==np.float32
        jb=top.astype(dtj).tobytes(); wb=tw.astype(np.float32).tobytes()
        B[oj:oj+len(jb)]=jb; B[ow:ow+len(wb)]=wb; total+=nv
out=bytearray(raw); out[bo+8:bo+8+bl]=B; open(dst,'wb').write(out); print('smoothed',total,'vertices,',ITER,'iterations ->',dst)
