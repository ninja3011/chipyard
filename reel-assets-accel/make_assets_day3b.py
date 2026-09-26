import cairosvg
BG="#0b1118"; PANEL="#111a24"; CY="#4dd2ff"; LI="#b6ff5c"; AM="#ffb020"; RD="#ff5c6c"; TX="#e8f1f8"; DIM="#7b8b99"
F="font-family=\"DejaVu Sans Mono, Menlo, Consolas, monospace\""
def t(x,y,s,size=16,fill=TX,w="700",anchor="start"):
    return f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{w}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
def rect(x,y,w,h,fill=PANEL,stroke=CY,sw=2,rx=10,op=1.0,fop=1.0):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" fill-opacity="{fop}" stroke="{stroke}" stroke-opacity="{op}" stroke-width="{sw}"/>'
def arrow(x1,y1,x2,y2,color=DIM,sw=2.5):
    import math
    a=math.atan2(y2-y1,x2-x1); L=9
    hx,hy=x2-L*math.cos(a),y2-L*math.sin(a)
    p=f"{x2},{y2} {hx+5*math.sin(a):.1f},{hy-5*math.cos(a):.1f} {hx-5*math.sin(a):.1f},{hy+5*math.cos(a):.1f}"
    return f'<line x1="{x1}" y1="{y1}" x2="{hx:.1f}" y2="{hy:.1f}" stroke="{color}" stroke-width="{sw}"/><polygon points="{p}" fill="{color}"/>'
