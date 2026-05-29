/*
 * procwatch_daemon.c - reads events from /dev/procwatch and prints them.
 * terminal, JSON, and stats modes. needs root to open the device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

// ioctl definitions - duplicated here to avoid pulling in kernel headers
#define PROCWATCH_IOC_MAGIC  'P'

struct procwatch_stats {
    unsigned long long total_forks;
    unsigned long long total_execs;
    unsigned long long total_exits;
    unsigned long long events_dropped;
    unsigned int       buffer_usage;
    unsigned int       buffer_capacity;
};

#define PROCWATCH_IOC_STATS  _IOR(PROCWATCH_IOC_MAGIC, 1, struct procwatch_stats)
#define PROCWATCH_IOC_CLEAR  _IO(PROCWATCH_IOC_MAGIC, 2)

#define DEVICE_PATH     "/dev/procwatch"
#define READ_BUF_SIZE   8192
#define MAX_LINE_LEN    512

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"

enum output_mode {
    MODE_TERMINAL = 0,
    MODE_JSON     = 1,
    MODE_PLAIN    = 2,
};

static volatile int running = 1;
static int use_color = 1;
static enum output_mode output_mode = MODE_TERMINAL;

static void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        running = 0;
        fprintf(stderr, "\n%sProcWatch daemon shutting down...%s\n",
                use_color ? COLOR_YELLOW : "", use_color ? COLOR_RESET : "");
    }
}

// not thread-safe, but we're single-threaded so fine
static const char *get_username(unsigned int uid)
{
    struct passwd *pw = getpwuid(uid);
    static char uid_str[16];

    if (pw)
        return pw->pw_name;

    snprintf(uid_str, sizeof(uid_str), "%u", uid);
    return uid_str;
}

static void format_timestamp(unsigned long long ts_ns, char *buf, size_t len)
{
    time_t seconds = ts_ns / 1000000000ULL;
    unsigned int millis = (ts_ns % 1000000000ULL) / 1000000;
    struct tm *tm_info = localtime(&seconds);

    if (tm_info) {
        snprintf(buf, len, "%02d:%02d:%02d.%03u",
                 tm_info->tm_hour, tm_info->tm_min,
                 tm_info->tm_sec, millis);
    } else {
        snprintf(buf, len, "%llu", ts_ns);
    }
}

static const char *get_event_color(const char *event_type)
{
    if (!use_color)
        return "";

    if (strcmp(event_type, "FORK") == 0)
        return COLOR_GREEN;
    if (strcmp(event_type, "EXEC") == 0)
        return COLOR_CYAN;
    if (strcmp(event_type, "EXIT") == 0)
        return COLOR_RED;
    return COLOR_WHITE;
}

static void print_header(void)
{
    if (output_mode == MODE_JSON)
        return;

    printf("\n");
    if (use_color)
        printf("%s%s", COLOR_BOLD, COLOR_WHITE);
    printf("  ProcWatch - Real-time Process Monitor\n");
    if (use_color)
        printf("%s", COLOR_RESET);
    printf("\n");

    if (use_color) {
        printf("%s%-12s %-8s %-8s %-10s %-20s %s%s\n",
               COLOR_DIM,
               "TIME", "EVENT", "PID", "PPID", "USER", "COMMAND",
               COLOR_RESET);
        printf("%s------------ -------- -------- ---------- "
               "-------------------- ---------------------%s\n",
               COLOR_DIM, COLOR_RESET);
    } else {
        printf("%-12s %-8s %-8s %-10s %-20s %s\n",
               "TIME", "EVENT", "PID", "PPID", "USER", "COMMAND");
        printf("------------ -------- -------- ---------- "
               "-------------------- ---------------------\n");
    }
}

// event format: TYPE PID PPID UID TIMESTAMP_NS COMM [exit_code=N]
static void process_line(const char *line)
{
    char event_type[8];
    int pid, ppid;
    unsigned int uid;
    unsigned long long timestamp;
    char comm[64];
    char extra[64] = "";
    char time_str[32];

    // ugly but works for now - would break if the kernel format changes
    int fields = sscanf(line, "%7s %d %d %u %llu %63s %63[^\n]",
                        event_type, &pid, &ppid, &uid,
                        &timestamp, comm, extra);

    if (fields < 6)
        return;

    format_timestamp(timestamp, time_str, sizeof(time_str));
    const char *username = get_username(uid);

    if (output_mode == MODE_JSON) {
        printf("{\"time\":\"%s\",\"event\":\"%s\",\"pid\":%d,"
               "\"ppid\":%d,\"uid\":%u,\"user\":\"%s\","
               "\"command\":\"%s\"",
               time_str, event_type, pid, ppid, uid, username, comm);
        if (extra[0])
            printf(",\"extra\":\"%s\"", extra);
        printf("}\n");
    } else {
        const char *color = get_event_color(event_type);
        const char *reset = use_color ? COLOR_RESET : "";

        printf("%s%-12s%s %s%-8s%s %-8d %-10d %-20s %s%s%s",
               use_color ? COLOR_DIM : "", time_str, reset,
               color, event_type, reset,
               pid, ppid, username,
               use_color ? COLOR_BOLD : "", comm, reset);

        if (extra[0])
            printf(" %s%s%s", use_color ? COLOR_DIM : "", extra, reset);

        printf("\n");
    }

    fflush(stdout);
}

static int show_statistics(int fd)
{
    struct procwatch_stats stats;

    if (ioctl(fd, PROCWATCH_IOC_STATS, &stats) < 0) {
        perror("ioctl PROCWATCH_IOC_STATS failed");
        return -1;
    }

    printf("\nProcWatch Statistics\n\n");
    printf("  Fork events:      %llu\n", stats.total_forks);
    printf("  Exec events:      %llu\n", stats.total_execs);
    printf("  Exit events:      %llu\n", stats.total_exits);
    printf("  Total events:     %llu\n",
           stats.total_forks + stats.total_execs + stats.total_exits);
    printf("  Events dropped:   %llu\n", stats.events_dropped);
    printf("  Buffer usage:     %u / %u (%.1f%%)\n\n",
           stats.buffer_usage, stats.buffer_capacity,
           stats.buffer_capacity > 0
               ? (100.0 * stats.buffer_usage / stats.buffer_capacity)
               : 0.0);

    return 0;
}

static int monitor_events(int fd)
{
    char buf[READ_BUF_SIZE];
    char line_buf[MAX_LINE_LEN];
    int line_pos = 0;

    print_header();

    while (running) {
        ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);

        if (bytes_read < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN) {
                usleep(10000); // 10ms poll - good enough
                continue;
            }
            perror("read /dev/procwatch");
            return -1;
        }

        if (bytes_read == 0) {
            usleep(50000);
            continue;
        }

        buf[bytes_read] = '\0';

        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buf[i] == '\n' || line_pos >= MAX_LINE_LEN - 1) {
                line_buf[line_pos] = '\0';
                if (line_pos > 0)
                    process_line(line_buf);
                line_pos = 0;
            } else {
                line_buf[line_pos++] = buf[i];
            }
        }
    }

    return 0;
}

static void print_usage(const char *prog)
{
    printf("\nUsage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -f, --follow     Continuously monitor events (default)\n");
    printf("  -j, --json       Output events as JSON (for web dashboard)\n");
    printf("  -s, --stats      Show statistics and exit\n");
    printf("  -c, --clear      Clear the event buffer\n");
    printf("  -n, --no-color   Disable colored output\n");
    printf("  -h, --help       Show this help message\n");
    printf("\nExamples:\n");
    printf("  sudo %s                     # monitor in terminal\n", prog);
    printf("  sudo %s --json | tee log    # JSON to file\n", prog);
    printf("  sudo %s --stats             # view statistics\n", prog);
    printf("\nDevice: %s\n\n", DEVICE_PATH);
}

int main(int argc, char *argv[])
{
    int fd;
    int show_stats = 0;
    int clear_buf = 0;
    int ret = 0;

    static struct option long_options[] = {
        {"follow",   no_argument, 0, 'f'},
        {"json",     no_argument, 0, 'j'},
        {"stats",    no_argument, 0, 's'},
        {"clear",    no_argument, 0, 'c'},
        {"no-color", no_argument, 0, 'n'},
        {"help",     no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "fjscnh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'f':
            break;
        case 'j':
            output_mode = MODE_JSON;
            use_color = 0;
            break;
        case 's':
            show_stats = 1;
            break;
        case 'c':
            clear_buf = 1;
            break;
        case 'n':
            use_color = 0;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Error: must run as root (sudo %s)\n", argv[0]);
        return 1;
    }

    if (access(DEVICE_PATH, F_OK) != 0) {
        fprintf(stderr, "Error: %s not found. Is the kernel module loaded?\n",
                DEVICE_PATH);
        fprintf(stderr, "  Load with: sudo insmod procwatch.ko\n");
        return 1;
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open " DEVICE_PATH);
        return 1;
    }

    if (show_stats) {
        ret = show_statistics(fd);
    } else if (clear_buf) {
        if (ioctl(fd, PROCWATCH_IOC_CLEAR) < 0) {
            perror("ioctl PROCWATCH_IOC_CLEAR failed");
            ret = -1;
        } else {
            printf("Event buffer cleared.\n");
        }
    } else {
        ret = monitor_events(fd);
    }

    close(fd);
    return ret ? 1 : 0;
}
