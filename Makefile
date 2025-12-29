#
# Ocean Microkernel - Main Makefile
#

# Configuration
ARCH := x86_64
KERNEL := kernel.elf
ISO := ocean.iso

# Check for cross-compiler
CROSS_COMPILE :=
ifneq ($(shell which x86_64-elf-gcc 2>/dev/null),)
    CROSS_COMPILE := x86_64-elf-
else ifneq ($(shell which x86_64-linux-gnu-gcc 2>/dev/null),)
    CROSS_COMPILE := x86_64-linux-gnu-
else
    # Check if we're on x86_64 and can use native compiler
    ifneq ($(shell uname -m),x86_64)
        NEED_CROSS := yes
    endif
endif

ifdef NEED_CROSS
$(warning =====================================================)
$(warning   No x86_64 cross-compiler found!)
$(warning   Run: ./tools/setup-toolchain.sh)
$(warning   Or install x86_64-elf-gcc manually)
$(warning =====================================================)
endif

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy

AS := nasm

# Directories
KERNEL_DIR := kernel
BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso_root

# Compiler flags
CFLAGS := -std=gnu11 -g \
          -ffreestanding \
          -fno-stack-protector \
          -fno-stack-check \
          -fno-pie \
          -fno-pic \
          -mno-80387 \
          -mno-mmx \
          -mno-sse \
          -mno-sse2 \
          -mno-red-zone \
          -mcmodel=kernel \
          -Wall -Wextra \
          -I$(KERNEL_DIR)/include \
          -DOCEAN_KERNEL

# Assembler flags
ASFLAGS := -f elf64 -g -F dwarf

# Linker flags
LDFLAGS := -nostdlib \
           -static \
           -z max-page-size=0x1000 \
           -T kernel.ld

# Find all source files
KERNEL_C_SRCS := $(shell find $(KERNEL_DIR) -name '*.c' 2>/dev/null)
KERNEL_ASM_SRCS := $(shell find $(KERNEL_DIR) -name '*.asm' 2>/dev/null)

# Generate object file names
KERNEL_C_OBJS := $(KERNEL_C_SRCS:$(KERNEL_DIR)/%.c=$(BUILD_DIR)/kernel/%.o)
KERNEL_ASM_OBJS := $(KERNEL_ASM_SRCS:$(KERNEL_DIR)/%.asm=$(BUILD_DIR)/kernel/%.o)
KERNEL_OBJS := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

# Include userspace build rules
include user.mk

# Default target
.PHONY: all
all: $(ISO)

# Build with userspace
.PHONY: all-user
all-user: $(BUILD_DIR)/$(KERNEL) userspace $(ISO)

# Build the kernel ELF
$(BUILD_DIR)/$(KERNEL): $(KERNEL_OBJS) kernel.ld
	@echo "  LD      $@"
	@mkdir -p $(dir $@)
	@$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# Compile C files
$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# Assemble ASM files
$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.asm
	@echo "  ASM     $<"
	@mkdir -p $(dir $@)
	@$(AS) $(ASFLAGS) $< -o $@

# Build the ISO image
$(ISO): $(BUILD_DIR)/$(KERNEL) $(SERVER_BINS) limine.conf
	@echo "  ISO     $@"
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot
	@cp $(BUILD_DIR)/$(KERNEL) $(ISO_DIR)/boot/
	@cp $(BUILD_DIR)/init.elf $(ISO_DIR)/boot/
	@cp $(BUILD_DIR)/mem.elf $(ISO_DIR)/boot/ 2>/dev/null || true
	@cp $(BUILD_DIR)/proc.elf $(ISO_DIR)/boot/ 2>/dev/null || true
	@cp $(BUILD_DIR)/vfs.elf $(ISO_DIR)/boot/ 2>/dev/null || true
	@cp $(BUILD_DIR)/ramfs.elf $(ISO_DIR)/boot/ 2>/dev/null || true
	@cp limine.conf $(ISO_DIR)/boot/
	@# Try to find limine in common locations
	@if [ -d "/usr/share/limine" ]; then \
		cp /usr/share/limine/limine-bios.sys $(ISO_DIR)/boot/; \
		cp /usr/share/limine/limine-bios-cd.bin $(ISO_DIR)/boot/; \
		cp /usr/share/limine/limine-uefi-cd.bin $(ISO_DIR)/boot/; \
	elif [ -d "limine" ]; then \
		cp limine/limine-bios.sys $(ISO_DIR)/boot/; \
		cp limine/limine-bios-cd.bin $(ISO_DIR)/boot/; \
		cp limine/limine-uefi-cd.bin $(ISO_DIR)/boot/; \
	else \
		echo "Warning: Limine not found, ISO may not be bootable"; \
	fi
	@xorriso -as mkisofs -b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $@ 2>/dev/null || \
		echo "Note: xorriso not found. Install it for ISO creation."
	@if [ -f "/usr/bin/limine" ]; then \
		limine bios-install $@ 2>/dev/null || true; \
	elif [ -f "limine/limine" ]; then \
		./limine/limine bios-install $@ 2>/dev/null || true; \
	fi

