#include "libretro.h"
#include "vcos_module.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ADAPTER_PATH_MAX 1024

typedef struct {
    const vcos_host_api *host;
    const vcos_content *content;
    const vcos_config *config;
    const vcos_input_state *input;
    vcos_video_frame *video;
    vcos_audio_buffer *audio;
    struct retro_system_info core_info;
    struct retro_system_av_info av_info;
    enum retro_pixel_format pixel_format;
    struct retro_frame_time_callback frame_time;
    char system_dir[ADAPTER_PATH_MAX];
    char save_dir[ADAPTER_PATH_MAX];
    char content_dir[ADAPTER_PATH_MAX];
    int frame_time_set;
    int initialized;
    int loaded;
} libretro_adapter;

static libretro_adapter adapter;
static vcos_module_instance instance;
static const vcos_module_info module_info;

static const char *adapter_option(const char *key)
{
    if (!adapter.config || !adapter.config->options || !key) {
        return NULL;
    }
    size_t stride = adapter.config->option_stride;
    if (stride < sizeof(vcos_option)) {
        stride = sizeof(vcos_option);
    }
    const unsigned char *base = (const unsigned char *)adapter.config->options;
    for (uint32_t i = 0; i < adapter.config->option_count; i++) {
        const vcos_option *option = (const vcos_option *)(base + i * stride);
        if (option->key && strcmp(option->key, key) == 0) {
            return option->value;
        }
    }
    return NULL;
}

static void adapter_copy_path(char *out, size_t size, const char *path)
{
    if (size == 0) {
        return;
    }
    snprintf(out, size, "%s", path ? path : "");
}

static void adapter_setup_paths(void)
{
    const char *system_dir = adapter_option("system_dir");
    const char *save_dir = adapter_option("save_dir");
    if (!system_dir || !system_dir[0]) {
        system_dir = "/var/lib/vcos/systems";
    }
    if (!save_dir || !save_dir[0]) {
        snprintf(adapter.save_dir, sizeof(adapter.save_dir),
                 "/var/lib/vcos/saves/%s", module_info.id);
    } else {
        adapter_copy_path(adapter.save_dir, sizeof(adapter.save_dir),
                          save_dir);
    }
    adapter_copy_path(adapter.system_dir, sizeof(adapter.system_dir),
                      system_dir);

    if (adapter.content && adapter.content->path &&
        adapter.content->path[0]) {
        const char *slash = strrchr(adapter.content->path, '/');
        if (slash) {
            size_t length = (size_t)(slash - adapter.content->path);
            if (length >= sizeof(adapter.content_dir)) {
                length = sizeof(adapter.content_dir) - 1;
            }
            memcpy(adapter.content_dir, adapter.content->path, length);
            adapter.content_dir[length] = '\0';
        } else {
            adapter_copy_path(adapter.content_dir,
                              sizeof(adapter.content_dir), ".");
        }
    } else {
        adapter_copy_path(adapter.content_dir, sizeof(adapter.content_dir),
                          ".");
    }
}

static void adapter_log(enum retro_log_level level, const char *format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    int vcos_level = VCOS_LOG_INFO;
    switch (level) {
    case RETRO_LOG_DEBUG:
        vcos_level = VCOS_LOG_DEBUG;
        break;
    case RETRO_LOG_INFO:
        vcos_level = VCOS_LOG_INFO;
        break;
    case RETRO_LOG_WARN:
        vcos_level = VCOS_LOG_WARN;
        break;
    case RETRO_LOG_ERROR:
        vcos_level = VCOS_LOG_ERROR;
        break;
    default:
        break;
    }

    if (adapter.host && adapter.host->log) {
        adapter.host->log(adapter.host->userdata, vcos_level, message);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}

static bool adapter_environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        if (!data) {
            return false;
        }
        enum retro_pixel_format format =
            *(const enum retro_pixel_format *)data;
        if (format != RETRO_PIXEL_FORMAT_0RGB1555 &&
            format != RETRO_PIXEL_FORMAT_XRGB8888 &&
            format != RETRO_PIXEL_FORMAT_RGB565) {
            return false;
        }
        adapter.pixel_format = format;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (!data) {
            return false;
        }
        *(const char **)data = adapter.system_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!data) {
            return false;
        }
        *(const char **)data = adapter.save_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        if (!data) {
            return false;
        }
        *(const char **)data = adapter.content_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        if (!data) {
            return false;
        }
        *(const char **)data = NULL;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        if (!data) {
            return false;
        }
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        if (!data) {
            return true;
        }
        struct retro_variable *variable = data;
        variable->value = adapter_option(variable->key);
        return variable->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (!data) {
            return false;
        }
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS_2:
        return true;
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        if (!data) {
            adapter.frame_time_set = 0;
            return true;
        }
        adapter.frame_time =
            *(const struct retro_frame_time_callback *)data;
        adapter.frame_time_set = adapter.frame_time.callback != NULL;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        if (!data) {
            return false;
        }
        adapter.av_info = *(const struct retro_system_av_info *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        if (!data) {
            return false;
        }
        adapter.av_info.geometry = *(const struct retro_game_geometry *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (!data) {
            return false;
        }
        *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!data) {
            return false;
        }
        ((struct retro_log_callback *)data)->log = adapter_log;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        if (!data) {
            return false;
        }
        *(enum retro_language *)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:
        if (adapter.host && adapter.host->request_quit) {
            adapter.host->request_quit(adapter.host->userdata);
        }
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        return false;
    default:
        return false;
    }
}

