# devlog

---

**Feb 24**

starting this thing. idea: kernel module that tracks process spawns and feeds them to userspace via a char device. should be simple.

tried tracepoints first — `sched_process_fork`, `sched_process_exec`. cleaner API than kprobes, supposedly. spent like 2 hours getting nowhere, turns out the tracepoint symbols aren't exported on my kernel config (`CONFIG_TRACEPOINTS` isn't the issue, it's something about the module ABI). every SO answer assumes you're building in-tree. gave up and switched to kprobes.

---

**Feb 24 (evening)**

kprobes attach fine. probing `copy_process` to catch forks.

hit a dumb bug: had the comm buffer at 64 bytes because "feels right". BUILD_BUG_ON fires at load time — kernel expects exactly `TASK_COMM_LEN` which is 16. fixed in 30 seconds once I actually read the error. should've just looked at the header first.

basic event write to the ring buffer works. can see fork events in dmesg.

---

**Feb 25**

tried hooking `do_fork` for the actual spawn notification. doesn't exist on 6.8 — got folded into `kernel_clone` a while back apparently. grepped the kernel source, switched to `wake_up_new_task`. this one fires right after the child task is set up and before it runs, which is actually better timing anyway.

exec events working too via `do_execve`. got pid, ppid, comm, and filename.

---

**Feb 25 (later)**

char device reads from userspace. wrote a quick test in C, `read()` blocks forever when the buffer is empty. obvious in retrospect — forgot `O_NONBLOCK` handling. added the flag check in the read handler, return `-EAGAIN` when queue is empty. works now.

also: wasn't locking around the ring buffer head/tail. added spinlock. probably fine without it on single-core but not worth the risk.

---

**Feb 26**

wrote the daemon. reads from `/dev/procwatch`, parses the binary events, logs to stdout. added a signal handler for clean shutdown.

the `/proc/procwatch` interface for stats was an afterthought — just wanted to see drop counts without attaching gdb. procfs entry is ugly but functional.

---

**Feb 27**

added the Flask dashboard. SSE stream so the browser updates in real time without polling. nothing fancy, just a table that prepends new rows. tested in firefox, works.

had a weird issue where the daemon would miss events under load. ring buffer was 256 entries, bumped to 512. drop counter in `/proc/procwatch` confirms it was actually dropping. better now.

---

**Feb 28**

cleaned up all the FIXMEs and debug printks I left in. wrote README and architecture doc. added install script.

pushed to github. calling it done for now.

known issues I'm not fixing today: no filtering by uid, no exec arg capture (just argv[0] essentially), dashboard has no auth. fine for a local tool.
