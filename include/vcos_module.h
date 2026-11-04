#ifndef VCOS_MODULE_H
#define VCOS_MODULE_H

//
// VCOS module ABI. This header file is a template to be imported by every module written for VCOS.
// Every module is, essentially, a .c file that is linked against this header. Only function required
// to be defined by this header is vcos_module_entry.
//

#include <stddef.h>
#include <stdint.h>

#define VCOS_MODULE_ABI_MAJOR 1u
#define VCOS_MODULE_ABI_MINOR 0u
#define VCOS_MODULE_ENTRY_SYMBOL "vcos_module_entry"

#define VCOS_MAX_PORTS 8u

#if defined(__GNUC__)
#define VCOS_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define VCOS_MODULE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t size;
    uint16_t major;
    uint16_t minor;
} vcos_abi_header;

#define VCOS_ABI_INIT(type) { sizeof(type), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR }

typedef int32_t vcos_result;

#define VCOS_OK 0
#define VCOS_ERR_INVALID 1
#define VCOS_ERR_VERSION 2
#define VCOS_ERR_UNSUPPORTED 3
#define VCOS_ERR_NOMEM 4
#define VCOS_ERR_IO 5
#define VCOS_ERR_STATE 6
#define VCOS_ERR_AGAIN 7

#define VCOS_LOG_ERROR 0
#define VCOS_LOG_WARN 1
#define VCOS_LOG_INFO 2
#define VCOS_LOG_DEBUG 3

#define VCOS_MODULE_CAP_SAVE_STATES (1ull << 0)
#define VCOS_MODULE_CAP_MULTIPLAYER (1ull << 1)

#define VCOS_SYSTEM_FLAG_SAVE_STATES (1u << 0)
#define VCOS_SYSTEM_FLAG_MULTIPLAYER (1u << 1)

#define VCOS_PIXEL_XRGB8888 1u
#define VCOS_AUDIO_S16 1u

#define VCOS_FILE_MAIN 0u
#define VCOS_FILE_RESOURCE 1u
#define VCOS_FILE_DISC 2u

#define VCOS_BTN_A 0x00000001u
#define VCOS_BTN_B 0x00000002u
#define VCOS_BTN_SELECT 0x00000004u
#define VCOS_BTN_START 0x00000008u
#define VCOS_BTN_UP 0x00000010u
#define VCOS_BTN_DOWN 0x00000020u
#define VCOS_BTN_LEFT 0x00000040u
#define VCOS_BTN_RIGHT 0x00000080u
#define VCOS_BTN_X 0x00000100u
#define VCOS_BTN_Y 0x00000200u
#define VCOS_BTN_L 0x00000400u
#define VCOS_BTN_R 0x00000800u
#define VCOS_BTN_L2 0x00001000u
#define VCOS_BTN_R2 0x00002000u
#define VCOS_BTN_L3 0x00004000u
#define VCOS_BTN_R3 0x00008000u
#define VCOS_BTN_HOME 0x00010000u
#define VCOS_BTN_CAPTURE 0x00020000u

typedef struct {
    uint32_t buttons;
    int16_t axis[4];
} vcos_input_port;

typedef struct {
    vcos_abi_header abi;
    uint32_t port_count;
    uint32_t port_stride;
    vcos_input_port ports[VCOS_MAX_PORTS];
} vcos_input_state;

typedef struct {
    vcos_abi_header abi;
    const char *name;
    const uint8_t *data;
    size_t size;
    uint32_t kind;
} vcos_content_file;

typedef struct {
    vcos_abi_header abi;
    const char *system_id;
    const char *path;
    uint32_t file_count;
    uint32_t file_stride;
    const vcos_content_file *files;
} vcos_content;

typedef struct {
    const char *key;
    const char *value;
} vcos_option;

typedef struct {
    vcos_abi_header abi;
    uint32_t option_count;
    uint32_t option_stride;
    const vcos_option *options;
} vcos_config;

typedef struct {
    vcos_abi_header abi;
    const char *id;
    const char *name;
    const char *const *extensions;
    uint32_t extension_count;
    uint32_t max_players;
    uint32_t flags;
} vcos_system_info;

typedef struct {
    vcos_abi_header abi;
    const char *id;
    const char *name;
    const char *version;
    const char *license;
    const vcos_system_info *systems;
    uint32_t system_count;
    uint32_t system_stride;
    uint64_t capabilities;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t sample_rate;
    uint32_t channels;
} vcos_module_info;

typedef struct {
    vcos_abi_header abi;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
    uint8_t *data;
    size_t capacity;
    size_t used;
} vcos_video_frame;

typedef struct {
    vcos_abi_header abi;
    int16_t *samples;
    size_t capacity_frames;
    size_t frames;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t format;
} vcos_audio_buffer;

typedef struct {
    vcos_abi_header abi;
    void *userdata;
    void (*log)(void *userdata, int level, const char *message);
    void (*request_quit)(void *userdata);
} vcos_host_api;

typedef struct {
    vcos_abi_header abi;
    vcos_result (*run_frame)(void *ctx,
                             const vcos_input_state *input,
                             vcos_video_frame *video,
                             vcos_audio_buffer *audio);
    vcos_result (*reset)(void *ctx, int hard);
    vcos_result (*state_size)(void *ctx, size_t *size);
    vcos_result (*state_save)(void *ctx, void *data, size_t *size);
    vcos_result (*state_load)(void *ctx, const void *data, size_t size);
    void (*destroy)(void *ctx);
} vcos_module_api;

typedef struct {
    vcos_abi_header abi;
    const vcos_module_api *api;
    void *ctx;
} vcos_module_instance;

typedef vcos_result (*vcos_module_create_fn)(const vcos_host_api *host,
                                             const vcos_content *content,
                                             const vcos_config *config,
                                             vcos_module_instance **out);

typedef struct {
    vcos_abi_header abi;
    const vcos_module_info *info;
    vcos_module_create_fn create;
} vcos_module_descriptor;

VCOS_MODULE_EXPORT const vcos_module_descriptor *vcos_module_entry(void);

#ifdef __cplusplus
}
#endif

#endif
