#!/usr/bin/env python3
"""Live viewer + keyboard for DOOM running on the Arty (color console protocol).

    python3 doom_viewer.py [/dev/ttyUSB2] [--record capture.log]

Click the window and play:
  W/S  forward/back      A/D  turn left/right      ,/.  strafe left/right
  SPACE fire             E  use / open doors       R  run (hold)
  ENTER / ESC  menu      TAB  automap              1-7  weapons     Y/N  menu prompts
  [ / ]  lower / higher resolution (160x100 .. 320x200)
Keys are forwarded straight to the board over the same FT232R link that carries
the picture (921600 baud). Hold a key: the terminal auto-repeat keeps it "down".
"""
import os, subprocess, sys, time
import tkinter as tk
import numpy as np
from PIL import Image, ImageTk
from doom_color import parse_frame, FRAME_MARKER, PacketDecoder

DEVICE = next((a for a in sys.argv[1:] if a.startswith("/dev/")), "/dev/ttyUSB2")
RECORD = sys.argv[sys.argv.index("--record") + 1] if "--record" in sys.argv else None
BAUD = "921600"
ROWS, COLS = 32, 64
SCALE_X, SCALE_Y = 16, 20            # legacy 64x32 stream -> 1024x640 (DOOM's 16:10)
VIEW3 = (960, 600)                   # packet stream, any resolution -> 960x600 (DOOM is 16:10)

KEYMAP = {"Return": b"\r", "Escape": b"\x1b", "Tab": b"\t", "space": b" ",
          "Up": b"w", "Down": b"s", "Left": b"a", "Right": b"d",
          "Shift_L": b"r", "Shift_R": b"r"}

def configure_serial(dev):
    subprocess.run(["stty", "-F", dev, BAUD, "cs8", "-cstopb", "-parenb", "raw", "-echo", "clocal"], check=True)

class Viewer:
    def __init__(self, root):
        self.root = root
        self.label = tk.Label(root, bg="black")
        self.label.pack()
        self.status = tk.StringVar(value=f"[{DEVICE}] connecting...")
        tk.Label(root, textvariable=self.status, bg="black", fg="#39ff14",
                 font=("monospace", 10), anchor="w").pack(fill="x")
        tk.Label(root, text="WASD move/turn  ,/. strafe  SPACE fire  E use  R run  ENTER/ESC menu  TAB map  1-7 weapons  [ ] resolution",
                 bg="black", fg="#7fdaff", font=("monospace", 9), anchor="w").pack(fill="x")
        self.fd = None; self.buf = b""; self.frames = 0; self.t0 = time.time(); self.rec = open(RECORD, "wb") if RECORD else None
        self.tk_img = None; self.last_text = ""; self.dec = PacketDecoder(); self.shown = 0
        root.bind("<KeyPress>", self.on_key); root.focus_set()
        root.after(10, self.connect)

    def connect(self):
        try:
            configure_serial(DEVICE)
            self.fd = os.open(DEVICE, os.O_RDWR | os.O_NONBLOCK)
            self.status.set(f"[{DEVICE}] connected @ {BAUD}, waiting for frames...")
            self.poll()
        except Exception as e:
            self.status.set(f"[{DEVICE}] connect failed: {e} -- retrying...")
            self.root.after(2000, self.connect)

    def on_key(self, ev):
        b = KEYMAP.get(ev.keysym) or (ev.char.encode("ascii", "ignore") if ev.char else b"")
        if b and self.fd is not None:
            try: os.write(self.fd, b)
            except OSError: pass

    def poll(self):
        chunk_bytes = b""
        try:
            chunk_bytes = os.read(self.fd, 1 << 16)
            if chunk_bytes and self.rec:
                self.rec.write(chunk_bytes); self.rec.flush()
        except BlockingIOError:
            pass
        if chunk_bytes:
            # v3: palette + checksummed row packets (160x100). Damaged rows are dropped alone.
            frames = self.dec.feed(chunk_bytes)
            if frames:
                arr = frames[-1][0]
                im = Image.fromarray(arr, "RGB").resize(VIEW3, Image.NEAREST)
                self.tk_img = ImageTk.PhotoImage(im)
                self.label.configure(image=self.tk_img)
                self.frames += len(frames); self.shown += 1
                st = self.dec.stats; dt = max(time.time() - self.t0, 1e-3)
                tot = st["rows_ok"] + st["bad_packets"]
                self.status.set(f"{arr.shape[1]}x{arr.shape[0]}   frames={self.frames}  {self.frames / dt:.1f} fps   damaged packets {100 * st['bad_packets'] / max(tot, 1):.1f}%   ( [ / ]  = lower / higher resolution )")
        if self.dec.stats["rows_ok"] == 0:
            # legacy 64x32 stream (ESC[H frames) -- only while no v3 packets have been seen
            self.buf += chunk_bytes
            if len(self.buf) > 1 << 20:
                self.buf = self.buf[-(1 << 16):]
            parts = self.buf.split(FRAME_MARKER)
            if len(parts) > 2:
                latest = None
                for raw in parts[1:-1]:
                    fr = parse_frame(raw, ROWS, COLS)
                    if fr is not None:
                        latest = fr; self.frames += 1
                self.buf = FRAME_MARKER + parts[-1]
                if latest is not None:
                    im = Image.fromarray(latest, "RGB").resize((COLS * SCALE_X, ROWS * SCALE_Y), Image.NEAREST)
                    self.tk_img = ImageTk.PhotoImage(im)
                    self.label.configure(image=self.tk_img)
                    dt = max(time.time() - self.t0, 1e-3)
                    self.status.set(f"[{DEVICE}] frames={self.frames}  avg {self.frames / dt:.1f} fps (legacy 64x32)")
            elif not self.frames:
                txt = self.buf.decode("ascii", "replace").replace("\r", "").strip().split("\n")
                if txt and txt[-1].strip(): self.status.set(f"[{DEVICE}] {txt[-1][:100]}")
        else:
            self.buf = b""
        self.root.after(15, self.poll)

def main():
    root = tk.Tk(); root.title("DOOM on Rocket / Arty A7-100T")
    Viewer(root); root.mainloop()

if __name__ == "__main__":
    main()
