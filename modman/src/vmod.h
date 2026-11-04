#ifndef VCOS_VMOD_H
#define VCOS_VMOD_H

#include <stddef.h>
#include <stdint.h>

#define VMOD_FORMAT_VERSION 1
#define VMOD_ABI_VERSION 1
#define VMOD_MAX_FILES 512
#define VMOD_MAX_MANIFEST (1024u * 1024u)
#define VMOD_MAX_FILE (512ull * 1024ull * 1024ull)
#define VMOD_MAX_TOTAL (1024ull * 1024ull * 1024ull)
#define VMOD_PATH_MAX 1024

typedef struct {
    char *path;
    char *sha256;
} vmod_file_hash;

typedef struct {
    int format;
    int abi_version;
    char *id;
    char *name;
    char *version;
    char *architecture;
    char *entrypoint;
    char *license;
    char *description;
    char **systems;
    size_t system_count;
    char **extensions;
    size_t extension_count;
    vmod_file_hash *files;
    size_t file_count;
} vmod_manifest;

void vmod_error(char **error, const char *format, ...);
void vmod_manifest_init(vmod_manifest *manifest);
void vmod_manifest_free(vmod_manifest *manifest);
int vmod_safe_relpath(const char *path);
int vmod_safe_id(const char *id);
int vmod_safe_version(const char *version);
int vmod_manifest_read(const char *archive, vmod_manifest *manifest,
                       char **error);
int vmod_mkdir_p(const char *path, unsigned mode);
int vmod_remove_tree(const char *path);
int vmod_manifest_load_file(const char *path, vmod_manifest *manifest,
                            char **error);
int vmod_install(const char *archive, const char *root,
                 vmod_manifest *manifest, char **error);

#endif
