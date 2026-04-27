# ═══════════════════════════════════════════════════════════════════════════
#  ProcWatch Kernel Module - Makefile
#
#  Usage:
#    make              Build the kernel module
#    make clean        Remove build artifacts
#    make load         Build and load the module
#    make unload       Unload the module
#    make reload       Unload, rebuild, and reload
#    make status       Check module status
#    make log          View kernel log for procwatch
#    make help         Show this help
# ═══════════════════════════════════════════════════════════════════════════

# Module name
MODULE_NAME := procwatch

# Kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Object files
obj-m := $(MODULE_NAME).o

# Extra compiler flags
ccflags-y := -Wall -Wextra -Wno-unused-parameter

# ─── Build Targets ────────────────────────────────────────────────────────

.PHONY: all clean load unload reload status log help

# Default: build the module
all:
	@echo "═══════════════════════════════════════════════════"
	@echo "  Building ProcWatch kernel module..."
	@echo "  Kernel: $(shell uname -r)"
	@echo "═══════════════════════════════════════════════════"
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@echo ""
	@echo "  ✓ Build complete: $(MODULE_NAME).ko"
	@echo ""

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@echo "  ✓ Clean complete"

# Load the module
load: all
	@echo "Loading $(MODULE_NAME)..."
	@sudo insmod $(MODULE_NAME).ko
	@echo "  ✓ Module loaded"
	@echo ""
	@echo "  Device: /dev/$(MODULE_NAME)"
	@echo "  Stats:  /proc/procwatch/stats"
	@echo ""
	@dmesg | tail -15

# Unload the module
unload:
	@echo "Unloading $(MODULE_NAME)..."
	@sudo rmmod $(MODULE_NAME) 2>/dev/null || echo "  Module not loaded"
	@echo "  ✓ Module unloaded"

# Reload: unload → build → load
reload: unload load
	@echo "  ✓ Module reloaded"

# Check module status
status:
	@echo "═══════════════════════════════════════════════════"
	@echo "  ProcWatch Status"
	@echo "═══════════════════════════════════════════════════"
	@echo ""
	@echo "Module loaded:"
	@lsmod | grep $(MODULE_NAME) || echo "  NOT LOADED"
	@echo ""
	@echo "Device node:"
	@ls -la /dev/$(MODULE_NAME) 2>/dev/null || echo "  NOT FOUND"
	@echo ""
	@echo "Stats:"
	@cat /proc/procwatch/stats 2>/dev/null || echo "  NOT AVAILABLE"
	@echo ""

# View kernel log
log:
	@echo "ProcWatch kernel log (last 30 lines):"
	@echo "──────────────────────────────────────"
	@dmesg | grep -i procwatch | tail -30

# Help
help:
	@echo "═══════════════════════════════════════════════════"
	@echo "  ProcWatch Kernel Module - Build System"
	@echo "═══════════════════════════════════════════════════"
	@echo ""
	@echo "  make          Build the kernel module"
	@echo "  make clean    Remove build artifacts"
	@echo "  make load     Build and load the module"
	@echo "  make unload   Unload the module"
	@echo "  make reload   Unload, rebuild, and reload"
	@echo "  make status   Check module status"
	@echo "  make log      View kernel log for procwatch"
	@echo "  make help     Show this help"
	@echo ""
	@echo "  Prerequisites:"
	@echo "    sudo apt-get install build-essential linux-headers-$$(uname -r)"
	@echo ""
