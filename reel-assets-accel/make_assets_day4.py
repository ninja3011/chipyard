import cairosvg, math
BG="#0b1118"; PANEL="#111a24"; CY="#4dd2ff"; LI="#b6ff5c"; AM="#ffb020"; RD="#ff5c6c"; TX="#e8f1f8"; DIM="#7b8b99"
F="font-family=\"DejaVu Sans Mono, Menlo, Consolas, monospace\""
W,H=480,270
def t(x,y,s,size=16,fill=TX,w="700",anchor="start"):
    return f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{w}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
def rect(x,y,w,h,stroke=CY,sw=2,rx=10,fill=PANEL,fop=1.0):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" fill-opacity="{fop}" stroke="{stroke}" stroke-width="{sw}"/>'
def arrow(x1,y1,x2,y2,color=DIM,sw=2.5):
    a=math.atan2(y2-y1,x2-x1); L=9; hx,hy=x2-L*math.cos(a),y2-L*math.sin(a)
    p=f"{x2},{y2} {hx+5*math.sin(a):.1f},{hy-5*math.cos(a):.1f} {hx-5*math.sin(a):.1f},{hy+5*math.cos(a):.1f}"
    return f'<line x1="{x1}" y1="{y1}" x2="{hx:.1f}" y2="{hy:.1f}" stroke="{color}" stroke-width="{sw}"/><polygon points="{p}" fill="{color}"/>'
