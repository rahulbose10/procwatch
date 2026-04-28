# ═══════════════════════════════════════════════════════════════════════════
#  ProcWatch Daemon - Makefile
# ═══════════════════════════════════════════════════════════════════════════

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter
TARGET  := procwatch_daemon
SRC     := procwatch_daemon.c

.PHONY: all clean install help

all: $(TARGET)
	@echo "  ✓ Built $(TARGET)"

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
	@echo "  ✓ Clean complete"

install: $(TARGET)
	sudo install -m 755 $(TARGET) /usr/local/bin/
	@echo "  ✓ Installed to /usr/local/bin/$(TARGET)"

help:
	@echo "  make          Build the daemon"
	@echo "  make clean    Remove build artifacts"
	@echo "  make install  Install to /usr/local/bin"
	@echo "  make help     Show this help"
