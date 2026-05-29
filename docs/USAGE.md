# Usage

## Setup

```bash
sudo apt-get install build-essential linux-headers-$(uname -r)
pip3 install flask   # only needed for the web dashboard
```

## Build

```bash
cd kernel && make
cd daemon && make
```

## Load/unload the module

```bash
sudo insmod kernel/procwatch.ko
lsmod | grep procwatch        # confirm it loaded
dmesg | tail -5               # check for errors
cat /proc/procwatch/stats     # quick sanity check

sudo rmmod procwatch
```

## Reading events

```bash
cat /dev/procwatch            # blocks until events arrive
cat /dev/procwatch | grep EXEC
```

Event format: `TYPE PID PPID UID TIMESTAMP_NS COMM [exit_code=N]`

## Daemon

```bash
sudo ./procwatch_daemon               # colored terminal output
sudo ./procwatch_daemon --json        # JSON, one object per line
sudo ./procwatch_daemon --stats       # print stats and exit
sudo ./procwatch_daemon --clear       # flush the ring buffer
sudo ./procwatch_daemon --no-color    # useful when redirecting to a file
```

## Web dashboard

```bash
sudo python3 web/app.py
# open http://localhost:5000
```

## Troubleshooting

**Module fails to load** - make sure kernel headers are installed (`ls /lib/modules/$(uname -r)/build` should exist). Check `dmesg` for the actual error. If the kprobe registration fails it'll print which symbol it couldn't find.

**No events showing up** - run `ls /tmp` or anything that forks a process, then check `cat /proc/procwatch/stats` to see if fork/exec/exit counts went up. If they did, something else is consuming the device.

**Permission denied on /dev/procwatch** - the device is root-only by default. Run with sudo or add a udev rule.

**High drop count in stats** - the buffer is only 4096 events. If nothing is reading the device the buffer fills and old events get overwritten. Either start the daemon or increase `RING_BUFFER_SIZE` in `procwatch.h` and rebuild.
