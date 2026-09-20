#!/usr/bin/env python3
"""Pixel-block viewer for mini_raycaster's ASCII video stream -- same
serial protocol as live_viewer.py, but rendered as a real graphical
window with each character cell drawn as a colored block instead of raw
text. No firmware changes needed: the density ramp the board already
sends (" .:-=+*#%@") maps directly to a brightness scale.

Built with PIL + a single Tk image label, not per-cell tkinter canvas
rectangles: an earlier version used 2048 individual canvas rectangle
items updated via itemconfig() every frame, which tkinter's canvas is
far too slow to redraw atomically at this rate -- the visible result
was old-frame/new-frame data mixing mid-redraw ("looks like garbage").
Compositing one small in-memory image per frame and blitting it as a
single PhotoImage is both fast enough and atomic (the whole frame
either shows or it doesn't -- no partial state).

Usage:
    python3 pixel_viewer.py [/dev/ttyUSB2]

WASD move/turn, f/space fire, focus the window and just type -- key
events are forwarded straight to the board the same way live_viewer.py
does over curses.
"""
import os
import subprocess
import sys
import tkinter as tk

from PIL import Image, ImageTk

DEVICE = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB2"
BAUD = "115200"
FRAME_MARKER = b"\x1b[H"

COLS, ROWS = 64, 32
CELL = 12  # pixels per character cell, after upscaling
WIN_W, WIN_H = COLS * CELL, ROWS * CELL
STATUS_H = 22

RAMP = " .:-=+*#%@"

# Minimap (mini_raycaster.c's drawMinimap()): a MAP_W x MAP_H top-down
# grid, sent as plain text lines right after the raycast view -- '#'
# wall, '.' floor, one of '><^v' for the player's own position/facing,
# 'x' for a living enemy. Previously sent but never actually drawn
# anywhere in this viewer (just sitting unused in the parsed line list,
# with only its "--- map ---" header text leaking into the old status
# bar) -- rendered here as a small real image, the same
# grid-of-colored-cells technique as the main view, just much smaller.
MAP_W, MAP_H = 16, 16
MAP_CELL = 10
MAP_HEADER = "--- map ---"


def minimap_color_for(ch):
    if ch == "#":
        return (150, 150, 160)  # wall
    if ch == "x":
        return (255, 60, 60)  # living enemy
    if ch in "><^v":
        return (57, 255, 20)  # player position/facing
    return (25, 25, 30)  # floor


def configure_serial(device):
    subprocess.run(
        ["stty", "-F", device, BAUD, "cs8", "-cstopb", "-parenb", "raw", "-echo", "clocal"],
        check=True,
    )


def color_for(ch, x=None, y=None):
    if ch == " ":
        return (12, 18, 32)  # ceiling -- dark navy, reads as "sky" not "missing pixel"
    if ch == ".":
        return (43, 36, 24)  # floor -- warm dark brown
    # First-person weapon HUD overlay (mini_raycaster.c's drawWeapon()):
    # 'G' is the gun body, 'Y' the muzzle flash -- both fixed screen-space
    # glyphs, never emitted by the raycast/sprite passes.
    if ch == "G":
        return (58, 58, 64)  # cool gunmetal gray
    if ch == "Y":
        return (255, 220, 90)  # bright muzzle-flash yellow
    # Wounded-enemy-only glyphs (never emitted by anything else this
    # viewer displays) -- tint them warm red so a hit enemy stands out.
    # Deliberately NOT special-casing '@' here: that's the densest
    # character on the shared wall-density ramp *and* doubles as the
    # raycaster's full-health-enemy glyph, but it's also the plain
    # "pixel on" character other programs (like the original bouncing-
    # ball animation) send -- tinting it red unconditionally painted
    # that animation's line art as scattered red blobs. Full-health
    # enemies now render as a bright wall-colored square instead of red
    # (a real ambiguity that only a richer wire protocol fixes), which
    # is the smaller cost of the two.
    if ch in ("o", ",", "X"):
        level = {",": 0.35, "o": 0.6, "X": 1.0}[ch]
        r = int(120 + 130 * level)
        g = int(20 + 20 * level)
        b = int(20 + 15 * level)
        return (r, g, b)
    idx = RAMP.find(ch)
    if idx < 0:
        return (32, 32, 32)
    level = idx / (len(RAMP) - 1)
    # Warm stone-gray wall tint, dim/distant to bright/near.
    v = int(35 + 205 * level)
    return (v, int(v * 0.94), int(v * 0.82))


