#ifndef VCOS_SIGNATURE_H
#define VCOS_SIGNATURE_H

#include <stddef.h>

#define VCOS_ED25519_SIGNATURE_SIZE 64
#define VCOS_SIGNATURE_SUFFIX ".sig"
#define VCOS_SIGNATURE_PATH_MAX 4096

int vcos_signature_path(const char *package, char *out, size_t out_size);
int vcos_signature_verify_package(const char *package, char **error);
int vcos_signature_verify_file(const char *package,
                               const char *signature_path,
                               char **error);
int vcos_ed25519_verify_digest(const unsigned char digest[32],
                               const unsigned char signature[64],
                               char **error);

#endif
