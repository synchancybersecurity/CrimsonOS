# Crimson OS - Makefile
# Independent Mobile Operating System
# Architecture: ARM64 (AArch64)
#
# Flat source layout — all .c/.S/.h files at project root
# Build artifacts go to build/ and bin/

# Cross-compiler toolchain
# Use aarch64-linux-gnu- on Ubuntu/Debian (apt install gcc-aarch64-linux-gnu)
# Use aarch64-none-elf- if using ARM's bare-metal toolchain
CROSS_COMPILE ?= aarch64-linux-gnu-
CC      = $(CROSS_COMPILE)gcc
AS      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

# Directories
SRC_DIR   = .
BUILD_DIR = build
BIN_DIR   = bin

# Target
TARGET        = crimson-os
KERNEL_ELF    = $(BIN_DIR)/$(TARGET).elf
KERNEL_IMG    = $(BIN_DIR)/$(TARGET).img
KERNEL_BIN    = $(BIN_DIR)/$(TARGET).bin
LINKER_SCRIPT = $(SRC_DIR)/linker.ld

# Board selection (default: QEMU virt)
BOARD ?= qemu
ifeq ($(BOARD),rpi4)
    BOARD_FLAGS = -DBOARD_RPI4 -mcpu=cortex-a72
else ifeq ($(BOARD),rpi3)
    BOARD_FLAGS = -DBOARD_RPI3 -mcpu=cortex-a53
else ifeq ($(BOARD),pinephone)
    BOARD_FLAGS = -DBOARD_PINEPHONE -mcpu=cortex-a53
 BOARD_FLAGS = -DBOARD_PINEPHONE_PRO -mcpu=cortex-a72+crc+crypto -mtune=cortex-a72
 BOARD_FLAGS = -DBOARD_PINEPHONE_PRO -mcpu=cortex-a72+crc+crypto -mtune=cortex-a72
else ifeq ($(BOARD),pinephone-pro)
 BOARD_FLAGS = -DBOARD_PINEPHONE_PRO -mcpu=cortex-a72+crc+crypto -mtune=cortex-a72
else
    BOARD_FLAGS = -DBOARD_QEMU -mcpu=cortex-a72
 BOARD_FLAGS = -DBOARD_PINEPHONE_PRO -mcpu=cortex-a72+crc+crypto -mtune=cortex-a72
endif

# Compiler flags
CFLAGS  = -Wall -Wextra -O2 -g3
CFLAGS += -ffreestanding -nostdlib -nostartfiles
CFLAGS += -march=armv8-a+crc+crypto
CFLAGS += $(BOARD_FLAGS)
CFLAGS += -DCRIMSON_OS_VERSION=\"0.1.0-alpha\"
CFLAGS += -DCRIMSON_CODENAME=\"BloodMoon\"
CFLAGS += -mgeneral-regs-only
CFLAGS += -fno-stack-protector
CFLAGS += -fno-exceptions
CFLAGS += -fomit-frame-pointer
CFLAGS += -Wno-unused-function
CFLAGS += -Wno-unused-variable
CFLAGS += -Wno-unused-parameter
# Flat include: #include <crimson/foo.h> resolves to ./foo.h
# We create a symlink crimson -> . so the include path works
CFLAGS += -I$(SRC_DIR)

# Flags for files that use ARMv8 crypto/SIMD instructions.
# -mgeneral-regs-only forbids SIMD in the rest of the kernel (safe for
# interrupt handlers), but crypto_stub.c and wifi_rtl8723cs.c use AESE/
# AESMC/PMULL so they need the SIMD register file available.
CFLAGS_CRYPTO = $(filter-out -mgeneral-regs-only,$(CFLAGS))

# Assembly flags
ASFLAGS  = -march=armv8-a+crc+crypto
ASFLAGS += $(BOARD_FLAGS)
ASFLAGS += -I$(SRC_DIR)

# Linker flags
LDFLAGS  = -T $(LINKER_SCRIPT) -nostdlib
LDFLAGS += -Map=$(BUILD_DIR)/$(TARGET).map

# ── Source files (flat layout) ──

ASM_SRCS = boot.S

KERN_SRCS = \
    kmain.c \
    mm.c \
    sched.c \
    process.c \
    printk.c \
    sync.c \
    shell.c \
    board.c \
    driver.c \
    crypto_stub.c \
    vmm_stub.c \
    syscall.c \
    security.c \
    net.c \
    bloodmoon.c \
    compositor.c \
    gui_graphics.c \
    gui_widgets.c \
    phone.c \
    pkg.c \
    crfs.c \
    vfs.c \
    stubs.c

DRV_SRCS = \
    uart.c \
    gpio.c \
    timer.c \
    gic.c \
    display.c \
    display_a64_dsi.c \
    touch.c \
    touch_gt917s.c \
    usb.c \
    wifi.c \
    wifi_rtl8723cs.c \
    cellular.c \
    modem_eg25g.c \
    audio.c \
    camera.c \
    power.c

LIB_SRCS = \
    string.c \
    stdlib.c

# ── Object files ──
ASM_OBJS  = $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SRCS))
KERN_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(KERN_SRCS))
DRV_OBJS  = $(patsubst %.c,$(BUILD_DIR)/%.o,$(DRV_SRCS))
LIB_OBJS  = $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
ALL_OBJS  = $(ASM_OBJS) $(KERN_OBJS) $(DRV_OBJS) $(LIB_OBJS)

