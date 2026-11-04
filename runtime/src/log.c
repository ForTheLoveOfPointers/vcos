#include "log.h"

#include <stdarg.h>
#include <stdio.h>

int vcos_verbose;

void vcos_log(const char *fmt, ...)
{
    if (!vcos_verbose) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "vcos: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void vcos_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "vcos: error: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
