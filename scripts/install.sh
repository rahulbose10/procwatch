#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════
#  ProcWatch - Quick Install Script
#
#  Usage: sudo ./scripts/install.sh
# ═══════════════════════════════════════════════════════════════════════════

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}══════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  ProcWatch Installer${NC}"
echo -e "${CYAN}══════════════════════════════════════════════════${NC}"
echo ""

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Please run as root (sudo $0)${NC}"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Step 1: Check prerequisites
echo -e "${YELLOW}[1/5] Checking prerequisites...${NC}"
if ! command -v gcc &> /dev/null; then
    echo "  Installing build-essential..."
    apt-get update -qq && apt-get install -y -qq build-essential
fi

HEADERS_DIR="/lib/modules/$(uname -r)/build"
if [ ! -d "$HEADERS_DIR" ]; then
    echo "  Installing kernel headers..."
    apt-get install -y -qq "linux-headers-$(uname -r)"
fi
echo -e "${GREEN}  ✓ Prerequisites OK${NC}"

# Step 2: Build kernel module
echo -e "${YELLOW}[2/5] Building kernel module...${NC}"
cd "$PROJECT_DIR/kernel"
make clean > /dev/null 2>&1 || true
make > /dev/null 2>&1
echo -e "${GREEN}  ✓ Kernel module built${NC}"

# Step 3: Build daemon
echo -e "${YELLOW}[3/5] Building daemon...${NC}"
cd "$PROJECT_DIR/daemon"
make clean > /dev/null 2>&1 || true
make > /dev/null 2>&1
echo -e "${GREEN}  ✓ Daemon built${NC}"

# Step 4: Load module
echo -e "${YELLOW}[4/5] Loading kernel module...${NC}"
rmmod procwatch 2>/dev/null || true
insmod "$PROJECT_DIR/kernel/procwatch.ko"
echo -e "${GREEN}  ✓ Module loaded${NC}"

# Step 5: Verify
echo -e "${YELLOW}[5/5] Verifying installation...${NC}"

if lsmod | grep -q procwatch; then
    echo -e "${GREEN}  ✓ Module is loaded${NC}"
else
    echo -e "${RED}  ✗ Module not found${NC}"
    exit 1
fi

if [ -c /dev/procwatch ]; then
    echo -e "${GREEN}  ✓ Device /dev/procwatch exists${NC}"
else
    echo -e "${RED}  ✗ Device not found${NC}"
    exit 1
fi

if [ -f /proc/procwatch/stats ]; then
    echo -e "${GREEN}  ✓ /proc/procwatch/stats available${NC}"
else
    echo -e "${YELLOW}  ⚠ /proc interface not available (non-fatal)${NC}"
fi

echo ""
echo -e "${CYAN}══════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  ProcWatch installed successfully!${NC}"
echo -e "${CYAN}══════════════════════════════════════════════════${NC}"
echo ""
echo -e "  ${CYAN}Quick test:${NC}"
echo -e "    cat /dev/procwatch"
echo -e "    cat /proc/procwatch/stats"
echo -e ""
echo -e "  ${CYAN}Run daemon:${NC}"
echo -e "    sudo $PROJECT_DIR/daemon/procwatch_daemon"
echo -e ""
echo ""
