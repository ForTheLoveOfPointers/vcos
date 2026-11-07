#ifndef VCOS_SERVICES_H
#define VCOS_SERVICES_H

#include <stddef.h>
#include <stdint.h>

#define VCOS_SVC_BASE 0x5000
#define VCOS_LIB_MAX 32
#define VCOS_LIB_NAME_LEN 16
#define VCOS_SYSTEM_MAX 16

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
#define VCOS_CMD_SYS_COUNT 12
#define VCOS_CMD_SYS_NAME 13
#define VCOS_CMD_SYS_GAME_COUNT 14
#define VCOS_CMD_SYS_GAME_NAME 15
#define VCOS_CMD_SYS_LAUNCH 16

#define VCOS_SVC_REQUEST_NONE 0
#define VCOS_SVC_REQUEST_LOAD 1
#define VCOS_SVC_REQUEST_SHUTDOWN 2
#define VCOS_SVC_REQUEST_REBOOT 3

#define VCOS_ABI_MAJOR 0
#define VCOS_ABI_MINOR 2

typedef struct {
    char name[VCOS_LIB_NAME_LEN];
    char system[VCOS_LIB_NAME_LEN];
    char path[512];
    char module_path[512];
    int external;
} vcos_library_entry;

typedef struct {
    char name[VCOS_LIB_NAME_LEN];
    int first;
    int count;
} vcos_system_entry;

typedef struct services {
    char shell_path[512];
    char lib_dir[512];
    char data_dir[512];
    char modules_dir[512];
    char module_host[512];
    int pending;
    char pending_path[512];
    char pending_module[512];
    int pending_external;
    uint8_t data[16];
    int verbose;
    int dirty;
    vcos_library_entry library[VCOS_LIB_MAX];
    int library_count;
    vcos_system_entry systems[VCOS_SYSTEM_MAX];
    int system_count;
} services;

void services_init(services *s, const char *shell_path, const char *lib_dir,
                   const char *data_dir, const char *modules_dir,
                   const char *module_host, int verbose);
uint8_t services_read(services *s, uint16_t addr);
void services_write(services *s, uint16_t addr, uint8_t value);
int services_take_request(services *s, char *path, size_t path_len,
                          char *module, size_t module_len, int *external);
void services_flush(services *s);

#endif
