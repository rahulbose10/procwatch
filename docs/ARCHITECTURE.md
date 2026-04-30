# ProcWatch Architecture

## System Overview

ProcWatch is a three-tier process monitoring system:

```
TIER 1: KERNEL
  kprobe Hooks -> Event Capture -> Ring Buffer -> Character Device (/dev/procwatch)

TIER 2: DAEMON
  read(/dev/procwatch) -> Parse Events -> Display/Serve

TIER 3: DASHBOARD
  Flask Server -> SSE Stream -> Browser (Charts + Table)
```

## Kernel Module Internals

### kprobe Hooks

ProcWatch registers three kprobes on internal kernel functions:

| Symbol | Trigger | Data Captured |
|--------|---------|---------------|
| `wake_up_new_task` | `fork()` / `clone()` | Child task struct (arg 0), parent PID via `real_parent` |
| `begin_new_exec` | `execve()` | `current` task at exec entry |
| `do_exit` | process exit | `current` task, exit code (arg 0) |

Arguments are read directly from registers (`pt_regs`) in the pre-handler. On x86_64 the first argument is `rdi`.

### Ring Buffer

Fixed-size circular array protected by a spinlock:

```
Capacity: 4096 events (configurable via RING_BUFFER_SIZE)
Memory:   ~400 KB (each event is ~96 bytes)

Push (producer - kprobe context):
  spin_lock_irqsave -> write at head -> advance head -> unlock
  If full: overwrite oldest, advance tail, increment drop counter

Pop (consumer - process context):
  spin_lock_irqsave -> read at tail -> advance tail -> unlock
  If empty: return error (caller decides to block or retry)
```

`spin_lock_irqsave` is required because kprobe pre-handlers can run in interrupt context.

### Character Device

`/dev/procwatch` supports:

- **`read()`**: Returns events as text lines. Blocks if buffer is empty (unless `O_NONBLOCK`). Format: `TYPE PID PPID UID TIMESTAMP_NS COMM [EXIT_CODE]`
- **`poll()`**: Returns `EPOLLIN` when events are available. Used by `select()`/`epoll()`.
- **`ioctl()`**: `PROCWATCH_IOC_STATS` returns statistics struct. `PROCWATCH_IOC_CLEAR` flushes the buffer.

### /proc Interface

`/proc/procwatch/stats` provides a human-readable statistics summary using the `seq_file` interface. Read-only, does not consume events from the ring buffer.

## Event Structure

```c
struct procwatch_event {
    __u32  event_type;             // FORK=1, EXEC=2, EXIT=3
    __s32  pid;                    // Process ID
    __s32  tgid;                   // Thread Group ID
    __s32  ppid;                   // Parent Process ID
    __u32  uid;                    // User ID
    __s32  exit_code;              // Exit code (EXIT only)
    __u64  timestamp_ns;           // Nanosecond timestamp
    char   comm[64];               // Command name
};
```

Total size: 96 bytes per event.

## Daemon Architecture

Single-threaded C program:

1. Opens `/dev/procwatch` in blocking mode
2. Reads event lines in a loop
3. Parses text into structured fields
4. Formats output (terminal with ANSI colors, or JSON)
5. Handles `SIGINT`/`SIGTERM` for graceful shutdown

## Web Dashboard Architecture

```
Flask App (app.py)
  Reader Thread (background)
    Reads /dev/procwatch -> Parses -> Stores in deque
  GET /              -> Serves index.html
  GET /api/events    -> Returns event array (JSON)
  GET /api/stats     -> Returns statistics (JSON)
  GET /api/stream    -> Server-Sent Events (real-time)

Browser (index.html)
  EventSource(/api/stream)  -> Live updates
  Polling fallback          -> GET /api/stats every 2s
  Rendering
    Event table (last 100 events)
    Stat cards (fork/exec/exit counts)
    Donut chart (event distribution)
    Bar chart (top commands)
```

## Module Lifecycle

### Loading (`insmod procwatch.ko`)

1. Allocate ring buffer (`kzalloc`)
2. Register character device (`alloc_chrdev_region` -> `cdev_add` -> `device_create`)
3. Create `/proc/procwatch/stats`
4. Register kprobes (`wake_up_new_task`, `begin_new_exec`, `do_exit`)
5. Set `pw_monitoring_active = true`

### Unloading (`rmmod procwatch`)

1. Set `pw_monitoring_active = false`
2. Unregister kprobes (`unregister_kprobe` x3)
3. Remove `/proc` entries
4. Destroy character device
5. Free ring buffer

Kprobes are unregistered before freeing the ring buffer. `unregister_kprobe` waits for any in-progress pre-handler to finish before returning, so there is no use-after-free window.
