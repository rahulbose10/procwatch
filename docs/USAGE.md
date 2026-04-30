# ProcWatch Usage Guide

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install -y build-essential linux-headers-$(uname -r)

# Fedora/RHEL
sudo dnf install -y kernel-devel kernel-headers gcc make

# For web dashboard
pip3 install flask
```

## Building

### Kernel Module

```bash
cd kernel/
make            # Build procwatch.ko
make clean      # Remove build artifacts
```

### Daemon

```bash
cd daemon/
make            # Build procwatch_daemon
make clean      # Remove build artifacts
```

## Kernel Module Operations

### Loading

```bash
sudo insmod kernel/procwatch.ko
```

Verify with:
```bash
# Check module is loaded
lsmod | grep procwatch

# Check device exists
ls -la /dev/procwatch

# View kernel messages
dmesg | tail -20
```

### Checking Status

```bash
# Quick statistics
cat /proc/procwatch/stats

# Sample output:
# ProcWatch v1.0.0 Statistics
# ========================
# Fork events:    1247
# Exec events:    892
# Exit events:    1193
# Events dropped: 0
# Buffer usage:   54 / 4096
# Monitoring:     active
```

### Unloading

```bash
sudo rmmod procwatch
```

## Reading Events

### Direct Device Access

```bash
# Read available events (blocks if empty)
cat /dev/procwatch

# Non-blocking read
dd if=/dev/procwatch bs=8192 count=1 iflag=nonblock 2>/dev/null

# Continuous monitoring
tail -f /dev/procwatch

# Filter by event type
cat /dev/procwatch | grep EXEC

# Filter by command
cat /dev/procwatch | grep "sshd"
```

### Event Format

Each line from `/dev/procwatch`:

```
TYPE   PID   PPID    UID  TIMESTAMP_NS         COMM [EXIT_INFO]
FORK   12345  1000  1000  1700000000000000000  bash
EXEC   12345  1000  1000  1700000000000100000  ls
EXIT   12345  1000  1000  1700000000000200000  ls exit_code=0
```

Fields:
- **TYPE**: `FORK`, `EXEC`, or `EXIT`
- **PID**: Process ID
- **PPID**: Parent Process ID
- **UID**: User ID of the process owner
- **TIMESTAMP_NS**: Kernel timestamp in nanoseconds
- **COMM**: Process command name (up to 63 chars)
- **EXIT_INFO**: Exit code (EXIT events only)

## Daemon Usage

```bash
# Default: colored terminal output
sudo ./procwatch_daemon

# JSON output (for piping to other tools)
sudo ./procwatch_daemon --json

# Show statistics only
sudo ./procwatch_daemon --stats

# Clear the event buffer
sudo ./procwatch_daemon --clear

# Disable colors (for log files)
sudo ./procwatch_daemon --no-color > procwatch.log

# JSON to file for analysis
sudo ./procwatch_daemon --json > events.json
```

### Daemon Options

| Flag | Short | Description |
|------|-------|-------------|
| `--follow` | `-f` | Continuously monitor (default) |
| `--json` | `-j` | JSON output mode |
| `--stats` | `-s` | Show statistics and exit |
| `--clear` | `-c` | Clear event buffer |
| `--no-color` | `-n` | Disable ANSI colors |
| `--help` | `-h` | Show help |

## Web Dashboard

```bash
cd web/
sudo python3 app.py                    # Default: localhost:5000
sudo python3 app.py --port 8080        # Custom port
sudo python3 app.py --host 0.0.0.0     # All interfaces
```

Open `http://localhost:5000` in your browser.

## Troubleshooting

### Module won't load

```bash
# Check kernel headers are installed
ls /lib/modules/$(uname -r)/build

# If missing:
sudo apt-get install linux-headers-$(uname -r)

# Check for build errors
cd kernel && make 2>&1

# Check kernel log for errors
dmesg | tail -20
```

### "Permission denied" on /dev/procwatch

```bash
# Must run as root
sudo cat /dev/procwatch

# Or add a udev rule for group access:
echo 'KERNEL=="procwatch", GROUP="adm", MODE="0440"' | \
    sudo tee /etc/udev/rules.d/99-procwatch.rules
sudo udevadm control --reload-rules
```

### Module loads but no events appear

```bash
# Verify monitoring is active
cat /proc/procwatch/stats

# Generate events manually
ls /tmp    # This triggers fork+exec+exit

# Check buffer isn't being consumed elsewhere
# Only one reader should be active at a time
```

### High event drop count

The ring buffer has a fixed capacity of 4096 events. If events are produced faster than consumed, older events are overwritten. Solutions:

1. Ensure the daemon or a reader is consuming events
2. Increase `RING_BUFFER_SIZE` in `procwatch.h` and rebuild
3. Use the daemon's JSON mode to pipe events to a file
