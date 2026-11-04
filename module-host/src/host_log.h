#ifndef VCOS_HOST_LOG_H
#define VCOS_HOST_LOG_H

#include <signal.h>

typedef struct host_log host_log;

host_log *host_log_open(const char *path, const char *module_id,
                        int mirror_kmsg);
void host_log_close(host_log *log);
void host_log_args(host_log *log, int level, const char *fmt, ...);
#define host_log(log, level, ...) host_log_args((log), (level), __VA_ARGS__)
void host_log_install_crash_handler(host_log *log);
const char *host_log_path(const host_log *log);

#endif
