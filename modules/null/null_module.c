#include "vcos_module.h"

#include <stdlib.h>
#include <string.h>

#define NULL_MODULE_ID "org.vcos.null"
#define NULL_MODULE_NAME "VCOS Null Test"
#define NULL_MODULE_VERSION "1.0.0"
#define NULL_MODULE_MAGIC "NUL1"
#define NULL_MODULE_STATE_VERSION 1u
#define NULL_MODULE_WIDTH 256u
#define NULL_MODULE_HEIGHT 240u
#define NULL_MODULE_RATE 44100u

typedef struct {
    vcos_module_instance *instance;
    const vcos_host_api *host;
    uint64_t frame;
    uint64_t tick;
} null_ctx;

typedef struct {
    char magic[4];
    uint32_t version;
    uint64_t frame;
    uint64_t tick;
} null_state;

static const char *const null_extensions[] = {".null", NULL};

static const vcos_system_info null_systems[] = {
    {
        .abi = {sizeof(vcos_system_info), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
        .id = "null",
        .name = "Null Test System",
        .extensions = null_extensions,
        .extension_count = 1,
        .max_players = 1,
        .flags = VCOS_SYSTEM_FLAG_SAVE_STATES,
    },
};

static const vcos_module_info null_info = {
    .abi = {sizeof(vcos_module_info), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
    .id = NULL_MODULE_ID,
    .name = NULL_MODULE_NAME,
    .version = NULL_MODULE_VERSION,
    .license = "MIT",
    .systems = null_systems,
    .system_count = 1,
    .system_stride = sizeof(vcos_system_info),
    .capabilities = VCOS_MODULE_CAP_SAVE_STATES,
    .max_width = NULL_MODULE_WIDTH,
    .max_height = NULL_MODULE_HEIGHT,
    .fps_num = 60,
    .fps_den = 1,
    .sample_rate = NULL_MODULE_RATE,
    .channels = 1,
};

static uint32_t null_pixel(uint32_t x, uint32_t y, uint64_t frame)
{
    uint32_t r = (x + (uint32_t)frame) & 0xFFu;
    uint32_t g = (y + (uint32_t)(frame * 2)) & 0xFFu;
    uint32_t b = (uint32_t)((x * 3 + y * 5 + frame * 7) & 0xFFu);
    return (r << 16) | (g << 8) | b;
}

static vcos_result null_run_frame(void *opaque,
                                  const vcos_input_state *input,
                                  vcos_video_frame *video,
                                  vcos_audio_buffer *audio)
{
    null_ctx *ctx = opaque;
    (void)input;

    if (!ctx || !video || !audio || !video->data) {
        return VCOS_ERR_INVALID;
    }
    if (video->format != VCOS_PIXEL_XRGB8888) {
        return VCOS_ERR_UNSUPPORTED;
    }
    if (video->capacity < (size_t)NULL_MODULE_WIDTH * NULL_MODULE_HEIGHT * 4u) {
        return VCOS_ERR_NOMEM;
    }

    video->width = NULL_MODULE_WIDTH;
    video->height = NULL_MODULE_HEIGHT;
    video->pitch = NULL_MODULE_WIDTH * 4u;
    video->used = (size_t)video->pitch * video->height;
    uint32_t *pixels = (uint32_t *)video->data;
    for (uint32_t y = 0; y < NULL_MODULE_HEIGHT; y++) {
        for (uint32_t x = 0; x < NULL_MODULE_WIDTH; x++) {
            pixels[(size_t)y * NULL_MODULE_WIDTH + x] =
                null_pixel(x, y, ctx->frame);
        }
    }

    audio->sample_rate = NULL_MODULE_RATE;
    audio->channels = 1;
    audio->format = VCOS_AUDIO_S16;
    audio->frames = 0;

    ctx->frame++;
    ctx->tick++;
    return VCOS_OK;
}

static vcos_result null_reset(void *opaque, int hard)
{
    null_ctx *ctx = opaque;
    if (!ctx) {
        return VCOS_ERR_INVALID;
    }
    if (hard) {
        ctx->frame = 0;
        ctx->tick = 0;
    }
    return VCOS_OK;
}

static vcos_result null_state_size(void *opaque, size_t *size)
{
    (void)opaque;
    if (!size) {
        return VCOS_ERR_INVALID;
    }
    *size = sizeof(null_state);
    return VCOS_OK;
}

static vcos_result null_state_save(void *opaque, void *data, size_t *size)
{
    null_ctx *ctx = opaque;
    if (!ctx || !data || !size) {
        return VCOS_ERR_INVALID;
    }
    if (*size < sizeof(null_state)) {
        *size = sizeof(null_state);
        return VCOS_ERR_NOMEM;
    }
    null_state state = {
        .version = NULL_MODULE_STATE_VERSION,
        .frame = ctx->frame,
        .tick = ctx->tick,
    };
    memcpy(state.magic, NULL_MODULE_MAGIC, 4);
    memcpy(data, &state, sizeof(state));
    *size = sizeof(state);
    return VCOS_OK;
}

static vcos_result null_state_load(void *opaque, const void *data, size_t size)
{
    null_ctx *ctx = opaque;
    if (!ctx || !data || size != sizeof(null_state)) {
        return VCOS_ERR_INVALID;
    }
    null_state state;
    memcpy(&state, data, sizeof(state));
    if (memcmp(state.magic, NULL_MODULE_MAGIC, 4) != 0 ||
        state.version != NULL_MODULE_STATE_VERSION) {
        return VCOS_ERR_VERSION;
    }
    ctx->frame = state.frame;
    ctx->tick = state.tick;
    return VCOS_OK;
}

static void null_destroy(void *opaque)
{
    null_ctx *ctx = opaque;
    if (!ctx) {
        return;
    }
    free(ctx->instance);
    free(ctx);
}

static const vcos_module_api null_api = {
    .abi = {sizeof(vcos_module_api), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
    .run_frame = null_run_frame,
    .reset = null_reset,
    .state_size = null_state_size,
    .state_save = null_state_save,
    .state_load = null_state_load,
    .destroy = null_destroy,
};

static vcos_result null_create(const vcos_host_api *host,
                               const vcos_content *content,
                               const vcos_config *config,
                               vcos_module_instance **out)
{
    (void)content;
    (void)config;
    if (!host || !out) {
        return VCOS_ERR_INVALID;
    }
    if (host->abi.major != VCOS_MODULE_ABI_MAJOR) {
        return VCOS_ERR_VERSION;
    }

    null_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return VCOS_ERR_NOMEM;
    }
    vcos_module_instance *instance = calloc(1, sizeof(*instance));
    if (!instance) {
        free(ctx);
        return VCOS_ERR_NOMEM;
    }
    instance->abi = (vcos_abi_header)VCOS_ABI_INIT(vcos_module_instance);
    instance->api = &null_api;
    instance->ctx = ctx;
    ctx->instance = instance;
    ctx->host = host;
    *out = instance;
    return VCOS_OK;
}

static const vcos_module_descriptor null_descriptor = {
    .abi = {sizeof(vcos_module_descriptor), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
    .info = &null_info,
    .create = null_create,
};

VCOS_MODULE_EXPORT const vcos_module_descriptor *vcos_module_entry(void)
{
    return &null_descriptor;
}
