# Architecture notes

## How it fits together

The kernel module hooks into fork/exec/exit via kprobes, stuffs events into a ring buffer, and exposes them through `/dev/procwatch`. The daemon reads that device and either pretty-prints to a terminal or spits out JSON. The Flask dashboard runs the daemon in JSON mode in a background thread and streams events to the browser over SSE.

## Kernel module

### kprobes

Three probes: `wake_up_new_task` for fork, `begin_new_exec` for exec, `do_exit` for exit. The pre-handler gets called before the probed function runs, so `pt_regs` still has the original arguments. On x86_64 the first argument is `regs->di`. `begin_new_exec` is the exception - by the time it fires, `current` is already the new task, so we just use that instead of reading a register.

`wake_up_new_task` and `do_exit` are internal kernel symbols with no stability guarantee. They've been stable for a while but technically could change.

### Ring buffer

Fixed circular array of 4096 `procwatch_event` structs (~400 KB total). Protected by a spinlock because kprobe pre-handlers can run in interrupt context, so a mutex won't work here.

Push from kprobe context: lock, write at head, advance head, unlock. If full, the tail advances too and the drop counter increments - oldest event is silently overwritten.

Pop from the char device read: lock, read at tail, advance tail, unlock.

### Character device

`/dev/procwatch` - read returns text lines, one event per line. Blocks if the buffer is empty unless `O_NONBLOCK` is set. `poll()` works so you can use `select`/`epoll`. Two ioctls: `PROCWATCH_IOC_STATS` fills a stats struct, `PROCWATCH_IOC_CLEAR` resets the buffer.

### /proc/procwatch/stats

Read-only summary via seq_file. Useful for quick checks without consuming events from the ring buffer.

## Event struct

```c
struct procwatch_event {
    __u32  event_type;      // FORK=1, EXEC=2, EXIT=3
    __s32  pid;
    __s32  tgid;
    __s32  ppid;
    __u32  uid;
    __s32  exit_code;       // only meaningful for EXIT
    __u64  timestamp_ns;
    char   comm[64];
};
```

96 bytes each. The text format written to the char device is: `TYPE PID PPID UID TIMESTAMP_NS COMM [exit_code=N]`.

## Daemon

Single-threaded C. Opens the device, reads in a loop, parses lines with sscanf, formats and prints. SIGINT/SIGTERM set a flag that breaks the loop.

## Web dashboard

Flask app with a background thread that reads `/dev/procwatch` continuously and stores events in a deque (capped at 1000). Three endpoints: `/api/events` returns the deque as JSON, `/api/stats` calls the ioctl, `/api/stream` is the SSE endpoint the browser connects to for live updates. The browser also polls `/api/stats` every 2 seconds as a fallback.

## Module load/unload order

Load: allocate buffer -> register char device -> create /proc entry -> register kprobes -> set monitoring active.

Unload goes in reverse: set monitoring inactive first, then unregister kprobes (`unregister_kprobe` waits for any running pre-handler to finish before returning, so there's no race between the last kprobe firing and the buffer being freed), then remove /proc, destroy char device, free buffer.