# QEMU
QEMU       = qemu-system-aarch64
QEMU_FLAGS = -M virt -cpu cortex-a72 -smp 4 -m 2G
QEMU_FLAGS += -kernel $(KERNEL_IMG)
QEMU_FLAGS += -nographic

QEMU_GFX_FLAGS = -M virt -cpu cortex-a72 -smp 4 -m 2G
QEMU_GFX_FLAGS += -kernel $(KERNEL_IMG)
QEMU_GFX_FLAGS += -serial stdio
QEMU_GFX_FLAGS += -device virtio-gpu-pci
QEMU_GFX_FLAGS += -device virtio-keyboard-pci
QEMU_GFX_FLAGS += -device virtio-mouse-pci

# ── Phony targets ──
.PHONY: all clean qemu qemu-gfx qemu-debug setup info check

all: setup $(KERNEL_IMG) info

# Create the crimson/ symlink so #include <crimson/types.h> works
setup:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)
	@if [ ! -L crimson ]; then ln -sf . crimson; fi

# ── Build rules ──

$(KERNEL_IMG): $(KERNEL_ELF)
	@echo "[OBJCOPY] $@"
	@$(OBJCOPY) -O binary $< $@

$(KERNEL_ELF): $(ALL_OBJS) $(LINKER_SCRIPT)
	@echo "[LD] Linking kernel..."
	@$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@$(SIZE) $@

# Assembly
$(BUILD_DIR)/%.o: %.S
	@echo "[AS] $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# Files that use ARM crypto/SIMD instructions — compiled without
# -mgeneral-regs-only so the assembler can access v0-v31 registers.
$(BUILD_DIR)/crypto_stub.o: crypto_stub.c
	@echo "[CC] $< (crypto)"
	@$(CC) $(CFLAGS_CRYPTO) -c $< -o $@

$(BUILD_DIR)/wifi_rtl8723cs.o: wifi_rtl8723cs.c
	@echo "[CC] $< (crypto)"
	@$(CC) $(CFLAGS_CRYPTO) -c $< -o $@

# Files that use double/float (GPS, audio DSP) — also compiled without
# -mgeneral-regs-only.
$(BUILD_DIR)/cellular.o: cellular.c
	@echo "[CC] $< (fp)"
	@$(CC) $(CFLAGS_CRYPTO) -c $< -o $@

$(BUILD_DIR)/modem_eg25g.o: modem_eg25g.c
	@echo "[CC] $< (fp)"
	@$(CC) $(CFLAGS_CRYPTO) -c $< -o $@

# C sources (generic rule — must come after the per-file overrides above)
$(BUILD_DIR)/%.o: %.c
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ── QEMU targets ──

# Console-only boot (UART on terminal — primary testing mode)
qemu: all
	@echo ""
	@echo "═══════════════════════════════════════════"
	@echo "  Crimson OS — QEMU ARM64 Console Boot"
	@echo "  Press Ctrl-A then X to exit"
	@echo "═══════════════════════════════════════════"
	@echo ""
	$(QEMU) $(QEMU_FLAGS)

# Graphical boot with virtio-gpu display
qemu-gfx: all
	@echo ""
	@echo "═══════════════════════════════════════════"
	@echo "  Crimson OS — QEMU ARM64 Graphical Boot"
	@echo "═══════════════════════════════════════════"
	@echo ""
	$(QEMU) $(QEMU_GFX_FLAGS)

# Debug boot — waits for GDB connection on port 1234
qemu-debug: all
	@echo ""
	@echo "═══════════════════════════════════════════"
	@echo "  Crimson OS — QEMU Debug Mode"
	@echo "  Connect: aarch64-linux-gnu-gdb $(KERNEL_ELF)"
	@echo "  Then: target remote :1234"
	@echo "═══════════════════════════════════════════"
	@echo ""
	$(QEMU) $(QEMU_FLAGS) -S -s

# ── Info and utilities ──

info: $(KERNEL_IMG)
	@echo ""
	@echo "═══════════════════════════════════════════"
	@echo "  Crimson OS Build Complete"
	@echo "  Version:  0.1.0-alpha (BloodMoon)"
	@echo "  Arch:     ARM64 (AArch64)"
	@echo "  Board:    $(BOARD)"
	@echo "  Kernel:   $(KERNEL_IMG)"
	@echo "  Objects:  $(words $(ALL_OBJS)) files"
	@echo "═══════════════════════════════════════════"
	@echo "  Run: make qemu"
	@echo "═══════════════════════════════════════════"

check:
	@echo "Checking toolchain..."
	@which $(CC) >/dev/null 2>&1 && echo "  ✓ $(CC)" || echo "  ✗ $(CC) — install gcc-aarch64-linux-gnu"
	@which $(LD) >/dev/null 2>&1 && echo "  ✓ $(LD)" || echo "  ✗ $(LD)"
	@which $(QEMU) >/dev/null 2>&1 && echo "  ✓ $(QEMU)" || echo "  ✗ $(QEMU) — install qemu-system-arm"
	@echo ""
	@echo "Source files: $(words $(ASM_SRCS) $(KERN_SRCS) $(DRV_SRCS) $(LIB_SRCS))"
	@echo "Headers:      $(words $(wildcard *.h))"

clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@rm -f crimson
