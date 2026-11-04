#define _POSIX_C_SOURCE 200809L

#include "download.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vmod.h"

int vcos_url_is_remote(const char *value)
{
    return value &&
           (strncmp(value, "https://", 8) == 0 ||
            strncmp(value, "http://", 7) == 0);
}

int vcos_url_validate_package(const char *url, char **error)
{
    if (!url || strncmp(url, "https://", 8) != 0) {
        vmod_error(error, "online install requires an https:// URL");
        return -1;
    }

    size_t length = strlen(url);
    if (length < 8 || length + 5 >= VMOD_PATH_MAX) {
        vmod_error(error, "package URL is too short or too long");
        return -1;
    }

    const char *host = url + 8;
    const char *slash = strchr(host, '/');
    if (!slash || slash == host) {
        vmod_error(error, "package URL is missing a host or path");
        return -1;
    }
    if (strchr(host, '@')) {
        vmod_error(error, "package URL must not contain credentials");
        return -1;
    }
    if (strchr(url, '?') || strchr(url, '#') || strchr(url, '\\')) {
        vmod_error(error,
                   "package URL must not contain a query, fragment or backslash");
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p <= 0x20 || *p == 0x7f) {
            vmod_error(error, "package URL contains a control character");
            return -1;
        }
    }
    if (strcmp(url + length - 5, ".vmod") != 0) {
        vmod_error(error, "package URL must end in .vmod");
        return -1;
    }

    return 0;
}

int vcos_download_file(const char *url, const char *output,
                       unsigned long long max_size, char **error)
{
    if (!url || !output || max_size == 0) {
        vmod_error(error, "invalid download request");
        return -1;
    }

    const char *curl = getenv("VCOS_CURL");
    if (!curl || !curl[0]) {
        curl = "curl";
    }

    char limit[32];
    snprintf(limit, sizeof(limit), "%llu", max_size);

    char *argv[32];
    int argc = 0;
    argv[argc++] = (char *)curl;
    argv[argc++] = "-q";
    argv[argc++] = "--fail";
    argv[argc++] = "--silent";
    argv[argc++] = "--show-error";
    argv[argc++] = "--location";
    argv[argc++] = "--max-redirs";
    argv[argc++] = "3";
    argv[argc++] = "--proto";
    argv[argc++] = "=https";
    argv[argc++] = "--proto-redir";
    argv[argc++] = "=https";
    argv[argc++] = "--connect-timeout";
    argv[argc++] = "15";
    argv[argc++] = "--max-time";
    argv[argc++] = "300";
    argv[argc++] = "--max-filesize";
    argv[argc++] = limit;
    argv[argc++] = "--globoff";
    argv[argc++] = "--user-agent";
    argv[argc++] = "vcos-modman/1";
    const char *ca_bundle = getenv("VCOS_CA_BUNDLE");
    if (ca_bundle && ca_bundle[0]) {
        argv[argc++] = "--cacert";
        argv[argc++] = (char *)ca_bundle;
    }
    argv[argc++] = "--output";
    argv[argc++] = (char *)output;
    argv[argc++] = (char *)url;
    argv[argc] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        vmod_error(error, "cannot start curl: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execvp(curl, argv);
        fprintf(stderr, "vcos-modman: cannot run %s: %s\n",
                curl, strerror(errno));
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            vmod_error(error, "cannot wait for curl: %s", strerror(errno));
            unlink(output);
            return -1;
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        vmod_error(error, "download failed: %s", url);
        unlink(output);
        return -1;
    }

    struct stat st;
    if (stat(output, &st) != 0) {
        vmod_error(error, "downloaded file is missing: %s", strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0 ||
        (unsigned long long)st.st_size > max_size) {
        vmod_error(error, "downloaded file is invalid or too large: %s", url);
        unlink(output);
        return -1;
    }

    return 0;
}
