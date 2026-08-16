# Makefile for DOOM on RISC-V 64-bit
# Target: BOOM CPU (RV64IMAFDv)
# Platforms: Verilator simulation + AWS FPGA

CC := riscv64-unknown-elf-gcc
OBJCOPY := riscv64-unknown-elf-objcopy
OBJDUMP := riscv64-unknown-elf-objdump

# Build configuration
CONFIG ?= release
PLATFORM ?= riscv64

# RISC-V compiler flags
CFLAGS := -O2 \
          -march=rv64imafd \
          -mabi=lp64d \
          -Wall -Wextra \
          -fno-builtin \
          -fno-stack-protector

# Linker flags
LDFLAGS := -nostdlib \
           -static \
           -lm \
           -Wl,-Ttext=0x80000000

# Debug symbols (if requested)
ifeq ($(CONFIG),debug)
    CFLAGS += -g -DDEBUG
    LDFLAGS += -g
endif

# Directories
DOOMGENERIC_DIR := doomgeneric
BUILD_DIR := build
OUTPUT_DIR := output

# Source files
DOOM_SOURCES := $(DOOMGENERIC_DIR)/doomgeneric.c \
                $(DOOMGENERIC_DIR)/doomgeneric_core.c \
                $(DOOMGENERIC_DIR)/dummy.c

PLATFORM_SOURCES := platform_riscv64.c

ALL_SOURCES := $(DOOM_SOURCES) $(PLATFORM_SOURCES)

# Object files
DOOM_OBJECTS := $(addprefix $(BUILD_DIR)/, $(notdir $(DOOM_SOURCES:.c=.o)))
PLATFORM_OBJECTS := $(addprefix $(BUILD_DIR)/, $(notdir $(PLATFORM_SOURCES:.c=.o)))
ALL_OBJECTS := $(DOOM_OBJECTS) $(PLATFORM_OBJECTS)

# Output binary
TARGET := $(OUTPUT_DIR)/doom.riscv
TARGET_ELF := $(TARGET)
TARGET_BIN := $(OUTPUT_DIR)/doom.bin

# Phony targets
.PHONY: all clean run help

# Default target
all: $(TARGET)

# Help
help:
	@echo "DOOM on RISC-V Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build DOOM binary (default)"
	@echo "  clean        - Remove build artifacts"
	@echo "  run          - Build and run on Verilator simulator"
	@echo "  disasm       - Generate assembly disassembly"
	@echo "  size         - Show binary size"
	@echo ""
	@echo "Configuration:"
	@echo "  CONFIG=debug - Build with debug symbols"
	@echo "  CONFIG=release - Build optimized (default)"
	@echo ""
	@echo "Example:"
	@echo "  make CONFIG=debug"
	@echo "  make clean && make all"

# Create directories
$(BUILD_DIR):
	@mkdir -p $@

$(OUTPUT_DIR):
	@mkdir -p $@

# Compile doomgeneric sources
$(BUILD_DIR)/doomgeneric.o: $(DOOMGENERIC_DIR)/doomgeneric.c | $(BUILD_DIR)
	@echo "Compiling DOOM core: $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/doomgeneric_core.o: $(DOOMGENERIC_DIR)/doomgeneric_core.c | $(BUILD_DIR)
	@echo "Compiling DOOM engine: $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dummy.o: $(DOOMGENERIC_DIR)/dummy.c | $(BUILD_DIR)
	@echo "Compiling DOOM dummy: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Compile platform backend
$(BUILD_DIR)/platform_riscv64.o: platform_riscv64.c | $(BUILD_DIR)
	@echo "Compiling RISC-V platform: $<"
	$(CC) $(CFLAGS) -I$(DOOMGENERIC_DIR) -c $< -o $@

# Link executable
$(TARGET): $(ALL_OBJECTS) | $(OUTPUT_DIR)
	@echo "Linking DOOM binary: $@"
	$(CC) $(CFLAGS) $(LDFLAGS) $(ALL_OBJECTS) -o $@
	@echo "✓ Build successful: $@"
	@ls -lh $@

# Generate disassembly
disasm: $(TARGET)
	@echo "Generating disassembly..."
	$(OBJDUMP) -d $(TARGET) > $(OUTPUT_DIR)/doom.disasm
	@echo "✓ Disassembly: $(OUTPUT_DIR)/doom.disasm"

# Show binary size
size: $(TARGET)
	@echo "Binary size analysis:"
	@$(OBJDUMP) -s -j .text $(TARGET) | head -20
	@echo ""
	@riscv64-unknown-elf-size $(TARGET)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
	@echo "✓ Clean complete"

# Run on simulator (with Linux)
run: all
	@echo ""
	@echo "Running DOOM on RISC-V simulator..."
	@echo "Simulator command:"
	@echo "  /home/ninadjangle/chipyard/sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \\"
	@echo "    /home/ninadjangle/chipyard/software/firemarshal/images/firechip/br-base/br-base-bin"
	@echo ""
	@echo "Once Linux boots, run:"
	@echo "  ./doom.riscv"
	@echo ""

# Verbose build (show all commands)
.VERBOSE: all

# Force rebuild
rebuild: clean all

# Print configuration
print-config:
	@echo "Configuration:"
	@echo "  CC: $(CC)"
	@echo "  CFLAGS: $(CFLAGS)"
	@echo "  LDFLAGS: $(LDFLAGS)"
	@echo "  Platform: $(PLATFORM)"
	@echo "  Config: $(CONFIG)"
	@echo "  Target: $(TARGET)"

