# Playing DOOM on the Arty (tag `doom-playable`)

RocketArty100TConfig bitstream, console over the FT232R on PMOD JA at 921600 baud.
160x100 exact-palette video (I_VideoBuffer decimated 2:1), ~3.5 fps measured, ~5% of row packets damaged
(dropped individually; the previous frame's row stays on screen).

## Build (from software/doom/baremetal-arty100t)
    export RISCV=<chipyard>/.conda-env/riscv-tools PATH=$RISCV/bin:$PATH; mkdir -p build_pal
    make -j8 OBJDIR=build_pal OUTPUT=doom-arty100t-pal.elf PLATFORM_SRC_S="doom_start_gated.S wad_embed.S" \
      CFLAGS="-march=rv64imafdc -mabi=lp64d -mcmodel=medany -O2 -Wall -DNORMALUNIX -D_DEFAULT_SOURCE -DCONSOLE_ASCII_VIDEO -DCONSOLE_PAL_VIDEO -DFRAME_EVERY=1 -DUART_CONSOLE_DIV=53 -DDG_SKIP_FB_CONVERT -I../doomgeneric/doomgeneric -I." \
      LDFLAGS="-march=rv64imafdc -mabi=lp64d -mcmodel=medany -specs=nano.specs -nostartfiles -T ../../accel/accel_link.ld -Wl,--gc-sections" \
      doom-arty100t-pal.elf
(needs freedoom1.wad symlinked in this directory; wad_embed.S incbins it)

## Load (reprogram the FPGA first, always)
    uart_tsi +tty=<Digilent if01> +baudrate=921600 +no_hart0_msip +init_read=0x80000000 \
             +init_write=0x86100000:0x600DF00D doom-arty100t-pal.elf
The `+init_write` flag opens the start gate (`doom_start_gated.S`); without it the core waits forever, and an
image without a gate starts running while still loading (that is what broke DOOM's startup before).

## Play (FT232R must be attached to WSL; close anything else reading the port)
    python3 doom_viewer.py /dev/ttyUSB2 [--record session.log]
W/S move, A/D turn, `,` `.` strafe, SPACE fire, E use, R run, ENTER/ESC menu, TAB map, 1-7 weapons, Y/N prompts.
A key stays "down" 220 ms after its last byte, so terminal auto-repeat gives held movement.

## Files
doomgeneric_arty100t.c (backend: palette/row packets, held-key input) - i_video.c (dg_palette export,
DG_SKIP_FB_CONVERT) - doom_color.py (protocol decoders + self-test: `python3 doom_color.py`) -
doom_viewer.py - ../../accel/accel_link.ld - ../../../reel-assets-doom-frames/ (reel renderer, evidence)
