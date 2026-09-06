# Bare-metal DOOM for RocketArty100TConfig (Digilent Arty A7-100T).
#
# Builds the real doomgeneric engine (unmodified) against this directory's
# platform layer (doomgeneric_arty100t.c, syscalls.c, wad_embed.S) instead
# of any of doomgeneric's stock platform backends. No Linux, no libc beyond
# newlib-nano, no network -- see doomgeneric_arty100t.c's header comment for
# what is and isn't real yet (video needs a VGA peripheral that doesn't
# exist in the design as of 2026-08-28; input over the UART console is real
# today).
#
# Usage:
#   make RISCV=/path/to/riscv-tools
# Produces: doom-arty100t.elf, loadable via uart_tsi once the board is
# connected (see docs/Prototyping/Arty.rst for the uart_tsi invocation).

RISCV       ?= /home/ninadjangle/chipyard/.conda-env/riscv-tools
CC          := $(RISCV)/bin/riscv64-unknown-elf-gcc
OBJDUMP     := $(RISCV)/bin/riscv64-unknown-elf-objdump
SIZE        := $(RISCV)/bin/riscv64-unknown-elf-size

DOOMGENERIC_DIR := ../doomgeneric/doomgeneric
WAD_DIR         := ../wad

OUTPUT := doom-arty100t.elf

# Same engine source list doomgeneric's own Makefile uses, minus its
# platform-specific files (doomgeneric.o's DG_* implementations, the x11
# backend) which we replace with our own.
SRC_DOOM = dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c \
	d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c \
	g_game.c hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c \
	i_scale.c i_sound.c i_system.c i_timer.c memio.c m_argv.c m_bbox.c \
	m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c \
	m_random.c p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c \
	p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c \
	p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c \
	r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c \
	r_things.c sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c \
	tables.c v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c w_wad.c \
	z_zone.c w_file_stdc.c i_input.c i_video.c doomgeneric.c

PLATFORM_SRC_C := doomgeneric_arty100t.c syscalls.c
PLATFORM_SRC_S := wad_embed.S

ARCH_FLAGS := -march=rv64imafdc -mabi=lp64d -mcmodel=medany

CFLAGS := $(ARCH_FLAGS) -Os -ggdb3 -Wall \
	-DNORMALUNIX -D_DEFAULT_SOURCE \
	-I$(DOOMGENERIC_DIR) -I.

# No SNDSERV (pulls in socket-based sound server code we don't want and
# can't support), no X11. -specs=nano.specs keeps the newlib footprint
# small; deliberately NOT linking nosys.specs since syscalls.c provides
# real implementations of every syscall newlib needs.
# -Ttext-segment (not -Ttext) is deliberate: -Ttext only pins the .text
# SECTION's address, but the toolchain's default linker script places
# ~4KB of other content (RISCV_ATTRIBUT note info, etc.) before .text
# within the same LOAD segment. With plain -Ttext=0x80000000, the actual
# LOAD segment's real start address ended up at 0x7ffff000 -- one page
# BELOW real DRAM (confirmed via the generated address map: DRAM is
# 0x80000000-0x90000000, nothing is mapped just below it). That would
# have made the target's loader (uart_tsi) try to write the first ~4KB
# of the binary somewhere with no real memory behind it on actual
# hardware. Caught via a virtual-serial dry run against uart_tsi before
# ever touching the board -- see readelf -l output in session notes.
# -Ttext-segment pins the whole LOAD segment's address instead, which is
# what's actually needed here.
LDFLAGS := $(ARCH_FLAGS) -specs=nano.specs -Wl,-Ttext-segment=0x80000000 \
	-Wl,--gc-sections -Wl,-Map,$(OUTPUT).map

# Bad Apple player: separate ELF, same runtime foundation (syscalls.c),
# targets RocketArty100TVGAConfig's VGA framebuffer at 0x04000000 directly
# -- see badapple_arty100t.c's header comment. Not combined into one boot
# image with DOOM: loaded one at a time via uart_tsi, which is how bring-up
# will actually use it, and avoids a needless menu/selector system.
BADAPPLE_OUTPUT := badapple-arty100t.elf
BADAPPLE_SRC_C := badapple_arty100t.c badapple_syscalls.c
BADAPPLE_SRC_S := badapple_vidf_embed.S
VIDF_DIR := ../../video
VIDF_FILE_NAME := bad-apple.vidf

