#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "download.h"
#include "signature.h"
#include "vmod.h"

#define DEFAULT_ROOT "/var/lib/vcos"
#define DEFAULT_HOST "/usr/bin/vcos-module-host"

static const char *default_root(void)
{
    const char *root = getenv("VCOS_DATA_ROOT");
    return root && root[0] ? root : DEFAULT_ROOT;
}

static void usage(FILE *out)
{
    fprintf(out,
            "usage:\n"
            "  vcos-modman install PACKAGE|https://URL [--allow-unsigned] [--root DIR]\n"
            "  vcos-modman list [--root DIR]\n"
            "  vcos-modman info PACKAGE\n"
            "  vcos-modman remove ID [--root DIR]\n"
            "  vcos-modman run ID CONTENT [--root DIR] [--host PATH] "
            "[-- HOST OPTIONS]\n"
            "\n"
            "install looks for PACKAGE.sig next to a local package or "
            "downloads PACKAGE.sig when given an https:// URL. Signed "
            "packages are verified before installation; --allow-unsigned "
            "permits unsigned development packages.\n");
}

static int invalid(const char *message)
{
    fprintf(stderr, "vcos-modman: %s\n", message);
    return 1;
}

static int path_join(char *out, size_t size, const char *a, const char *b)
{
    if (snprintf(out, size, "%s/%s", a, b) >= (int)size) {
        return -1;
    }
    return 0;
}

static int installed_paths(const char *root, const char *id, char *dir,
                           size_t dir_size, char *manifest,
                           size_t manifest_size)
{
    if (!vmod_safe_id(id)) {
        return -1;
    }
    char modules[VMOD_PATH_MAX];
    if (path_join(modules, sizeof(modules), root, "modules") != 0 ||
        path_join(dir, dir_size, modules, id) != 0 ||
        path_join(manifest, manifest_size, dir, "current/manifest.json") != 0) {
        return -1;
    }
    return 0;
}

static int load_installed(const char *root, const char *id,
                          vmod_manifest *manifest, char **error)
{
    char dir[VMOD_PATH_MAX];
    char path[VMOD_PATH_MAX];
    if (installed_paths(root, id, dir, sizeof(dir), path, sizeof(path)) != 0) {
        vmod_error(error, "invalid module id");
        return -1;
    }
    if (vmod_manifest_load_file(path, manifest, error) != 0) {
        return -1;
    }
    if (strcmp(manifest->id, id) != 0) {
        vmod_error(error, "installed manifest id does not match directory");
        vmod_manifest_free(manifest);
        return -1;
    }
    return 0;
}

static int fetch_remote_package(const char *url, const char *root,
                                int allow_unsigned, char *package_out,
                                size_t package_out_size, char **temp_dir_out,
                                char **error)
{
    char staging[VMOD_PATH_MAX];
    char path[VMOD_PATH_MAX];
    if (path_join(staging, sizeof(staging), root, "downloads") != 0 ||
        path_join(path, sizeof(path), staging, "staging") != 0) {
        vmod_error(error, "download path is too long");
        return -1;
    }
    if (vmod_mkdir_p(path, 0755) != 0) {
        vmod_error(error, "cannot create %s: %s", path, strerror(errno));
        return -1;
    }

    char template[VMOD_PATH_MAX];
    if (snprintf(template, sizeof(template), "%s/download-XXXXXX", path) >=
        (int)sizeof(template)) {
        vmod_error(error, "download path is too long");
        return -1;
    }
    char *directory = mkdtemp(template);
    if (!directory) {
        vmod_error(error, "cannot create download directory: %s",
                   strerror(errno));
        return -1;
    }

    char package[VMOD_PATH_MAX];
    char signature[VMOD_PATH_MAX];
    char signature_url[VMOD_PATH_MAX];
    if (path_join(package, sizeof(package), directory, "package.vmod") != 0 ||
        vcos_signature_path(package, signature, sizeof(signature)) != 0 ||
        snprintf(signature_url, sizeof(signature_url), "%s.sig", url) >=
            (int)sizeof(signature_url)) {
        vmod_error(error, "download path is too long");
        goto fail;
    }

    if (vcos_download_file(url, package, VMOD_MAX_TOTAL, error) != 0) {
        goto fail;
    }

    char *signature_error = NULL;
    if (vcos_download_file(signature_url, signature,
                           VCOS_ED25519_SIGNATURE_SIZE,
                           &signature_error) != 0) {
        if (!allow_unsigned) {
            vmod_error(error, "downloaded package has no usable signature: %s",
                       signature_error ? signature_error : "download failed");
            free(signature_error);
            goto fail;
        }
        free(signature_error);
        unlink(signature);
        fprintf(stderr,
                "vcos-modman: warning: downloaded package is unsigned\n");
    }

    if (snprintf(package_out, package_out_size, "%s", package) >=
        (int)package_out_size) {
        vmod_error(error, "download path is too long");
        goto fail;
    }
    char *directory_copy = strdup(directory);
    if (!directory_copy) {
        vmod_error(error, "out of memory");
        goto fail;
    }
    *temp_dir_out = directory_copy;
    return 0;

fail:
    vmod_remove_tree(directory);
    return -1;
}