static void adapter_video(const void *data, unsigned width, unsigned height,
                          size_t pitch);
static void adapter_audio_sample(int16_t left, int16_t right);
static size_t adapter_audio_batch(const int16_t *data, size_t frames);
static void adapter_input_poll(void);
static int16_t adapter_input_state(unsigned port, unsigned device,
                                   unsigned index, unsigned id);

static vcos_result libretro_run_frame(void *ctx,
                                      const vcos_input_state *input,
                                      vcos_video_frame *video,
                                      vcos_audio_buffer *audio);
static vcos_result libretro_reset(void *ctx, int hard);
static vcos_result libretro_state_size(void *ctx, size_t *size);
static vcos_result libretro_state_save(void *ctx, void *data, size_t *size);
static vcos_result libretro_state_load(void *ctx, const void *data,
                                       size_t size);
static void libretro_destroy(void *ctx);

static const vcos_module_api libretro_api = {
    .abi = VCOS_ABI_INIT(vcos_module_api),
    .run_frame = libretro_run_frame,
    .reset = libretro_reset,
    .state_size = libretro_state_size,
    .state_save = libretro_state_save,
    .state_load = libretro_state_load,
    .destroy = libretro_destroy,
};

static const char *const module_extensions[] = {
    ".md", ".gen", ".smd", ".bin", NULL
};

static const vcos_system_info module_systems[] = {
    {
        .abi = VCOS_ABI_INIT(vcos_system_info),
        .id = "genesis",
        .name = "Sega Genesis / Mega Drive",
        .extensions = module_extensions,
        .extension_count = 4,
        .max_players = 2,
        .flags = VCOS_SYSTEM_FLAG_SAVE_STATES | VCOS_SYSTEM_FLAG_MULTIPLAYER,
    },
};

static const vcos_module_info module_info = {
    .abi = VCOS_ABI_INIT(vcos_module_info),
    .id = "org.vcos.genesis",
    .name = "Genesis Plus GX",
    .version = "1.0.0",
    .license = "Genesis Plus GX",
    .systems = module_systems,
    .system_count = 1,
    .system_stride = sizeof(module_systems[0]),
    .capabilities = VCOS_MODULE_CAP_SAVE_STATES | VCOS_MODULE_CAP_MULTIPLAYER,
    .max_width = 720,
    .max_height = 576,
    .fps_num = 60,
    .fps_den = 1,
    .sample_rate = 44100,
    .channels = 2,
};

static vcos_result libretro_create(const vcos_host_api *host,
                                   const vcos_content *content,
                                   const vcos_config *config,
                                   vcos_module_instance **out)
{
    memset(&adapter, 0, sizeof(adapter));
    adapter.host = host;
    adapter.content = content;
    adapter.config = config;
    adapter.pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
    adapter_setup_paths();

    retro_set_environment(adapter_environment);
    retro_set_video_refresh(adapter_video);
    retro_set_audio_sample(adapter_audio_sample);
    retro_set_audio_sample_batch(adapter_audio_batch);
    retro_set_input_poll(adapter_input_poll);
    retro_set_input_state(adapter_input_state);

    if (retro_api_version() != RETRO_API_VERSION) {
        return VCOS_ERR_VERSION;
    }

    retro_get_system_info(&adapter.core_info);
    retro_init();
    adapter.initialized = 1;

    struct retro_game_info game = {
        .path = content ? content->path : NULL,
        .data = NULL,
        .size = 0,
        .meta = NULL,
    };
    if (content && content->file_count > 0 &&
        !adapter.core_info.need_fullpath) {
        game.data = content->files[0].data;
        game.size = content->files[0].size;
    }

    if (!retro_load_game(content ? &game : NULL)) {
        retro_deinit();
        adapter.initialized = 0;
        return VCOS_ERR_IO;
    }
    adapter.loaded = 1;

    retro_get_system_av_info(&adapter.av_info);
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

    instance.abi = (vcos_abi_header)VCOS_ABI_INIT(vcos_module_instance);
    instance.api = &libretro_api;
    instance.ctx = &adapter;
    *out = &instance;
    return VCOS_OK;
}