def card(body,tag,W=480,H=270):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<rect x="4" y="4" width="{W-8}" height="{H-8}" rx="18" fill="{BG}" fill-opacity="0.88" stroke="{CY}" stroke-opacity="0.55" stroke-width="2"/>
<text x="22" y="30" {F} font-size="12" fill="{DIM}" letter-spacing="2">{tag}</text>{body}</svg>'''
def cells(x,y,n,cs,color,pattern=None,op=0.9):
    o=""
    for r in range(n):
        for c in range(n):
            v=pattern(r,c) if pattern else 0.6
            o+=f'<rect x="{x+c*cs}" y="{y+r*cs}" width="{cs-2}" height="{cs-2}" rx="2" fill="{color}" fill-opacity="{0.15+0.8*v:.2f}"/>'
    return o
A={}
# 11: the loop
steps=[("clear","acc_clear()",LI),("load A tile","acc_load_a()",CY),("load B tile","acc_load_b()",CY),("multiply + add","acc_mac()",AM),("store","acc_store32 / 8",LI)]
b=""
for i,(a,c,col) in enumerate(steps):
    y=46+i*38
    b+=rect(24,y,290,32,PANEL,col,1.8,8)+t(40,y+21,f"{i+1}",15,col,"800")+t(64,y+21,a,14,TX,"800")+t(300,y+21,c,11,DIM,"400","end")
# repeat bracket around 2-4
b+=f'<path d="M330,88 h14 v112 h-14" fill="none" stroke="{AM}" stroke-width="2.5"/>'
b+=t(352,140,"repeat",13,AM,"800")+t(352,158,"for every",12,DIM,"400")+t(352,174,"8-wide k",12,DIM,"400")
b+=t(240,254,"one output tile = a short loop",12,DIM,"400","middle")
A['11_loop']=card(b,"11  THE CPU LOOP")
# 12: free flip
pat=lambda r,c: 0.95 if (c>=r) else (0.25 if (r+c)%3 else 0.6)
b=cells(40,52,6,22,CY,pat)+t(106,200,"stored tile",11,CY,"700","middle")
b+=arrow(186,118,262,118,AM,3)+t(224,106,"flag",13,AM,"800","middle")+t(224,142,"1 bit",11,DIM,"400","middle")
tp=lambda r,c: pat(c,r)
b+=cells(282,52,6,22,LI,tp)+t(348,200,"read by column",11,LI,"700","middle")
b+=t(240,232,"the data never moves",15,TX,"800","middle")+t(240,250,"load the mirrored tile, set the flag",11,DIM,"400","middle")
A['12_free_flip']=card(b,"12  THE FREE FLIP")
# 13: shrink
b=rect(24,84,130,60,PANEL,AM,2.2,10)+t(89,112,"32-bit",20,AM,"800","middle")+t(89,132,"result",11,DIM,"400","middle")
b+=rect(376,96,80,36,PANEL,LI,2.2,10)+t(416,120,"8-bit",16,LI,"800","middle")
ops=[(">> shift",CY),("ReLU",CY),("clamp",CY)]
for i,(o,c) in enumerate(ops):
    x=170+i*68
    b+=rect(x,92,60,44,PANEL,c,1.6,8)+t(x+30,119,o,11,TX,"800","middle")
b+=arrow(154,114,168,114)+arrow(374,114,376,114) if False else ""
for x1,x2 in [(154,168),(230,238),(298,306),(358,374)]:
    b+=f'<line x1="{x1}" y1="114" x2="{x2}" y2="114" stroke="{DIM}" stroke-width="2.5"/>'
b+=t(240,180,"ready for the next layer",15,TX,"800","middle")
b+=t(240,206,"shift down, negatives to zero, clamp to -128..127",11,DIM,"400","middle")
b+=t(240,246,"done inside the store instruction",12,DIM,"400","middle")
A['13_shrink']=card(b,"13  SHRINK ON THE WAY OUT")
# ---------- architecture diagram (large) ----------
W,H=1280,720
d=f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="{W}" height="{H}" fill="{BG}"/>'
d+=t(40,54,"HOW THE GEMM WORKS",28,TX,"800")+t(40,80,"one 8x8 INT8 tile engine, driven by a loop on the CPU, builds a multiplier for ANY size",14,DIM,"400")
# panel 1
d+=rect(30,110,390,560,PANEL,CY,2,14,0.6)+t(50,142,"1  CUT INTO 8x8 TILES",15,CY,"800")
ts=44
def tilegrid(x,y,rows,cols,hl,color):
    o=""
    for r in range(rows):
        for c in range(cols):
            on=hl(r,c)
            o+=f'<rect x="{x+c*(ts+4)}" y="{y+r*(ts+4)}" width="{ts}" height="{ts}" rx="6" fill="{color if on else CY}" fill-opacity="{0.95 if on else 0.22}"/>'
    return o
# A 2x4, B 4x3, C 2x3
d+=t(50,186,"A  16 x 32   (2 x 4 tiles)",12,DIM,"700")
d+=tilegrid(50,196,2,4,lambda r,c: r==0,AM)
d+=t(50,320,"B  32 x 24   (4 x 3 tiles)",12,DIM,"700")
d+=tilegrid(50,330,4,3,lambda r,c: c==1,CY)
d+=t(50,548,"C  16 x 24   (2 x 3 tiles)",12,DIM,"700")
d+=tilegrid(50,558,2,3,lambda r,c: r==0 and c==1,LI)
d+=t(250,262,"row of A tiles",12,AM,"800")+t(250,280,"x",22,TX,"800")+t(250,300,"column of B tiles",12,CY,"800")
d+=t(250,340,"=",22,TX,"800")+t(250,362,"ONE tile of C",12,LI,"800")
d+=t(250,420,"2x3 output tiles",12,TX,"700")+t(250,438,"x 4 k-slices",12,TX,"700")+t(250,462,"= 24 tile multiplies",13,AM,"800")
# panel 2: loop
d+=rect(450,110,380,560,PANEL,AM,2,14,0.6)+t(470,142,"2  THE CPU LOOP  (acc_gemm)",15,AM,"800")
lines=[("for each output tile (i, j):",0,TX),("acc_clear()",1,LI),("for k in 0, 8, 16 ... K:",1,TX),("acc_cfg(stride)",2,DIM),("acc_load_a(tile)",2,CY),("acc_load_b(tile)",2,CY),("acc_mac(flipA, flipB)",2,AM),("acc_store32()  or  acc_store8()",1,LI),("acc_sync()  when all done",0,DIM)]
for i,(s,ind,c) in enumerate(lines):
    y=190+i*44
    d+=rect(470+ind*24,y-24,340-ind*24,34,BG,c if c!=TX else DIM,1.4,7,0.8,0.8)+t(484+ind*24,y-2,s,13,c,"700")
d+=t(470,610,"flip = load the mirrored tile",12,DIM,"400")+t(470,630,"and set a flag. Data never moves.",12,DIM,"400")
# panel 3: engine
d+=rect(860,110,390,560,PANEL,LI,2,14,0.6)+t(880,142,"3  THE ENGINE (hardware)",15,LI,"800")
d+=rect(880,160,350,44,BG,DIM,1.6,8)+t(1055,188,"command decoder + state machine",13,TX,"700","middle")
d+=rect(880,232,160,64,BG,CY,2,10)+t(960,262,"A regs",15,CY,"800","middle")+t(960,282,"8x8 int8",11,DIM,"400","middle")
d+=rect(1070,232,160,64,BG,CY,2,10)+t(1150,262,"B regs",15,CY,"800","middle")+t(1150,282,"8x8 int8",11,DIM,"400","middle")
d+=arrow(960,296,1055,336,DIM)+arrow(1150,296,1055,336,DIM)
d+=rect(910,340,290,70,BG,AM,2.4,12)+t(1055,372,"64 multipliers",17,AM,"800","middle")+t(1055,394,"one k-step per cycle (8 cycles/tile)",11,DIM,"400","middle")
d+=arrow(1055,410,1055,448,DIM)
d+=rect(910,452,290,60,BG,LI,2.4,12)+t(1055,480,"C accumulators  8x8 int32",14,LI,"800","middle")+t(1055,500,"keeps adding across k",11,DIM,"400","middle")
d+=arrow(1055,512,1055,548,DIM)
d+=rect(910,552,290,44,BG,LI,1.6,10)+t(1055,579,"shift / ReLU / clamp -> int8",13,TX,"700","middle")
d+=t(1055,634,"64-bit requests (8 per tile) via the CPU's cache",12,DIM,"400","middle")
d+=t(1055,654,"to and from DDR3",12,DIM,"400","middle")
# connectors between panels
d+=arrow(422,390,448,390,DIM,3)+arrow(832,390,858,390,DIM,3)
d+='</svg>'
A['14_gemm_architecture']=d
for k,s in A.items():
    open(k+'.svg','w').write(s)
    if k.startswith('14'): cairosvg.svg2png(bytestring=s.encode(),write_to=k+'.png',output_width=1920,output_height=1080)
    else: cairosvg.svg2png(bytestring=s.encode(),write_to=k+'.png',output_width=960,output_height=540)
from PIL import Image
ims=[Image.open(k+'.png').convert('RGBA') for k in ['11_loop','12_free_flip','13_shrink']]
sheet=Image.new('RGBA',(3*490+10,290),(40,60,90,255))
for i,im in enumerate(ims):
    r=im.resize((480,270)); sheet.paste(r,(10+i*490,10),r)
sheet.convert('RGB').save('_contact_sheet_day3b.png'); print(sorted(A))