def card(body,tag):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<rect x="4" y="4" width="{W-8}" height="{H-8}" rx="18" fill="{BG}" fill-opacity="0.88" stroke="{CY}" stroke-opacity="0.55" stroke-width="2"/>
<text x="22" y="30" {F} font-size="12" fill="{DIM}" letter-spacing="2">{tag}</text>{body}</svg>'''
A={}
# 15 model
A['15_model']=card(
 t(240,104,"15,000,000",44,LI,"800","middle")+t(240,130,"parameters (learned numbers)",13,DIM,"400","middle")+
 rect(30,158,130,52,CY,1.8,10)+t(95,180,"6",20,CY,"800","middle")+t(95,198,"layers",11,DIM,"400","middle")+
 rect(175,158,130,52,AM,1.8,10)+t(240,180,"32,000",18,AM,"800","middle")+t(240,198,"known tokens",11,DIM,"400","middle")+
 rect(320,158,130,52,LI,1.8,10)+t(385,180,"8-bit",18,LI,"800","middle")+t(385,198,"engine math",11,DIM,"400","middle")+
 t(240,246,"a real language model, on our chip",13,TX,"700","middle"),"THE MODEL")
# 16 layer
b=rect(20,86,88,56,DIM,1.8)+t(64,112,"words",14,TX,"800","middle")+t(64,130,"so far",11,DIM,"400","middle")
b+=arrow(110,114,132,114)
b+=f'<rect x="134" y="66" width="214" height="96" rx="12" fill="none" stroke="{DIM}" stroke-width="1.6" stroke-dasharray="5 4"/>'
b+=rect(144,90,88,48,CY,2)+t(188,110,"attention",13,CY,"800","middle")+t(188,126,"look back",10,DIM,"400","middle")
b+=arrow(234,114,250,114)
b+=rect(252,90,88,48,LI,2)+t(296,108,"feed-forward",12,LI,"800","middle")+t(296,126,"think it over",10,DIM,"400","middle")
b+=t(241,182,"x 6 identical layers",13,AM,"800","middle")
b+=arrow(350,114,372,114)
b+=rect(374,86,88,56,AM,1.8)+t(418,108,"32,000",14,AM,"800","middle")+t(418,124,"scores",11,DIM,"400","middle")+t(418,138,"pick best",10,DIM,"400","middle")
b+=t(240,232,"best-scoring token = the next word",13,TX,"700","middle")+t(240,252,"then repeat",11,DIM,"400","middle")
A['16_layer']=card(b,"INSIDE THE MODEL")
# 17 attention
words=["The","dog","chased","the","?"]; xs=[52,132,226,326,414]; wts=[1.5,5,5,2.5]
b=""
for i,(w_,x) in enumerate(zip(words,xs)):
    col=AM if w_=="?" else CY
    b+=rect(x-38,166,76,34,col,1.8,8)+t(x,188,w_,14,TX,"800","middle")
for i in range(4):
    x1,x2=xs[4],xs[i]; ym=112-wts[i]*4
    b+=f'<path d="M{x1},166 Q{(x1+x2)/2},{ym-40:.0f} {x2},166" fill="none" stroke="{LI}" stroke-width="{wts[i]}" stroke-opacity="0.85"/>'
b+=t(240,86,"each word looks back and decides:",14,TX,"800","middle")+t(240,106,"which earlier words matter?",14,LI,"800","middle")
b+=t(240,232,"thicker line = more attention",11,DIM,"400","middle")+t(240,252,"guessing the next word from what matters",11,DIM,"400","middle")
A['17_attention']=card(b,"ATTENTION")
# 18 quantization
b=rect(24,74,150,74,AM,2.2)+t(99,98,"32-bit decimal",11,DIM,"400","middle")+t(99,126,"0.7318264",18,AM,"800","middle")
b+=arrow(178,111,264,111)+t(221,100,"round",12,TX,"800","middle")+t(221,132,"+ scale",11,DIM,"400","middle")
b+=rect(268,74,110,74,LI,2.2)+t(323,98,"8-bit integer",11,DIM,"400","middle")+t(323,128,"93",30,LI,"800","middle")
b+=rect(392,74,66,74,DIM,1.6)+t(425,100,"scale",11,DIM,"400","middle")+t(425,126,"0.00787",11,TX,"700","middle")+t(425,140,"kept",11,DIM,"400","middle")
b+=f'<line x1="40" y1="190" x2="440" y2="190" stroke="{DIM}" stroke-width="2"/>'
for v,x in [(-128,40),(0,240),(127,440)]:
    b+=f'<line x1="{x}" y1="184" x2="{x}" y2="196" stroke="{DIM}" stroke-width="2"/>'+t(x,212,str(v),11,DIM,"400","middle")
b+=f'<circle cx="{240+93/127*200:.0f}" cy="190" r="6" fill="{LI}"/>'
b+=t(240,244,"scale back later: 93 x 0.00787 = 0.7319",13,TX,"700","middle")+t(240,262,"a little precision lost, answers barely change",11,DIM,"400","middle")
A['18_quantization']=card(b,"QUANTIZATION: 32-BIT TO 8-BIT (EXAMPLE)")
# 19 split
b=rect(24,52,206,176,AM,2.2)+t(127,80,"COPROCESSOR",14,AM,"800","middle")+t(127,98,"(our engine)",11,DIM,"400","middle")
b+=t(127,140,"the big matrix",15,TX,"800","middle")+t(127,160,"multiplies",15,TX,"800","middle")+t(127,196,"nearly all the work",12,LI,"800","middle")
b+=rect(250,52,206,176,CY,2.2)+t(353,80,"CPU",14,CY,"800","middle")+t(353,98,"(the small stuff)",11,DIM,"400","middle")
b+=t(353,132,"RMSNorm",14,TX,"800","middle")+t(353,150,"keeps numbers in check",10,DIM,"400","middle")
b+=t(353,176,"softmax",14,TX,"800","middle")+t(353,194,"scores > probabilities",10,DIM,"400","middle")
b+=t(353,216,"pick the next token",13,TX,"800","middle")
b+=t(240,254,"hardware for the heavy math, software for the rest",12,DIM,"400","middle")
A['19_split']=card(b,"WHO DOES WHAT")
# 20 padding
b=""
for r in range(8):
    for c in range(8):
        on=(c==0)
        b+=f'<rect x="{40+c*26}" y="{56+r*22}" width="22" height="18" rx="3" fill="{LI if on else CY}" fill-opacity="{0.95 if on else 0.16}"/>'
b+=t(276,96,"1 token =",15,TX,"800")+t(276,118,"1 vector",15,TX,"800")
b+=t(276,150,"the engine wants a",12,DIM,"400")+t(276,166,"full 8x8 matrix,",12,DIM,"400")+t(276,182,"so the vector is padded",12,DIM,"400")
b+=t(276,214,"1 / 8 of the tile",14,AM,"800")+t(276,232,"does useful work",14,AM,"800")
b+=t(240,262,"it works, but it wastes most of the tile",11,DIM,"400","middle")
A['20_padding']=card(b,"WHY GENERATING IS WASTEFUL")
# 21 verified
b=(t(240,96,"32,000 / 32,000",34,CY,"800","middle")+t(240,122,"output scores identical",15,TX,"800","middle")+
 t(240,148,"engine vs a plain CPU loop, bit for bit",12,DIM,"400","middle")+
 rect(40,170,400,58,LI,1.8,10)+t(240,194,"40 / 40",20,LI,"800","middle")+t(240,214,"next-word picks match full precision (PC check)",11,DIM,"400","middle")+
 t(240,254,"32,000 check: real board  |  40/40 check: on a PC",11,DIM,"400","middle"))
A['21_verified']=card(b,"VERIFIED")
# 22 speed
b=(t(240,116,"1.5",58,AM,"800","middle")+t(240,146,"tokens per second",16,TX,"800","middle")+
 t(240,186,"slow, because time goes to",13,TX,"700","middle")+t(240,206,"fetching data from memory,",13,TX,"700","middle")+t(240,226,"not multiplying",13,LI,"800","middle")+
 t(240,254,"memory-bound behaviour measured on the small model",10,DIM,"400","middle"))
A['22_speed']=card(b,"THE SPEED")
# 23 next
b=(t(240,96,"NEXT",14,DIM,"800","middle")+t(240,132,"benchmark vs",22,TX,"800","middle")+t(240,162,"the plain CPU run",22,AM,"800","middle")+
 t(70,206,"CPU",13,DIM,"700")+rect(140,192,280,18,DIM,1.5,6)+t(430,206,"?",14,DIM,"800")+
 t(70,236,"engine",13,LI,"700")+rect(140,222,280,18,LI,1.5,6)+t(430,236,"?",14,LI,"800"))
A['23_next']=card(b,"TOMORROW")
for k,s in A.items():
    open(k+'.svg','w').write(s)
    cairosvg.svg2png(bytestring=s.encode(),write_to=k+'.png',output_width=960,output_height=540)
from PIL import Image
names=sorted(A); ims=[Image.open(k+'.png').convert('RGBA') for k in names]
cols=3; rows=(len(ims)+cols-1)//cols
sheet=Image.new('RGBA',(cols*490+10,rows*280+10),(40,60,90,255))
for i,im in enumerate(ims):
    r=im.resize((480,270)); sheet.paste(r,(10+(i%cols)*490,10+(i//cols)*280),r)
sheet.convert('RGB').save('_contact_sheet_day4.png'); print(names)
