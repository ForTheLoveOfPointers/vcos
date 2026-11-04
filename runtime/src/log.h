#ifndef VCOS_LOG_H
#define VCOS_LOG_H

extern int vcos_verbose;

void vcos_log(const char *fmt, ...);
void vcos_error(const char *fmt, ...);

#endif
