#define _POSIX_C_SOURCE 200809L

#include "signature.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include "sha256.h"
#include "vmod.h"

#include "vcos_pubkey.inc"

static int sha256_package(const char *path, unsigned char digest[32],
                          char **error)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        vmod_error(error, "cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    vcos_sha256_ctx ctx;
    vcos_sha256_init(&ctx);

    unsigned char buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        vcos_sha256_update(&ctx, buffer, count);
    }

    if (ferror(file)) {
        vmod_error(error, "cannot read %s: %s", path, strerror(errno));
        fclose(file);
        return -1;
    }

    fclose(file);
    vcos_sha256_final(&ctx, digest);
    return 0;
}

static int read_signature(const char *path,
                          unsigned char signature[VCOS_ED25519_SIGNATURE_SIZE],
                          char **error)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) {
            return 1;
        }
        vmod_error(error, "cannot open signature %s: %s",
                   path, strerror(errno));
        return -1;
    }

    size_t count = fread(signature, 1, VCOS_ED25519_SIGNATURE_SIZE, file);
    int extra = fgetc(file);
    int failed = ferror(file);

    fclose(file);

    if (failed || count != VCOS_ED25519_SIGNATURE_SIZE || extra != EOF) {
        vmod_error(error,
                   "invalid signature file %s: expected exactly %d bytes",
                   path, VCOS_ED25519_SIGNATURE_SIZE);
        return -1;
    }

    return 0;
}

int vcos_signature_path(const char *package, char *out, size_t out_size)
{
    if (!package || !package[0] || !out || out_size == 0) {
        return -1;
    }

    int length = snprintf(out, out_size, "%s%s", package,
                          VCOS_SIGNATURE_SUFFIX);
    if (length < 0 || (size_t)length >= out_size) {
        return -1;
    }

    return 0;
}

int vcos_ed25519_verify_digest(const unsigned char digest[32],
                               const unsigned char signature[64],
                               char **error)
{
    int result = -1;
    EVP_MD_CTX *ctx = NULL;
    EVP_PKEY *key = NULL;
    const unsigned char *der = vcos_pubkey_der;

    key = d2i_PUBKEY(NULL, &der, (long)vcos_pubkey_der_len);
    if (!key) {
        vmod_error(error, "embedded Ed25519 public key is invalid");
        return -1;
    }

    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        vmod_error(error, "cannot allocate signature verification context");
        goto out;
    }

    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, key) != 1) {
        vmod_error(error, "cannot initialize signature verification");
        goto out;
    }

    if (EVP_DigestVerify(ctx, signature, VCOS_ED25519_SIGNATURE_SIZE,
                         digest, 32) != 1) {
        vmod_error(error, "signature verification failed");
        goto out;
    }

    result = 0;

out:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return result;
}

int vcos_signature_verify_file(const char *package,
                               const char *signature_path,
                               char **error)
{
    unsigned char signature[VCOS_ED25519_SIGNATURE_SIZE];
    unsigned char digest[32];

    int status = read_signature(signature_path, signature, error);
    if (status != 0) {
        return status;
    }

    if (sha256_package(package, digest, error) != 0) {
        return -1;
    }

    return vcos_ed25519_verify_digest(digest, signature, error);
}

int vcos_signature_verify_package(const char *package, char **error)
{
    char signature_path[VCOS_SIGNATURE_PATH_MAX];

    if (vcos_signature_path(package, signature_path,
                            sizeof(signature_path)) != 0) {
        vmod_error(error, "signature path is too long for %s", package);
        return -1;
    }

    return vcos_signature_verify_file(package, signature_path, error);
}
