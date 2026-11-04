#ifndef VCOS_SERVICES_H
#define VCOS_SERVICES_H

#include <stddef.h>
#include <stdint.h>

#define VCOS_SVC_BASE 0x5000
#define VCOS_LIB_MAX 32
#define VCOS_LIB_NAME_LEN 16

#define VCOS_CMD_LAUNCH 1
#define VCOS_CMD_SHELL 2
#define VCOS_CMD_SHUTDOWN 3
#define VCOS_CMD_LOG 4
#define VCOS_CMD_SAVE 5
#define VCOS_CMD_LOAD 6
#define VCOS_CMD_VERSION 7
#define VCOS_CMD_LIB_COUNT 8
#define VCOS_CMD_LIB_NAME 9
#define VCOS_CMD_LIB_LAUNCH 10
#define VCOS_CMD_REBOOT 11

#define VCOS_SVC_REQUEST_NONE 0
#define VCOS_SVC_REQUEST_LOAD 1
#define VCOS_SVC_REQUEST_SHUTDOWN 2
#define VCOS_SVC_REQUEST_REBOOT 3

#define VCOS_ABI_MAJOR 0
#define VCOS_ABI_MINOR 2

typedef struct {
    char name[VCOS_LIB_NAME_LEN];
    char path[512];
} vcos_library_entry;

typedef struct services {
    char shell_path[512];
    char lib_dir[512];
    char data_dir[512];
    int pending;
    char pending_path[512];
    uint8_t data[16];
    int verbose;
    int dirty;
    vcos_library_entry library[VCOS_LIB_MAX];
    int library_count;
} services;

void services_init(services *s, const char *shell_path, const char *lib_dir,
                   const char *data_dir, int verbose);
uint8_t services_read(services *s, uint16_t addr);
void services_write(services *s, uint16_t addr, uint8_t value);
int services_take_request(services *s, char *path, size_t path_len);
void services_flush(services *s);

#endif
