#include "vcos_module.h"

#include <stdlib.h>
#include <string.h>

#include "apu.h"
#include "bus.h"
#include "cartridge.h"
#include "cpu6502.h"
#include "input.h"
#include "ppu.h"

#define NES_MODULE_ID "org.vcos.nes"
#define NES_MODULE_NAME "VCOS NES"
#define NES_MODULE_VERSION "1.0.0"
#define NES_STATE_MAGIC "NES1"
#define NES_STATE_VERSION 1u
#define NES_WIDTH 256u
#define NES_HEIGHT 240u
#define NES_RATE 44100u
#define NES_FPS 60u
#define NES_MAX_STEPS 500000

typedef struct {
    vcos_module_instance *instance;
    const vcos_host_api *host;
    cartridge *cart;
    ppu ppu;
    apu apu;
    input input;
    bus bus;
    uint64_t audio_acc;
    uint64_t frame_count;
} nes_ctx;

typedef struct {
    char magic[4];
    uint32_t version;
    uint64_t frame_count;
    uint64_t audio_acc;
    cpu6502 cpu;
    uint8_t ram[2048];
    ppu ppu;
    apu apu;
    input input;
    uint64_t cycles;
    int nmi_pending;
    int irq_pending;
    uint8_t prg_ram[8192];
    uint8_t chr_ram[8192];
    uint8_t prg_bank;
    uint8_t chr_bank;
    uint8_t mmc1_shift;
    uint8_t mmc1_control;
    uint8_t mmc1_chr0;
    uint8_t mmc1_chr1;
    uint8_t mmc1_prg;
    uint8_t mmc3_select;
    uint8_t mmc3_regs[8];
    uint8_t mmc3_irq_latch;
    uint8_t mmc3_irq_counter;
    uint8_t mmc3_irq_reload;
    uint8_t mmc3_irq_enabled;
    uint8_t mmc3_irq_pending;
} nes_state;

static const char *const nes_extensions[] = {".nes", NULL};

