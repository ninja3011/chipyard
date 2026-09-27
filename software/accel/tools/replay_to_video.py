#!/usr/bin/env python3
"""Replay a REAL timestamped serial capture (serial_log.py .ts.jsonl) as a vertical terminal video.
Not a screen recording: it re-draws the captured text with the captured timing.
usage: replay_to_video.py in.ts.jsonl out.mp4 --title "..." [--speed 1] [--max-gap 3] [--w 1080 --h 1920] [--tail 0]
--speed N compresses time N x (a badge on screen says so); --max-gap caps silent gaps (also badged)."""
import sys, json, subprocess, argparse
from PIL import Image, ImageDraw, ImageFont
ap = argparse.ArgumentParser(); ap.add_argument('inp'); ap.add_argument('out'); ap.add_argument('--title', default='live from the chip')
ap.add_argument('--speed', type=float, default=1.0); ap.add_argument('--max-gap', type=float, default=0); ap.add_argument('--w', type=int, default=1080)
ap.add_argument('--h', type=int, default=1920); ap.add_argument('--fps', type=int, default=30); ap.add_argument('--hold', type=float, default=2.5)
ap.add_argument('--fontsize', type=int, default=34); ap.add_argument('--start', default=''); ap.add_argument('--sub', default='real serial capture (FT232R, 115200 baud)')
a = ap.parse_args()
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"; FB = FONT.replace("Mono.ttf", "Mono-Bold.ttf")
f = ImageFont.truetype(FONT, a.fontsize); fb = ImageFont.truetype(FB, a.fontsize); fs = ImageFont.truetype(FONT, 26); ft = ImageFont.truetype(FB, 40)
chunks = [json.loads(l) for l in open(a.inp)]
# start marker: drop everything before the first occurrence of --start (e.g. "> /train")
if a.start:
    joined = ''.join(c[1] for c in chunks); k = joined.find(a.start)
    if k > 0:
        acc = 0
        for i, c in enumerate(chunks):
            if acc + len(c[1]) > k: chunks[i][1] = c[1][k - acc:]; chunks = chunks[i:]; break
            acc += len(c[1])
t_base = chunks[0][0]; events = []; comp = 0.0; prev = t_base; capped = False
for t, s in chunks:
    gap = t - prev
    if a.max_gap and gap > a.max_gap: comp += gap - a.max_gap; capped = True
    prev = t; events.append(((t - t_base - comp) / a.speed, s))
total = events[-1][0] + a.hold
cw = f.getbbox("M")[2]; lh = a.fontsize + 10; cols = (a.w - 80) // cw; rows = (a.h - 380) // lh
def screen(upto):
    text = ''.join(s for tt, s in events if tt <= upto).replace('\r', '')
    out = []; line = ''
    for ch in text:
        if ch == '\n': out.append(line); line = ''
        elif ch == '\b': line = line[:-1]
        else: line += ch
        while len(line) > cols:
            cut = line.rfind(' ', 0, cols)
            if cut <= 0: cut = cols
            out.append(line[:cut]); line = line[cut:].lstrip(' ') if cut < cols or line[cut:cut+1] == ' ' else line[cut:]
    out.append(line); return out[-rows:]
ff = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{a.w}x{a.h}', '-r', str(a.fps), '-i', '-',
                       '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', a.out], stdin=subprocess.PIPE)
n = int(total * a.fps)
for i in range(n):
    tt = i / a.fps; im = Image.new('RGB', (a.w, a.h), (13, 17, 23)); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, a.w, 90], fill=(33, 38, 45))
    for k, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]): d.ellipse([34 + k * 44, 30, 62 + k * 44, 58], fill=c)
    d.text((190, 24), a.title, font=ft, fill=(201, 209, 217))
    y = 130
    for ln in screen(tt):
        col = (121, 192, 255) if ln.startswith('>') else (63, 185, 80) if ('PASS' in ln or ' ok' in ln) else (210, 168, 255) if ln.startswith('epoch') else (240, 136, 62) if ln.startswith('[') else (201, 209, 217)
        d.text((40, y), ln, font=fb if ln.startswith('>') else f, fill=col); y += lh
    if int(tt * 2) % 2 == 0: d.rectangle([40, y - lh + 6, 40 + cw * 0.6, y - 8], fill=(201, 209, 217))
    badge = [a.sub]
    if a.speed != 1: badge.append(f'time compressed {a.speed:g}x')
    if capped: badge.append(f'idle gaps > {a.max_gap:g}s shortened')
    d.rectangle([0, a.h - 150, a.w, a.h], fill=(33, 38, 45)); yy = a.h - 132
    for b_ in badge: d.text((40, yy), b_, font=fs, fill=(240, 136, 62) if 'compressed' in b_ or 'shortened' in b_ else (139, 148, 158)); yy += 38
    ff.stdin.write(im.tobytes())
ff.stdin.close(); ff.wait(); print('wrote', a.out, f'{total:.1f}s')
