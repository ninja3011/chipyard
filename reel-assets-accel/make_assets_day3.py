import cairosvg
W,H=480,270
BG="#0b1118"; PANEL="#111a24"; CY="#4dd2ff"; LI="#b6ff5c"; AM="#ffb020"; TX="#e8f1f8"; DIM="#7b8b99"
F="font-family=\"DejaVu Sans Mono, Menlo, Consolas, monospace\""
def wrap(body, tag):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<rect x="4" y="4" width="{W-8}" height="{H-8}" rx="18" fill="{BG}" fill-opacity="0.88" stroke="{CY}" stroke-opacity="0.55" stroke-width="2"/>
<text x="22" y="30" {F} font-size="12" fill="{DIM}" letter-spacing="2">{tag}</text>
{body}</svg>'''
def t(x,y,s,size=16,fill=TX,w="700",anchor="start"):
    return f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{w}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
def tile(x,y,s,color,op=0.9,label=None):
    o=f'<rect x="{x}" y="{y}" width="{s}" height="{s}" rx="5" fill="{color}" fill-opacity="{op}"/>'
    if label: o+=t(x+s/2,y+s/2+4,label,11,BG,"800","middle")
    return o
A={}
# card 9: cut -> multiply -> add
body=""
gx,gy,bs,gap=26,44,30,5
for r in range(4):
    for c in range(4):
        hot = (r==1)
        body+=tile(gx+c*(bs+gap),gy+r*(bs+gap),bs,AM if hot else CY,0.95 if hot else 0.28)
body+=t(gx+70,gy+4*(bs+gap)+16,"BIG MATRIX",11,DIM,"700","middle")
body+=f'<line x1="188" y1="108" x2="212" y2="108" stroke="{DIM}" stroke-width="2.5"/><polygon points="220,108 210,102 210,114" fill="{DIM}"/>'
# three tiles adding into accumulator
x0=232; y0=88
for i in range(3):
    body+=tile(x0+i*46,y0,34,AM,0.95,str(i+1))
    if i<2: body+=t(x0+i*46+40,y0+23,"+",18,TX,"800","middle")
body+=t(x0+3*46-4,y0+23,"=",20,TX,"800","middle")
body+=tile(x0+3*46+14,y0-3,40,LI,0.95)
body+=t(x0+3*46+34,y0+60,"C int32",11,LI,"700","middle")
body+=t(354,182,"8x8 tiles, one after another",12,DIM,"400","middle")
body+=t(240,232,"CUT  >  MULTIPLY  >  KEEP ADDING",16,AM,"800","middle")
body+=t(240,254,"same 32-bit accumulator until the answer is done",11,DIM,"400","middle")
A['09_tiling']=wrap(body,"09  ANY SIZE")
# card 10: test result
body=(
 t(240,84,"16 x 24 x 32",34,CY,"800","middle")+t(240,106,"multi-tile multiply, real test",12,DIM,"400","middle")+
 t(40,150,"4 / 4",26,LI,"800")+t(150,150,"flip combos",15,TX,"700")+t(440,150,"MATCH C",15,LI,"800","end")+
 t(40,190,"~120x",26,AM,"800")+t(150,190,"faster than the CPU",15,TX,"700")+t(440,190,"in simulation",12,DIM,"400","end")+
 t(240,240,"flip = load the mirrored tile + one switch",12,DIM,"400","middle"))
A['10_gemm_test']=wrap(body,"10  THE TEST")
for k,s in A.items():
    open(k+'.svg','w').write(s)
    cairosvg.svg2png(bytestring=s.encode(),write_to=k+'.png',output_width=960,output_height=540)
from PIL import Image
ims=[Image.open(k+'.png').convert('RGBA') for k in A]
sheet=Image.new('RGBA',(980,290),(40,60,90,255))
for i,im in enumerate(ims):
    r=im.resize((480,270)); sheet.paste(r,(10+i*485,10),r)
sheet.convert('RGB').save('_contact_sheet_day3.png'); print(sorted(A))
