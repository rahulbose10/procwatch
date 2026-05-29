# ProcWatch - Linux Process Monitor

Kernel module that tracks process lifecycle events (fork, exec, exit) using kprobes. Events are stored in a ring buffer inside the kernel and exposed through a character device at `/dev/procwatch`. There's also a user-space daemon that pretty-prints events to a terminal.

This was written as a learning project to understand how kernel modules, kprobes, and character devices work together.

## How it works

The kernel module registers kprobe handlers on `wake_up_new_task`, `begin_new_exec`, and `do_exit`. Each event captures the PID, PPID, UID, process name, and a timestamp. Events go into a circular buffer (4096 slots) protected by a spinlock. The buffer overwrites the oldest entry when full.

User space reads events from `/dev/procwatch` as newline-delimited text. The daemon pretty-prints them in the terminal or as JSON. `/proc/procwatch/stats` shows counts without consuming events from the buffer.

One challenge during development was that some tracepoint symbols aren't exported to modules on certain kernel versions, which is why the implementation ended up using kprobes directly instead of the tracepoint API. kprobe registration itself also has a few failure modes that weren't obvious at first, particularly around symbol resolution at module load time when the target function name doesn't match exactly what's in kallsyms.

## Quick Start

```bash
# Install kernel headers if you don't have them
sudo apt-get install build-essential linux-headers-$(uname -r)

# Build and load the module
cd kernel
make
sudo make load

# Confirm it loaded
lsmod | grep procwatch
cat /proc/procwatch/stats

# Stream events in another terminal
cat /dev/procwatch
```

Build and run the daemon:

```bash
cd daemon
make
sudo ./procwatch_daemon
```

To unload:

```bash
cd kernel
sudo make unload
```

## Usage

### Kernel module

```bash
sudo insmod procwatch.ko

cat /proc/procwatch/stats
dmesg | grep procwatch

sudo rmmod procwatch
```

### Daemon

```bash
sudo ./procwatch_daemon           # real-time terminal output
sudo ./procwatch_daemon --json    # JSON output
sudo ./procwatch_daemon --stats   # print statistics only
sudo ./procwatch_daemon --clear   # clear the ring buffer
sudo ./procwatch_daemon --no-color
```

### Direct device access

```bash
cat /dev/procwatch
cat /dev/procwatch | grep EXEC
```

## Sample output

```
TIME         EVENT    PID      PPID       USER                 COMMAND
------------ -------- -------- ---------- -------------------- ---------------------
14:23:01.447 FORK       15234     1182    john                 bash
14:23:01.449 EXEC       15234     1182    john                 ls
14:23:01.452 EXIT       15234     1182    john                 ls exit_code=0
14:23:02.103 FORK       15235      892    root                 cron
14:23:02.105 EXEC       15235      892    root                 run-parts
```

## Project structure

```
procwatch/
├── kernel/
│   ├── procwatch.c
│   ├── procwatch.h
│   └── Makefile
├── daemon/
│   ├── procwatch_daemon.c
│   └── Makefile
├── docs/
│   ├── ARCHITECTURE.md
│   └── USAGE.md
├── scripts/
│   └── install.sh
└── README.md
```

## Technical notes

- kprobes on `wake_up_new_task`, `begin_new_exec`, and `do_exit`
- spinlock-protected ring buffer, O(1) push/pop
- character device supports blocking read, poll, and ioctl for stats
- events are plain text, one per line
- ring buffer takes about 400 KB of kernel memory

Tested on Ubuntu 24.04 with kernel 6.8.0-94-generic.

## Known limitations

- x86_64 only; no support for other architectures
- The ring buffer has a fixed size (4096 slots), so events can be dropped under heavy process creation load
- kprobe target symbols (`wake_up_new_task`, `begin_new_exec`, `do_exit`) are internal kernel functions and may be renamed or inlined across kernel versions, breaking the probes at module load time

## License

GPL-2.0 - see [LICENSE](LICENSE).

## Author

- GitHub: [@rahulbose10](https://github.com/rahulbose10)
- LinkedIn: [Rahul Bose](https://www.linkedin.com/in/rahul-bose-40518a137/)
