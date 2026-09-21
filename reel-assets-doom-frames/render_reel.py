#!/usr/bin/env python3
"""Turn a raw UART console capture of the DOOM ASCII stream into a reel video.

Usage: render_reel.py capture.log out_dir [max_frames] [fps]

The board sends each frame as ESC[H followed by 32 lines of 64 density
characters (" .:-=+*#%@"). Only COMPLETE frames (exactly 32 lines x 64
columns) are used -- the first (after ESC[2J) and the last (cut off when the
capture stopped) are dropped, never repaired. Every kept frame becomes one
video frame, in capture order, nothing interpolated or duplicated.
"""
import os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 64, 32
RAMP = " .:-=+*#%@"
W, H = 1080, 1920

def parse_frames(path):
    d = open(path, 'rb').read().replace(b'\r', b'')
    parts = d.split(b'\x1b[H')[1:]            # drop everything before the first frame marker
    frames = []
    for p in parts:
        lines = p.decode('ascii', 'replace').split('\n')
        while lines and lines[-1] == '':
            lines.pop()
        if len(lines) == ROWS and all(len(l) == COLS for l in lines):
            frames.append(lines)
    return frames

def color(ch):
    i = RAMP.find(ch)
    if i < 0:
        return (32, 32, 32)
    v = i / (len(RAMP) - 1)
    g = int(14 + 236 * v)                      # neutral gray, slightly warm like a CRT
    return (g, int(g * 0.97), int(g * 0.90))

def frame_image(lines):
    im = Image.new('RGB', (COLS, ROWS))
    px = im.load()
    for y, l in enumerate(lines):
        for x, ch in enumerate(l):
            px[x, y] = color(ch)
    return im

def main(log, out, max_frames=1000, fps=25):
    os.makedirs(out, exist_ok=True)
    frames = parse_frames(log)[:max_frames]
    n = len(frames)
    print('complete frames used:', n)
    fdir = os.path.join(out, 'frames'); os.makedirs(fdir, exist_ok=True)
    f_big = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 64)
    f_mid = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 34)
    f_sm = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 28)
    bg = (11, 13, 18)
    view_w, view_h = 1008, 504                  # 16x cells, exact integer scale
    vx, vy = (W - view_w) // 2, 560
    for k, lines in enumerate(frames):
        cv = Image.new('RGB', (W, H), bg)
        d = ImageDraw.Draw(cv)
        d.text((W // 2, 200), "DOOM", font=f_big, fill=(255, 176, 66), anchor='mm')
        d.text((W // 2, 290), "running on a CPU we designed", font=f_mid, fill=(214, 220, 232), anchor='mm')
        d.text((W // 2, 350), "RISC-V Rocket, 50 MHz, Arty A7-100T FPGA", font=f_sm, fill=(140, 150, 168), anchor='mm')
        d.rectangle([vx - 6, vy - 6, vx + view_w + 5, vy + view_h + 5], outline=(70, 78, 96), width=3)
        cv.paste(frame_image(lines).resize((view_w, view_h), Image.NEAREST), (vx, vy))
        d.text((W // 2, vy + view_h + 70), f"frame {k + 1:04d} / {n:04d}", font=f_mid, fill=(255, 176, 66), anchor='mm')
        d.text((W // 2, vy + view_h + 125), "each frame streamed live over UART", font=f_sm, fill=(140, 150, 168), anchor='mm')
        bar_w = int((W - 160) * (k + 1) / n)
        d.rectangle([80, H - 220, W - 80, H - 200], outline=(70, 78, 96), width=2)
        d.rectangle([80, H - 220, 80 + bar_w, H - 200], fill=(255, 176, 66))
        d.text((W // 2, H - 150), "not an emulator: real chip, real UART", font=f_sm, fill=(140, 150, 168), anchor='mm')
        cv.save(os.path.join(fdir, f'f{k:04d}.png'))
        if k == n // 2:
            cv.save(os.path.join(out, 'poster.png'))
    mp4 = os.path.join(out, 'doom_frames_reel.mp4')
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(fps), '-i', os.path.join(fdir, 'f%04d.png'),
                    '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18', mp4], check=True)
    print('wrote', mp4)
    return n

if __name__ == '__main__':
    a = sys.argv
    main(a[1], a[2], int(a[3]) if len(a) > 3 else 1000, int(a[4]) if len(a) > 4 else 25)
