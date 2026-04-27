/* SPDX-License-Identifier: GPL-2.0 */
/*
 * procwatch.h - ProcWatch Kernel Module Header
 *
 * Real-time Linux process monitoring via kernel hooks.
 * Captures fork, exec, and exit events and exposes them
 * through a character device (/dev/procwatch).
 *
 * Author: ProcWatch Contributors
 * License: GPL-2.0
 */

#ifndef _PROCWATCH_H
#define _PROCWATCH_H

#include <linux/types.h>

/* ─── Version ─────────────────────────────────────────────────────────────── */
#define PROCWATCH_VERSION_MAJOR  1
#define PROCWATCH_VERSION_MINOR  0
#define PROCWATCH_VERSION_PATCH  0
#define PROCWATCH_VERSION_STRING "1.0.0"

/* ─── Device Configuration ────────────────────────────────────────────────── */
#define DEVICE_NAME    "procwatch"       /* /dev/procwatch                    */
#define CLASS_NAME     "procwatch"       /* Device class name                 */

/* ─── Ring Buffer Configuration ───────────────────────────────────────────── */
#define RING_BUFFER_SIZE  4096           /* Max events in ring buffer         */

/* ─── Process Name Limits ─────────────────────────────────────────────────── */
#define PROCWATCH_NAME_MAX  16           /* Max length of process name        */

/* ─── Event Types ─────────────────────────────────────────────────────────── */
enum procwatch_event_type {
    PW_EVENT_FORK = 1,              /* Process forked (new child)        */
    PW_EVENT_EXEC = 2,               /* Process called exec               */
    PW_EVENT_EXIT = 3,              /* Process exited                    */
};

/* ─── Process Event Structure ─────────────────────────────────────────────── */
/*
 * Each captured process event is stored in this structure.
 * The structure is designed to be compact for efficient ring buffer usage
 * and easy serialization to user-space.
 */
struct procwatch_event {
    __u32  event_type;                   /* PROC_EVENT_FORK/EXEC/EXIT         */
    __s32  pid;                          /* Process ID                        */
    __s32  tgid;                         /* Thread Group ID                   */
    __s32  ppid;                         /* Parent Process ID                 */
    __u32  uid;                          /* User ID of the process owner      */
    __s32  exit_code;                    /* Exit code (only for EXIT events)  */
    __u64  timestamp_ns;                 /* Kernel timestamp (nanoseconds)    */
    char   comm[PROCWATCH_NAME_MAX];     /* Process command name              */
};

/* ─── IOCTL Commands ──────────────────────────────────────────────────────── */
#define PROCWATCH_IOC_MAGIC  'P'
#define PROCWATCH_IOC_STATS  _IOR(PROCWATCH_IOC_MAGIC, 1, struct procwatch_stats)
#define PROCWATCH_IOC_CLEAR  _IO(PROCWATCH_IOC_MAGIC, 2)

/* ─── Statistics Structure ────────────────────────────────────────────────── */
struct procwatch_stats {
    __u64  total_forks;                  /* Total fork events captured        */
    __u64  total_execs;                  /* Total exec events captured        */
    __u64  total_exits;                  /* Total exit events captured        */
    __u64  events_dropped;               /* Events dropped (buffer full)      */
    __u32  buffer_usage;                 /* Current events in buffer          */
    __u32  buffer_capacity;              /* Max events in buffer              */
};

#endif /* _PROCWATCH_H */
