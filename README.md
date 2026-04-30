# ProcWatch — Linux Process Monitor

![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)
![Kernel](https://img.shields.io/badge/kernel-5.x%2B-orange.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C%20%7C%20Python-green.svg)

Kernel module for real-time process monitoring. Tracks fork, exec, and exit events via kprobes, stores them in a ring buffer, and exposes them through a character device.

---

## Overview

ProcWatch hooks into the kernel via kprobes to capture every process lifecycle event — creation, execution, and termination — with nanosecond timestamps and no polling.

Unlike user-space tools (`top`, `htop`, `ps`), it runs at the kernel level:

- No events are missed — every fork, exec, and exit is captured
- Minimal overhead — kprobe hooks add negligible latency
- Nanosecond timestamps for precise event ordering
- Parent-child relationships tracked via PID/PPID

### Features

| Feature | Description |
|---------|-------------|
| Kernel-level monitoring | kprobe hooks capture all process lifecycle events |
| Real-time tracking | Fork, exec, and exit events with nanosecond timestamps |
| Ring buffer storage | Lock-free circular buffer (4096 events, configurable) |
| Character device | `/dev/procwatch` for user-space data access |
| Web dashboard | Flask-based real-time visualization |
| Proc interface | `/proc/procwatch/stats` for quick statistics |
| Safe unloading | Clean teardown with kprobe synchronization |

---

## Architecture

```
+-------------------------------------------------------------------+
|                         KERNEL SPACE                              |
|                                                                   |
|  +---------------+    +--------------+    +------------------+   |
|  |  kprobe       |    |  Event       |    |  Ring Buffer     |   |
|  |  Hooks        |--->|  Capture     |--->|  (4096 slots)    |   |
|  |               |    |              |    |                  |   |
|  | - fork        |    | - PID/PPID   |    |  +-+-+-+-+-+    |   |
|  | - exec        |    | - UID        |    |  | | | | | |    |   |
|  | - exit        |    | - timestamp  |    |  +-+-+-+-+-+    |   |
|  +---------------+    | - comm       |    |  head --> tail   |   |
|                       +--------------+    +--------+---------+   |
|                                                    |             |
|  +-----------------------------------------------+ |             |
|  |  /proc/procwatch/stats                        | |             |
|  +-----------------------------------------------+ |             |
|                                                    |             |
+----------------------------------------------------+-------------+
|                     CHARACTER DEVICE               |             |
|                   /dev/procwatch                   |             |
|              (read, poll, ioctl)                   |             |
+----------------------------------------------------+-------------+
|                         USER SPACE                 |             |
|                                                    v             |
|  +---------------+    +--------------+    +------------------+   |
|  |  Daemon       |    |  Web Server  |    |  CLI Tools       |   |
|  |  (C binary)   |<---|  (Flask)     |    |  (cat, scripts)  |   |
|  |               |    |              |    |                  |   |
|  | - Terminal    |    | - Dashboard  |    | - cat /dev/pw    |   |
|  | - JSON mode   |    | - Charts     |    | - custom apps    |   |
|  | - Stats       |    | - SSE stream |    |                  |   |
|  +---------------+    +--------------+    +------------------+   |
+-------------------------------------------------------------------+
```

### Data Flow

1. Process event occurs (fork/exec/exit) in the kernel
2. kprobe callback captures event data (PID, name, timestamp, etc.)
3. Ring buffer stores the event, overwriting oldest if full
4. Character device makes data available to user-space via `read()`
5. Daemon or dashboard reads, parses, and displays events

---

## Quick Start

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y build-essential linux-headers-$(uname -r)

# For web dashboard (optional)
pip3 install flask
```

### Build & Run

```bash
git clone https://github.com/rahulbose10/procwatch.git
cd procwatch

# Build and load kernel module
cd kernel
make
sudo make load

# Verify
lsmod | grep procwatch
cat /proc/procwatch/stats

# Watch events
cat /dev/procwatch

# Build and run the daemon
cd ../daemon
make
sudo ./procwatch_daemon
```

### Expected Output

```
===================================================================
  ProcWatch - Real-time Process Monitor
===================================================================

TIME         EVENT    PID      PPID       USER                 COMMAND
------------ -------- -------- ---------- -------------------- ---------------------
14:23:01.447 FORK       15234     1182    john                 bash
14:23:01.449 EXEC       15234     1182    john                 ls
14:23:01.452 EXIT       15234     1182    john                 ls exit_code=0
14:23:02.103 FORK       15235      892    root                 cron
14:23:02.105 EXEC       15235      892    root                 run-parts
```

### Cleanup

```bash
cd kernel
sudo make unload
```

---

## Usage

### Kernel Module

```bash
sudo insmod procwatch.ko

# Check status
cat /proc/procwatch/stats

# Kernel log output
dmesg | grep procwatch

# Unload
sudo rmmod procwatch
```

### Daemon

```bash
# Real-time terminal output
sudo ./procwatch_daemon

# JSON output
sudo ./procwatch_daemon --json

# Print statistics
sudo ./procwatch_daemon --stats

# Clear event buffer
sudo ./procwatch_daemon --clear

# Disable color (for logging)
sudo ./procwatch_daemon --no-color
```

### Web Dashboard

```bash
cd web
sudo python3 app.py
# Open http://localhost:5000
```

### Direct Device Access

```bash
# Read raw events
cat /dev/procwatch

# Stream events
tail -f /dev/procwatch

# Filter by type
cat /dev/procwatch | grep EXEC

# Measure throughput
cat /dev/procwatch | pv -l > /dev/null
```

---

## Web Dashboard

Real-time visualization of process activity:

- Live event stream — scrolling table of all process events
- Statistics — fork/exec/exit counts and throughput
- Event distribution chart
- Top commands by activity

Start with `sudo python3 web/app.py`, open `http://localhost:5000`.

---

## Project Structure

```
procwatch/
├── kernel/                     # Kernel module
│   ├── procwatch.c             # Main module source
│   ├── procwatch.h             # Shared header (structs, ioctl defs)
│   └── Makefile
├── daemon/                     # User-space daemon
│   ├── procwatch_daemon.c      # Daemon source
│   └── Makefile
├── web/                        # Web dashboard
│   ├── app.py                  # Flask server
│   └── templates/
│       └── index.html
├── docs/
│   ├── ARCHITECTURE.md
│   └── USAGE.md
├── scripts/
│   └── install.sh
├── README.md
├── LICENSE
└── .gitignore
```

---

## Testing

### Verified Platforms

| Distribution | Kernel | Status |
|-------------|--------|--------|
| Ubuntu 20.04 LTS | 5.15.x | OK |
| Ubuntu 22.04 LTS | 6.2.x  | OK |
| Ubuntu 24.04 LTS | 6.8.x  | OK |
| Debian 12   | 6.1.x  | OK |

### Smoke Test

```bash
sudo insmod kernel/procwatch.ko
lsmod | grep procwatch
ls -la /dev/procwatch
cat /proc/procwatch/stats

# Generate events
for i in $(seq 1 10); do ls > /dev/null; done

# Verify captured
cat /proc/procwatch/stats

sudo rmmod procwatch
```

---

## Technical Details

- **Event capture**: kprobes on `do_fork`, `do_execve`, and the process exit path
- **Ring buffer**: Spinlock-protected circular buffer, O(1) push/pop
- **Character device**: Blocking and non-blocking read, poll support, ioctl for stats
- **Data format**: Text-based, one event per line
- **Memory**: ~400 KB (ring buffer) + minimal kernel overhead

---

## Contributing

Areas for improvement:

- [ ] Process CPU/memory usage tracking
- [ ] Filtering by UID, PID, or command name
- [ ] Binary protocol for higher throughput
- [ ] Systemd service file for daemon
- [ ] Grafana/Prometheus integration
- [ ] eBPF backend option

---

## License

GPL-2.0 — see [LICENSE](LICENSE).

---

## Author

- GitHub: [@rahulbose10](https://github.com/rahulbose10)
- LinkedIn: [Rahul Bose](https://www.linkedin.com/in/rahul-bose-40518a137/)
