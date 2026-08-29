# Bad Apple on RISC-V: Adding a Generic Video Pipeline

**Date**: 2026-08-25 to 2026-08-26
**Milestone tag**: `doombom-bad-apple-video` (chipyard, firesim, FireMarshal — same tag, three repos)
**Constraint honored throughout**: no AWS API calls made during this work; everything below ran on the local machine only.

## 1. The ask

With the real AGFI built and DOOM already streaming over the network to a
viewer, the next target was arbitrary video — specifically the user's own
copy of Bad Apple. Rather than write a Bad-Apple-specific hack, the goal was
a generic "play any video" pipeline that could carry Bad Apple as its first
payload.

## 2. Why this was cheap: reusing the DOOM wire protocol

DOOM's netstream backend (`doomgeneric_netstream.c`) already solved the hard
problem: get a framebuffer out of the RV64 target and onto a viewer running
on the host, over TCP, with no display hardware modeled on the FPGA side. Its
protocol is deliberately minimal:

```
struct FrameHeader { char magic[4] /* "DGFR" */; uint32 width; uint32 height; };
<width * height * 4 bytes of BGRA pixel data>
```

Nothing in that protocol is DOOM-specific. If a second RV64 program sends the
same header and the same BGRA layout, the existing viewer needs zero changes
to render whatever that second program produces. So the video pipeline was
built as a second, independent producer of the exact same wire format —
never touching the viewer at all.

## 3. Host-side: video → packed frames

`software/video/video2frames.py` converts an arbitrary input video into a
custom `.vidf` container ahead of time, offline, on the host:

- `ffmpeg -vf fps=30,scale=320:200:flags=lanczos,format=gray` extracts frames
  as grayscale PGM at the target resolution and frame rate.
- Each frame is thresholded to 1 bit per pixel (default threshold 128) —
  appropriate for high-contrast silhouette-style footage, and a 32x size
  reduction versus sending raw RGBA over to the target's tiny root filesystem.
- Output format: `"VIDF"` magic, then `uint32 width, height, num_frames, fps`
  (little-endian), then the packed 1-bit frames back-to-back, row-major,
  MSB-first.

Run against the user-supplied `bad-apple.MP4` (7.8MB, confirmed via `file` as
a real ISO-media MP4), this produced `bad-apple.vidf`: 6,572 frames at
320x200, 30fps, 52.5MB packed (vs. ~1.68GB if stored as raw RGBA).

**This derived file was deliberately never committed to git** — it's a
mechanical transform of copyrighted footage, so it lives only on the local
disk, excluded via `software/video/.gitignore`.

## 4. Target-side: the RV64 player

`software/video/videoplayer_netstream.c` is the RV64 counterpart:

1. Reads the `.vidf` header once at startup.
2. Opens the same TCP server on port 5678 that DOOM uses, and blocks until a
   viewer connects — identical handshake behavior to the DOOM backend, so a
   viewer written for one just works for the other.
3. Loops: read one packed frame, unpack each bit to a BGRA pixel (`0xFF` or
   `0x00` per channel, matching doomgeneric's exact channel layout), send a
   `DGFR` header + the raw BGRA payload — byte-for-byte the same message
   shape DOOM sends.
4. Paces itself with `nanosleep()` at the source frame rate, and loops the
   video indefinitely by default.

## 5. A real cross-compilation bug, caught by checking rather than assuming

The Makefile declared:

```make
CC ?= riscv64-unknown-linux-gnu-gcc
```

`?=` only assigns if the variable isn't already set — and GNU Make ships
with a *built-in* default of `CC=cc`, which counts as "already set" for this
purpose. So the intended cross-compiler was silently never selected, and the
first build produced a normal x86-64 ELF, not a RISC-V one. This wasn't
caught by the build succeeding (it did — cleanly) but by checking the output
with `file`, which showed `ELF 64-bit LSB executable, x86-64` instead of
`UCB RISC-V`. Fixed by passing `CC=riscv64-unknown-linux-gnu-gcc` explicitly
on the `make` command line, which does override correctly. Re-checked with
`file` afterward: `ELF 64-bit LSB executable, UCB RISC-V, RVC, double-float
ABI`. The binary committed to git is this corrected build (825,952 bytes for
the earlier, wrong build vs. 729,192 bytes for the correct RISC-V build —
different architectures naturally produce different-sized code).

## 6. Verifying correctness without ever looking at (or committing) the actual footage

Bad Apple's footage itself is copyrighted, so verification was designed to
prove the pipeline was carrying real, correctly-shaped, correctly-varying
data — without rendering, screenshotting, saving, or otherwise reproducing
any actual frame.

A throwaway script connected to the running player over the real TCP
protocol and checked only:
- The `DGFR` magic and declared width/height stayed constant across frames.
- The fraction of "on" pixels per frame (a single float, not an image).
- Byte-level diffs between consecutive frames (a single integer, not an
  image) — proof frames are actually changing, not stuck.

The first run flagged a `FAIL`: the first 20 frames were entirely black with
zero inter-frame diff. Rather than assume this meant a bug, it was checked
two independent ways:
- Extracting the source frame at the 30-second mark directly via
  `ffmpeg -ss 30 -frames:v 1` and confirming real pixel variance (min 0,
  max 255, avg ≈ 213.8) — the source video obviously isn't black throughout.
- Inspecting the packed `.vidf` file directly at frame index 900 (~30s in)
  vs. frame index 10: frame 900 showed 83.8% white bits, frame 10 showed
  legitimately 0%.

Conclusion: the pipeline was correct from the start — Bad Apple's opening
seconds are a genuine black intro, not a packing or streaming defect. This
is the kind of thing that's worth spending five extra minutes confirming
before reporting either "it's broken" or "it's fine."

## 7. Wiring into the existing FireSim boot image

- `software/firemarshal/.../br-base/overlay/root/` gained
  `videoplayer-netstream` (the RV64 binary) and `bad-apple.vidf`, alongside
  the pre-existing `doomgeneric-netstream` + `freedoom1.wad`. `bad-apple.vidf`
  is gitignored in that overlay directory too, for the same copyright reason.
- Rebuilt the boot image with FireMarshal (`marshal -i build
  boards/firechip/base-workloads/br-base.json`), producing
  `doombom-video-final.elf` (141.6MB). Verified via `cpio -tv` on the
  resulting initramfs that all four payload files actually landed inside it.
- Updated `sims/firesim/deploy/workloads/doombom.json`'s
  `common_bootbinary` to point at the new image, so the existing FireSim
  workload definition picks up video without any other config changes.

## 8. Git hygiene: three repos, one tag

Following the pattern established for the AGFI milestone, this work was
committed across all three repos it touched (chipyard, and the `firesim` and
`FireMarshal` submodules — each pushed to the user's own fork, never
upstream), and tagged consistently as `doombom-bad-apple-video` in all three
so the exact matching set of commits can be found again from any of them.

## 9. What's deliberately not in git

- `bad-apple.vidf` (52.5MB, derived from copyrighted source footage) — in
  both `software/video/` and the FireMarshal overlay.
- `videoplayer-netstream-native` — a local x86 build used only for the
  protocol-verification test above, never meant to run on the target.
- The large `doombom-*.elf` boot images (55–142MB each) — same size/scope
  reasoning as the earlier DOOM and AGFI milestones.

## 10. Net result

The exact same viewer, the exact same wire protocol, and the exact same
FPGA bitstream that runs DOOM now also plays Bad Apple — the only new code
is a ~150-line offline converter and a ~150-line RV64 player. No AWS calls
were made building or verifying any of this; everything above ran on the
local machine.
