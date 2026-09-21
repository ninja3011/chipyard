# DOOM on the Arty A7-100T (RISC-V Rocket @ 50 MHz): evidence + reel asset

## The reel
* `out/doom_frames_reel.mp4` - 1080x1920, 25 fps, exactly **1000 frames = 1000 frames received from the board**
  (40 s time-lapse; the chip produced them at roughly 1.2 frames/s, about 14 minutes of real time).
* `out/poster.png`, `out/contact_sheet_8_of_1000.png`, `out/frames/` (all 1000 PNGs).
* Source of truth: `doom_capture_raw.log` (2.2 MB, raw UART bytes from the FT232R console). `render_reel.py` turns it
  into the video; only complete 64x32 frames are used (torn ones dropped, never repaired, nothing interpolated).
* Checks: 958 of 1000 frames are distinct; 960 of 999 consecutive pairs differ; zero TRAP / I_Error / exit lines in the capture.
* The content is DOOM's own title + demo sequence rendered as 64x32 density characters (" .:-=+*#%@") by the game on the chip.
  It is not player gameplay, and it is low-resolution by construction (one character = one pixel block).

## What changed, with evidence (all on the same RocketArty100TConfig bitstream, clean reprogram before every run)
| run | result | log |
|---|---|---|
| original DOOM image, no start gate | prints startup line, clears screen, `[doom] exited with code -1` | `software/accel/results/doom_control2_ungated_original_console.log` |
| start gate + old memory layout | initializes fully, frames stream, 156+ frames | `.../doom_control1_gate_oldlayout_console.log` |
| start gate + sentinel-hole layout (5+ min) | frames stream, 433 frames | `.../doom_gated_layout_console.log` |
| same, capture for the reel | 1042 frame markers, 0 errors | `doom_capture_raw.log` |

Cause: the core starts running while `uart_tsi` is still streaming the ELF (no "load finished" barrier), so the 30 MB
image ran on half-loaded memory. The fix is a start gate (first instructions spin on a DRAM flag that `uart_tsi` writes
after the load: `+init_write=0x86100000:0x600DF00D`). See `software/doom/baremetal-arty100t/doom_start_gated.S`.

Correction to an earlier guess: the DMI-autoloader sentinel words (0x80001004/8) are NOT what broke DOOM; in the earlier DOOM
images they landed in unused automap code. They did corrupt the LLM programs (see software/accel/README.md).
The earlier "unfixable hold-time violation" explanation for DOOM's crashes is not supported by this test: the same
bitstream, with its known small hold violation, runs DOOM for 1000+ frames.

Not tested: real gameplay/input, or how long it runs beyond ~14 minutes.
