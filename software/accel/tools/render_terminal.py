#!/usr/bin/env python3
"""Render REAL captured console text as a terminal-style PNG (not a screen grab).
usage: render_terminal.py out.png "title" "provenance line" textfile [--note "footnote"]
The text is drawn exactly as given; nothing is edited."""
import sys
from PIL import Image, ImageDraw, ImageFont
FONT="/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"; FONTB=FONT.replace("Mono.ttf","Mono-Bold.ttf")
def render(out,title,prov,text,note=None,cols=100):
    fs=17; f=ImageFont.truetype(FONT,fs); fb=ImageFont.truetype(FONTB,fs); fsm=ImageFont.truetype(FONT,13)
    # hard-wrap only very long lines at `cols`
    lines=[]
    for l in text.rstrip("\n").split("\n"):
        while len(l)>cols: lines.append(l[:cols]); l=l[cols:]
        lines.append(l)
    cw=f.getbbox("M")[2]; lh=fs+5
    W=cw*cols+48; H=60+lh*len(lines)+(58 if note else 40)+26
    im=Image.new("RGB",(W,H),(13,17,23)); d=ImageDraw.Draw(im)
    d.rectangle([0,0,W,38],fill=(33,38,45))
    for i,c in enumerate([(255,95,86),(255,189,46),(39,201,63)]): d.ellipse([14+i*22,12,26+i*22,24],fill=c)
    d.text((100,10),title,font=fsm,fill=(201,209,217))
    y=52
    for l in lines:
        col=(201,209,217)
        if l.startswith(">"): col=(121,192,255)
        elif "ALL PASS" in l or l.strip().endswith(" ok") or " ok " in l: col=(63,185,80)
        elif "MISMATCH" in l or "FAIL" in l: col=(248,81,73)
        elif l.startswith("epoch") or "loss" in l: col=(210,168,255)
        elif l.startswith("["): col=(240,136,62)
        d.text((24,y),l,font=fb if l.startswith(">") else f,fill=col); y+=lh
    y+=8; d.line([24,y,W-24,y],fill=(48,54,61)); y+=8
    d.text((24,y),prov,font=fsm,fill=(139,148,158)); y+=20
    if note: d.text((24,y),note,font=fsm,fill=(210,153,34))
    im.save(out); return im.size
if __name__=="__main__":
    a=sys.argv[1:]; note=None
    if "--note" in a: i=a.index("--note"); note=a[i+1]; a=a[:i]+a[i+2:]
    print(render(a[0],a[1],a[2],open(a[3]).read(),note))
