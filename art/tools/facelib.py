import json,struct,io,numpy as np
from PIL import Image
Image.MAX_IMAGE_PIXELS=None
CT={5120:np.int8,5121:np.uint8,5122:np.int16,5123:np.uint16,5125:np.uint32,5126:np.float32}; NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
class Model:
    def __init__(s,path):
        raw=open(path,'rb').read(); n,_=struct.unpack('<I4s',raw[12:20]); s.j=json.loads(raw[20:20+n]); bo=20+n; bl,_=struct.unpack('<I4s',raw[bo:bo+8]); s.B=raw[bo+8:bo+8+bl]
        j=s.j; pr=j['meshes'][0]['primitives'][0]; A=pr['attributes']
        s.P=s.acc(A['POSITION']).astype(np.float64); s.UV=s.acc(A['TEXCOORD_0']).astype(np.float64); s.N=s.acc(A['NORMAL']).astype(np.float64)
        s.I=s.acc(pr['indices']).reshape(-1,3).astype(np.int64); J=s.acc(A['JOINTS_0']); W=s.acc(A['WEIGHTS_0'])
        names=[j['nodes'][k].get('name') for k in j['skins'][0]['joints']]
        dom=J[np.arange(len(J)),W.argmax(1)]; s.dom=np.array([names[d] for d in dom])
        m=j['materials'][0]; src=j['textures'][m['pbrMetallicRoughness']['baseColorTexture']['index']]['source']; v=j['bufferViews'][j['images'][src]['bufferView']]
        s.tex=np.array(Image.open(io.BytesIO(s.B[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']])).convert('RGB'))
    def acc(s,i):
        a=s.j['accessors'][i]; v=s.j['bufferViews'][a['bufferView']]; c=NC[a['type']]; dt=CT[a['componentType']]
        return np.frombuffer(s.B,dtype=dt,count=a['count']*c,offset=v.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,c) if c>1 else np.frombuffer(s.B,dtype=dt,count=a['count'],offset=v.get('byteOffset',0)+a.get('byteOffset',0))
    def head_tris(s, front=0.25):
        hv=np.isin(s.dom,['Head','head_end','headfront'])
        t=s.I[hv[s.I].all(1)]
        fn=np.cross(s.P[t[:,1]]-s.P[t[:,0]], s.P[t[:,2]]-s.P[t[:,0]]); fn/=np.maximum(np.linalg.norm(fn,axis=1,keepdims=True),1e-12)
        return t[fn[:,2]>front]
def bary_raster(p0,p1,p2,W,H):
    """yield pixel coords (xs,ys) and barycentric weights for a 2D triangle in pixel space"""
    x0=int(max(0,np.floor(min(p0[0],p1[0],p2[0])))); x1=int(min(W-1,np.ceil(max(p0[0],p1[0],p2[0]))))
    y0=int(max(0,np.floor(min(p0[1],p1[1],p2[1])))); y1=int(min(H-1,np.ceil(max(p0[1],p1[1],p2[1]))))
    if x1<x0 or y1<y0: return None
    xs,ys=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
    d=(p1[1]-p2[1])*(p0[0]-p2[0])+(p2[0]-p1[0])*(p0[1]-p2[1])
    if abs(d)<1e-12: return None
    a=((p1[1]-p2[1])*(xs-p2[0])+(p2[0]-p1[0])*(ys-p2[1]))/d; b=((p2[1]-p0[1])*(xs-p2[0])+(p0[0]-p2[0])*(ys-p2[1]))/d; c=1-a-b
    m=(a>=-1e-4)&(b>=-1e-4)&(c>=-1e-4)
    if not m.any(): return None
    return xs[m].astype(int),ys[m].astype(int),a[m],b[m],c[m]
def front_view(M, size=700):
    t=M.head_tris(); P=M.P; hp=P[np.unique(t)]
    lo=hp.min(0); hi=hp.max(0); sc=size/max(hi[0]-lo[0],hi[1]-lo[1])
    W=int((hi[0]-lo[0])*sc)+2; H=int((hi[1]-lo[1])*sc)+2
    img=np.zeros((H,W,3),np.uint8); zb=np.full((H,W),-1e9); th,tw=M.tex.shape[:2]
    for a,b,c in t:
        q=[((P[i][0]-lo[0])*sc,(hi[1]-P[i][1])*sc) for i in (a,b,c)]
        r=bary_raster(q[0],q[1],q[2],W,H)
        if r is None: continue
        xs,ys,wa,wb,wc=r; z=wa*P[a][2]+wb*P[b][2]+wc*P[c][2]; ok=z>zb[ys,xs]
        if not ok.any(): continue
        xs,ys,wa,wb,wc,z=xs[ok],ys[ok],wa[ok],wb[ok],wc[ok],z[ok]
        uv=wa[:,None]*M.UV[a]+wb[:,None]*M.UV[b]+wc[:,None]*M.UV[c]
        tx=np.clip((uv[:,0]*tw).astype(int),0,tw-1); ty=np.clip((uv[:,1]*th).astype(int),0,th-1)
        img[ys,xs]=M.tex[ty,tx]; zb[ys,xs]=z
    return img,(lo,hi,sc)
