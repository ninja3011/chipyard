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


# ---------------------------------------------------------------------------
# Protocol v3: palette + checksummed row packets (160x100, exact DOOM colors)
#   packet = D0 0D | type | body | s1 s2      (Fletcher-16 over type+body)
#   PALETTE type 2: 768 bytes r,g,b x 256      ROW type 1: row(1) + 160 palette indices
# A damaged packet is dropped on its own; the previous frame's row stays visible.
# ---------------------------------------------------------------------------
MAGIC = b"\xd0\x0d"
MODES = [(160, 100), (192, 120), (224, 140), (256, 160), (320, 200)]     # (W, H); index = mode byte
PAL_LEN = 2 + 1 + 768 + 2

def fletcher16(data):
    s1 = s2 = 0
    for b in data:
        s1 = (s1 + b) % 255
        s2 = (s2 + s1) % 255
    return s1, s2

class PacketDecoder:
    def __init__(self):
        self.buf = bytearray()
        g = np.arange(256, dtype=np.uint8)
        self.palette = np.stack([g, g, g], axis=-1)
        self.idx = np.zeros((200, 320), np.uint8)
        self.frame_rows = set(); self.mode = 0
        self.stats = dict(rows_ok=0, pal_ok=0, bad_packets=0, frames=0, frames_complete=0)

    def feed(self, data):
        """Consume bytes; return [(rgb[H,W,3], complete)] for every frame finished by this data
        (a frame ends when its last row arrives; complete = all rows intact)."""
        self.buf += data
        b, i, out = self.buf, 0, []
        while True:
            j = b.find(MAGIC, i)
            if j < 0:
                i = max(i, len(b) - 1)               # keep 1 byte: the magic may be split across reads
                break
            if len(b) - j < 4:
                i = j; break
            t = b[j + 2]
            if t == 2:
                L = PAL_LEN
            elif t == 1 and b[j + 3] < len(MODES):
                L = 2 + 1 + 1 + 1 + MODES[b[j + 3]][0] + 2
            else:
                i = j + 1; continue
            if len(b) - j < L:
                i = j; break                          # wait for the rest of this packet
            body = bytes(b[j + 2: j + L - 2])
            if fletcher16(body) != (b[j + L - 2], b[j + L - 1]):
                self.stats["bad_packets"] += 1
                i = j + 1; continue
            i = j + L
            if t == 2:
                self.palette = np.frombuffer(body[1:769], np.uint8).reshape(256, 3).copy()
                self.stats["pal_ok"] += 1
                self.frame_rows = set()
            else:
                mode, row = body[1], body[2]
                W, H = MODES[mode]
                if mode != self.mode:
                    self.mode = mode; self.frame_rows = set()
                if row < H:
                    self.idx[row, :W] = np.frombuffer(body[3:3 + W], np.uint8)
                    self.frame_rows.add(row)
                    self.stats["rows_ok"] += 1
                    if row == H - 1:
                        complete = len(self.frame_rows) == H
                        self.stats["frames"] += 1
                        self.stats["frames_complete"] += int(complete)
                        out.append((self.palette[self.idx[:H, :W]].copy(), complete))
        del b[:i]
        return out

def _encode_stream(frames_idx, palette, mode=0, corrupt_row=None):
    """Reference encoder mirroring the C firmware -- used by the self-test."""
    def pkt(t, body):
        s1 = s2 = 0
        for x in bytes([t]) + body:
            s1 = (s1 + x) % 255; s2 = (s2 + s1) % 255
        return MAGIC + bytes([t]) + body + bytes([s1, s2])
    out = b""
    W, H = MODES[mode]
    for f in frames_idx:
        out += pkt(2, palette.tobytes())
        for r in range(H):
            p = bytearray(pkt(1, bytes([mode, r]) + f[r].tobytes()))
            if corrupt_row == r: p[20] ^= 0x55
            out += bytes(p)
    return out

if __name__ == "__main__":        # round-trip self-test of the protocol
    import random
    rows, cols = 32, 64
    img = np.array([[[random.randrange(256) for _ in range(3)] for _ in range(cols)] for _ in range(rows)], np.uint8)
    raw = b"".join(b"".join(encode_cell(*map(int, img[y, x])) for x in range(cols)) + b"\r\n" for y in range(rows))
    dec = parse_frame(raw, rows, cols)
    assert dec is not None and np.abs(dec.astype(int) - (img.astype(int) >> 4 << 4) - 0).max() <= 15
    assert parse_frame(raw[:-40], rows, cols) is None               # torn frame rejected
    print("protocol round-trip OK (max quantization error 15/255 per channel)")
    # ---- v3 (every resolution mode, awkward chunking, one corrupted row, a live mode switch) ----
    pal = np.array([[random.randrange(256) for _ in range(3)] for _ in range(256)], np.uint8)
    def rnd(m): W, H = MODES[m]; return np.array([[random.randrange(256) for _ in range(W)] for _ in range(H)], np.uint8)
    for m in range(len(MODES)):
        fr = [rnd(m) for _ in range(2)]
        dec = PacketDecoder(); stream = _encode_stream(fr, pal, m); got = []
        for k in range(0, len(stream), 97): got += dec.feed(stream[k:k + 97])
        W, H = MODES[m]
        assert len(got) == 2 and all(c for _, c in got) and got[1][0].shape == (H, W, 3) and (got[1][0] == pal[fr[1]]).all(), m
    fr = [rnd(1)]
    dec2 = PacketDecoder(); g2 = dec2.feed(_encode_stream(fr, pal, 1, corrupt_row=50))
    assert len(g2) == 1 and not g2[0][1] and dec2.stats["bad_packets"] == 1 and dec2.stats["rows_ok"] == MODES[1][1] - 1
    dec3 = PacketDecoder(); s3 = _encode_stream([rnd(0)], pal, 0) + _encode_stream([rnd(3)], pal, 3)
    g3 = dec3.feed(s3); assert [x[0].shape[:2] for x in g3] == [(100, 160), (160, 256)] and all(c for _, c in g3)
    print("v3 packet decoder OK: all 5 resolutions intact; a corrupted row is rejected alone; live mode switch handled")
