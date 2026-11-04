#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "vmod.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "json.h"
#include "sha256.h"

#define TAR_BLOCK 512u

typedef struct {
    FILE *file;
} tar_reader;

typedef struct {
    char path[VMOD_PATH_MAX];
    uint64_t size;
    unsigned mode;
    char type;
} tar_entry;

void vmod_error(char **error, const char *format, ...)
{
    if (!error) {
        return;
    }
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        *error = strdup("out of memory");
    } else {
        char *message = malloc((size_t)length + 1);
        if (message) {
            vsnprintf(message, (size_t)length + 1, format, args);
            *error = message;
        }
    }
    va_end(args);
}

void vmod_manifest_init(vmod_manifest *manifest)
{
    memset(manifest, 0, sizeof(*manifest));
}

void vmod_manifest_free(vmod_manifest *manifest)
{
    if (!manifest) {
        return;
    }
    free(manifest->id);
    free(manifest->name);
    free(manifest->version);
    free(manifest->architecture);
    free(manifest->entrypoint);
    free(manifest->license);
    free(manifest->description);
    for (size_t i = 0; i < manifest->system_count; i++) {
        free(manifest->systems[i]);
    }
    free(manifest->systems);
    for (size_t i = 0; i < manifest->extension_count; i++) {
        free(manifest->extensions[i]);
    }
    free(manifest->extensions);
    for (size_t i = 0; i < manifest->file_count; i++) {
        free(manifest->files[i].path);
        free(manifest->files[i].sha256);
    }
    free(manifest->files);
    vmod_manifest_init(manifest);
}

int vmod_safe_relpath(const char *path)
{
    if (!path || !path[0] || path[0] == '/') {
        return 0;
    }
    size_t length = strlen(path);
    if (length >= VMOD_PATH_MAX || path[length - 1] == '/') {
        return 0;
    }
    const char *part = path;
    while (*part) {
        const char *slash = strchr(part, '/');
        size_t len = slash ? (size_t)(slash - part) : strlen(part);
        if (len == 0 || (len == 1 && part[0] == '.') ||
            (len == 2 && part[0] == '.' && part[1] == '.')) {
            return 0;
        }
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)part[i];
            if (c == '\\' || c < 0x20) {
                return 0;
            }
        }
        if (!slash) {
            break;
        }
        part = slash + 1;
    }
    return 1;
}

int vmod_safe_id(const char *id)
{
    if (!id || !id[0] || strlen(id) >= 128) {
        return 0;
    }
    for (const char *p = id; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
            return 0;
        }
    }
    return strcmp(id, ".") != 0 && strcmp(id, "..") != 0 &&
           id[0] != '.';
}

int vmod_safe_version(const char *version)
{
    if (!version || !version[0] || strlen(version) >= 128) {
        return 0;
    }
    for (const char *p = version; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-' || c == '+')) {
            return 0;
        }
    }
    return strcmp(version, ".") != 0 && strcmp(version, "..") != 0;
}

static const char *target_arch(void)
{
#if defined(__x86_64__) || defined(__amd64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#elif defined(__i386__)
    return "i386";
#else
    return "unknown";
#endif
}

static int parse_octal(const unsigned char *field, size_t size,
                       uint64_t *out)
{
    size_t i = 0;
    while (i < size && (field[i] == ' ' || field[i] == '\0')) {
        i++;
    }
    if (i == size) {
        *out = 0;
        return 0;
    }
    uint64_t value = 0;
    int digits = 0;
    while (i < size) {
        unsigned char c = field[i++];
        if (c == ' ' || c == '\0') {
            break;
        }
        if (c < '0' || c > '7') {
            return -1;
        }
        if (value > (UINT64_MAX >> 3)) {
            return -1;
        }
        value = (value << 3) | (uint64_t)(c - '0');
        digits++;
    }
    if (!digits) {
        return -1;
    }
    *out = value;
    return 0;
}

static int field_string(const unsigned char *field, size_t size,
                        char *out, size_t out_size)
{
    size_t len = 0;
    while (len < size && field[len] != '\0') {
        len++;
    }
    if (len >= out_size) {
        return -1;
    }
    memcpy(out, field, len);
    out[len] = '\0';
    return 0;
}

