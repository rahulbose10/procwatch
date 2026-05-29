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

## output

```
IT   9468   9467     0 1780087495831406701 cat exit_code=2
EXIT   9467   9466  1000 1780087495832396971 sudo exit_code=256
EXIT   9466   6273  1000 1780087495833390300 sudo exit_code=2
EXIT   9576   5585  1000 1780087497006991382 StreamTrans #42 exit_code=0
FORK   9586   4395  1000 1780087497664100391 gnome-shell
EXIT   9586   4395  1000 1780087498164477626 pool-gnome-shel exit_code=0
FORK   9587   6273  1000 1780087499703918732 bash
FORK   9588   6273  1000 1780087499704111430 bash
EXEC   9588   6273  1000 1780087499704628023 bash
EXEC   9587   6273  1000 1780087499704628070 bash
FORK   9589   9587  1000 1780087499711940722 sudo
FORK   9590   9589  1000 1780087499712565322 sudo
EXEC   9590   9589     0 1780087499712814220 sudo
EXIT   9451   5585  1000 1780087500435367454 StreamTrans #56 exit_code=0
EXIT   9579   4578  1000 1780087503290214047 StreamT~ns #483 exit_code=0

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