class Viewer:
    def __init__(self, root):
        self.root = root
        self.label = tk.Label(root, bg="black")
        self.label.pack()

        # Live HP/AMMO/ENEMIES readout, overlaid in the actual top-right
        # corner of the rendered view via place() -- real text with a
        # real font, unlike the in-grid-character attempt that got
        # reverted (each grid cell there was one solid pixel block, with
        # no way to draw an actual letter shape). Parsed from the plain
        # HUD text line drawHud() already sends, searched for by content
        # ("HP:" prefix) rather than assumed to sit at a fixed line
        # index -- the previous status-bar code assumed line index ROWS
        # and actually displayed the minimap's "--- map ---" header
        # instead, since the minimap section comes first.
        self.stats_var = tk.StringVar(value="HP:--  AMMO:--  ENEMIES:--")
        self.stats_label = tk.Label(
            self.label, textvariable=self.stats_var, bg="#0a0a0a", fg="#39ff14",
            font=("monospace", 12, "bold"), anchor="e", justify="right",
            padx=8, pady=4,
        )
        self.stats_label.place(relx=1.0, rely=0.0, anchor="ne")

        # Minimap overlay, bottom-left corner -- own small PIL image and
        # PhotoImage, same pattern as the main view.
        self.minimap_img = Image.new("RGB", (MAP_W, MAP_H), (25, 25, 30))
        self.minimap_tk_img = None
        self.minimap_label = tk.Label(self.label, bg="black", bd=1, relief="solid")
        self.minimap_label.place(relx=0.0, rely=1.0, anchor="sw")

        # Always-visible key legend, bottom of the window.
        self.legend_label = tk.Label(
            root, text="WASD: move/turn   F / space: shoot   R: restart",
            bg="black", fg="#7fdaff", font=("monospace", 10), anchor="w",
        )
        self.legend_label.pack(fill="x")

        # Connection/status line -- separate from the game stats above,
        # this is about the viewer's own link state, not game state.
        self.status_var = tk.StringVar(value=f"[{DEVICE}] connecting...")
        self.status_label = tk.Label(
            root, textvariable=self.status_var, bg="black", fg="#39ff14",
            font=("monospace", 10), anchor="w", justify="left",
        )
        self.status_label.pack(fill="x")

        # Big win/lose banner, hidden until the matching line shows up in
        # a frame, hidden again once it stops appearing (i.e. a restart
        # happened and the game is back to GAME_PLAYING).
        self.banner_label = tk.Label(
            self.label, text="", bg="black", fg="#ff3b30",
            font=("monospace", 28, "bold"), padx=20, pady=10,
        )

        self.small_img = Image.new("RGB", (COLS, ROWS), (12, 18, 32))
        self.tk_img = None  # created after the first real frame, once we know the canvas exists

        self.fd = None
        self.buf = b""

        root.bind("<KeyPress>", self.on_key)
        root.focus_set()
        # Show the window immediately, then attempt the (occasionally
        # slow/stuck, e.g. after a USB passthrough hiccup) serial connect
        # on the next event-loop tick rather than blocking __init__ --
        # a stuck device used to mean no window ever appeared at all.
        self.root.after(10, self.connect)

    def connect(self):
        try:
            configure_serial(DEVICE)
            self.fd = os.open(DEVICE, os.O_RDWR | os.O_NONBLOCK)
            self.status_var.set(f"[{DEVICE}] connected, waiting for frames...")
            self.poll()
        except Exception as e:
            self.status_var.set(f"[{DEVICE}] connect failed: {e} -- retrying...")
            self.root.after(2000, self.connect)

    def on_key(self, event):
        ch = event.char
        if ch:
            try:
                os.write(self.fd, ch.encode("ascii", errors="ignore"))
            except OSError:
                pass

    def poll(self):
        try:
            chunk = os.read(self.fd, 65536)
            if chunk:
                self.buf += chunk
        except BlockingIOError:
            pass
        # Cap unbounded growth if frames stop arriving but writes continue.
        if len(self.buf) > 1 << 20:
            self.buf = self.buf[-(1 << 16):]

        last_lines = None
        while True:
            idx = self.buf.find(FRAME_MARKER, 1)
            if idx == -1:
                break
            frame_bytes = self.buf[:idx]
            self.buf = self.buf[idx:]
            if frame_bytes.startswith(FRAME_MARKER):
                frame_bytes = frame_bytes[len(FRAME_MARKER):]
            text = frame_bytes.decode("ascii", errors="replace").replace("\x00", "")
            lines = [l for l in text.split("\r\n") if l != ""]
            if lines:
                last_lines = lines

        if last_lines:
            px = self.small_img.load()
            for y in range(ROWS):
                row = last_lines[y] if y < len(last_lines) else ""
                for x in range(COLS):
                    ch = row[x] if x < len(row) else " "
                    px[x, y] = color_for(ch, x, y)
            big = self.small_img.resize((WIN_W, WIN_H), Image.NEAREST)
            self.tk_img = ImageTk.PhotoImage(big)
            self.label.configure(image=self.tk_img)

            # Find the HUD stats line by content ("HP:" prefix), not by a
            # fixed index -- the minimap section's variable line count
            # sits between the raycast rows and this line, so any fixed
            # offset breaks the moment the map size changes.
            stats_line = next((l for l in last_lines if l.startswith("HP:")), None)
            if stats_line:
                self.stats_var.set(stats_line.strip())

            # Minimap: found by its own header text, then the next MAP_H
            # lines are the grid -- same "search by content" reasoning as
            # the stats line, immune to the exact line count above it.
            if MAP_HEADER in last_lines:
                start = last_lines.index(MAP_HEADER) + 1
                map_rows = last_lines[start:start + MAP_H]
                if len(map_rows) == MAP_H:
                    mpx = self.minimap_img.load()
                    for my in range(MAP_H):
                        row = map_rows[my]
                        for mx in range(MAP_W):
                            ch = row[mx] if mx < len(row) else "."
                            mpx[mx, my] = minimap_color_for(ch)
                    map_big = self.minimap_img.resize(
                        (MAP_W * MAP_CELL, MAP_H * MAP_CELL), Image.NEAREST)
                    self.minimap_tk_img = ImageTk.PhotoImage(map_big)
                    self.minimap_label.configure(image=self.minimap_tk_img)

            # Win/lose banners: mini_raycaster.c's drawHud() emits one of
            # these exact lines only while the game is actually in that
            # end state, and neither once a restart ('r') puts it back
            # into GAME_PLAYING -- so showing/hiding the banner based on
            # whether the line appears in *this* frame, every frame,
            # naturally tracks a restart with no separate state machine
            # needed here.
            died = any("YOU DIED" in l for l in last_lines)
            won = any("GAME OVER!" in l for l in last_lines)
            if died:
                self.banner_label.configure(text="YOU DIED", fg="#ff3b30")
                self.banner_label.place(relx=0.5, rely=0.5, anchor="center")
            elif won:
                self.banner_label.configure(text="GAME OVER!", fg="#ffd23f")
                self.banner_label.place(relx=0.5, rely=0.5, anchor="center")
            else:
                self.banner_label.place_forget()

        self.root.after(30, self.poll)


def main():
    root = tk.Tk()
    root.title("mini_raycaster -- pixel view")
    Viewer(root)
    root.mainloop()


if __name__ == "__main__":
    main()
