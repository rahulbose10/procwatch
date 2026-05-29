// SPDX-License-Identifier: GPL-2.0
/*
 * procwatch.c - kernel module for monitoring process lifecycle events
 * via kprobes. Exposes events through /dev/procwatch as text lines.
 *
 * Kprobes instead of tracepoints because some tracepoint symbols aren't
 * exported to modules depending on kernel config (learned this the hard way).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/cred.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "procwatch.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ProcWatch Contributors");
MODULE_DESCRIPTION("Real-time process monitoring via kprobes");
MODULE_VERSION(PROCWATCH_VERSION_STRING);

struct procwatch_ring_buffer {
    struct procwatch_event  events[RING_BUFFER_SIZE];
    unsigned int            head;
    unsigned int            tail;
    unsigned int            count;
    spinlock_t              lock;
};

static dev_t              pw_dev_num;
static struct cdev        pw_cdev;
static struct class      *pw_class;
static struct device     *pw_device;
static struct procwatch_ring_buffer *pw_buffer;

static atomic64_t         stat_forks   = ATOMIC64_INIT(0);
static atomic64_t         stat_execs   = ATOMIC64_INIT(0);
static atomic64_t         stat_exits   = ATOMIC64_INIT(0);
static atomic64_t         stat_dropped = ATOMIC64_INIT(0);

static DECLARE_WAIT_QUEUE_HEAD(pw_waitqueue);
static bool               pw_monitoring_active = false;
static DEFINE_MUTEX(pw_dev_mutex);

static int ring_buffer_init(void)
{
    pw_buffer = kzalloc(sizeof(*pw_buffer), GFP_KERNEL);
    if (!pw_buffer)
        return -ENOMEM;
    pw_buffer->head  = 0;
    pw_buffer->tail  = 0;
    pw_buffer->count = 0;
    spin_lock_init(&pw_buffer->lock);
    pr_info("procwatch: ring buffer initialized (%d slots)\n", RING_BUFFER_SIZE);
    return 0;
}

static void ring_buffer_destroy(void)
{
    kfree(pw_buffer);
    pw_buffer = NULL;
}

static void ring_buffer_push(const struct procwatch_event *event)
{
    unsigned long flags;
    // tried using mutex here first, crashed in interrupt context
    spin_lock_irqsave(&pw_buffer->lock, flags);
    memcpy(&pw_buffer->events[pw_buffer->head], event, sizeof(*event));
    pw_buffer->head = (pw_buffer->head + 1) % RING_BUFFER_SIZE;
    if (pw_buffer->count < RING_BUFFER_SIZE) {
        pw_buffer->count++;
    } else {
        // buffer full, oldest event silently dropped
        pw_buffer->tail = (pw_buffer->tail + 1) % RING_BUFFER_SIZE;
        atomic64_inc(&stat_dropped);
    }
    spin_unlock_irqrestore(&pw_buffer->lock, flags);
    wake_up_interruptible(&pw_waitqueue);
}

static int ring_buffer_pop(struct procwatch_event *event)
{
    unsigned long flags;
    int ret = -1;
    spin_lock_irqsave(&pw_buffer->lock, flags);
    if (pw_buffer->count > 0) {
        memcpy(event, &pw_buffer->events[pw_buffer->tail], sizeof(*event));
        pw_buffer->tail = (pw_buffer->tail + 1) % RING_BUFFER_SIZE;
        pw_buffer->count--;
        ret = 0;
    }
    spin_unlock_irqrestore(&pw_buffer->lock, flags);
    return ret;
}

static void ring_buffer_clear(void)
{
    unsigned long flags;
    spin_lock_irqsave(&pw_buffer->lock, flags);
    pw_buffer->head  = 0;
    pw_buffer->tail  = 0;
    pw_buffer->count = 0;
    spin_unlock_irqrestore(&pw_buffer->lock, flags);
}

// TODO: add filtering by uid/pid/comm to reduce noise on busy systems
static void capture_event(enum procwatch_event_type type,
                          struct task_struct *task, int exit_code)
{
    struct procwatch_event event;

    if (!pw_monitoring_active || !pw_buffer)
        return;

    memset(&event, 0, sizeof(event));
    event.event_type   = type;
    event.pid          = task->pid;
    event.tgid         = task->tgid;
    event.ppid         = task->real_parent ? task->real_parent->pid : 0;
    event.uid          = from_kuid_munged(current_user_ns(), task_uid(task));
    event.exit_code    = exit_code;
    event.timestamp_ns = ktime_get_real_ns();
    get_task_comm(event.comm, task);

    ring_buffer_push(&event);
    // pr_info("DEBUG: event type=%d pid=%d\n", type, event.pid);

    switch (type) {
    case PW_EVENT_FORK: atomic64_inc(&stat_forks); break;
    case PW_EVENT_EXEC: atomic64_inc(&stat_execs); break;
    case PW_EVENT_EXIT: atomic64_inc(&stat_exits); break;
    }
}

// kprobes on wake_up_new_task, begin_new_exec, do_exit
// note: wake_up_new_task is an internal symbol, not stable ABI

// TODO: x86_64 only - ARM64 needs regs->regs[0] instead of regs->di
static int fork_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    struct task_struct *task;
    task = (struct task_struct *)regs->di;
    // pr_info("fork_pre: task=%p\n", task);
    if (task)
        capture_event(PW_EVENT_FORK, task, 0);
    return 0;
}

static struct kprobe fork_kp = {
    .symbol_name = "wake_up_new_task",
    .pre_handler = fork_pre_handler,
};

// current is already the execing task when begin_new_exec fires
static int exec_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    capture_event(PW_EVENT_EXEC, current, 0);
    return 0;
}

static struct kprobe exec_kp = {
    .symbol_name = "begin_new_exec",
    .pre_handler = exec_pre_handler,
};

static int exit_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    long code = (long)regs->di;
    capture_event(PW_EVENT_EXIT, current, (int)code);
    return 0;
}

static struct kprobe exit_kp = {
    .symbol_name = "do_exit",
    .pre_handler = exit_pre_handler,
};

static int register_probes(void)
{
    int ret;

    ret = register_kprobe(&fork_kp);
    if (ret) {
        pr_err("procwatch: failed to register fork kprobe (%d)\n", ret);
        return ret;
    }
    pr_info("procwatch: fork kprobe registered at %pS\n", fork_kp.addr);

    ret = register_kprobe(&exec_kp);
    if (ret) {
        pr_err("procwatch: failed to register exec kprobe (%d)\n", ret);
        goto unreg_fork;
    }
    pr_info("procwatch: exec kprobe registered at %pS\n", exec_kp.addr);

    ret = register_kprobe(&exit_kp);
    if (ret) {
        pr_err("procwatch: failed to register exit kprobe (%d)\n", ret);
        goto unreg_exec;
    }
    pr_info("procwatch: exit kprobe registered at %pS\n", exit_kp.addr);

    return 0;

unreg_exec:
    unregister_kprobe(&exec_kp);
unreg_fork:
    unregister_kprobe(&fork_kp);
    return ret;
}

static void unregister_probes(void)
{
    unregister_kprobe(&exit_kp);
    unregister_kprobe(&exec_kp);
    unregister_kprobe(&fork_kp);
    pr_info("procwatch: kprobes unregistered\n");
}

static int pw_dev_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int pw_dev_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t pw_dev_read(struct file *file, char __user *buf,
                           size_t count, loff_t *ppos)
{
    struct procwatch_event event;
    char line[256];
    int len;
    ssize_t total = 0;
    const char *type_str;

    if (!pw_buffer)
        return -ENODEV;

    if (pw_buffer->count == 0) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(pw_waitqueue, pw_buffer->count > 0))
            return -ERESTARTSYS;
    }

    mutex_lock(&pw_dev_mutex);

    while (total < count) {
        if (ring_buffer_pop(&event) != 0)
            break;

        switch (event.event_type) {
        case PW_EVENT_FORK: type_str = "FORK"; break;
        case PW_EVENT_EXEC: type_str = "EXEC"; break;
        case PW_EVENT_EXIT: type_str = "EXIT"; break;
        default: type_str = "UNKN"; break;
        }

        if (event.event_type == PW_EVENT_EXIT) {
            len = snprintf(line, sizeof(line),
                           "%-4s %6d %6d %5u %llu %s exit_code=%d\n",
                           type_str, event.pid, event.ppid,
                           event.uid, event.timestamp_ns,
                           event.comm, event.exit_code);
        } else {
            len = snprintf(line, sizeof(line),
                           "%-4s %6d %6d %5u %llu %s\n",
                           type_str, event.pid, event.ppid,
                           event.uid, event.timestamp_ns,
                           event.comm);
        }

        if (len <= 0 || total + len > count)
            break;

        if (copy_to_user(buf + total, line, len)) {
            mutex_unlock(&pw_dev_mutex);
            return total > 0 ? total : -EFAULT;
        }
        total += len;
    }

    mutex_unlock(&pw_dev_mutex);
    return total;
}

static __poll_t pw_dev_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;
    poll_wait(file, &pw_waitqueue, wait);
    if (pw_buffer && pw_buffer->count > 0)
        mask |= EPOLLIN | EPOLLRDNORM;
    return mask;
}

static long pw_dev_ioctl(struct file *file, unsigned int cmd,
                         unsigned long arg)
{
    struct procwatch_stats stats;

    switch (cmd) {
    case PROCWATCH_IOC_STATS:
        stats.total_forks    = atomic64_read(&stat_forks);
        stats.total_execs    = atomic64_read(&stat_execs);
        stats.total_exits    = atomic64_read(&stat_exits);
        stats.events_dropped = atomic64_read(&stat_dropped);
        stats.buffer_usage   = pw_buffer ? pw_buffer->count : 0;
        stats.buffer_capacity = RING_BUFFER_SIZE;
        if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
            return -EFAULT;
        return 0;
    case PROCWATCH_IOC_CLEAR:
        ring_buffer_clear();
        return 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations pw_fops = {
    .owner          = THIS_MODULE,
    .open           = pw_dev_open,
    .release        = pw_dev_release,
    .read           = pw_dev_read,
    .poll           = pw_dev_poll,
    .unlocked_ioctl = pw_dev_ioctl,
};

// /proc/procwatch/stats - read-only, doesn't consume events from the ring buffer

static struct proc_dir_entry *pw_proc_dir;
static struct proc_dir_entry *pw_proc_stats;

static int pw_proc_stats_show(struct seq_file *m, void *v)
{
    seq_printf(m, "ProcWatch v%s Statistics\n", PROCWATCH_VERSION_STRING);
    seq_printf(m, "========================\n");
    seq_printf(m, "Fork events:    %lld\n", atomic64_read(&stat_forks));
    seq_printf(m, "Exec events:    %lld\n", atomic64_read(&stat_execs));
    seq_printf(m, "Exit events:    %lld\n", atomic64_read(&stat_exits));
    seq_printf(m, "Events dropped: %lld\n", atomic64_read(&stat_dropped));
    seq_printf(m, "Buffer usage:   %u / %u\n",
               pw_buffer ? pw_buffer->count : 0, RING_BUFFER_SIZE);
    seq_printf(m, "Monitoring:     %s\n",
               pw_monitoring_active ? "active" : "inactive");
    return 0;
}

static int pw_proc_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, pw_proc_stats_show, NULL);
}

static const struct proc_ops pw_proc_stats_ops = {
    .proc_open    = pw_proc_stats_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int create_proc_entries(void)
{
    pw_proc_dir = proc_mkdir("procwatch", NULL);
    if (!pw_proc_dir)
        return -ENOMEM;
    pw_proc_stats = proc_create("stats", 0444, pw_proc_dir, &pw_proc_stats_ops);
    if (!pw_proc_stats) {
        remove_proc_entry("procwatch", NULL);
        return -ENOMEM;
    }
    return 0;
}

static void remove_proc_entries(void)
{
    if (pw_proc_stats)
        remove_proc_entry("stats", pw_proc_dir);
    if (pw_proc_dir)
        remove_proc_entry("procwatch", NULL);
}

static int create_char_device(void)
{
    int ret;

    ret = alloc_chrdev_region(&pw_dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;

    cdev_init(&pw_cdev, &pw_fops);
    pw_cdev.owner = THIS_MODULE;
    ret = cdev_add(&pw_cdev, pw_dev_num, 1);
    if (ret < 0) goto unreg;

    pw_class = class_create(CLASS_NAME);
    if (IS_ERR(pw_class)) { ret = PTR_ERR(pw_class); goto del_cdev; }

    pw_device = device_create(pw_class, NULL, pw_dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(pw_device)) { ret = PTR_ERR(pw_device); goto del_class; }

    pr_info("procwatch: char device created (major=%d)\n", MAJOR(pw_dev_num));
    return 0;

del_class:  class_destroy(pw_class);
del_cdev:   cdev_del(&pw_cdev);
unreg:      unregister_chrdev_region(pw_dev_num, 1);
    return ret;
}

static void destroy_char_device(void)
{
    device_destroy(pw_class, pw_dev_num);
    class_destroy(pw_class);
    cdev_del(&pw_cdev);
    unregister_chrdev_region(pw_dev_num, 1);
}

static int __init procwatch_init(void)
{
    int ret;

    pr_info("procwatch: initializing v%s\n", PROCWATCH_VERSION_STRING);

    ret = ring_buffer_init();
    if (ret) return ret;

    ret = create_char_device();
    if (ret) goto err_buf;

    ret = create_proc_entries();
    if (ret)
        pr_warn("procwatch: /proc creation failed (non-fatal)\n");

    ret = register_probes();
    if (ret) goto err_proc;

    pw_monitoring_active = true;

    pr_info("procwatch: loaded v%s, device=/dev/%s, buffer=%d events\n",
            PROCWATCH_VERSION_STRING, DEVICE_NAME, RING_BUFFER_SIZE);
    return 0;

err_proc:
    remove_proc_entries();
    destroy_char_device();
err_buf:
    ring_buffer_destroy();
    return ret;
}

static void __exit procwatch_exit(void)
{
    pw_monitoring_active = false;
    unregister_probes();
    remove_proc_entries();
    destroy_char_device();
    ring_buffer_destroy();

    pr_info("procwatch: unloaded, fork=%lld exec=%lld exit=%lld dropped=%lld\n",
            atomic64_read(&stat_forks), atomic64_read(&stat_execs),
            atomic64_read(&stat_exits), atomic64_read(&stat_dropped));
}

module_init(procwatch_init);
module_exit(procwatch_exit);
