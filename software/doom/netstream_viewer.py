#!/usr/bin/env python3
"""Live viewer for doomgeneric's netstream backend.

Connects to the DOOM target over TCP, displays incoming frames in a window,
and forwards keypresses back to the target so it plays like a normal game.

Usage:
    python3 netstream_viewer.py <target-host> [port]

If the target is a FireSim-simulated node reachable only from the run-farm
host (e.g. 172.16.0.2 per FireSim's networking docs), tunnel first:

    ssh -N -L 5678:172.16.0.2:5678 YOUR_RUN_FARM_INSTANCE_IP

then run this viewer against 127.0.0.1.
"""

import socket
import struct
import sys
import threading
import tkinter as tk
from PIL import Image, ImageTk

FRAME_HEADER = struct.Struct("<4sII")  # magic, width, height

# doomkeys.h values this viewer can produce
KEY_RIGHTARROW = 0xAE
KEY_LEFTARROW = 0xAC
KEY_UPARROW = 0xAD
KEY_DOWNARROW = 0xAF
KEY_STRAFE_L = 0xA0
KEY_STRAFE_R = 0xA1
KEY_USE = 0xA2
KEY_FIRE = 0xA3
KEY_ESCAPE = 27
KEY_ENTER = 13
KEY_TAB = 9
KEY_BACKSPACE = 0x7F
KEY_RSHIFT = 0x80 + 0x36
KEY_RCTRL = 0x80 + 0x1D
KEY_RALT = 0x80 + 0x38

TK_KEYSYM_TO_DOOM = {
    "Right": KEY_RIGHTARROW,
    "Left": KEY_LEFTARROW,
    "Up": KEY_UPARROW,
    "Down": KEY_DOWNARROW,
    "Return": KEY_ENTER,
    "Escape": KEY_ESCAPE,
    "Tab": KEY_TAB,
    "BackSpace": KEY_BACKSPACE,
    "space": KEY_USE,       # use / open door
    "Control_L": KEY_FIRE,
    "Control_R": KEY_FIRE,
    "Shift_L": KEY_RSHIFT,
    "Shift_R": KEY_RSHIFT,
    "Alt_L": KEY_STRAFE_L,  # strafe modifier, matches common doomgeneric convention
    "Alt_R": KEY_STRAFE_R,
    "comma": KEY_STRAFE_L,
    "period": KEY_STRAFE_R,
}


def keysym_to_doom(keysym: str, char: str):
    if keysym in TK_KEYSYM_TO_DOOM:
        return TK_KEYSYM_TO_DOOM[keysym]
    if char and len(char) == 1 and char.isprintable():
        return ord(char.lower())
    return None


class NetstreamViewer:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.sock = None
        self.root = tk.Tk()
        self.root.title(f"DOOM — live from {host}:{port}")
        self.label = tk.Label(self.root)
        self.label.pack()
        self.root.bind("<KeyPress>", self._on_key_press)
        self.root.bind("<KeyRelease>", self._on_key_release)
        self.photo = None  # keep a reference so Tk doesn't GC it

    def connect(self):
        print(f"connecting to {self.host}:{self.port} ...")
        self.sock = socket.create_connection((self.host, self.port))
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print("connected — waiting for first frame")

    def _recv_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("target closed the connection")
            buf.extend(chunk)
        return bytes(buf)

    def _reader_thread(self):
        try:
            while True:
                header = self._recv_exact(FRAME_HEADER.size)
                magic, w, h = FRAME_HEADER.unpack(header)
                if magic != b"DGFR":
                    raise ValueError(f"bad frame magic: {magic!r}")
                pixels = self._recv_exact(w * h * 4)
                img = Image.frombytes("RGBA", (w, h), pixels, "raw", "BGRA")
                self.root.after(0, self._show_frame, img)
        except (ConnectionError, OSError) as e:
            print(f"connection ended: {e}")

    def _show_frame(self, img: Image.Image):
        # Scale up 2x — DOOM's native 320x200 is tiny on a modern screen.
        img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
        self.photo = ImageTk.PhotoImage(img)
        self.label.configure(image=self.photo)

    def _send_key(self, keysym: str, char: str, pressed: bool):
        key = keysym_to_doom(keysym, char)
        if key is None or not self.sock:
            return
        try:
            self.sock.sendall(bytes([1 if pressed else 0, key & 0xFF]))
        except OSError:
            pass

    def _on_key_press(self, event):
        self._send_key(event.keysym, event.char, True)

    def _on_key_release(self, event):
        self._send_key(event.keysym, event.char, False)

    def run(self):
        self.connect()
        threading.Thread(target=self._reader_thread, daemon=True).start()
        self.root.mainloop()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    host = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 5678
    NetstreamViewer(host, port).run()


if __name__ == "__main__":
    main()