static int cmd_install(int argc, char **argv)
{
    const char *root = default_root();
    const char *package = NULL;
    char local_package[VMOD_PATH_MAX];
    char *temp_dir = NULL;
    char *error = NULL;
    int allow_unsigned = 0;
    int status = 1;
    vmod_manifest manifest;
    vmod_manifest_init(&manifest);

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0) {
            if (++i >= argc) return invalid("--root needs a directory");
            root = argv[i];
        } else if (strcmp(argv[i], "--allow-unsigned") == 0) {
            allow_unsigned = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "vcos-modman: unknown option %s\n", argv[i]);
            return 1;
        } else if (!package) {
            package = argv[i];
        } else {
            return invalid("install takes exactly one package");
        }
    }
    if (!package) {
        usage(stderr);
        return 1;
    }

    if (vcos_url_is_remote(package)) {
        if (vcos_url_validate_package(package, &error) != 0) {
            fprintf(stderr, "vcos-modman: %s\n",
                    error ? error : "invalid package URL");
            goto out;
        }
        if (fetch_remote_package(package, root, allow_unsigned, local_package,
                                 sizeof(local_package), &temp_dir,
                                 &error) != 0) {
            fprintf(stderr, "vcos-modman: %s\n",
                    error ? error : "download failed");
            goto out;
        }
        package = local_package;
    } else if (strstr(package, "://")) {
        return invalid("only https:// package URLs are supported");
    } else {
        struct stat st;
        if (stat(package, &st) != 0 || !S_ISREG(st.st_mode)) {
            fprintf(stderr, "vcos-modman: cannot read %s: %s\n",
                    package, strerror(errno));
            return 1;
        }
    }

    int signature_status = vcos_signature_verify_package(package, &error);
    if (signature_status < 0) {
        fprintf(stderr, "vcos-modman: %s\n",
                error ? error : "signature verification failed");
        goto out;
    }
    if (signature_status == 1 && !allow_unsigned) {
        free(error);
        error = NULL;
        invalid("package is unsigned; pass --allow-unsigned");
        goto out;
    }
    if (vmod_install(package, root, &manifest, &error) != 0) {
        fprintf(stderr, "vcos-modman: install failed: %s\n",
                error ? error : "unknown error");
        goto out;
    }
    if (signature_status == 0) {
        fprintf(stderr, "vcos-modman: signature verified\n");
    } else {
        fprintf(stderr,
                "vcos-modman: warning: installing unsigned package\n");
    }
    printf("installed %s %s\n", manifest.id, manifest.version);
    status = 0;

out:
    free(error);
    if (temp_dir) {
        vmod_remove_tree(temp_dir);
        free(temp_dir);
    }
    vmod_manifest_free(&manifest);
    return status;
}