# Download and build Limine if not present
.PHONY: limine
limine:
	@if [ ! -d "limine" ]; then \
		echo "Downloading Limine..."; \
		git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1; \
	fi

# Run in QEMU
.PHONY: run
run: $(ISO)
	@echo "Starting QEMU..."
	@qemu-system-x86_64 \
		-cdrom $(ISO) \
		-serial stdio \
		-m 256M \
		-smp 2 \
		-no-reboot \
		-no-shutdown

# Run with debug (GDB server)
.PHONY: debug
debug: $(ISO)
	@echo "Starting QEMU with GDB server on :1234..."
	@qemu-system-x86_64 \
		-cdrom $(ISO) \
		-serial stdio \
		-m 256M \
		-smp 2 \
		-no-reboot \
		-no-shutdown \
		-s -S

# Run kernel directly (faster, no ISO)
.PHONY: run-kernel
run-kernel: $(BUILD_DIR)/$(KERNEL)
	@echo "Starting QEMU with kernel directly..."
	@qemu-system-x86_64 \
		-kernel $(BUILD_DIR)/$(KERNEL) \
		-serial stdio \
		-m 256M \
		-smp 2 \
		-no-reboot \
		-no-shutdown

# Clean build artifacts
.PHONY: clean
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR) $(ISO)

# Show configuration
.PHONY: info
info:
	@echo "Ocean Kernel Build Configuration"
	@echo "================================"
	@echo "CC:      $(CC)"
	@echo "LD:      $(LD)"
	@echo "AS:      $(AS)"
	@echo "CFLAGS:  $(CFLAGS)"
	@echo ""
	@echo "Sources: $(KERNEL_C_SRCS) $(KERNEL_ASM_SRCS)"
	@echo "Objects: $(KERNEL_OBJS)"

# Generate compile_commands.json for IDE support
.PHONY: compile_commands
compile_commands:
	@echo "Generating compile_commands.json..."
	@echo "[" > compile_commands.json
	@first=true; \
	for src in $(KERNEL_C_SRCS); do \
		if [ "$$first" = true ]; then first=false; else echo "," >> compile_commands.json; fi; \
		echo "  {" >> compile_commands.json; \
		echo "    \"directory\": \"$(shell pwd)\"," >> compile_commands.json; \
		echo "    \"command\": \"$(CC) $(CFLAGS) -c $$src\"," >> compile_commands.json; \
		echo "    \"file\": \"$$src\"" >> compile_commands.json; \
		echo -n "  }" >> compile_commands.json; \
	done
	@echo "" >> compile_commands.json
	@echo "]" >> compile_commands.json

# Help
.PHONY: help
help:
	@echo "Ocean Kernel Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all              Build kernel and ISO (default)"
	@echo "  run              Run in QEMU"
	@echo "  debug            Run in QEMU with GDB server"
	@echo "  run-kernel       Run kernel directly (no ISO)"
	@echo "  clean            Remove build artifacts"
	@echo "  limine           Download Limine bootloader"
	@echo "  info             Show build configuration"
	@echo "  compile_commands Generate compile_commands.json"
	@echo "  help             Show this help"
