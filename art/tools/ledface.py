"""Draw LED dot-matrix faces for Turbotron's visor, and a 'no signal' screen for Channel Zero."""
import numpy as np, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont
EYES={
 'round':["..###..",".#####.",".#####.",".#####.","..###.."],
 'caret':["...#...","..#.#..",".#...#.","#.....#","......."],
 'gt':   ["#......",".##....","...##..",".##....","#......"],
 'lt':   ["......#","....##.","..##...","....##.","......#"],
 'x':    ["#.....#","..#.#..","...#...","..#.#..","#.....#"],
 'spiral':[".#####.","#.....#","#.###.#","#.#...#",".#.###."],
 'line': [".......",".......","#######",".......","......."],
 'heart':[".##.##.","#######","#######",".#####.","...#..."],
}
MOUTH={
 'flat':  ["..............","##############","..............",".............."],
 'smile': ["#............#",".#..........#.","..##########..",".............."],
 'open':  ["....######....","...#......#...","...#......#...","....######...."],
 'wavy':  ["..............","#.#.#.#.#.#.#.",".#.#.#.#.#.#.#",".............."],
 'tongue':["##############",".........###..",".........###..",".............."],
 'grin':  ["##############","#.#.#.#.#.#.##",".############.",".............."],
}
FACES={'neutral':('round','round','flat'),'happy':('caret','caret','smile'),'hit':('gt','lt','open'),
       'dizzy':('spiral','spiral','wavy'),'ko':('x','x','tongue'),'love':('heart','heart','grin'),
       'talk':('round','round','open'),'blink':('line','line','flat')}
def led_face(name, w=920, h=520, cols=30, rows=15, color=(255,40,30)):
    le,re,mo=FACES[name]; grid=np.zeros((rows,cols),bool)
    def put(g,r0,c0):
        for r,row in enumerate(g):
            for c,ch in enumerate(row):
                if ch=='#': grid[r0+r,c0+c]=True
    put(EYES[le],2,5); put(EYES[re],2,cols-5-7); put(MOUTH[mo],9,(cols-14)//2)
    S=3; im=Image.new('RGB',(w*S,h*S),(6,5,7)); d=ImageDraw.Draw(im)
    cw=w*S/(cols+2); chh=h*S/(rows+2); r=min(cw,chh)*0.36
    for i in range(rows):
        for j in range(cols):
            cx=(j+1.5)*cw; cy=(i+1.5)*chh
            if grid[i,j]: d.ellipse([cx-r,cy-r,cx+r,cy+r],fill=color)
            else: d.ellipse([cx-r*0.45,cy-r*0.45,cx+r*0.45,cy+r*0.45],fill=(26,8,8))
    glow=im.filter(ImageFilter.GaussianBlur(r*1.3))
    out=Image.fromarray(np.clip(np.array(im).astype(int)+np.array(glow).astype(int)*0.7,0,255).astype(np.uint8))
    return out.resize((w,h),Image.LANCZOS)
def no_signal(w=1104, h=808, seed=7):
    rng=np.random.default_rng(seed); px=6
    n=rng.integers(20,235,(h//px+1,w//px+1)).astype(np.uint8)
    im=Image.fromarray(np.repeat(np.repeat(n,px,0),px,1)[:h,:w]).convert('RGB')
    a=np.array(im).astype(int)
    for y in range(0,h,4): a[y:y+1]=a[y:y+1]*0.55
    a[int(h*0.40):int(h*0.60)]=a[int(h*0.40):int(h*0.60)]*0.25+np.array([10,12,60])
    im=Image.fromarray(np.clip(a,0,255).astype(np.uint8)); d=ImageDraw.Draw(im)
    # blocky pixel text, drawn from a tiny built-in font and scaled up without smoothing
    f=ImageFont.load_default(); t=Image.new('L',(80,12),0); ImageDraw.Draw(t).text((1,0),'NO SIGNAL',fill=255,font=f)
    bb=t.getbbox(); t=t.crop(bb); k=int(w*0.62/t.width); t=t.resize((t.width*k,t.height*k),Image.NEAREST)
    im.paste(Image.new('RGB',t.size,(255,255,255)),((w-t.width)//2,(h-t.height)//2),t)
    return im
if __name__=='__main__':
    names=list(FACES); ims=[led_face(n).resize((460,260)) for n in names]+[no_signal().resize((355,260))]
    s=Image.new('RGB',(460*3+12,260*3+12),(40,40,40))
    for i,im in enumerate(ims): s.paste(im,((i%3)*466,(i//3)*266))
    s.save('led-faces.jpg',quality=90); print(names)
