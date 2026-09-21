#!/usr/bin/env python3
"""Turn a raw UART console capture of the DOOM stream into a reel video.

Usage: render_reel.py capture.log out_dir [max_frames] [fps]

The board sends each frame as ESC[H followed by 32 lines of 64 density
characters (" .:-=+*#%@"). Only COMPLETE frames (exactly 32 lines x 64
columns) are used -- the first (after ESC[2J) and the last (cut off when the
capture stopped) are dropped, never repaired. Every kept frame becomes one
video frame, in capture order, nothing interpolated or duplicated.
"""
import os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'software', 'doom', 'baremetal-arty100t'))
from doom_color import parse_frame as _parse_frame, FRAME_MARKER, PacketDecoder, MAGIC

COLS, ROWS = 64, 32
RAMP = " .:-=+*#%@"
W, H = 1080, 1920

def parse_frames(path):
    """Complete frames only (damaged ones dropped, never repaired).
    v3 stream (160x100 palette + row packets): a frame counts only if ALL 100 rows arrived intact.
    v2 stream (64x32 color) / legacy density stream: as before."""
    d = open(path, 'rb').read()
    if MAGIC in d[:400000] and d.count(MAGIC) > 200:
        dec = PacketDecoder()
        frames = [f for f, complete in dec.feed(d) if complete]
        print('v3 stream:', dec.stats)
        return frames
    i = d.find(b'backend up')
    if i >= 0:
        d = d[i:]
    frames = []
    for p in d.split(FRAME_MARKER)[1:-1]:
        fr = _parse_frame(p, ROWS, COLS)
        if fr is not None:
            frames.append(fr)
    return frames

def frame_image(arr):
    return Image.fromarray(arr, 'RGB')

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
    view_w, view_h = (960, 600) if frames and frames[0].shape[1] >= 160 else (1024, 640)   # DOOM is 16:10
    vx, vy = (W - view_w) // 2, 520
    for k, arr in enumerate(frames):
        cv = Image.new('RGB', (W, H), bg)
        d = ImageDraw.Draw(cv)
        d.text((W // 2, 200), "DOOM", font=f_big, fill=(255, 176, 66), anchor='mm')
        d.text((W // 2, 290), "running on a CPU we designed", font=f_mid, fill=(214, 220, 232), anchor='mm')
        d.text((W // 2, 350), "RISC-V Rocket, 50 MHz, Arty A7-100T FPGA", font=f_sm, fill=(140, 150, 168), anchor='mm')
        d.rectangle([vx - 6, vy - 6, vx + view_w + 5, vy + view_h + 5], outline=(70, 78, 96), width=3)
        cv.paste(frame_image(arr).resize((view_w, view_h), Image.NEAREST), (vx, vy))
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