BADAPPLE_CFLAGS := $(CFLAGS) -DVIDF_FILE='"bad-apple.vidf"'

OBJDIR := build
DOOM_OBJS := $(addprefix $(OBJDIR)/, $(SRC_DOOM:.c=.o))
PLATFORM_OBJS := $(addprefix $(OBJDIR)/, $(PLATFORM_SRC_C:.c=.o) $(PLATFORM_SRC_S:.S=.o))
OBJS := $(DOOM_OBJS) $(PLATFORM_OBJS)
BADAPPLE_OBJS := $(addprefix $(OBJDIR)/badapple_, $(notdir $(BADAPPLE_SRC_C:.c=.o)) $(notdir $(BADAPPLE_SRC_S:.S=.o)))

.PHONY: all clean wad-check vidf-check badapple vga_probe uart_probe
all: wad-check $(OUTPUT)
badapple: vidf-check $(BADAPPLE_OUTPUT)

# Small, isolated integration tests -- no doomgeneric engine, no WAD/VIDF,
# each with zero dependency on the other subsystem (see the header comment
# in each .c file for what a pass/fail result means).
vga_probe: vga_probe.elf
uart_probe: uart_probe.elf

vga_probe.elf: vga_probe.c
	$(CC) $(ARCH_FLAGS) -Os -ggdb3 -Wall -specs=nano.specs \
		-Wl,-Ttext-segment=0x80000000 -Wl,--gc-sections \
		-Wl,-Map,vga_probe.elf.map vga_probe.c -o vga_probe.elf
	$(SIZE) vga_probe.elf

uart_probe.elf: uart_probe.c uart.h
	$(CC) $(ARCH_FLAGS) -Os -ggdb3 -Wall -specs=nano.specs \
		-Wl,-Ttext-segment=0x80000000 -Wl,--gc-sections \
		-Wl,-Map,uart_probe.elf.map uart_probe.c -o uart_probe.elf
	$(SIZE) uart_probe.elf

# .incbin in wad_embed.S resolves relative to the assembler's cwd, so
# symlink the real WAD in here rather than fighting include-path rules
# for .incbin (which, unlike #include, ignores -I).
wad-check:
	@if [ ! -e freedoom1.wad ]; then \
		ln -sf $(WAD_DIR)/freedoom1.wad freedoom1.wad; \
	fi

vidf-check:
	@if [ ! -e bad-apple.vidf ]; then \
		ln -sf $(VIDF_DIR)/bad-apple.vidf bad-apple.vidf; \
	fi

$(OUTPUT): $(OBJS)
	@echo [Linking $@]
	$(CC) $(LDFLAGS) $(OBJS) -o $(OUTPUT)
	$(SIZE) $(OUTPUT)

$(BADAPPLE_OUTPUT): $(BADAPPLE_OBJS)
	@echo [Linking $@]
	$(CC) $(ARCH_FLAGS) -specs=nano.specs -Wl,-Ttext-segment=0x80000000 \
		-Wl,--gc-sections -Wl,-Map,$(BADAPPLE_OUTPUT).map \
		$(BADAPPLE_OBJS) -o $(BADAPPLE_OUTPUT)
	$(SIZE) $(BADAPPLE_OUTPUT)

$(OBJDIR)/badapple_%.o: %.c
	@echo [Compiling $<]
	$(CC) $(BADAPPLE_CFLAGS) -c $< -o $@

$(OBJDIR)/badapple_%.o: %.S
	@echo [Assembling $<]
	$(CC) $(BADAPPLE_CFLAGS) -c $< -o $@

$(OBJS) $(BADAPPLE_OBJS): | $(OBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Explicit paths, deliberately not VPATH: doomgeneric's own source tree has
# a stale build/ directory of x86 .o files from the netstream port left
# over from earlier in this project, and VPATH search collides with it --
# make silently treats our targets as already built. See git history/
# session notes for how this was diagnosed (make build/am_map.o reported
# "up to date" against a file that was never actually built for this target).
$(OBJDIR)/%.o: $(DOOMGENERIC_DIR)/%.c
	@echo [Compiling $<]
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.c
	@echo [Compiling $<]
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.S
	@echo [Assembling $<]
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR)
	rm -f $(OUTPUT) $(OUTPUT).map freedoom1.wad
	rm -f $(BADAPPLE_OUTPUT) $(BADAPPLE_OUTPUT).map bad-apple.vidf