static const vcos_system_info nes_systems[] = {
    {
        .abi = {sizeof(vcos_system_info), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
        .id = "nes",
        .name = "Nintendo Entertainment System",
        .extensions = nes_extensions,
        .extension_count = 1,
        .max_players = 2,
        .flags = VCOS_SYSTEM_FLAG_SAVE_STATES | VCOS_SYSTEM_FLAG_MULTIPLAYER,
    },
    {
        .abi = {sizeof(vcos_system_info), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
        .id = "famicom",
        .name = "Famicom",
        .extensions = nes_extensions,
        .extension_count = 1,
        .max_players = 2,
        .flags = VCOS_SYSTEM_FLAG_SAVE_STATES | VCOS_SYSTEM_FLAG_MULTIPLAYER,
    },
};

static const vcos_module_info nes_info = {
    .abi = {sizeof(vcos_module_info), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
    .id = NES_MODULE_ID,
    .name = NES_MODULE_NAME,
    .version = NES_MODULE_VERSION,
    .license = "MIT",
    .systems = nes_systems,
    .system_count = 2,
    .system_stride = sizeof(vcos_system_info),
    .capabilities = VCOS_MODULE_CAP_SAVE_STATES | VCOS_MODULE_CAP_MULTIPLAYER,
    .max_width = NES_WIDTH,
    .max_height = NES_HEIGHT,
    .fps_num = NES_FPS,
    .fps_den = 1,
    .sample_rate = NES_RATE,
    .channels = 1,
};

static const vcos_content_file *content_file_at(const vcos_content *content,
                                                uint32_t index)
{
    if (!content || !content->files || index >= content->file_count) {
        return NULL;
    }
    size_t stride = content->file_stride ? content->file_stride :
                    sizeof(vcos_content_file);
    return (const vcos_content_file *)((const uint8_t *)content->files +
                                       stride * index);
}

static const uint8_t *main_content(const vcos_content *content, size_t *size)
{
    if (!content || !content->files || content->file_count == 0) {
        return NULL;
    }
    for (uint32_t i = 0; i < content->file_count; i++) {
        const vcos_content_file *file = content_file_at(content, i);
        if (file && (file->kind == VCOS_FILE_MAIN || content->file_count == 1)) {
            *size = file->size;
            return file->data;
        }
    }
    const vcos_content_file *file = content_file_at(content, 0);
    if (!file) {
        return NULL;
    }
    *size = file->size;
    return file->data;
}

static void map_input(const vcos_input_state *state, input *in)
{
    input_init(in);
    if (!state) {
        return;
    }
    size_t stride = state->port_stride ? state->port_stride :
                    sizeof(vcos_input_port);
    for (uint32_t i = 0; i < state->port_count && i < 2; i++) {
        const vcos_input_port *port =
            (const vcos_input_port *)((const uint8_t *)state->ports +
                                      stride * i);
        input_set(in, (int)i, (uint8_t)(port->buttons & 0xFFu));
    }
}

static vcos_result nes_run_frame(void *opaque,
                                 const vcos_input_state *input_state,
                                 vcos_video_frame *video,
                                 vcos_audio_buffer *audio)
{
    nes_ctx *ctx = opaque;
    if (!ctx || !video || !audio || !video->data || !audio->samples) {
        return VCOS_ERR_INVALID;
    }
    if (video->format != VCOS_PIXEL_XRGB8888) {
        return VCOS_ERR_UNSUPPORTED;
    }
    if (video->capacity < (size_t)NES_WIDTH * NES_HEIGHT * 4u) {
        return VCOS_ERR_NOMEM;
    }

    map_input(input_state, &ctx->input);

    ctx->ppu.frame_ready = 0;
    int steps = 0;
    while (!ctx->ppu.frame_ready && steps < NES_MAX_STEPS) {
        if (ctx->bus.nmi_pending) {
            ctx->bus.nmi_pending = 0;
            cpu_nmi(&ctx->bus.cpu, &ctx->bus);
        }
        if (ctx->bus.irq_pending && !(ctx->bus.cpu.p & CPU_FLAG_I)) {
            ctx->bus.irq_pending = 0;
            cpu_irq(&ctx->bus.cpu, &ctx->bus);
        }
        int cycles = cpu_step(&ctx->bus.cpu, &ctx->bus);
        bus_tick(&ctx->bus, cycles);
        steps++;
    }
    if (!ctx->ppu.frame_ready) {
        return VCOS_ERR_STATE;
    }

    video->width = NES_WIDTH;
    video->height = NES_HEIGHT;
    video->pitch = NES_WIDTH * 4u;
    video->used = (size_t)video->pitch * video->height;
    memcpy(video->data, ctx->ppu.fb, video->used);

    ctx->audio_acc += NES_RATE;
    uint32_t frames = (uint32_t)(ctx->audio_acc / NES_FPS);
    ctx->audio_acc %= NES_FPS;
    if (frames > audio->capacity_frames) {
        frames = (uint32_t)audio->capacity_frames;
    }
    if (frames > 0) {
        apu_render(&ctx->apu, audio->samples, (int)frames, NES_RATE);
    }
    audio->sample_rate = NES_RATE;
    audio->channels = 1;
    audio->format = VCOS_AUDIO_S16;
    audio->frames = frames;

    ctx->frame_count++;
    return VCOS_OK;
}

static vcos_result nes_reset(void *opaque, int hard)
{
    nes_ctx *ctx = opaque;
    if (!ctx) {
        return VCOS_ERR_INVALID;
    }
    if (hard) {
        bus_reset(&ctx->bus);
        ctx->audio_acc = 0;
        ctx->frame_count = 0;
    }
    return VCOS_OK;
}

static vcos_result nes_state_size(void *opaque, size_t *size)
{
    (void)opaque;
    if (!size) {
        return VCOS_ERR_INVALID;
    }
    *size = sizeof(nes_state);
    return VCOS_OK;
}

static vcos_result nes_state_save(void *opaque, void *data, size_t *size)
{
    nes_ctx *ctx = opaque;
    if (!ctx || !data || !size) {
        return VCOS_ERR_INVALID;
    }
    if (*size < sizeof(nes_state)) {
        *size = sizeof(nes_state);
        return VCOS_ERR_NOMEM;
    }

    nes_state state;
    memset(&state, 0, sizeof(state));
    memcpy(state.magic, NES_STATE_MAGIC, 4);
    state.version = NES_STATE_VERSION;
    state.frame_count = ctx->frame_count;
    state.audio_acc = ctx->audio_acc;
    state.cpu = ctx->bus.cpu;
    memcpy(state.ram, ctx->bus.ram, sizeof(state.ram));
    state.ppu = ctx->ppu;
    state.ppu.cart = NULL;
    state.apu = ctx->apu;
    state.input = ctx->input;
    state.cycles = ctx->bus.cycles;
    state.nmi_pending = ctx->bus.nmi_pending;
    state.irq_pending = ctx->bus.irq_pending;
    memcpy(state.prg_ram, ctx->cart->prg_ram, sizeof(state.prg_ram));
    if (ctx->cart->chr_is_ram && ctx->cart->chr &&
        ctx->cart->chr_size >= sizeof(state.chr_ram)) {
        memcpy(state.chr_ram, ctx->cart->chr, sizeof(state.chr_ram));
    }
    state.prg_bank = ctx->cart->prg_bank;
    state.chr_bank = ctx->cart->chr_bank;
    state.mmc1_shift = ctx->cart->mmc1_shift;
    state.mmc1_control = ctx->cart->mmc1_control;
    state.mmc1_chr0 = ctx->cart->mmc1_chr0;
    state.mmc1_chr1 = ctx->cart->mmc1_chr1;
    state.mmc1_prg = ctx->cart->mmc1_prg;
    state.mmc3_select = ctx->cart->mmc3_select;
    memcpy(state.mmc3_regs, ctx->cart->mmc3_regs, sizeof(state.mmc3_regs));
    state.mmc3_irq_latch = ctx->cart->mmc3_irq_latch;
    state.mmc3_irq_counter = ctx->cart->mmc3_irq_counter;
    state.mmc3_irq_reload = ctx->cart->mmc3_irq_reload;
    state.mmc3_irq_enabled = ctx->cart->mmc3_irq_enabled;
    state.mmc3_irq_pending = ctx->cart->mmc3_irq_pending;

    memcpy(data, &state, sizeof(state));
    *size = sizeof(state);
    return VCOS_OK;
}

static vcos_result nes_state_load(void *opaque, const void *data, size_t size)
{
    nes_ctx *ctx = opaque;
    if (!ctx || !data || size != sizeof(nes_state)) {
        return VCOS_ERR_INVALID;
    }

    nes_state state;
    memcpy(&state, data, sizeof(state));
    if (memcmp(state.magic, NES_STATE_MAGIC, 4) != 0 ||
        state.version != NES_STATE_VERSION) {
        return VCOS_ERR_VERSION;
    }

    ctx->frame_count = state.frame_count;
    ctx->audio_acc = state.audio_acc;
    ctx->bus.cpu = state.cpu;
    memcpy(ctx->bus.ram, state.ram, sizeof(state.ram));
    ctx->ppu = state.ppu;
    ctx->ppu.cart = ctx->cart;
    ctx->ppu.frame_ready = 0;
    ctx->apu = state.apu;
    ctx->input = state.input;
    ctx->bus.cycles = state.cycles;
    ctx->bus.nmi_pending = state.nmi_pending;
    ctx->bus.irq_pending = state.irq_pending;
    memcpy(ctx->cart->prg_ram, state.prg_ram, sizeof(state.prg_ram));
    if (ctx->cart->chr_is_ram && ctx->cart->chr &&
        ctx->cart->chr_size >= sizeof(state.chr_ram)) {
        memcpy(ctx->cart->chr, state.chr_ram, sizeof(state.chr_ram));
    }
    ctx->cart->prg_bank = state.prg_bank;
    ctx->cart->chr_bank = state.chr_bank;
    ctx->cart->mmc1_shift = state.mmc1_shift;
    ctx->cart->mmc1_control = state.mmc1_control;
    ctx->cart->mmc1_chr0 = state.mmc1_chr0;
    ctx->cart->mmc1_chr1 = state.mmc1_chr1;
    ctx->cart->mmc1_prg = state.mmc1_prg;
    ctx->cart->mmc3_select = state.mmc3_select;
    memcpy(ctx->cart->mmc3_regs, state.mmc3_regs, sizeof(state.mmc3_regs));
    ctx->cart->mmc3_irq_latch = state.mmc3_irq_latch;
    ctx->cart->mmc3_irq_counter = state.mmc3_irq_counter;
    ctx->cart->mmc3_irq_reload = state.mmc3_irq_reload;
    ctx->cart->mmc3_irq_enabled = state.mmc3_irq_enabled;
    ctx->cart->mmc3_irq_pending = state.mmc3_irq_pending;
    return VCOS_OK;
}

static void nes_destroy(void *opaque)
{
    nes_ctx *ctx = opaque;
    if (!ctx) {
        return;
    }
    if (ctx->cart) {
        cart_free(ctx->cart);
    }
    free(ctx->instance);
    free(ctx);
}

static const vcos_module_api nes_api = {
    .abi = {sizeof(vcos_module_api), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
    .run_frame = nes_run_frame,
    .reset = nes_reset,
    .state_size = nes_state_size,
    .state_save = nes_state_save,
    .state_load = nes_state_load,
    .destroy = nes_destroy,
};

static vcos_result nes_create(const vcos_host_api *host,
                              const vcos_content *content,
                              const vcos_config *config,
                              vcos_module_instance **out)
{
    (void)config;
    if (!host || !content || !out) {
        return VCOS_ERR_INVALID;
    }
    if (host->abi.major != VCOS_MODULE_ABI_MAJOR) {
        return VCOS_ERR_VERSION;
    }

    size_t size = 0;
    const uint8_t *data = main_content(content, &size);
    if (!data || size == 0) {
        return VCOS_ERR_INVALID;
    }

    nes_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return VCOS_ERR_NOMEM;
    }
    ctx->cart = cart_from_memory(data, size);
    if (!ctx->cart) {
        free(ctx);
        return VCOS_ERR_INVALID;
    }
    ppu_init(&ctx->ppu, ctx->cart);
    apu_init(&ctx->apu);
    input_init(&ctx->input);
    bus_init(&ctx->bus, ctx->cart, &ctx->ppu, &ctx->apu, &ctx->input, NULL);
    bus_reset(&ctx->bus);

    vcos_module_instance *instance = calloc(1, sizeof(*instance));
    if (!instance) {
        cart_free(ctx->cart);
        free(ctx);
        return VCOS_ERR_NOMEM;
    }
    instance->abi = (vcos_abi_header)VCOS_ABI_INIT(vcos_module_instance);
    instance->api = &nes_api;
    instance->ctx = ctx;
    ctx->instance = instance;
    ctx->host = host;
    *out = instance;
    return VCOS_OK;
}

static const vcos_module_descriptor nes_descriptor = {
    .abi = {sizeof(vcos_module_descriptor), VCOS_MODULE_ABI_MAJOR, VCOS_MODULE_ABI_MINOR},
    .info = &nes_info,
    .create = nes_create,
};

VCOS_MODULE_EXPORT const vcos_module_descriptor *vcos_module_entry(void)
{
    return &nes_descriptor;
}
