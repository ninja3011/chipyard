#!/usr/bin/env python3
"""Reconstructs a PNG image from a VGAFramebufferBadAppleUnitTest run's
console output. Every "FRAMEDUMP row=<n> bits=<hex>" line is the DUT's
real io.vga_video output for that row, packed LSB-first-by-column (bit 0
= column 0) -- not the expected/reference model, the actual hardware
output, reconstructed into a real picture.

Each row appears 4 times (2x pixel-doubling in both h and v), and half of
those are degenerate: the accumulator reset fires on *both* back-to-back
visits to the last column (h-doubling means fbX==319 twice in a row), so
the second visit dumps a near-empty accumulator (a single bit) instead of
the real row. Picking the reading with the most bits set per row reliably
selects a real dump over a degenerate one, regardless of which specific
occurrence order they land in.

Usage:
    ./simulator-chipyard.unittest-VGAFramebufferBadAppleUnitTestConfig +verbose | \
        python3 render_framedump.py output.png
"""
import re
import sys

from PIL import Image

WIDTH = 320
HEIGHT = 200

LINE_RE = re.compile(r"FRAMEDUMP row=\s*(\d+) bits=([0-9a-fA-F]+)")


def main():
    if len(sys.argv) != 2:
        print("usage: render_framedump.py output.png < sim_output.log", file=sys.stderr)
        sys.exit(1)
    out_path = sys.argv[1]

    rows = {}
    for line in sys.stdin:
        m = LINE_RE.search(line)
        if not m:
            continue
        row = int(m.group(1))
        bits = int(m.group(2), 16)
        # keep whichever reading for this row has more bits set -- the
        # degenerate (reset-clobbered) reading always has fewer.
        if row not in rows or bin(bits).count("1") > bin(rows[row]).count("1"):
            rows[row] = bits

    missing = [r for r in range(HEIGHT) if r not in rows]
    if missing:
        print(f"warning: {len(missing)} rows never appeared in the dump "
              f"(first few: {missing[:5]})", file=sys.stderr)

    img = Image.new("1", (WIDTH, HEIGHT), 0)
    px = img.load()
    for row in range(HEIGHT):
        bits = rows.get(row, 0)
        for col in range(WIDTH):
            px[col, row] = 255 if (bits >> col) & 1 else 0

    img.save(out_path)
    print(f"wrote {out_path} ({len(rows)}/{HEIGHT} rows captured)")


if __name__ == "__main__":
    main()
