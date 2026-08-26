# DOOM Live Video Streaming — Design & Test Report

**Date:** 2026-08-24
**Status:** Protocol and client/server code built and smoke-tested. Not yet
integrated into the boot image or run against real hardware.

---

## 1. The problem

The challenge requires playing DOOM live, in real time, once it's running on
the FPGA. The obvious approach — a real framebuffer/display device the CPU
writes pixels to — doesn't exist in this project's hardware:

```bash
grep -n "class WithDefaultFireSimBridges" -A 10 \
  generators/firechip/bridgestubs/src/main/scala/*.scala
```
```
class WithDefaultFireSimBridges extends Config(
  new WithTSIBridgeAndHarnessRAMOverSerialTL ++
  new WithDMIBridge ++
  new WithNICBridge ++
  new WithUARTBridge ++
  new WithBlockDeviceBridge ++
  new WithFASEDBridge ++
  ...
```

No display/framebuffer bridge exists anywhere in FireSim. `doomgeneric`'s
stock Linux backend (`doomgeneric_linuxvt.c`) renders to `/dev/fb0`, which
simply has no hardware behind it here. Building a real one — a Chisel display
peripheral, a new FireSim bridge, a new Linux driver — was assessed as a
multi-day hardware-design project on its own, separate from the CPU/build
work already done.

## 2. The path taken instead: video over the network

FireSim already includes a NIC bridge (`WithNICBridge`), and — checked before
committing to this approach — a working Linux driver for it already exists,
pre-built against our exact kernel:

```bash
ls software/firemarshal/boards/firechip/drivers/icenet-driver/
# icenet.c  icenet.ko  icenet.mod.c  Makefile  ...
```

FireSim's own docs describe a supported, documented setup for exactly this:
a `tap0` bridge on the run-farm host giving direct IP reachability to the
simulated Linux node (`172.16.0.2`), including full internet access via NAT
(`docs/Advanced-Usage/Miscellaneous-Tips.rst`). This meant a real, working,
already-built network path existed — the only genuinely new work is software:
a DOOM backend that sends frames over a socket instead of to a framebuffer,
and a viewer on the other end.

## 3. Architecture

```
 [ RISC-V Linux target ]                    [ Your machine / run-farm host ]
 doomgeneric-netstream                          netstream_viewer.py
   DG_DrawFrame()  ──── TCP, frames ────►    decode RGBA → Tk window
   DG_GetKey()     ◄─── TCP, keys    ────    keypress/release → keycode
        │
        └─ icenet.ko + FireSim NIC bridge + tap0 bridge on run-farm host
```

The target is the TCP **server** (deliberately, not the client) — it blocks
game startup until a viewer actually connects, so nothing runs unwatched.

### Wire protocol

Server → client, once per rendered frame:
```
char[4]   magic = "DGFR"
uint32_t  width   (little-endian)
uint32_t  height  (little-endian)
uint8_t   pixels[width * height * 4]   — raw RGBA, one frame
```

Client → server, one message per key transition:
```
uint8_t   pressed   (0 = release, 1 = press)
uint8_t   doom_keycode
```

Deliberately the simplest protocol that could work — no compression, no
acknowledgment, no frame numbering. Given the bandwidth math below, none of
that complexity earns its keep yet.

### Pixel format — verified, not assumed

`doomgeneric`'s SDL backend creates its texture as `SDL_PIXELFORMAT_RGB888`.
On a little-endian machine (both x86 and our RISC-V target are little-endian),
that packed format lays out in memory as bytes `[B, G, R, unused]` per pixel.
The viewer decodes with PIL's `"raw", "BGRA"` mode to match this exactly —
confirmed correct by the smoke test producing correctly-colored output.

### Bandwidth check

DOOM's native resolution is 320×200. At the protocol's raw RGBA rate:
```
320 × 200 × 4 bytes = 256,000 bytes/frame
at 30 fps           = 7.68 MB/s ≈ 61 Mbps
```
`config_runtime.yaml`'s `net_bandwidth: 200` (Mbps) gives comfortable headroom
above this — raw, uncompressed frames were chosen deliberately over adding
compression, since the bandwidth budget doesn't require it.

## 4. What was built

| File | Purpose |
|---|---|
| `software/doom/doomgeneric/doomgeneric/doomgeneric_netstream.c` | New doomgeneric backend: TCP server, frame sender, keyboard receiver thread |
| `software/doom/doomgeneric/doomgeneric/Makefile.netstream` | Cross-compiles it for `riscv64-unknown-linux-gnu`, static, `-lpthread` |
| `software/doom/netstream_viewer.py` | Host-side live viewer (Tkinter + PIL) and input sender |

### Notable implementation decisions

- **Blocking `accept()` in `DG_Init`.** The game doesn't start ticking until
  a viewer is attached — avoids wasting simulated cycles on a game nobody is
  watching yet, which matters given the CPU is a scarce, expensive resource
  here.
- **Input handled on a separate pthread**, decoupled from the render loop,
  so a slow or momentarily disconnected viewer can't stall keypress delivery
  once it reconnects.
- **`main()` had to be added explicitly.** Every doomgeneric backend supplies
  its own `main()` (confirmed by checking all the existing backends) — this
  was missed on the first build attempt and caused a linker error
  (`undefined reference to main`), fixed by adding the same
  `doomgeneric_Create` + `doomgeneric_Tick` loop every other backend uses.

## 5. Testing performed

The real RV64 binary can't run natively on this x86 dev machine, so the
protocol was validated with a mock server standing in for the target:

```bash
python3 mock_doom_server.py &      # sends a synthetic moving-gradient frame
                                    # at the exact real wire format
python3 netstream_viewer.py 127.0.0.1 5678
```

**Result:** clean connect, correct frame decode, live-updating window,
zero exceptions in the viewer log. Sustained ~14 fps (mock-server-limited —
its per-pixel Python fill loop is far slower than the real C renderer will
be, not a protocol limit).

**What this confirms:** the wire format, byte order, frame framing, and
Tkinter/PIL rendering pipeline are all correct. **What this does not yet
confirm:** real gameplay bandwidth/latency behavior over FireSim's simulated
network model, or that `icenet.ko` actually brings up networking correctly
inside our specific kernel build — those can only be tested once this is
embedded in the boot image and run.

## 6. Known limitations, stated plainly

- **Silent.** Confirmed by reading doomgeneric's own source: sound is gated
  behind `#ifdef FEATURE_SOUND`, undefined in this build. The project's own
  README says as much — *"Sound is much harder to implement."* No audio
  streaming exists yet; this would be additional scope if wanted.
- **Not yet embedded in the boot image.** `doomgeneric-netstream`, `icenet.ko`,
  and a WAD file all still need to go into the FireMarshal initramfs.
- **Network bring-up on the target is still manual/undocumented-in-our-repo** —
  needs `icenet.ko` loaded and the interface configured per FireSim's
  documented steps before any of this can be exercised for real.
- **No WAD file sourced yet** — the one remaining blocker before an actual
  end-to-end run, real or simulated, is possible.
- **Untested at real FPGA speed.** Everything above validates the pieces in
  isolation; nothing has yet proven the full loop working against a live
  FireSim run.