static int compare_strings(const void *a, const void *b)
{
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static void print_array(const char *label, char **items, size_t count)
{
    printf("%s:", label);
    for (size_t i = 0; i < count; i++) {
        printf("%s%s", i ? "," : " ", items[i]);
    }
    putchar('\n');
}

static int cmd_list(int argc, char **argv)
{
    const char *root = default_root();
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0) {
            if (++i >= argc) return invalid("--root needs a directory");
            root = argv[i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "vcos-modman: unknown option %s\n", argv[i]);
            return 1;
        }
    }
    char modules[VMOD_PATH_MAX];
    if (path_join(modules, sizeof(modules), root, "modules") != 0) {
        return invalid("data root path is too long");
    }
    DIR *dir = opendir(modules);
    if (!dir) {
        if (errno == ENOENT) {
            return 0;
        }
        fprintf(stderr, "vcos-modman: cannot open %s: %s\n",
                modules, strerror(errno));
        return 1;
    }
    char **ids = NULL;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !vmod_safe_id(entry->d_name)) {
            continue;
        }
        char **next = realloc(ids, (count + 1) * sizeof(*next));
        if (!next) {
            closedir(dir);
            free(ids);
            return invalid("out of memory");
        }
        ids = next;
        ids[count] = strdup(entry->d_name);
        if (!ids[count]) {
            closedir(dir);
            free(ids);
            return invalid("out of memory");
        }
        count++;
    }
    closedir(dir);
    if (!count) {
        free(ids);
        return 0;
    }
    qsort(ids, count, sizeof(*ids), compare_strings);
    int status = 0;
    for (size_t i = 0; i < count; i++) {
        vmod_manifest manifest;
        vmod_manifest_init(&manifest);
        char *error = NULL;
        if (load_installed(root, ids[i], &manifest, &error) != 0) {
            fprintf(stderr, "vcos-modman: skipping %s: %s\n", ids[i],
                    error ? error : "invalid install");
            free(error);
            status = 1;
            continue;
        }
        printf("%s %s %s", manifest.id, manifest.version, manifest.name);
        for (size_t j = 0; j < manifest.system_count; j++) {
            printf(" %s", manifest.systems[j]);
        }
        putchar('\n');
        vmod_manifest_free(&manifest);
    }
    for (size_t i = 0; i < count; i++) {
        free(ids[i]);
    }
    free(ids);
    return status;
}

static int cmd_info(int argc, char **argv)
{
    const char *package = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "vcos-modman: unknown option %s\n", argv[i]);
            return 1;
        } else if (!package) {
            package = argv[i];
        } else {
            return invalid("info takes exactly one package");
        }
    }
    if (!package) {
        usage(stderr);
        return 1;
    }
    vmod_manifest manifest;
    vmod_manifest_init(&manifest);
    char *error = NULL;
    if (vmod_manifest_read(package, &manifest, &error) != 0) {
        fprintf(stderr, "vcos-modman: %s\n", error ? error : "invalid package");
        free(error);
        return 1;
    }
    printf("id: %s\n", manifest.id);
    printf("name: %s\n", manifest.name);
    printf("version: %s\n", manifest.version);
    printf("architecture: %s\n", manifest.architecture);
    printf("abi_version: %d\n", manifest.abi_version);
    printf("entrypoint: %s\n", manifest.entrypoint);
    if (manifest.license) {
        printf("license: %s\n", manifest.license);
    }
    if (manifest.description) {
        printf("description: %s\n", manifest.description);
    }
    print_array("systems", manifest.systems, manifest.system_count);
    print_array("extensions", manifest.extensions,
                manifest.extension_count);
    printf("files:\n");
    for (size_t i = 0; i < manifest.file_count; i++) {
        printf("  %s %s\n", manifest.files[i].sha256,
               manifest.files[i].path);
    }
    char *signature_error = NULL;
    int signature_status =
        vcos_signature_verify_package(package, &signature_error);
    if (signature_status == 0) {
        printf("signature: valid\n");
    } else if (signature_status == 1) {
        printf("signature: none\n");
    } else {
        fprintf(stderr, "vcos-modman: %s\n",
                signature_error ? signature_error : "invalid signature");
        free(signature_error);
        vmod_manifest_free(&manifest);
        return 1;
    }
    free(signature_error);
    vmod_manifest_free(&manifest);
    return 0;
}

