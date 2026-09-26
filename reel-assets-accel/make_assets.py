import cairosvg
W,H=480,270
BG="#0b1118"; PANEL="#111a24"; CY="#4dd2ff"; LI="#b6ff5c"; AM="#ffb020"; RD="#ff5c6c"; TX="#e8f1f8"; DIM="#7b8b99"
F="font-family=\"DejaVu Sans Mono, Menlo, Consolas, monospace\""
def wrap(body, tag):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<rect x="4" y="4" width="{W-8}" height="{H-8}" rx="18" fill="{BG}" fill-opacity="0.88" stroke="{CY}" stroke-opacity="0.55" stroke-width="2"/>
<text x="22" y="30" {F} font-size="12" fill="{DIM}" letter-spacing="2">{tag}</text>
{body}</svg>'''
def t(x,y,s,size=16,fill=TX,w="700",anchor="start"):
    return f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{w}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
def box(x,y,w,h,label,color=CY,sub=None):
    o=f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{PANEL}" stroke="{color}" stroke-width="2"/>'
    o+=t(x+w/2,y+h/2+(0 if sub else 5),label,15,color,anchor="middle")
    if sub: o+=t(x+w/2,y+h/2+18,sub,11,DIM,"400","middle")
    return o
def arrow(x1,y,x2,color=DIM):
    return f'<line x1="{x1}" y1="{y}" x2="{x2-6}" y2="{y}" stroke="{color}" stroke-width="2.5"/><polygon points="{x2},{y} {x2-9},{y-5} {x2-9},{y+5}" fill="{color}"/>'

A={}
# 1 HOOK
A['01_hook']=wrap(
 t(240,108,"15,000,000",46,LI,"800","middle")+
 t(240,138,"PARAMETERS",16,TX,"700","middle")+
 t(240,182,"FINE-TUNED ON MY OWN CHIP",20,CY,"800","middle")+
 t(240,214,"no GPU  |  Arty A7 FPGA  |  live",13,DIM,"400","middle"),"01  THE CLAIM")
# 2 CURIOSITY GAP
A['02_one_op']=wrap(
 t(240,112,"EVERY AI LAYER =",16,DIM,"700","middle")+
 t(240,170,"C = A x B",50,AM,"800","middle")+
 t(240,214,"so build hardware for ONE thing",15,TX,"700","middle")+
 t(240,240,"attention, MLP, gradients: same op",12,DIM,"400","middle"),"02  THE INSIGHT")
# 3 ANCHOR architecture
A['03_architecture']=wrap(
 box(22,70,120,62,"CPU","#b6ff5c","RISC-V Rocket")+arrow(146,101,178)+
 box(182,70,116,62,"ENGINE",AM,"8x8 INT8")+arrow(302,101,334)+
 box(338,70,120,62,"DDR3",CY,"256 MB")+
 f'<line x1="82" y1="132" x2="82" y2="176" stroke="{DIM}" stroke-width="2" stroke-dasharray="4 4"/>'+
 f'<line x1="82" y1="176" x2="398" y2="176" stroke="{DIM}" stroke-width="2" stroke-dasharray="4 4"/>'+
 f'<line x1="398" y1="176" x2="398" y2="134" stroke="{DIM}" stroke-width="2" stroke-dasharray="4 4"/>'+
 t(240,200,"engine borrows the CPU's cache",13,TX,"700","middle")+
 t(240,224,"no DMA. stays coherent.",12,DIM,"400","middle"),"03  THE ARCHITECTURE")
# 4 PLAN ladder
rows=[("1","ENGINE","one 8x8 tile",LI),("2","GEMM","any size + transpose",LI),("3","LLM","real model, bit-exact",LI),("4","LoRA","it LEARNS",AM)]
body=""
for i,(n,a,b,c) in enumerate(rows):
    y=52+i*52
    body+=f'<rect x="22" y="{y}" width="436" height="42" rx="9" fill="{PANEL}" stroke="{c}" stroke-width="1.6"/>'
    body+=t(44,y+27,n,18,c,"800")+t(76,y+27,a,17,TX,"800")+t(190,y+27,b,13,DIM,"400")+t(436,y+27,"OK" if n!="4" else "OK",13,c,"800","end")
A['04_plan']=wrap(body,"04  HOW I PLANNED IT: PROVE EACH LAYER")
# 5 tile
def grid(x,y,n,color,cell=13):
    o=""
    for r in range(n):
        for c in range(n):
            o+=f'<rect x="{x+c*cell}" y="{y+r*cell}" width="{cell-2}" height="{cell-2}" rx="2" fill="{color}" fill-opacity="{0.35+0.5*((r*3+c*5)%7)/7:.2f}"/>'
    return o
A['05_tile']=wrap(
 grid(30,80,8,CY)+t(82,200,"A  int8",12,CY,"700","middle")+t(148,138,"x",26,TX,"800","middle")+
 grid(168,80,8,LI)+t(220,200,"B  int8",12,LI,"700","middle")+t(284,138,"=",26,TX,"800","middle")+
 grid(304,80,8,AM,cell=16)+t(368,222,"C  int32",12,AM,"700","middle")+
 t(240,252,"64 multipliers, one k per cycle  |  transpose is free",12,DIM,"400","middle"),"05  ONE TILE")
# 6 PROOF
A['06_proof']=wrap(
 t(120,112,"4.1x",44,LI,"800","middle")+t(120,138,"260K model",12,DIM,"400","middle")+
 t(360,112,"7.8x",44,AM,"800","middle")+t(360,138,"15M model",12,DIM,"400","middle")+
 t(240,182,"faster than plain C, same core",14,TX,"700","middle")+
 t(240,214,"logits BIT-IDENTICAL: 512 / 512",14,CY,"800","middle")+
 t(240,240,"7,907 forward passes, 0 mismatches",12,DIM,"400","middle"),"06  MEASURED ON THE BOARD")
# 7 TWIST
A['07_twist']=wrap(
 t(240,100,"THE BUG",16,RD,"800","middle")+
 t(240,142,"AAAAAAAA",32,RD,"800","middle")+t(240,178,"F00DF00D",32,RD,"800","middle")+
 t(240,212,"two words my boot logic overwrote",14,TX,"700","middle")+
 t(240,238,"every 'random' stall traced to these",12,DIM,"400","middle"),"07  THE TWIST")
# 8 CTA loss curve
import math
pts=[(2.89,0),(2.21,1),(0.81,2),(0.46,3),(1.07,4),(0.30,5),(0.03,6),(0.04,7),(0.002,8),(0.002,9),(0.0001,14),(0.0001,19)]
px=[]
for v,e in pts:
    x=40+e/19*220; y=210-(math.log10(v+1e-4)+4)/ (math.log10(2.89)+4)*120
    px.append(f"{x:.1f},{y:.1f}")
A['08_learns']=wrap(
 f'<polyline points="{" ".join(px)}" fill="none" stroke="{LI}" stroke-width="3" stroke-linejoin="round"/>'+
 t(40,232,"loss 2.89",11,DIM,"400")+t(260,232,"0.0001",11,LI,"800","end")+t(150,250,"20 epochs, on-chip",11,DIM,"400","middle")+
 t(276,90,"BEFORE",11,DIM,"700")+t(276,110,"girl named Lily",14,TX,"700")+
 t(276,150,"AFTER",11,AM,"700")+t(276,170,"robot named Arty",14,AM,"800")+
 t(276,206,"it learned",14,LI,"800")+t(276,226,"on my silicon",14,LI,"800"),"08  IT LEARNS")
for k,s in A.items():
    open(k+'.svg','w').write(s)
    cairosvg.svg2png(bytestring=s.encode(),write_to=k+'.png',output_width=960,output_height=540)
print(sorted(A))
