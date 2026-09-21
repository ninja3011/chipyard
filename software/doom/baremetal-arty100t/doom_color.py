"""Frame decoding for the DOOM color console protocol (v2) -- shared by the
live viewer and the reel renderer.

Each frame: ESC[H, then ROWS lines of `\r\n`-terminated text. Each cell is two
characters c1,c2 in 0x40..0x7f carrying 12-bit RGB444:
    v = ((c1-0x40)<<6) | (c2-0x40);  r=(v>>8)&15, g=(v>>4)&15, b=v&15
(the legacy v1 stream, one density character per cell, is still accepted).
"""
import numpy as np

RAMP = " .:-=+*#%@"
FRAME_MARKER = b"\x1b[H"

def split_frames(buf):
    """Return (complete_frame_byte_strings, remainder). A frame is complete once
    the next marker has arrived."""
    parts = buf.split(FRAME_MARKER)
    return parts[1:-1], FRAME_MARKER + parts[-1] if len(parts) > 1 else buf

def parse_frame(raw, rows=32, cols=64):
    """raw: bytes after ESC[H. Returns an (rows, cols, 3) uint8 array, or None if
    the frame is torn (wrong number of lines / wrong line length)."""
    lines = raw.replace(b"\r", b"").split(b"\n")
    while lines and lines[-1] == b"":
        lines.pop()
    if len(lines) != rows:
        return None
    n = len(lines[0])
    if any(len(l) != n for l in lines):
        return None
    a = np.frombuffer(b"".join(lines), dtype=np.uint8).reshape(rows, n)
    if n == 2 * cols:                                   # color v2
        if a.min() < 0x40 or a.max() > 0x7f:
            return None
        v = ((a[:, 0::2].astype(np.uint16) - 0x40) << 6) | (a[:, 1::2].astype(np.uint16) - 0x40)
        out = np.empty((rows, cols, 3), np.uint8)
        out[..., 0] = ((v >> 8) & 15) * 17
        out[..., 1] = ((v >> 4) & 15) * 17
        out[..., 2] = (v & 15) * 17
        return out
    if n == cols:                                       # legacy density ramp
        lut = np.zeros(256, np.uint8)
        for i, ch in enumerate(RAMP):
            lut[ord(ch)] = int(14 + 236 * i / (len(RAMP) - 1))
        g = lut[a]
        return np.stack([g, (g * 0.97).astype(np.uint8), (g * 0.90).astype(np.uint8)], axis=-1)
    return None

def encode_cell(r8, g8, b8):
    """Reference encoder (mirrors the C firmware) -- used by the self-test."""
    v = ((r8 >> 4) << 8) | ((g8 >> 4) << 4) | (b8 >> 4)
    return bytes([0x40 + (v >> 6), 0x40 + (v & 0x3f)])

if __name__ == "__main__":        # round-trip self-test of the protocol
    import random
    rows, cols = 32, 64
    img = np.array([[[random.randrange(256) for _ in range(3)] for _ in range(cols)] for _ in range(rows)], np.uint8)
    raw = b"".join(b"".join(encode_cell(*map(int, img[y, x])) for x in range(cols)) + b"\r\n" for y in range(rows))
    dec = parse_frame(raw, rows, cols)
    assert dec is not None and np.abs(dec.astype(int) - (img.astype(int) >> 4 << 4) - 0).max() <= 15
    assert parse_frame(raw[:-40], rows, cols) is None               # torn frame rejected
    print("protocol round-trip OK (max quantization error 15/255 per channel)")
