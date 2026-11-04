#define _POSIX_C_SOURCE 200809L

#include "host_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "vcos_module.h"

#define HOST_LOG_MAX_MESSAGE 4096u
#define HOST_LOG_MAX_BYTES (1024u * 1024u)
#define HOST_LOG_MIRROR_PER_SEC 10u
#define HOST_LOG_MIRROR_TOTAL 200u

struct host_log {
    FILE *file;
    char *path;
    char module[64];
    int mirror_fd;
    int owns_file;
    time_t mirror_second;
    unsigned mirror_second_count;
    unsigned mirror_total;
    unsigned lines;
};

static volatile sig_atomic_t crash_log_fd = -1;

static const char *level_name(int level)
{
    switch (level) {
    case VCOS_LOG_ERROR:
        return "ERROR";
    case VCOS_LOG_WARN:
        return "WARN";
    case VCOS_LOG_INFO:
        return "INFO";
    default:
        return "DEBUG";
    }
}

static char kmsg_level(int level)
{
    switch (level) {
    case VCOS_LOG_ERROR:
        return '3';
    case VCOS_LOG_WARN:
        return '4';
    case VCOS_LOG_INFO:
        return '6';
    default:
        return '7';
    }
}

static void sanitize(char *text)
{
    for (char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n' || c == '\r' || (c < 0x20u && c != '\t')) {
            *p = ' ';
        }
    }
}

static void rotate_if_needed(host_log *log)
{
    if (!log || !log->file || !log->owns_file || !log->path) {
        return;
    }
    log->lines++;
    if (log->lines % 64u != 0) {
        return;
    }
    long size = ftell(log->file);
    if (size < (long)HOST_LOG_MAX_BYTES) {
        return;
    }
    fclose(log->file);
    log->file = NULL;

    char old_path[1024];
    snprintf(old_path, sizeof(old_path), "%s.1", log->path);
    unlink(old_path);
    rename(log->path, old_path);
    log->file = fopen(log->path, "a");
    if (!log->file) {
        log->file = stderr;
        log->owns_file = 0;
    }
    if (log->file) {
        setvbuf(log->file, NULL, _IOLBF, 0);
        crash_log_fd = fileno(log->file);
    }
}

static void mirror_kmsg(host_log *log, int level, const char *message)
{
    if (!log || log->mirror_fd < 0 || level > VCOS_LOG_INFO) {
        return;
    }
    time_t now = time(NULL);
    if (now != log->mirror_second) {
        log->mirror_second = now;
        log->mirror_second_count = 0;
    }
    if (log->mirror_second_count >= HOST_LOG_MIRROR_PER_SEC ||
        log->mirror_total >= HOST_LOG_MIRROR_TOTAL) {
        return;
    }
    log->mirror_second_count++;
    log->mirror_total++;

    char line[4352];
    int n = snprintf(line, sizeof(line), "<%c>vcos[%s]: %s\n",
                     kmsg_level(level), log->module, message);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        ssize_t ignored = write(log->mirror_fd, line, len);
        (void)ignored;
    }
}

host_log *host_log_open(const char *path, const char *module_id,
                        int mirror_kmsg)
{
    host_log *log = calloc(1, sizeof(*log));
    if (!log) {
        return NULL;
    }
    log->mirror_fd = -1;
    snprintf(log->module, sizeof(log->module), "%s",
             module_id && module_id[0] ? module_id : "module");

    if (path && path[0]) {
        log->path = strdup(path);
        log->file = fopen(path, "a");
        log->owns_file = 1;
    }
    if (!log->file) {
        log->file = stderr;
        log->owns_file = 0;
        if (!log->path) {
            log->path = strdup("stderr");
        }
    }
    if (log->file) {
        setvbuf(log->file, NULL, _IOLBF, 0);
        crash_log_fd = fileno(log->file);
    }
    if (mirror_kmsg) {
        log->mirror_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    }
    return log;
}

void host_log_args(host_log *log, int level, const char *fmt, ...)
{
    if (!log || !fmt) {
        return;
    }
    char message[HOST_LOG_MAX_MESSAGE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    sanitize(message);

    struct timespec ts;
    struct tm tm;
    char stamp[32] = "1970-01-01T00:00:00Z";
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0 &&
        gmtime_r(&ts.tv_sec, &tm)) {
        strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }

    if (log->file) {
        fprintf(log->file, "%s %-5s [%s] %s\n", stamp, level_name(level),
                log->module, message);
        fflush(log->file);
    }
    mirror_kmsg(log, level, message);
    rotate_if_needed(log);
}

void host_log_close(host_log *log)
{
    if (!log) {
        return;
    }
    if (log->file) {
        fflush(log->file);
        if (log->owns_file) {
            fclose(log->file);
        }
    }
    if (log->mirror_fd >= 0) {
        close(log->mirror_fd);
    }
    crash_log_fd = -1;
    free(log->path);
    free(log);
}

const char *host_log_path(const host_log *log)
{
    return log && log->path ? log->path : "";
}

static void crash_handler(int sig)
{
    char buf[64];
    const char *prefix = "vcos: fatal signal ";
    size_t n = 0;
    while (*prefix && n + 1 < sizeof(buf)) {
        buf[n++] = *prefix++;
    }
    if (sig >= 100 && n + 1 < sizeof(buf)) {
        buf[n++] = (char)('0' + (sig / 100) % 10);
    }
    if (sig >= 10 && n + 1 < sizeof(buf)) {
        buf[n++] = (char)('0' + (sig / 10) % 10);
    }
    if (n + 2 < sizeof(buf)) {
        buf[n++] = (char)('0' + sig % 10);
        buf[n++] = '\n';
    }
    if (crash_log_fd >= 0) {
        ssize_t ignored = write((int)crash_log_fd, buf, n);
        (void)ignored;
    }
    _exit(128 + sig);
}

void host_log_install_crash_handler(host_log *log)
{
    if (log && log->file) {
        crash_log_fd = fileno(log->file);
    }
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        sigaction(signals[i], &sa, NULL);
    }
}
