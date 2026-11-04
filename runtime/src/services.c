#define _POSIX_C_SOURCE 200809L
#include "services.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

#define SAVE_VERSION 1
#define SAVE_DATA_LEN 16

static uint32_t crc32_bytes(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

static void path_join(char *out, size_t len, const char *dir, const char *name)
{
    if (dir[0] == '\0') {
        snprintf(out, len, "%s", name);
    } else {
        snprintf(out, len, "%s/%s", dir, name);
    }
}

static int library_cmp(const void *a, const void *b)
{
    const vcos_library_entry *ea = a;
    const vcos_library_entry *eb = b;
    return strcmp(ea->name, eb->name);
}

static void scan_dir(services *s, const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s->library_count < VCOS_LIB_MAX) {
        size_t len = strlen(entry->d_name);
        if (len < 5 || strcmp(entry->d_name + len - 4, ".nes") != 0) {
            continue;
        }
        if (strncmp(entry->d_name, "shell", 5) == 0) {
            continue;
        }
        vcos_library_entry *lib = &s->library[s->library_count];
        snprintf(lib->name, sizeof(lib->name), "%.*s", VCOS_LIB_NAME_LEN - 1,
                 entry->d_name);
        char *dot = strrchr(lib->name, '.');
        if (dot) {
            *dot = '\0';
        }
        for (char *p = lib->name; *p; p++) {
            if (*p >= 'a' && *p <= 'z') {
                *p = (char)(*p - 'a' + 'A');
            }
        }
        path_join(lib->path, sizeof(lib->path), dir_path, entry->d_name);
        s->library_count++;
    }
    closedir(dir);
}

static void scan_library(services *s)
{
    s->library_count = 0;
    scan_dir(s, s->lib_dir);

    char games_dir[600];
    path_join(games_dir, sizeof(games_dir), s->data_dir, "games");
    mkdir(games_dir, 0755);
    scan_dir(s, games_dir);

    qsort(s->library, (size_t)s->library_count, sizeof(s->library[0]),
          library_cmp);
    for (int i = 0; i < s->library_count; i++) {
        vcos_log("services: library[%d] %s", i, s->library[i].name);
    }
}

static void write_manifest(services *s)
{
    char path[600];
    path_join(path, sizeof(path), s->data_dir, "library.txt");
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    for (int i = 0; i < s->library_count; i++) {
        fprintf(f, "%s\t%s\n", s->library[i].name, s->library[i].path);
    }
    fclose(f);
}

void services_init(services *s, const char *shell_path, const char *lib_dir,
                   const char *data_dir, int verbose)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->shell_path, sizeof(s->shell_path), "%s", shell_path ? shell_path : "");
    snprintf(s->lib_dir, sizeof(s->lib_dir), "%s", lib_dir ? lib_dir : ".");
    snprintf(s->data_dir, sizeof(s->data_dir), "%s", data_dir ? data_dir : s->lib_dir);
    s->verbose = verbose;
    s->pending = VCOS_SVC_REQUEST_NONE;
    scan_library(s);
    write_manifest(s);
}

uint8_t services_read(services *s, uint16_t addr)
{
    switch (addr - VCOS_SVC_BASE) {
    case 0x00:
    case 0x01:
    case 0x02:
        return 0;
    case 0x03:
        return 0x01;
    case 0x04:
        return 'V';
    case 0x05:
        return 'C';
    case 0x06:
        return 'O';
    case 0x07:
        return 'S';
    default:
        if (addr >= VCOS_SVC_BASE + 0x08 && addr < VCOS_SVC_BASE + 0x18) {
            return s->data[addr - VCOS_SVC_BASE - 0x08];
        }
        return 0;
    }
}

static void save_data(services *s)
{
    char path[600];
    char tmp[620];
    path_join(path, sizeof(path), s->data_dir, "save.bin");
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        vcos_error("services: cannot write %s", tmp);
        return;
    }
    uint8_t header[12];
    memcpy(header, "VCS1", 4);
    header[4] = SAVE_VERSION;
    header[5] = SAVE_DATA_LEN;
    header[6] = 0;
    header[7] = 0;
    uint32_t crc = crc32_bytes(0, s->data, sizeof(s->data));
    header[8] = (uint8_t)(crc & 0xFF);
    header[9] = (uint8_t)((crc >> 8) & 0xFF);
    header[10] = (uint8_t)((crc >> 16) & 0xFF);
    header[11] = (uint8_t)((crc >> 24) & 0xFF);
    int ok = fwrite(header, 1, sizeof(header), f) == sizeof(header) &&
             fwrite(s->data, 1, sizeof(s->data), f) == sizeof(s->data);
    if (ok) {
        fflush(f);
        fsync(fileno(f));
    }
    fclose(f);
    if (!ok || rename(tmp, path) != 0) {
        vcos_error("services: cannot commit %s", path);
        unlink(tmp);
        return;
    }
    s->dirty = 0;
    vcos_log("services: saved %s", path);
}

