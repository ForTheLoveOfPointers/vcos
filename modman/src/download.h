#ifndef VCOS_DOWNLOAD_H
#define VCOS_DOWNLOAD_H

#include <stddef.h>

int vcos_url_is_remote(const char *value);
int vcos_url_validate_package(const char *url, char **error);
int vcos_download_file(const char *url, const char *output,
                       unsigned long long max_size, char **error);

#endif