static vcos_result libretro_run_frame(void *ctx,
                                      const vcos_input_state *input,
                                      vcos_video_frame *video,
                                      vcos_audio_buffer *audio)
{
    (void)ctx;
    adapter.input = input;
    adapter.video = video;
    adapter.audio = audio;
    if (audio) {
        audio->frames = 0;
        audio->sample_rate = 44100;
        audio->channels = 2;
        audio->format = VCOS_AUDIO_S16;
    }
    if (adapter.frame_time_set && adapter.frame_time.callback) {
        adapter.frame_time.callback(adapter.frame_time.reference);
    }
    retro_run();
    return VCOS_OK;
}

static vcos_result libretro_reset(void *ctx, int hard)
{
    (void)ctx;
    (void)hard;
    retro_reset();
    return VCOS_OK;
}

static vcos_result libretro_state_size(void *ctx, size_t *size)
{
    (void)ctx;
    if (!size) {
        return VCOS_ERR_INVALID;
    }
    *size = retro_serialize_size();
    return *size ? VCOS_OK : VCOS_ERR_UNSUPPORTED;
}

static vcos_result libretro_state_save(void *ctx, void *data, size_t *size)
{
    (void)ctx;
    if (!data || !size) {
        return VCOS_ERR_INVALID;
    }
    size_t required = retro_serialize_size();
    if (required == 0 || *size < required) {
        return VCOS_ERR_STATE;
    }
    if (!retro_serialize(data, required)) {
        return VCOS_ERR_STATE;
    }
    *size = required;
    return VCOS_OK;
}

static vcos_result libretro_state_load(void *ctx, const void *data,
                                       size_t size)
{
    (void)ctx;
    if (!data || size == 0) {
        return VCOS_ERR_INVALID;
    }
    return retro_unserialize(data, size) ? VCOS_OK : VCOS_ERR_STATE;
}

static void libretro_destroy(void *ctx)
{
    (void)ctx;
    if (adapter.loaded) {
        retro_unload_game();
        adapter.loaded = 0;
    }
    if (adapter.initialized) {
        retro_deinit();
        adapter.initialized = 0;
    }
}

static uint32_t adapter_expand5(uint32_t value)
{
    return (value * 255u + 15u) / 31u;
}

static uint32_t adapter_expand6(uint32_t value)
{
    return (value * 255u + 31u) / 63u;
}

static void adapter_video(const void *data, unsigned width, unsigned height,
                          size_t pitch)
{
    vcos_video_frame *frame = adapter.video;

    if (!frame || !frame->data || !data) {
        return;
    }
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        return;
    }
    if (width == 0 || height == 0 || pitch == 0) {
        return;
    }

    size_t destination_pitch = (size_t)width * 4u;
    if (destination_pitch / 4u != width) {
        return;
    }

    size_t used = destination_pitch * height;
    if (used / destination_pitch != height || used > frame->capacity) {
        return;
    }

    uint32_t *destination = (uint32_t *)frame->data;

    if (adapter.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
        if (pitch < destination_pitch) {
            return;
        }
        for (unsigned y = 0; y < height; y++) {
            const uint8_t *row = (const uint8_t *)data + y * pitch;
            memcpy(destination + (size_t)y * width, row, destination_pitch);
        }
    } else if (adapter.pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
        if (pitch < (size_t)width * 2u) {
            return;
        }
        for (unsigned y = 0; y < height; y++) {
            const uint8_t *row = (const uint8_t *)data + y * pitch;
            for (unsigned x = 0; x < width; x++) {
                uint16_t pixel;
                memcpy(&pixel, row + (size_t)x * 2u, sizeof(pixel));
                uint32_t r = adapter_expand5((pixel >> 11) & 0x1f);
                uint32_t g = adapter_expand6((pixel >> 5) & 0x3f);
                uint32_t b = adapter_expand5(pixel & 0x1f);
                destination[(size_t)y * width + x] =
                    (r << 16) | (g << 8) | b;
            }
        }
    } else if (adapter.pixel_format == RETRO_PIXEL_FORMAT_0RGB1555) {
        if (pitch < (size_t)width * 2u) {
            return;
        }
        for (unsigned y = 0; y < height; y++) {
            const uint8_t *row = (const uint8_t *)data + y * pitch;
            for (unsigned x = 0; x < width; x++) {
                uint16_t pixel;
                memcpy(&pixel, row + (size_t)x * 2u, sizeof(pixel));
                uint32_t r = adapter_expand5((pixel >> 10) & 0x1f);
                uint32_t g = adapter_expand5((pixel >> 5) & 0x1f);
                uint32_t b = adapter_expand5(pixel & 0x1f);
                destination[(size_t)y * width + x] =
                    (r << 16) | (g << 8) | b;
            }
        }
    } else {
        return;
    }

    frame->width = width;
    frame->height = height;
    frame->pitch = (uint32_t)destination_pitch;
    frame->format = VCOS_PIXEL_XRGB8888;
    frame->used = used;
}