static void load_data(services *s)
{
    char path[600];
    path_join(path, sizeof(path), s->data_dir, "save.bin");
    FILE *f = fopen(path, "rb");
    if (!f) {
        vcos_log("services: no save file at %s", path);
        return;
    }
    uint8_t buf[12 + SAVE_DATA_LEN + 1];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n >= 12 && memcmp(buf, "VCS1", 4) == 0) {
        uint8_t version = buf[4];
        uint8_t length = buf[5];
        uint32_t crc = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                       ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
        if (version != SAVE_VERSION || length != SAVE_DATA_LEN ||
            n != 12 + SAVE_DATA_LEN ||
            crc32_bytes(0, buf + 12, SAVE_DATA_LEN) != crc) {
            vcos_error("services: ignoring corrupt save file %s", path);
            return;
        }
        memcpy(s->data, buf + 12, SAVE_DATA_LEN);
    } else if (n == SAVE_DATA_LEN) {
        memcpy(s->data, buf, SAVE_DATA_LEN);
        s->dirty = 1;
        vcos_log("services: migrated legacy save file %s", path);
        save_data(s);
    } else {
        vcos_error("services: ignoring invalid save file %s", path);
        return;
    }
    vcos_log("services: loaded %s", path);
}

static int launch_index(services *s, uint8_t index)
{
    if (index >= s->library_count) {
        vcos_log("services: cartridge index %u out of range", index);
        return -1;
    }
    snprintf(s->pending_path, sizeof(s->pending_path), "%s", s->library[index].path);
    s->pending = VCOS_SVC_REQUEST_LOAD;
    vcos_log("services: launch request -> %s", s->pending_path);
    return 0;
}

void services_write(services *s, uint16_t addr, uint8_t value)
{
    uint16_t off = (uint16_t)(addr - VCOS_SVC_BASE);
    switch (off) {
    case 0x01:
        s->data[0] = value;
        return;
    case 0x02:
        s->data[1] = value;
        return;
    case 0x00:
        break;
    default:
        if (off >= 0x08 && off < 0x18) {
            s->data[off - 0x08] = value;
            s->dirty = 1;
        }
        return;
    }

    switch (value) {
    case VCOS_CMD_LAUNCH:
        if (s->library_count > 0) {
            launch_index(s, 0);
        } else {
            path_join(s->pending_path, sizeof(s->pending_path), s->lib_dir, "demo.nes");
            s->pending = VCOS_SVC_REQUEST_LOAD;
        }
        break;
    case VCOS_CMD_SHELL:
        snprintf(s->pending_path, sizeof(s->pending_path), "%s", s->shell_path);
        s->pending = VCOS_SVC_REQUEST_LOAD;
        vcos_log("services: shell request -> %s", s->pending_path);
        break;
    case VCOS_CMD_SHUTDOWN:
        s->pending = VCOS_SVC_REQUEST_SHUTDOWN;
        vcos_log("services: shutdown request");
        break;
    case VCOS_CMD_REBOOT:
        s->pending = VCOS_SVC_REQUEST_REBOOT;
        vcos_log("services: reboot request");
        break;
    case VCOS_CMD_LOG:
        vcos_log("guest log: %c", s->data[0]);
        break;
    case VCOS_CMD_SAVE:
        save_data(s);
        break;
    case VCOS_CMD_LOAD:
        load_data(s);
        break;
    case VCOS_CMD_VERSION:
        s->data[0] = VCOS_ABI_MAJOR;
        s->data[1] = VCOS_ABI_MINOR;
        s->data[2] = 0;
        break;
    case VCOS_CMD_LIB_COUNT:
        s->data[0] = (uint8_t)s->library_count;
        s->data[1] = (uint8_t)(s->library_count >> 8);
        break;
    case VCOS_CMD_LIB_NAME: {
        uint8_t index = s->data[0];
        memset(s->data, 0, sizeof(s->data));
        if (index < s->library_count) {
            memcpy(s->data, s->library[index].name, sizeof(s->data));
        }
        break;
    }
    case VCOS_CMD_LIB_LAUNCH:
        launch_index(s, s->data[0]);
        break;
    default:
        vcos_log("services: unknown command %u", value);
        break;
    }
}

int services_take_request(services *s, char *path, size_t path_len)
{
    if (s->pending == VCOS_SVC_REQUEST_NONE) {
        return VCOS_SVC_REQUEST_NONE;
    }
    int request = s->pending;
    s->pending = VCOS_SVC_REQUEST_NONE;
    if (request == VCOS_SVC_REQUEST_LOAD && path && path_len > 0) {
        snprintf(path, path_len, "%s", s->pending_path);
    }
    return request;
}

void services_flush(services *s)
{
    if (s->dirty) {
        save_data(s);
    }
}
