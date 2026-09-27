import cairosvg
BG="#0b1118"; PANEL="#111a24"; CY="#4dd2ff"; LI="#b6ff5c"; AM="#ffb020"; TX="#e8f1f8"; DIM="#7b8b99"
F="font-family=\"DejaVu Sans Mono, Menlo, Consolas, monospace\""
t=lambda x,y,s,size=16,fill=TX,w="700",anchor="start": f'<text x="{x}" y="{y}" {F} font-size="{size}" font-weight="{w}" fill="{fill}" text-anchor="{anchor}">{s}</text>'
W,H=1080,1080
hdr=["build","tokens/s","vs CPU","LUTs","DSPs","hold ns"]
xs=[50,430,570,690,850,940]
rows=[("plain CPU (no engine)","0.24","1x","46,596","25","n/a",DIM),
      ("broadcast engine","1.74","7.0x","55,037","27","-0.02*",AM),
      ("systolic (LUT mults)","1.57","6.3x","55,050","27","+0.05",CY),
      ("systolic (DSP slices)","1.57","6.3x","49,103","91","0.00",LI)]
b=t(W//2,90,"FOUR BUILDS, ONE MODEL",56,TX,"800","middle")+t(W//2,140,"15M-parameter model generating text, measured on the board",26,DIM,"400","middle")
y=230
for x,h in zip(xs,hdr): b+=t(x,y,h,26,DIM,"700")
b+=f'<line x1="40" y1="{y+16}" x2="1040" y2="{y+16}" stroke="#30363d" stroke-width="2"/>'
y+=80
for name,tps,sp,lut,dsp,tm,col in rows:
    b+=f'<rect x="30" y="{y-46}" width="1020" height="84" rx="14" fill="{PANEL}" stroke="{col}" stroke-opacity="0.6" stroke-width="2"/>'
    for x,v,c in zip(xs,[name,tps,sp,lut,dsp,tm],[TX,col,col,TX,TX,TX]):
        b+=t(x,y+8,v,26 if x==xs[0] else 30,c,"800")
    y+=110
b+=t(50,y+30,"Both systolic builds run at the same speed;",26,TX,"700")+t(50,y+64,"the DSP one uses ~6,000 fewer LUTs.",26,TX,"700")
b+=t(50,y+112,"Broadcast is ~10% faster (systolic skew adds ~12 cycles/tile).",23,DIM,"400")
b+=t(50,y+154,"* -0.020 ns, inside the RISC-V debug module (not the CPU/engine).",22,DIM,"400")
b+=t(50,y+188,"tokens/s measured on the board; LUT/DSP/hold slack from Vivado.",22,DIM,"400")
svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}"><rect width="{W}" height="{H}" fill="{BG}"/>{b}</svg>'
open('28_four_builds_table.svg','w').write(svg); cairosvg.svg2png(bytestring=svg.encode(),write_to='28_four_builds_table.png'); print('ok')