static int tar_open(tar_reader *reader, const char *path, char **error)
{
    reader->file = fopen(path, "rb");
    if (!reader->file) {
        vmod_error(error, "cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int tar_next(tar_reader *reader, tar_entry *entry, char **error)
{
    unsigned char block[TAR_BLOCK];
    size_t got = fread(block, 1, sizeof(block), reader->file);
    if (got == 0) {
        vmod_error(error, "archive is missing its end marker");
        return -1;
    }
    if (got != sizeof(block)) {
        vmod_error(error, "truncated tar header");
        return -1;
    }
    int all_zero = 1;
    for (size_t i = 0; i < sizeof(block); i++) {
        if (block[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        return 0;
    }
    if (memcmp(block + 257, "ustar", 5) != 0) {
        vmod_error(error, "package is not a ustar archive");
        return -1;
    }
    uint64_t stored_checksum;
    if (parse_octal(block + 148, 8, &stored_checksum) != 0) {
        vmod_error(error, "invalid tar header checksum");
        return -1;
    }
    uint64_t checksum = 0;
    for (size_t i = 0; i < sizeof(block); i++) {
        checksum += (i >= 148 && i < 156) ? ' ' : block[i];
    }
    if ((checksum & 0xffffffffu) != stored_checksum) {
        vmod_error(error, "tar header checksum mismatch");
        return -1;
    }
    char name[100];
    char prefix[155];
    if (field_string(block, 100, name, sizeof(name)) != 0 ||
        field_string(block + 345, 155, prefix, sizeof(prefix)) != 0) {
        vmod_error(error, "archive path is too long");
        return -1;
    }
    if (prefix[0]) {
        if (snprintf(entry->path, sizeof(entry->path), "%s/%s",
                     prefix, name) >= (int)sizeof(entry->path)) {
            vmod_error(error, "archive path is too long");
            return -1;
        }
    } else {
        if (strlen(name) >= sizeof(entry->path)) {
            vmod_error(error, "archive path is too long");
            return -1;
        }
        strcpy(entry->path, name);
    }
    char type = (char)block[156];
    if (type != '0' && type != '\0' && type != '5') {
        vmod_error(error, "unsupported tar entry type for %s", entry->path);
        return -1;
    }
    entry->type = type == '5' ? '5' : '0';
    uint64_t mode;
    uint64_t size;
    if (parse_octal(block + 100, 8, &mode) != 0 ||
        parse_octal(block + 124, 12, &size) != 0) {
        vmod_error(error, "invalid tar metadata for %s", entry->path);
        return -1;
    }
    if (entry->type == '5') {
        size_t len = strlen(entry->path);
        while (len > 0 && entry->path[len - 1] == '/') {
            entry->path[--len] = '\0';
        }
        if (size != 0) {
            vmod_error(error, "directory entry %s has data", entry->path);
            return -1;
        }
    }
    if (!vmod_safe_relpath(entry->path)) {
        vmod_error(error, "unsafe archive path: %s", entry->path);
        return -1;
    }
    entry->size = size;
    entry->mode = (unsigned)(mode & 0777u);
    return 1;
}

static int tar_skip_data(tar_reader *reader, uint64_t size, char **error)
{
    uint64_t padding = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
    uint64_t skip = size + padding;
    if (skip > (uint64_t)INT64_MAX ||
        fseeko(reader->file, (off_t)skip, SEEK_CUR) != 0) {
        vmod_error(error, "cannot skip tar data: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int read_file_limit(const char *path, size_t limit,
                           char **data, size_t *size, char **error)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        vmod_error(error, "cannot read %s: %s", path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || (uint64_t)st.st_size > limit) {
        close(fd);
        vmod_error(error, "invalid size for %s", path);
        return -1;
    }
    size_t length = (size_t)st.st_size;
    char *buffer = malloc(length + 1);
    if (!buffer) {
        close(fd);
        vmod_error(error, "out of memory reading %s", path);
        return -1;
    }
    size_t done = 0;
    while (done < length) {
        ssize_t got = read(fd, buffer + done, length - done);
        if (got < 0) {
            if (errno == EINTR) continue;
            free(buffer);
            close(fd);
            vmod_error(error, "cannot read %s: %s", path, strerror(errno));
            return -1;
        }
        if (got == 0) {
            free(buffer);
            close(fd);
            vmod_error(error, "truncated file %s", path);
            return -1;
        }
        done += (size_t)got;
    }
    close(fd);
    buffer[length] = '\0';
    *data = buffer;
    *size = length;
    return 0;
}

static int hex_digest(const char *text)
{
    if (!text || strlen(text) != 64) {
        return 0;
    }
    for (size_t i = 0; i < 64; i++) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

static int hex_equal(const char *a, const char *b)
{
    for (size_t i = 0; i < 64; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'F') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'F') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) {
            return 0;
        }
    }
    return a[64] == '\0' && b[64] == '\0';
}

static int set_string(char **target, const json_value *value,
                      const char *field, char **error)
{
    const char *string = json_string(value);
    if (!string) {
        vmod_error(error, "manifest field %s must be a string", field);
        return -1;
    }
    char *copy = strdup(string);
    if (!copy) {
        vmod_error(error, "out of memory");
        return -1;
    }
    free(*target);
    *target = copy;
    return 0;
}

static int get_string_array(const json_value *value, char ***items,
                            size_t *count, const char *field, char **error)
{
    if (!value || value->type != JSON_ARRAY) {
        vmod_error(error, "manifest field %s must be an array", field);
        return -1;
    }
    for (size_t i = 0; i < value->u.array.count; i++) {
        const char *string = json_string(value->u.array.items[i]);
        if (!string || !string[0]) {
            vmod_error(error, "manifest field %s has an invalid entry",
                       field);
            return -1;
        }
        if (*count >= 64) {
            vmod_error(error, "manifest field %s has too many entries",
                       field);
            return -1;
        }
        char *copy = strdup(string);
        if (!copy) {
            vmod_error(error, "out of memory");
            return -1;
        }
        char **next = realloc(*items, (*count + 1) * sizeof(*next));
        if (!next) {
            free(copy);
            vmod_error(error, "out of memory");
            return -1;
        }
        *items = next;
        (*items)[(*count)++] = copy;
    }
    return 0;
}

static int manifest_from_json(const char *text, vmod_manifest *manifest,
                              char **error)
{
    char *json_error = NULL;
    json_value *root = json_parse(text, &json_error);
    if (!root) {
        vmod_error(error, "invalid manifest JSON: %s",
                   json_error ? json_error : "parse error");
        free(json_error);
        return -1;
    }
    if (root->type != JSON_OBJECT) {
        json_free(root);
        vmod_error(error, "manifest root must be an object");
        return -1;
    }

    const json_value *format = json_get(root, "format");
    const json_value *abi = json_get(root, "abi_version");
    if (!format || format->type != JSON_NUMBER ||
        !abi || abi->type != JSON_NUMBER) {
        vmod_error(error, "manifest format and abi_version are required");
        json_free(root);
        return -1;
    }
    manifest->format = (int)json_number(format);
    manifest->abi_version = (int)json_number(abi);

    const char *required[] = {
        "id", "name", "version", "architecture", "entrypoint"
    };
    char **targets[] = {
        &manifest->id, &manifest->name, &manifest->version,
        &manifest->architecture, &manifest->entrypoint
    };
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (set_string(targets[i], json_get(root, required[i]),
                       required[i], error) != 0) {
            json_free(root);
            return -1;
        }
    }
    if (json_get(root, "license") &&
        set_string(&manifest->license, json_get(root, "license"),
                   "license", error) != 0) {
        json_free(root);
        return -1;
    }
    if (json_get(root, "description") &&
        set_string(&manifest->description, json_get(root, "description"),
                   "description", error) != 0) {
        json_free(root);
        return -1;
    }
    if (json_get(root, "systems") &&
        get_string_array(json_get(root, "systems"), &manifest->systems,
                         &manifest->system_count, "systems", error) != 0) {
        json_free(root);
        return -1;
    }
    if (json_get(root, "extensions") &&
        get_string_array(json_get(root, "extensions"), &manifest->extensions,
                         &manifest->extension_count, "extensions",
                         error) != 0) {
        json_free(root);
        return -1;
    }

    const json_value *files = json_get(root, "files");
    if (!files || files->type != JSON_OBJECT) {
        vmod_error(error, "manifest files must be an object");
        json_free(root);
        return -1;
    }
    for (size_t i = 0; i < files->u.object.count; i++) {
        const char *path = files->u.object.keys[i];
        const char *hash = json_string(files->u.object.values[i]);
        if (!vmod_safe_relpath(path)) {
            vmod_error(error, "unsafe manifest file path: %s", path);
            json_free(root);
            return -1;
        }
        if (!hex_digest(hash)) {
            vmod_error(error, "invalid SHA-256 for %s", path);
            json_free(root);
            return -1;
        }
        if (manifest->file_count >= VMOD_MAX_FILES) {
            vmod_error(error, "manifest has too many files");
            json_free(root);
            return -1;
        }
        char *path_copy = strdup(path);
        char *hash_copy = strdup(hash);
        vmod_file_hash *next = realloc(manifest->files,
                                       (manifest->file_count + 1) *
                                       sizeof(*next));
        if (!path_copy || !hash_copy || !next) {
            free(path_copy);
            free(hash_copy);
            vmod_error(error, "out of memory");
            json_free(root);
            return -1;
        }
        manifest->files = next;
        manifest->files[manifest->file_count].path = path_copy;
        manifest->files[manifest->file_count].sha256 = hash_copy;
        manifest->file_count++;
    }
    json_free(root);

    if (manifest->format != VMOD_FORMAT_VERSION) {
        vmod_error(error, "unsupported package format %d",
                   manifest->format);
        return -1;
    }
    if (manifest->abi_version != VMOD_ABI_VERSION) {
        vmod_error(error, "package requires module ABI %d, expected %d",
                   manifest->abi_version, VMOD_ABI_VERSION);
        return -1;
    }
    if (strcmp(manifest->architecture, target_arch()) != 0) {
        vmod_error(error, "package architecture %s does not match %s",
                   manifest->architecture, target_arch());
        return -1;
    }
    if (!vmod_safe_id(manifest->id)) {
        vmod_error(error, "invalid module id: %s", manifest->id);
        return -1;
    }
    if (!vmod_safe_version(manifest->version)) {
        vmod_error(error, "invalid module version: %s", manifest->version);
        return -1;
    }
    if (strncmp(manifest->entrypoint, "payload/", 8) != 0) {
        vmod_error(error, "entrypoint must be under payload/");
        return -1;
    }
    size_t entry_length = strlen(manifest->entrypoint);
    if (entry_length < 4 ||
        strcmp(manifest->entrypoint + entry_length - 3, ".so") != 0) {
        vmod_error(error, "entrypoint must be a .so file");
        return -1;
    }
    if (!vmod_safe_relpath(manifest->entrypoint)) {
        vmod_error(error, "unsafe entrypoint path");
        return -1;
    }
    int entry_found = 0;
    for (size_t i = 0; i < manifest->file_count; i++) {
        if (strcmp(manifest->files[i].path, manifest->entrypoint) == 0) {
            entry_found = 1;
            break;
        }
    }
    if (!entry_found) {
        vmod_error(error, "manifest does not list the entrypoint");
        return -1;
    }
    return 0;
}

int vmod_manifest_read(const char *archive, vmod_manifest *manifest,
                       char **error)
{
    tar_reader reader = {0};
    if (tar_open(&reader, archive, error) != 0) {
        return -1;
    }
    char *manifest_text = NULL;
    size_t manifest_size = 0;
    int manifest_count = 0;
    int status = 0;
    for (;;) {
        tar_entry entry;
        int next = tar_next(&reader, &entry, error);
        if (next < 0) {
            status = -1;
            break;
        }
        if (next == 0) {
            break;
        }
        if (entry.type == '0' && strcmp(entry.path, "manifest.json") == 0) {
            if (manifest_count || entry.size > VMOD_MAX_MANIFEST) {
                vmod_error(error, "invalid manifest.json entry");
                status = -1;
                break;
            }
            char *buffer = malloc((size_t)entry.size + 1);
            if (!buffer) {
                vmod_error(error, "out of memory");
                status = -1;
                break;
            }
            if (fread(buffer, 1, (size_t)entry.size, reader.file) !=
                (size_t)entry.size) {
                free(buffer);
                vmod_error(error, "truncated manifest.json");
                status = -1;
                break;
            }
            buffer[entry.size] = '\0';
            manifest_text = buffer;
            manifest_size = (size_t)entry.size;
            manifest_count++;
            uint64_t padding =
                (TAR_BLOCK - (entry.size % TAR_BLOCK)) % TAR_BLOCK;
            if (padding && fseeko(reader.file, (off_t)padding, SEEK_CUR) != 0) {
                vmod_error(error, "truncated manifest.json");
                status = -1;
                break;
            }
        } else if (tar_skip_data(&reader, entry.size, error) != 0) {
            status = -1;
            break;
        }
    }
    fclose(reader.file);
    if (status == 0) {
        if (!manifest_text) {
            vmod_error(error, "package has no manifest.json");
            status = -1;
        } else if (manifest_from_json(manifest_text, manifest, error) != 0) {
            status = -1;
        }
    }
    free(manifest_text);
    (void)manifest_size;
    if (status != 0) {
        vmod_manifest_free(manifest);
    }
    return status;
}

int vmod_manifest_load_file(const char *path, vmod_manifest *manifest,
                            char **error)
{
    char *text = NULL;
    size_t size = 0;
    if (read_file_limit(path, VMOD_MAX_MANIFEST, &text, &size, error) != 0) {
        return -1;
    }
    (void)size;
    int status = manifest_from_json(text, manifest, error);
    free(text);
    if (status != 0) {
        vmod_manifest_free(manifest);
    }
    return status;
}

int vmod_mkdir_p(const char *path, unsigned mode)
{
    char buffer[VMOD_PATH_MAX];
    if (strlen(path) >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(buffer, path);
    for (char *p = buffer + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(buffer, mode) != 0 && errno != EEXIST) {
            return -1;
        }
        struct stat st;
        if (stat(buffer, &st) != 0 || !S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        *p = '/';
    }
    if (mkdir(buffer, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    struct stat st;
    if (stat(buffer, &st) != 0 || !S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static int write_all(int fd, const void *data, size_t size)
{
    const unsigned char *p = data;
    while (size) {
        ssize_t written = write(fd, p, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        p += written;
        size -= (size_t)written;
    }
    return 0;
}

static int make_parent(const char *dest, const char *path, char **error)
{
    char full[VMOD_PATH_MAX * 2];
    if (snprintf(full, sizeof(full), "%s/%s", dest, path) >=
        (int)sizeof(full)) {
        vmod_error(error, "path is too long: %s", path);
        return -1;
    }
    char *slash = strrchr(full, '/');
    if (!slash) {
        return 0;
    }
    *slash = '\0';
    if (vmod_mkdir_p(full, 0755) != 0) {
        vmod_error(error, "cannot create directory %s: %s",
                   full, strerror(errno));
        return -1;
    }
    return 0;
}

static int remove_callback(const char *path, const struct stat *st,
                           int type, struct FTW *ftw)
{
    (void)st;
    (void)type;
    (void)ftw;
    return remove(path);
}

int vmod_remove_tree(const char *path)
{
    return nftw(path, remove_callback, 32, FTW_DEPTH | FTW_PHYS);
}

static int extract_and_validate(const char *archive, const char *dest,
                                vmod_manifest *manifest, char **error)
{
    tar_reader reader = {0};
    if (tar_open(&reader, archive, error) != 0) {
        return -1;
    }
    vmod_file_hash *seen = calloc(VMOD_MAX_FILES, sizeof(*seen));
    if (!seen) {
        fclose(reader.file);
        vmod_error(error, "out of memory");
        return -1;
    }
    size_t seen_count = 0;
    uint64_t total = 0;
    int status = 0;

    for (;;) {
        tar_entry entry;
        int next = tar_next(&reader, &entry, error);
        if (next < 0) {
            status = -1;
            break;
        }
        if (next == 0) {
            break;
        }
        char full[VMOD_PATH_MAX * 2];
        if (snprintf(full, sizeof(full), "%s/%s", dest, entry.path) >=
            (int)sizeof(full)) {
            vmod_error(error, "path is too long: %s", entry.path);
            status = -1;
            break;
        }
        if (entry.type == '5') {
            if (vmod_mkdir_p(full, 0755) != 0) {
                vmod_error(error, "cannot create %s: %s",
                           full, strerror(errno));
                status = -1;
                break;
            }
            continue;
        }
        if (entry.size > VMOD_MAX_FILE) {
            vmod_error(error, "file is too large: %s", entry.path);
            status = -1;
            break;
        }
        if (total > VMOD_MAX_TOTAL - entry.size) {
            vmod_error(error, "package is too large");
            status = -1;
            break;
        }
        total += entry.size;
        if (seen_count >= VMOD_MAX_FILES) {
            vmod_error(error, "package has too many files");
            status = -1;
            break;
        }
        for (size_t i = 0; i < seen_count; i++) {
            if (strcmp(seen[i].path, entry.path) == 0) {
                vmod_error(error, "duplicate archive path: %s", entry.path);
                status = -1;
                break;
            }
        }
        if (status != 0) {
            break;
        }
        if (!strcmp(entry.path, "manifest.json") &&
            entry.size > VMOD_MAX_MANIFEST) {
            vmod_error(error, "manifest.json is too large");
            status = -1;
            break;
        }
        if (make_parent(dest, entry.path, error) != 0) {
            status = -1;
            break;
        }
        int fd = open(full, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) {
            vmod_error(error, "cannot create %s: %s", full, strerror(errno));
            status = -1;
            break;
        }
        vcos_sha256_ctx hash;
        vcos_sha256_init(&hash);
        uint64_t remaining = entry.size;
        int failed = 0;
        while (remaining) {
            unsigned char buffer[65536];
            size_t want = remaining < sizeof(buffer) ?
                          (size_t)remaining : sizeof(buffer);
            size_t got = fread(buffer, 1, want, reader.file);
            if (got != want) {
                vmod_error(error, "truncated file: %s", entry.path);
                failed = 1;
                break;
            }
            vcos_sha256_update(&hash, buffer, got);
            if (write_all(fd, buffer, got) != 0) {
                vmod_error(error, "cannot write %s: %s",
                           full, strerror(errno));
                failed = 1;
                break;
            }
            remaining -= got;
        }
        if (!failed) {
            uint64_t padding =
                (TAR_BLOCK - (entry.size % TAR_BLOCK)) % TAR_BLOCK;
            unsigned char padding_data[64];
            while (padding) {
                size_t want = padding < sizeof(padding_data) ?
                              (size_t)padding : sizeof(padding_data);
                if (fread(padding_data, 1, want, reader.file) != want) {
                    vmod_error(error, "truncated archive data");
                    failed = 1;
                    break;
                }
                padding -= want;
            }
        }
        if (!failed) {
            unsigned char digest[32];
            char hex[65];
            vcos_sha256_final(&hash, digest);
            for (unsigned i = 0; i < 32; i++) {
                static const char digits[] = "0123456789abcdef";
                hex[i * 2] = digits[digest[i] >> 4];
                hex[i * 2 + 1] = digits[digest[i] & 15];
            }
            hex[64] = '\0';
            if (fchmod(fd, entry.mode ? entry.mode : 0644) != 0) {
                vmod_error(error, "cannot set mode on %s: %s",
                           full, strerror(errno));
                failed = 1;
            }
            if (!failed) {
                seen[seen_count].path = strdup(entry.path);
                seen[seen_count].sha256 = strdup(hex);
                if (!seen[seen_count].path || !seen[seen_count].sha256) {
                    vmod_error(error, "out of memory");
                    failed = 1;
                } else {
                    seen_count++;
                }
            }
        }
        if (close(fd) != 0 && !failed) {
            vmod_error(error, "cannot close %s: %s", full, strerror(errno));
            failed = 1;
        }
        if (failed) {
            unlink(full);
            status = -1;
            break;
        }
    }
    fclose(reader.file);

    char *manifest_text = NULL;
    size_t manifest_size = 0;
    if (status == 0) {
        char manifest_path[VMOD_PATH_MAX * 2];
        if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json",
                     dest) >= (int)sizeof(manifest_path)) {
            vmod_error(error, "staging path is too long");
            status = -1;
        } else if (read_file_limit(manifest_path, VMOD_MAX_MANIFEST,
                                   &manifest_text, &manifest_size,
                                   error) != 0) {
            status = -1;
        } else if (manifest_from_json(manifest_text, manifest, error) != 0) {
            status = -1;
        }
    }
    (void)manifest_size;

    if (status == 0) {
        size_t checked = 0;
        for (size_t i = 0; i < seen_count; i++) {
            if (strcmp(seen[i].path, "manifest.json") == 0) {
                continue;
            }
            int found = 0;
            for (size_t j = 0; j < manifest->file_count; j++) {
                if (strcmp(manifest->files[j].path, seen[i].path) == 0) {
                    if (!hex_equal(seen[i].sha256,
                                   manifest->files[j].sha256)) {
                        vmod_error(error, "SHA-256 mismatch for %s",
                                   seen[i].path);
                        status = -1;
                    }
                    found = 1;
                    break;
                }
            }
            if (!found) {
                vmod_error(error, "manifest does not list %s", seen[i].path);
                status = -1;
            }
            if (status != 0) {
                break;
            }
            checked++;
        }
        if (status == 0 && checked != manifest->file_count) {
            vmod_error(error, "manifest lists a missing file");
            status = -1;
        }
    }
    if (status == 0) {
        char entry_path[VMOD_PATH_MAX * 2];
        if (snprintf(entry_path, sizeof(entry_path), "%s/%s", dest,
                     manifest->entrypoint) >= (int)sizeof(entry_path)) {
            vmod_error(error, "entrypoint path is too long");
            status = -1;
        } else {
            struct stat st;
            if (stat(entry_path, &st) != 0 || !S_ISREG(st.st_mode)) {
                vmod_error(error, "entrypoint is missing");
                status = -1;
            }
        }
    }
    free(manifest_text);
    for (size_t i = 0; i < seen_count; i++) {
        free(seen[i].path);
        free(seen[i].sha256);
    }
    free(seen);
    if (status != 0) {
        vmod_manifest_free(manifest);
    }
    return status;
}

int vmod_install(const char *archive, const char *root,
                 vmod_manifest *manifest, char **error)
{
    char modules[VMOD_PATH_MAX];
    if (snprintf(modules, sizeof(modules), "%s/modules", root) >=
        (int)sizeof(modules)) {
        vmod_error(error, "data root path is too long");
        return -1;
    }
    if (vmod_mkdir_p(modules, 0755) != 0) {
        vmod_error(error, "cannot create %s: %s", modules, strerror(errno));
        return -1;
    }
    char staging[VMOD_PATH_MAX];
    if (snprintf(staging, sizeof(staging), "%s/.staging.XXXXXX", modules) >=
        (int)sizeof(staging)) {
        vmod_error(error, "staging path is too long");
        return -1;
    }
    if (!mkdtemp(staging)) {
        vmod_error(error, "cannot create staging directory: %s",
                   strerror(errno));
        return -1;
    }
    if (extract_and_validate(archive, staging, manifest, error) != 0) {
        vmod_remove_tree(staging);
        return -1;
    }

    char parent[VMOD_PATH_MAX];
    char final[VMOD_PATH_MAX];
    if (snprintf(parent, sizeof(parent), "%s/%s", modules, manifest->id) >=
            (int)sizeof(parent) ||
        snprintf(final, sizeof(final), "%s/%s", parent,
                 manifest->version) >= (int)sizeof(final)) {
        vmod_remove_tree(staging);
        vmod_error(error, "install path is too long");
        return -1;
    }
    if (vmod_mkdir_p(parent, 0755) != 0) {
        vmod_remove_tree(staging);
        vmod_error(error, "cannot create %s: %s", parent, strerror(errno));
        return -1;
    }
    struct stat st;
    if (lstat(final, &st) == 0) {
        vmod_remove_tree(staging);
        vmod_error(error, "%s %s is already installed",
                   manifest->id, manifest->version);
        return -1;
    }
    if (errno != ENOENT) {
        vmod_remove_tree(staging);
        vmod_error(error, "cannot inspect %s: %s", final, strerror(errno));
        return -1;
    }
    if (rename(staging, final) != 0) {
        vmod_remove_tree(staging);
        vmod_error(error, "cannot install to %s: %s", final, strerror(errno));
        return -1;
    }
    if (chmod(final, 0755) != 0) {
        vmod_remove_tree(final);
        vmod_error(error, "cannot set mode on %s: %s", final, strerror(errno));
        return -1;
    }

    char current[VMOD_PATH_MAX];
    char temporary[VMOD_PATH_MAX];
    if (snprintf(current, sizeof(current), "%s/current", parent) >=
            (int)sizeof(current) ||
        snprintf(temporary, sizeof(temporary), "%s/.current.%ld", parent,
                 (long)getpid()) >= (int)sizeof(temporary)) {
        vmod_remove_tree(final);
        vmod_error(error, "current link path is too long");
        return -1;
    }
    unlink(temporary);
    if (symlink(manifest->version, temporary) != 0 ||
        rename(temporary, current) != 0) {
        int saved = errno;
        unlink(temporary);
        vmod_remove_tree(final);
        vmod_error(error, "cannot update current link: %s", strerror(saved));
        return -1;
    }
    return 0;
}
