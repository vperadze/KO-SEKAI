"""Copy animation clips from one or more Meshy-rigged GLBs onto another Meshy-rigged GLB.
Bones are matched by name. Rotations are copied; bone lengths stay the target's own
(only the hip translation is copied, scaled to the target's hip height), so a tall or short
fighter keeps its proportions.
usage: transplant_anim.py target_rigged.glb out.glb source1.glb [source2.glb ...]"""
import json,struct,sys,math
def load(p):
    raw=open(p,'rb').read(); n,_=struct.unpack('<I4s',raw[12:20]); j=json.loads(raw[20:20+n]); bo=20+n; bl,_=struct.unpack('<I4s',raw[bo:bo+8])
    return j, bytearray(raw[bo+8:bo+8+bl])
SIZE={5120:1,5121:1,5122:2,5123:2,5125:4,5126:4}; NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def acc_bytes(j,B,i):
    a=j['accessors'][i]; v=j['bufferViews'][a['bufferView']]; n=a['count']*NC[a['type']]*SIZE[a['componentType']]
    assert not v.get('byteStride'), 'interleaved animation data not supported'
    o=v.get('byteOffset',0)+a.get('byteOffset',0); return bytes(B[o:o+n])
def hips(j):
    for i,nd in enumerate(j['nodes']):
        if nd.get('name')=='Hips': return i,nd.get('translation',[0,0,0])
    return None,[0,0,0]
tgt,out=sys.argv[1],sys.argv[2]; srcs=sys.argv[3:]
T,TB=load(tgt); tname={nd.get('name'):i for i,nd in enumerate(T['nodes'])}
_,th=hips(T); T['animations']=[]
def add_accessor(data, proto):
    while len(TB)%4: TB.append(0)
    T['bufferViews'].append({'buffer':0,'byteOffset':len(TB),'byteLength':len(data)}); TB.extend(data)
    a={k:proto[k] for k in ('componentType','count','type') }
    for k in ('min','max','normalized'):
        if k in proto: a[k]=proto[k]
    a['bufferView']=len(T['bufferViews'])-1; T['accessors'].append(a); return len(T['accessors'])-1
for sp in srcs:
    S,SB=load(sp); _,sh=hips(S)
    ls=math.sqrt(sum(x*x for x in sh)); lt=math.sqrt(sum(x*x for x in th)); ratio=(lt/ls) if ls>1e-9 else 1.0
    for an in S.get('animations',[]):
        samplers=[]; channels=[]; cache={}
        for ch in an['channels']:
            node=S['nodes'][ch['target']['node']].get('name'); path=ch['target']['path']
            if node not in tname: continue
            if path=='scale': continue
            if path=='translation' and node!='Hips': continue
            sm=an['samplers'][ch['sampler']]
            if sm['input'] not in cache: cache[sm['input']]=add_accessor(acc_bytes(S,SB,sm['input']), S['accessors'][sm['input']])
            data=acc_bytes(S,SB,sm['output']); proto=dict(S['accessors'][sm['output']])
            if path=='translation' and abs(ratio-1)>1e-6:
                assert proto['componentType']==5126
                vals=list(struct.unpack('<%df'%(len(data)//4),data)); data=struct.pack('<%df'%len(vals),*[v*ratio for v in vals]); proto.pop('min',None); proto.pop('max',None)
            o=add_accessor(data, proto)
            samplers.append({'input':cache[sm['input']],'output':o,'interpolation':sm.get('interpolation','LINEAR')})
            channels.append({'sampler':len(samplers)-1,'target':{'node':tname[node],'path':path}})
        T['animations'].append({'name':an.get('name','clip%d'%len(T['animations'])),'samplers':samplers,'channels':channels})
    print('from',sp.split('/')[-2],'clips',len(S.get('animations',[])),'hip scale %.3f'%ratio)
while len(TB)%4: TB.append(0)
T['buffers'][0]['byteLength']=len(TB)
js=json.dumps(T,separators=(',',':')).encode(); js+=b' '*((4-len(js)%4)%4)
blob=struct.pack('<4sII',b'glTF',2,12+8+len(js)+8+len(TB))+struct.pack('<I4s',len(js),b'JSON')+js+struct.pack('<I4s',len(TB),b'BIN\x00')+bytes(TB)
open(out,'wb').write(blob); print('wrote',out,'with',len(T['animations']),'clips:',[a['name'][-40:] for a in T['animations']][:20])
