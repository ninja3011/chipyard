#!/usr/bin/env python3
"""Converts a video file into a .vidf frame file for the netstream video
player (videoplayer_netstream.c) running on the DOOMBOOM target.

High-contrast / silhouette-style source video packs extremely well as
1-bit-per-pixel frames — 32x smaller than the RGBA format doomgeneric uses,
which leaves enormous bandwidth headroom over the same network link.

Requires ffmpeg on PATH.

.vidf format:
    char[4]   magic = "VIDF"
    uint32_t  width
    uint32_t  height
    uint32_t  num_frames
    uint32_t  fps
    then num_frames * ceil(width*height/8) bytes of packed 1-bit frame data,
    row-major, MSB-first within each byte. Bit=1 -> white, bit=0 -> black.

Usage:
    python3 video2frames.py input.mp4 output.vidf [--width 320] [--height 200] [--fps 30] [--threshold 128]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

MAGIC = b"VIDF"


def extract_frames_ffmpeg(input_path, width, height, fps, tmpdir):
    pattern = os.path.join(tmpdir, "frame_%06d.pgm")
    cmd = [
        "ffmpeg", "-y", "-i", input_path,
        "-vf", f"fps={fps},scale={width}:{height}:flags=lanczos,format=gray",
        pattern,
    ]
    print("running:", " ".join(cmd))
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    frames = sorted(f for f in os.listdir(tmpdir) if f.startswith("frame_"))
    return [os.path.join(tmpdir, f) for f in frames]


def read_pgm_grayscale(path):
    with open(path, "rb") as f:
        data = f.read()
    # minimal binary PGM (P5) parser
    assert data[:2] == b"P5", "expected binary PGM (P5) from ffmpeg"
    idx = 2
    vals = []
    while len(vals) < 3:
        while data[idx] in b" \t\r\n":
            idx += 1
        if data[idx:idx + 1] == b"#":
            while data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while data[idx] not in b" \t\r\n":
            idx += 1
        vals.append(int(data[start:idx]))
    idx += 1
    w, h, maxval = vals
    pixels = data[idx:idx + w * h]
    return w, h, pixels


def pack_1bit(pixels, width, height, threshold):
    row_bytes = (width + 7) // 8
    out = bytearray(row_bytes * height)
    for y in range(height):
        row_off = y * width
        out_off = y * row_bytes
        bitpos = 7
        acc = 0
        obyte = 0
        for x in range(width):
            bit = 1 if pixels[row_off + x] >= threshold else 0
            acc |= bit << bitpos
            bitpos -= 1
            if bitpos < 0:
                out[out_off + obyte] = acc
                obyte += 1
                acc = 0
                bitpos = 7
        if bitpos != 7:
            out[out_off + obyte] = acc
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=200)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--threshold", type=int, default=128,
                     help="grayscale cutoff (0-255) for black/white packing")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmpdir:
        frame_paths = extract_frames_ffmpeg(args.input, args.width, args.height, args.fps, tmpdir)
        if not frame_paths:
            print("ffmpeg produced no frames", file=sys.stderr)
            sys.exit(1)
        print(f"{len(frame_paths)} frames extracted, packing to 1-bit...")

        with open(args.output, "wb") as out:
            out.write(MAGIC)
            out.write(struct.pack("<IIII", args.width, args.height, len(frame_paths), args.fps))
            for i, p in enumerate(frame_paths):
                w, h, pixels = read_pgm_grayscale(p)
                assert (w, h) == (args.width, args.height)
                out.write(pack_1bit(pixels, w, h, args.threshold))
                if i % 200 == 0:
                    print(f"  packed {i}/{len(frame_paths)}")

        row_bytes = (args.width + 7) // 8
        total = 16 + len(frame_paths) * row_bytes * args.height
        print(f"wrote {args.output}: {total} bytes ({len(frame_paths)} frames @ "
              f"{args.width}x{args.height}, {args.fps}fps)")


if __name__ == "__main__":
    main()
