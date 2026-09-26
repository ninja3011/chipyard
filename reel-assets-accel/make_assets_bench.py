import cairosvg
BG="#0b1118"; PANEL="#111a24"; CY="#4dd2ff"; LI="#b6ff5c"; AM="#ffb020"; TX="#e8f1f8"; DIM="#7b8b99"
F="font-family=\"DejaVu Sans Mono, Menlo, Consolas, monospace\""
def t(x,y,s,size=16,fill=TX,w="700",anchor="start"): return f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{w}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
W,H=480,270
rows=[("plain CPU",0.24,DIM,"1x"),("systolic",1.57,CY,"6.3x"),("broadcast",1.74,LI,"7.0x")]
b=""
mx=1.74; y=68
for name,v,col,sp in rows:
    w=max(6,int(205*v/mx)); b+=t(22,y+22,name,14,TX,"800")+f'<rect x="130" y="{y}" width="{w}" height="34" rx="8" fill="{col}" fill-opacity="0.9"/>'+t(130+w+10,y+23,f"{v} tok/s",14,col,"800")+t(458,y+50,sp,12,DIM,"700","end"); y+=62
svg=f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect x="4" y="4" width="{W-8}" height="{H-8}" rx="18" fill="{BG}" fill-opacity="0.88" stroke="{CY}" stroke-opacity="0.55" stroke-width="2"/>
<text x="22" y="30" {F} font-size="12" fill="{DIM}" letter-spacing="2">BENCHMARK: 15M-PARAM MODEL, GENERATING TEXT</text>{b}
{t(240,258,"measured on the board, same tokens in every run",11,DIM,"400","middle")}</svg>'''
open('24_benchmark.svg','w').write(svg); cairosvg.svg2png(bytestring=svg.encode(),write_to='24_benchmark.png',output_width=960,output_height=540); print('ok')
