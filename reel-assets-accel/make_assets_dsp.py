import cairosvg
BG="#0b1118"; PANEL="#111a24"; CY="#4dd2ff"; LI="#b6ff5c"; AM="#ffb020"; TX="#e8f1f8"; DIM="#7b8b99"
F="font-family=\"DejaVu Sans Mono, Menlo, Consolas, monospace\""
def t(x,y,s,size=16,fill=TX,w="700",anchor="start"): return f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{w}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
W,H=480,270
def card(body,tag):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect x="4" y="4" width="{W-8}" height="{H-8}" rx="18" fill="{BG}" fill-opacity="0.88" stroke="{CY}" stroke-opacity="0.55" stroke-width="2"/><text x="22" y="30" {F} font-size="12" fill="{DIM}" letter-spacing="2">{tag}</text>{body}</svg>'''
# card 25: LUT usage of the three builds (device has 63,400 LUTs)
rows=[("no accelerator",46596,DIM),("systolic (LUT multipliers)",55050,AM),("systolic (DSP slices)",49103,LI)]
b=""; y=62
for name,v,col in rows:
    w=int(300*v/63400)
    b+=t(22,y+12,name,12,TX,"700")+f'<rect x="22" y="{y+18}" width="300" height="24" rx="6" fill="{PANEL}" stroke="{DIM}" stroke-opacity="0.5"/><rect x="22" y="{y+18}" width="{w}" height="24" rx="6" fill="{col}" fill-opacity="0.9"/>'+t(332,y+37,f"{v:,}",14,col,"800")
    y+=58
b+=t(240,246,"logic cells used, out of 63,400 on the chip",11,DIM,"400","middle")
open('25_luts.svg','w').write(card(b,"FPGA LOGIC USED (LUTs)"))
# card 26: DSP slices
b=(t(240,100,"+64 DSP slices",40,LI,"800","middle")+t(240,128,"one per processing element",14,TX,"700","middle")+
   t(240,170,"engine logic: 8,450 -> 2,500 LUTs",15,AM,"800","middle")+t(240,194,"about 70% smaller",13,TX,"700","middle")+
   t(240,232,"27 -> 91 of 240 DSP slices used  |  timing met",11,DIM,"400","middle")+t(240,250,"numbers from the Vivado reports",10,DIM,"400","middle"))
open('26_dsp.svg','w').write(card(b,"MOVING THE MULTIPLIERS ONTO DSP SLICES"))
for k in ['25_luts','26_dsp']:
    cairosvg.svg2png(url=k+'.svg',write_to=k+'.png',output_width=960,output_height=540)
from PIL import Image
a=Image.open('25_luts.png').convert('RGBA').resize((480,270)); c=Image.open('26_dsp.png').convert('RGBA').resize((480,270))
s=Image.new('RGBA',(980,290),(40,60,90,255)); s.paste(a,(10,10),a); s.paste(c,(495,10),c); s.convert('RGB').save('_contact_sheet_dsp.png'); print('ok')