static void adapter_audio_sample(int16_t left, int16_t right)
{
    vcos_audio_buffer *audio = adapter.audio;
    if (!audio || !audio->samples || audio->frames >= audio->capacity_frames) {
        return;
    }
    size_t channels = audio->channels ? audio->channels : 2;
    if (channels > 2) {
        channels = 2;
    }
    int16_t *out = audio->samples + audio->frames * channels;
    out[0] = left;
    if (channels > 1) {
        out[1] = right;
    }
    audio->frames++;
}

static size_t adapter_audio_batch(const int16_t *data, size_t frames)
{
    vcos_audio_buffer *audio = adapter.audio;
    if (!data || frames == 0) {
        return frames;
    }
    if (!audio || !audio->samples) {
        return frames;
    }

    size_t channels = audio->channels ? audio->channels : 2;
    if (channels > 2) {
        channels = 2;
    }
    size_t available = audio->capacity_frames > audio->frames ?
                       audio->capacity_frames - audio->frames : 0;
    size_t copy = frames < available ? frames : available;
    if (copy > 0) {
        memcpy(audio->samples + audio->frames * channels, data,
               copy * channels * sizeof(int16_t));
        audio->frames += copy;
    }
    return frames;
}

static const vcos_input_port *adapter_input_port(unsigned port)
{
    const vcos_input_state *state = adapter.input;
    if (!state || port >= state->port_count ||
        port >= VCOS_MAX_PORTS) {
        return NULL;
    }
    size_t stride = state->port_stride ? state->port_stride :
                    sizeof(vcos_input_port);
    return (const vcos_input_port *)((const uint8_t *)state->ports +
                                     stride * port);
}

static uint32_t adapter_button_mask(unsigned id)
{
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_A: return VCOS_BTN_A;
    case RETRO_DEVICE_ID_JOYPAD_B: return VCOS_BTN_B;
    case RETRO_DEVICE_ID_JOYPAD_X: return VCOS_BTN_X;
    case RETRO_DEVICE_ID_JOYPAD_Y: return VCOS_BTN_Y;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return VCOS_BTN_SELECT;
    case RETRO_DEVICE_ID_JOYPAD_START: return VCOS_BTN_START;
    case RETRO_DEVICE_ID_JOYPAD_UP: return VCOS_BTN_UP;
    case RETRO_DEVICE_ID_JOYPAD_DOWN: return VCOS_BTN_DOWN;
    case RETRO_DEVICE_ID_JOYPAD_LEFT: return VCOS_BTN_LEFT;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: return VCOS_BTN_RIGHT;
    case RETRO_DEVICE_ID_JOYPAD_L: return VCOS_BTN_L;
    case RETRO_DEVICE_ID_JOYPAD_R: return VCOS_BTN_R;
    case RETRO_DEVICE_ID_JOYPAD_L2: return VCOS_BTN_L2;
    case RETRO_DEVICE_ID_JOYPAD_R2: return VCOS_BTN_R2;
    case RETRO_DEVICE_ID_JOYPAD_L3: return VCOS_BTN_L3;
    case RETRO_DEVICE_ID_JOYPAD_R3: return VCOS_BTN_R3;
    default: return 0;
    }
}

static void adapter_input_poll(void)
{
}

static int16_t adapter_input_state(unsigned port, unsigned device,
                                   unsigned index, unsigned id)
{
    const vcos_input_port *input = adapter_input_port(port);
    if (!input) {
        return 0;
    }

    if (device == RETRO_DEVICE_JOYPAD) {
        uint32_t mask = adapter_button_mask(id);
        return (input->buttons & mask) ? 1 : 0;
    }

    if (device == RETRO_DEVICE_ANALOG) {
        unsigned axis = 0;
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            axis = 0;
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            axis = 2;
        } else {
            return 0;
        }
        if (id == RETRO_DEVICE_ID_ANALOG_X) {
            return input->axis[axis];
        }
        if (id == RETRO_DEVICE_ID_ANALOG_Y) {
            return input->axis[axis + 1];
        }
    }

    return 0;
}

static const vcos_module_descriptor descriptor = {
    .abi = VCOS_ABI_INIT(vcos_module_descriptor),
    .info = &module_info,
    .create = libretro_create,
};

VCOS_MODULE_EXPORT const vcos_module_descriptor *vcos_module_entry(void)
{
    return &descriptor;
}
