#!/usr/bin/env python3
"""Render a text transcript as a paced terminal video, for content that has NO real per-character timing
(e.g. a host-run script that finishes in seconds). Pacing here is ARTIFICIAL, chosen for readability, and
the video says so on screen -- this is NOT a replay of a real capture (see replay_to_video.py for that).
usage: render_paced_video.py in.txt out.mp4 --title "..." --sub "..." --realnote "..."
"""
import sys, argparse, subprocess
from PIL import Image, ImageDraw, ImageFont
ap = argparse.ArgumentParser(); ap.add_argument('inp'); ap.add_argument('out')
ap.add_argument('--title', default='terminal'); ap.add_argument('--sub', default='illustrative pacing -- not a real-time capture')
ap.add_argument('--realnote', default=''); ap.add_argument('--w', type=int, default=1080); ap.add_argument('--h', type=int, default=1920)
ap.add_argument('--fps', type=int, default=30); ap.add_argument('--fontsize', type=int, default=32); ap.add_argument('--hold', type=float, default=3.0)
a = ap.parse_args()
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"; FB = FONT.replace("Mono.ttf", "Mono-Bold.ttf")
f = ImageFont.truetype(FONT, a.fontsize); fb = ImageFont.truetype(FB, a.fontsize); fs = ImageFont.truetype(FONT, 24); ft = ImageFont.truetype(FB, 38)

# Build a stream of (char, is_new_line_start) events with per-line pacing rules:
#  prompt lines (">"): typed at human typing speed
#  epoch/loss lines: appear whole, one per beat, so the curve is readable
#  reply lines: streamed at a steady "generation" pace
raw_lines = open(a.inp).read().rstrip("\n").split("\n")
events = []  # (char_or_None_for_line_reveal, dt)
for ln in raw_lines:
    if ln.startswith(">"):
        for ch in ln: events.append((ch, 0.035))
        events.append(('\n', 0.15))
    elif ln.startswith("epoch"):
        events.append(('LINE:' + ln, 0.55))
    elif ln == "" or ln.startswith("---"):
        events.append(('LINE:' + ln, 0.25))
    else:
        for ch in ln: events.append((ch, 0.018))
        events.append(('\n', 0.4))

cw = f.getbbox("M")[2]; lh = a.fontsize + 12; cols = (a.w - 80) // cw; rows = (a.h - 420) // lh

def wrap_push(lines, cur, text):
    cur += text
    while len(cur) > cols:
        cut = cur.rfind(' ', 0, cols)
        if cut <= 0: cut = cols
        lines.append(cur[:cut]); cur = cur[cut:].lstrip(' ')
    return cur

ff = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{a.w}x{a.h}', '-r', str(a.fps), '-i', '-',
                       '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', a.out], stdin=subprocess.PIPE)

lines = []; cur = ''
t = 0.0
frame_script = []  # (t_at, lines_snapshot, cur_snapshot)
for ev, dt in events:
    if isinstance(ev, str) and ev.startswith('LINE:'):
        if cur: lines.append(cur); cur = ''
        lines.append(ev[5:])
    elif ev == '\n':
        lines.append(cur); cur = ''
    else:
        cur = wrap_push(lines, cur, ev)
    t += dt
    frame_script.append((t, list(lines), cur))
total = t + a.hold

def draw_frame(tt):
    lines_now, cur_now = [], ''
    for tstamp, ls, c in frame_script:
        if tstamp <= tt: lines_now, cur_now = ls, c
        else: break
    disp = (lines_now + ([cur_now] if cur_now else []))[-rows:]
    im = Image.new('RGB', (a.w, a.h), (13, 17, 23)); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, a.w, 90], fill=(33, 38, 45))
    for k, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]): d.ellipse([34 + k * 44, 30, 62 + k * 44, 58], fill=c)
    d.text((190, 24), a.title, font=ft, fill=(201, 209, 217))
    y = 130
    for ln in disp:
        col = (121, 192, 255) if ln.startswith('>') else (210, 168, 255) if ln.startswith('epoch') else (201, 209, 217)
        d.text((40, y), ln, font=fb if ln.startswith('>') else f, fill=col); y += lh
    if int(tt * 2) % 2 == 0 and tt < total - a.hold: d.rectangle([40, y - lh + 10, 40 + cw * 0.6, y - 6], fill=(201, 209, 217))
    d.rectangle([0, a.h - 190, a.w, a.h], fill=(33, 38, 45)); yy = a.h - 172
    d.text((40, yy), a.sub, font=fs, fill=(139, 148, 158)); yy += 32
    words = a.realnote.split(' '); wlines = []; cur_w = ''
    for w in words:
        if len(cur_w) + 1 + len(w) > 78 and cur_w: wlines.append(cur_w); cur_w = w
        else: cur_w = (cur_w + ' ' + w).strip()
    if cur_w: wlines.append(cur_w)
    for wl in wlines:
        d.text((40, yy), wl, font=fs, fill=(240, 136, 62)); yy += 32
    ff.stdin.write(im.tobytes())

n = int(total * a.fps)
for i in range(n): draw_frame(i / a.fps)
ff.stdin.close(); ff.wait(); print('wrote', a.out, f'{total:.1f}s')
