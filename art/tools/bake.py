"""Paint a flat picture onto a fighter's screen (front of the head) inside its texture atlas."""
import sys,json,numpy as np
sys.path.insert(0,'/tmp/claude-1000/-home-nikoloz/6520a29d-e1da-4fff-a883-3e17cbb90623/scratchpad/face')
import facelib
from PIL import Image, ImageDraw
def coverage(M):
    th,tw=M.tex.shape[:2]; m=Image.new('L',(tw,th),0); d=ImageDraw.Draw(m); P=(M.UV*[tw,th]).tolist()
    for a,b,c in M.I.tolist(): d.polygon([tuple(P[a]),tuple(P[b]),tuple(P[c])],fill=255)
    return np.array(m)>0
def bake(M, rect_px, view, picture, cover, pad=10):
    """rect_px: (x0,y0,x1,y1) in the head front-view image; view: (lo,hi,sc) from front_view."""
    lo,hi,sc=view; x0,y0,x1,y1=rect_px
    wx0=lo[0]+x0/sc; wx1=lo[0]+x1/sc; wy0=hi[1]-y0/sc; wy1=hi[1]-y1/sc      # world rect (top y0 -> wy0)
    pic=np.array(picture.convert('RGB')); ph,pw=pic.shape[:2]
    tex=M.tex.copy(); th,tw=tex.shape[:2]; written=np.zeros((th,tw),bool)
    for a,b,c in M.head_tris():
        q=[(M.UV[i][0]*tw,M.UV[i][1]*th) for i in (a,b,c)]
        r=facelib.bary_raster(q[0],q[1],q[2],tw,th)
        if r is None: continue
        xs,ys,wa,wb,wc=r
        X=wa*M.P[a][0]+wb*M.P[b][0]+wc*M.P[c][0]; Y=wa*M.P[a][1]+wb*M.P[b][1]+wc*M.P[c][1]
        s=(X-wx0)/(wx1-wx0); t=(wy0-Y)/(wy0-wy1); ok=(s>=0)&(s<=1)&(t>=0)&(t<=1)
        if not ok.any(): continue
        px=np.clip((s[ok]*pw).astype(int),0,pw-1); py=np.clip((t[ok]*ph).astype(int),0,ph-1)
        tex[ys[ok],xs[ok]]=pic[py,px]; written[ys[ok],xs[ok]]=True
    # spread the new colours a few texels into the unused gaps around the painted islands
    w=written.copy()
    for _ in range(pad):
        grow=np.zeros_like(w); acc=np.zeros(tex.shape,np.int32); cnt=np.zeros(w.shape,np.int32)
        for dy,dx in ((0,1),(0,-1),(1,0),(-1,0)):
            sw=np.roll(w,(dy,dx),(0,1)); st=np.roll(tex,(dy,dx),(0,1))
            acc+=st*sw[:,:,None]; cnt+=sw; grow|=sw
        new=grow&~w&~cover
        if not new.any(): break
        tex[new]=(acc[new]/cnt[new][:,None]).astype(np.uint8); w|=new
    return tex, int(written.sum())

def bake_soft(M, rect_px, view, picture, cover, feather=0.07, pad=8):
    """Like bake(), but fades the new picture in from the edges of the rectangle, for faces on curved heads."""
    lo,hi,sc=view; x0,y0,x1,y1=rect_px
    wx0=lo[0]+x0/sc; wx1=lo[0]+x1/sc; wy0=hi[1]-y0/sc; wy1=hi[1]-y1/sc
    pic=np.array(picture.convert('RGB')).astype(np.float64); ph,pw=pic.shape[:2]
    tex=M.tex.copy(); th,tw=tex.shape[:2]; n=0
    for a,b,c in M.head_tris():
        q=[(M.UV[i][0]*tw,M.UV[i][1]*th) for i in (a,b,c)]
        r=facelib.bary_raster(q[0],q[1],q[2],tw,th)
        if r is None: continue
        xs,ys,wa,wb,wc=r
        X=wa*M.P[a][0]+wb*M.P[b][0]+wc*M.P[c][0]; Y=wa*M.P[a][1]+wb*M.P[b][1]+wc*M.P[c][1]
        s=(X-wx0)/(wx1-wx0); t=(wy0-Y)/(wy0-wy1); ok=(s>=0)&(s<=1)&(t>=0)&(t<=1)
        if not ok.any(): continue
        s,t,xs,ys=s[ok],t[ok],xs[ok],ys[ok]
        al=np.clip(np.minimum(np.minimum(s,1-s),np.minimum(t,1-t))/feather,0,1)[:,None]
        px=np.clip((s*pw).astype(int),0,pw-1); py=np.clip((t*ph).astype(int),0,ph-1)
        tex[ys,xs]=(tex[ys,xs]*(1-al)+pic[py,px]*al).astype(np.uint8); n+=len(xs)
    return tex,n