static int cmd_remove(int argc, char **argv)
{
    const char *root = default_root();
    const char *id = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0) {
            if (++i >= argc) return invalid("--root needs a directory");
            root = argv[i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "vcos-modman: unknown option %s\n", argv[i]);
            return 1;
        } else if (!id) {
            id = argv[i];
        } else {
            return invalid("remove takes exactly one module id");
        }
    }
    if (!id) {
        usage(stderr);
        return 1;
    }
    if (!vmod_safe_id(id)) {
        return invalid("invalid module id");
    }
    char modules[VMOD_PATH_MAX];
    char path[VMOD_PATH_MAX];
    if (path_join(modules, sizeof(modules), root, "modules") != 0 ||
        path_join(path, sizeof(path), modules, id) != 0) {
        return invalid("module path is too long");
    }
    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "vcos-modman: %s is not installed\n", id);
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return invalid("module path is not a directory");
    }
    if (vmod_remove_tree(path) != 0) {
        fprintf(stderr, "vcos-modman: cannot remove %s: %s\n",
                id, strerror(errno));
        return 1;
    }
    printf("removed %s\n", id);
    return 0;
}

static int cmd_run(int argc, char **argv)
{
    const char *root = default_root();
    const char *host = getenv("VCOS_MODULE_HOST");
    if (!host || !host[0]) {
        host = DEFAULT_HOST;
    }
    const char *id = NULL;
    const char *content = NULL;
    int extra_start = argc;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0) {
            if (++i >= argc) return invalid("--root needs a directory");
            root = argv[i];
        } else if (strcmp(argv[i], "--host") == 0) {
            if (++i >= argc) return invalid("--host needs a path");
            host = argv[i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--") == 0) {
            extra_start = i + 1;
            break;
        } else if (argv[i][0] == '-' && id && !content) {
            return invalid("missing content path before host options");
        } else if (argv[i][0] == '-' && !id) {
            fprintf(stderr, "vcos-modman: unknown option %s\n", argv[i]);
            return 1;
        } else if (!id) {
            id = argv[i];
        } else if (!content) {
            content = argv[i];
        } else {
            extra_start = i;
            break;
        }
    }
    if (!id || !content) {
        usage(stderr);
        return 1;
    }
    vmod_manifest manifest;
    vmod_manifest_init(&manifest);
    char *error = NULL;
    if (load_installed(root, id, &manifest, &error) != 0) {
        fprintf(stderr, "vcos-modman: %s\n", error ? error : "not installed");
        free(error);
        return 1;
    }
    char module[VMOD_PATH_MAX * 2];
    char log_path[VMOD_PATH_MAX];
    char logs[VMOD_PATH_MAX];
    if (path_join(logs, sizeof(logs), root, "logs") != 0 ||
        vmod_mkdir_p(logs, 0755) != 0 ||
        path_join(log_path, sizeof(log_path), logs, "module-host.log") != 0 ||
        snprintf(module, sizeof(module), "%s/modules/%s/current/%s",
                 root, id, manifest.entrypoint) >= (int)sizeof(module)) {
        fprintf(stderr, "vcos-modman: cannot prepare module path\n");
        vmod_manifest_free(&manifest);
        return 1;
    }
    struct stat st;
    if (stat(module, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "vcos-modman: entrypoint is missing\n");
        vmod_manifest_free(&manifest);
        return 1;
    }
    int extra_count = argc - extra_start;
    if (extra_count < 0) {
        extra_count = 0;
    }
    char **args = calloc((size_t)extra_count + 8, sizeof(*args));
    if (!args) {
        vmod_manifest_free(&manifest);
        return invalid("out of memory");
    }
    int n = 0;
    args[n++] = (char *)host;
    args[n++] = "--module";
    args[n++] = module;
    args[n++] = "--content";
    args[n++] = (char *)content;
    args[n++] = "--log";
    args[n++] = log_path;
    for (int i = 0; i < extra_count; i++) {
        args[n++] = argv[extra_start + i];
    }
    args[n] = NULL;
    execv(host, args);
    fprintf(stderr, "vcos-modman: cannot run %s: %s\n",
            host, strerror(errno));
    free(args);
    vmod_manifest_free(&manifest);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "help") == 0) {
        usage(argv[1] && strcmp(argv[1], "--help") == 0 ? stdout : stderr);
        return argc < 2 ? 1 : 0;
    }
    if (strcmp(argv[1], "install") == 0) {
        return cmd_install(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "list") == 0) {
        return cmd_list(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "info") == 0) {
        return cmd_info(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "remove") == 0) {
        return cmd_remove(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc - 2, argv + 2);
    }
    fprintf(stderr, "vcos-modman: unknown command %s\n", argv[1]);
    usage(stderr);
    return 1;
}
