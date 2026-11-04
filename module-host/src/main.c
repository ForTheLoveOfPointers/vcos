#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "vcos_module.h"

#include "audio.h"
#include "backend.h"
#include "host_log.h"
#include "input.h"
#include "log.h"

#define MAX_CONTENT_FILES 32
#define MAX_VIDEO_BYTES (64u * 1024u * 1024u)
#define DEFAULT_LOG "/var/lib/vcos/logs/module-host.log"

typedef struct {
    const char *path;
    uint32_t kind;
} content_arg;

typedef struct {
    host_log *log;
    const char *module_id;
    volatile sig_atomic_t quit;
} host_ctx;

static volatile sig_atomic_t signal_quit;

static void on_signal(int sig)
{
    (void)sig;
    signal_quit = 1;
}

static void usage(const char *argv0)
{
    printf("usage: %s --module PATH --content PATH [options]\n", argv0);
    printf("\n");
    printf("  --module PATH       emulator module (.so)\n");
    printf("  --content PATH      main ROM, disc or content file\n");
    printf("  --file PATH         extra resource file (BIOS, data, disc)\n");
    printf("  --disc PATH         extra disc image\n");
    printf("  --system ID         system id to launch (default: first)\n");
    printf("  --backend NAME      x11, drm, fb, ppm or null\n");
    printf("  --scale N           integer upscale factor\n");
    printf("  --frames N          stop after N frames (0 = run forever)\n");
    printf("  --screenshot PATH   write final frame as PPM\n");
    printf("  --audio SINK        auto, none, alsa, or wav:PATH\n");
    printf("  --state PATH        save state on exit\n");
    printf("  --load-state PATH   load state before running\n");
    printf("  --log PATH          persistent log path\n");
    printf("  --config PATH       logging config (default /etc/vcos/logging.conf)\n");
    printf("  --no-kmsg           do not mirror events to /dev/kmsg\n");
    printf("  --no-pace           do not sleep between frames\n");
    printf("  --verbose           verbose runtime logging\n");
    printf("  --help              this message\n");
}

static const backend *select_backend(const char *name)
{
    if (name) {
        if (strcmp(name, "x11") == 0) return &backend_x11;
        if (strcmp(name, "drm") == 0) return &backend_drm;
        if (strcmp(name, "fb") == 0) return &backend_fb;
        if (strcmp(name, "ppm") == 0) return &backend_ppm;
        if (strcmp(name, "null") == 0) return &backend_null;
        return NULL;
    }
    if (getenv("DISPLAY")) {
        return &backend_x11;
    }
    if (access("/dev/dri/card0", R_OK | W_OK) == 0) {
        return &backend_drm;
    }
    if (access("/dev/fb0", R_OK | W_OK) == 0) {
        return &backend_fb;
    }
    return &backend_null;
}

static int abi_ok(const vcos_abi_header *header, size_t minimum)
{
    return header && header->size >= minimum &&
           header->major == VCOS_MODULE_ABI_MAJOR &&
           header->minor <= VCOS_MODULE_ABI_MINOR;
}

static const vcos_system_info *system_at(const vcos_module_info *info,
                                         uint32_t index)
{
    size_t stride = info->system_stride ? info->system_stride :
                    sizeof(vcos_system_info);
    return (const vcos_system_info *)((const uint8_t *)info->systems +
                                      stride * index);
}

static const vcos_system_info *choose_system(const vcos_module_info *info,
                                             const char *wanted)
{
    if (!info->systems || info->system_count == 0) {
        return NULL;
    }
    for (uint32_t i = 0; i < info->system_count; i++) {
        const vcos_system_info *system = system_at(info, i);
        if (!abi_ok(&system->abi,
                    offsetof(vcos_system_info, flags) + sizeof(uint32_t)) ||
            !system->id) {
            continue;
        }
        if (!wanted || strcmp(system->id, wanted) == 0) {
            return system;
        }
    }
    return NULL;
}

static int read_logging_config(const char *path, int *mirror)
{
    if (!path || !path[0] || strcmp(path, "none") == 0) {
        return 0;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\n' || *p == '\0') {
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = p;
        char *value = eq + 1;
        char *end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }
        while (*value == ' ' || *value == '\t') {
            value++;
        }
        end = value + strlen(value);
        while (end > value && (end[-1] == '\n' || end[-1] == '\r' ||
                               end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }
        if (strcmp(key, "mirror_kmsg") == 0 && mirror) {
            *mirror = atoi(value) != 0;
        }
    }
    fclose(file);
    return 0;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    uint8_t *data = malloc((size_t)length);
    if (!data && length > 0) {
        fclose(file);
        return NULL;
    }
    if (length > 0 && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static int write_file_atomic(const char *path, const void *data, size_t size)
{
    char tmp[1024];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -1;
    }
    FILE *file = fopen(tmp, "wb");
    if (!file) {
        return -1;
    }
    int ok = fwrite(data, 1, size, file) == size;
    if (ok) {
        fflush(file);
        fsync(fileno(file));
    }
    fclose(file);
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static void host_log_cb(void *userdata, int level, const char *message)
{
    host_ctx *ctx = userdata;
    if (!ctx || !ctx->log) {
        return;
    }
    host_log_args(ctx->log, level, "%s", message ? message : "");
}

static void host_quit_cb(void *userdata)
{
    host_ctx *ctx = userdata;
    if (ctx) {
        ctx->quit = 1;
    }
}

static void frame_wait(struct timespec *next, uint32_t fps_num,
                       uint32_t fps_den)
{
    uint64_t step = (1000000000ull * fps_den) / fps_num;
    if (step == 0) {
        step = 1;
    }
    next->tv_nsec += (long)step;
    while (next->tv_nsec >= 1000000000L) {
        next->tv_nsec -= 1000000000L;
        next->tv_sec++;
    }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next, NULL) ==
           EINTR) {
    }
}

static int load_save_state(const vcos_module_instance *instance,
                           host_log *log, const char *path)
{
    const vcos_module_api *api = instance->api;
    if (!api->state_load) {
        host_log(log, VCOS_LOG_WARN, "module does not support save states");
        return -1;
    }
    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    if (!data) {
        host_log(log, VCOS_LOG_ERROR, "cannot read state %s", path);
        return -1;
    }
    vcos_result result = api->state_load(instance->ctx, data, size);
    free(data);
    if (result != VCOS_OK) {
        host_log(log, VCOS_LOG_ERROR, "state load failed: %d", (int)result);
        return -1;
    }
    host_log(log, VCOS_LOG_INFO, "state loaded from %s", path);
    return 0;
}

static int save_save_state(const vcos_module_instance *instance,
                           host_log *log, const char *path)
{
    const vcos_module_api *api = instance->api;
    if (!api->state_size || !api->state_save) {
        host_log(log, VCOS_LOG_WARN, "module does not support save states");
        return -1;
    }
    size_t size = 0;
    vcos_result result = api->state_size(instance->ctx, &size);
    if (result != VCOS_OK || size == 0) {
        host_log(log, VCOS_LOG_ERROR, "state_size failed: %d", (int)result);
        return -1;
    }
    uint8_t *data = malloc(size);
    if (!data) {
        host_log(log, VCOS_LOG_ERROR, "state allocation failed");
        return -1;
    }
    size_t written = size;
    result = api->state_save(instance->ctx, data, &written);
    if (result == VCOS_OK && written > 0 &&
        write_file_atomic(path, data, written) == 0) {
        host_log(log, VCOS_LOG_INFO, "state saved to %s (%zu bytes)", path,
                 written);
        free(data);
        return 0;
    }
    free(data);
    host_log(log, VCOS_LOG_ERROR, "state save failed: %d", (int)result);
    return -1;
}

int main(int argc, char **argv)
{
    const char *module_path = NULL;
    const char *content_path = NULL;
    const char *system_name = NULL;
    const char *backend_name = NULL;
    const char *screenshot = NULL;
    const char *audio_sink = NULL;
    const char *state_path = NULL;
    const char *load_state_path = NULL;
    const char *log_path = NULL;
    const char *config_path = "/etc/vcos/logging.conf";
    content_arg extras[MAX_CONTENT_FILES];
    int extra_count = 0;
    int scale = 0;
    int frames = 0;
    int verbose = 0;
    int no_kmsg = 0;
    int no_pace = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(arg, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(arg, "--no-kmsg") == 0) {
            no_kmsg = 1;
        } else if (strcmp(arg, "--no-pace") == 0) {
            no_pace = 1;
        } else if (strcmp(arg, "--module") == 0 && i + 1 < argc) {
            module_path = argv[++i];
        } else if (strcmp(arg, "--content") == 0 && i + 1 < argc) {
            content_path = argv[++i];
        } else if ((strcmp(arg, "--file") == 0 ||
                    strcmp(arg, "--disc") == 0) && i + 1 < argc) {
            if (extra_count < MAX_CONTENT_FILES) {
                extras[extra_count].path = argv[++i];
                extras[extra_count].kind = strcmp(arg, "--disc") == 0 ?
                                           VCOS_FILE_DISC : VCOS_FILE_RESOURCE;
                extra_count++;
            } else {
                i++;
            }
        } else if (strcmp(arg, "--system") == 0 && i + 1 < argc) {
            system_name = argv[++i];
        } else if (strcmp(arg, "--backend") == 0 && i + 1 < argc) {
            backend_name = argv[++i];
        } else if (strcmp(arg, "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (strcmp(arg, "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (strcmp(arg, "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(arg, "--audio") == 0 && i + 1 < argc) {
            audio_sink = argv[++i];
        } else if (strcmp(arg, "--state") == 0 && i + 1 < argc) {
            state_path = argv[++i];
        } else if (strcmp(arg, "--load-state") == 0 && i + 1 < argc) {
            load_state_path = argv[++i];
        } else if (strcmp(arg, "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        } else if (strcmp(arg, "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else {
            fprintf(stderr, "vcos-module-host: unknown option %s\n", arg);
            usage(argv[0]);
            return 2;
        }
    }

    if (!module_path || !content_path) {
        usage(argv[0]);
        return 2;
    }

    vcos_verbose = verbose;
    int mirror_kmsg = 1;
    read_logging_config(config_path, &mirror_kmsg);
    if (no_kmsg) {
        mirror_kmsg = 0;
    }
    char default_log[512];
    if (!log_path) {
        if (access("/var/lib/vcos", W_OK) == 0) {
            mkdir("/var/lib/vcos/logs", 0755);
            snprintf(default_log, sizeof(default_log), "%s", DEFAULT_LOG);
        } else {
            snprintf(default_log, sizeof(default_log), "module-host.log");
        }
        log_path = default_log;
    }

    host_log *log = host_log_open(log_path, "module-host", mirror_kmsg);
    if (!log) {
        fprintf(stderr, "vcos-module-host: cannot open log\n");
        return 1;
    }
    host_log_install_crash_handler(log);
    host_log(log, VCOS_LOG_INFO, "loading %s", module_path);

    void *handle = dlopen(module_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        host_log(log, VCOS_LOG_ERROR, "dlopen failed: %s", dlerror());
        host_log_close(log);
        return 1;
    }

    typedef const vcos_module_descriptor *(*entry_fn)(void);
    entry_fn entry = NULL;
    *(void **)(&entry) = dlsym(handle, VCOS_MODULE_ENTRY_SYMBOL);
    if (!entry) {
        host_log(log, VCOS_LOG_ERROR, "missing %s: %s",
                 VCOS_MODULE_ENTRY_SYMBOL, dlerror());
        dlclose(handle);
        host_log_close(log);
        return 1;
    }

    const vcos_module_descriptor *descriptor = entry();
    if (!descriptor ||
        !abi_ok(&descriptor->abi,
                offsetof(vcos_module_descriptor, create) +
                sizeof(vcos_module_create_fn)) ||
        !descriptor->info) {
        host_log(log, VCOS_LOG_ERROR, "invalid module descriptor");
        dlclose(handle);
        host_log_close(log);
        return 1;
    }

    const vcos_module_info *info = descriptor->info;
    if (!abi_ok(&info->abi,
                offsetof(vcos_module_info, channels) + sizeof(uint32_t)) ||
        !info->id || !info->name || info->fps_num == 0 || info->fps_den == 0 ||
        info->max_width == 0 || info->max_height == 0 ||
        info->sample_rate == 0 || info->channels == 0 ||
        info->channels > 2) {
        host_log(log, VCOS_LOG_ERROR, "invalid module info");
        dlclose(handle);
        host_log_close(log);
        return 1;
    }
    size_t video_bytes = (size_t)info->max_width * info->max_height * 4u;
    if (video_bytes == 0 || video_bytes > MAX_VIDEO_BYTES) {
        host_log(log, VCOS_LOG_ERROR, "unsupported video size %ux%u",
                 info->max_width, info->max_height);
        dlclose(handle);
        host_log_close(log);
        return 1;
    }

    const vcos_system_info *system = choose_system(info, system_name);
    if (!system) {
        host_log(log, VCOS_LOG_ERROR, "no matching system%s%s",
                 system_name ? " " : "", system_name ? system_name : "");
        dlclose(handle);
        host_log_close(log);
        return 1;
    }
    host_log(log, VCOS_LOG_INFO, "module=%s version=%s system=%s",
             info->id, info->version ? info->version : "?", system->id);

    vcos_content_file files[MAX_CONTENT_FILES];
    memset(files, 0, sizeof(files));
    uint8_t *file_data[MAX_CONTENT_FILES] = {0};
    size_t main_size = 0;
    file_data[0] = read_file(content_path, &main_size);
    if (!file_data[0]) {
        host_log(log, VCOS_LOG_ERROR, "cannot read content %s", content_path);
        dlclose(handle);
        host_log_close(log);
        return 1;
    }
    files[0].abi = (vcos_abi_header)VCOS_ABI_INIT(vcos_content_file);
    files[0].name = base_name(content_path);
    files[0].data = file_data[0];
    files[0].size = main_size;
    files[0].kind = VCOS_FILE_MAIN;
    int file_count = 1;
    for (int i = 0; i < extra_count && file_count < MAX_CONTENT_FILES; i++) {
        size_t size = 0;
        uint8_t *data = read_file(extras[i].path, &size);
        if (!data) {
            host_log(log, VCOS_LOG_ERROR, "cannot read resource %s",
                     extras[i].path);
            for (int j = 0; j < file_count; j++) {
                free(file_data[j]);
            }
            dlclose(handle);
            host_log_close(log);
            return 1;
        }
        file_data[file_count] = data;
        files[file_count].abi =
            (vcos_abi_header)VCOS_ABI_INIT(vcos_content_file);
        files[file_count].name = base_name(extras[i].path);
        files[file_count].data = data;
        files[file_count].size = size;
        files[file_count].kind = extras[i].kind;
        file_count++;
    }

    vcos_content content = {
        .abi = {sizeof(vcos_content), VCOS_MODULE_ABI_MAJOR,
                VCOS_MODULE_ABI_MINOR},
        .system_id = system->id,
        .path = content_path,
        .file_count = (uint32_t)file_count,
        .file_stride = sizeof(vcos_content_file),
        .files = files,
    };

    host_ctx ctx = {
        .log = log,
        .module_id = info->id,
    };
    vcos_host_api host = {
        .abi = {sizeof(vcos_host_api), VCOS_MODULE_ABI_MAJOR,
                VCOS_MODULE_ABI_MINOR},
        .userdata = &ctx,
        .log = host_log_cb,
        .request_quit = host_quit_cb,
    };

    vcos_module_instance *instance = NULL;
    vcos_result result = descriptor->create(&host, &content, NULL, &instance);
    if (result != VCOS_OK || !instance || !instance->api ||
        !abi_ok(&instance->api->abi,
                offsetof(vcos_module_api, destroy) + sizeof(void *)) ||
        !instance->api->run_frame || !instance->api->destroy) {
        host_log(log, VCOS_LOG_ERROR, "module create failed: %d", (int)result);
        for (int i = 0; i < file_count; i++) {
            free(file_data[i]);
        }
        dlclose(handle);
        host_log_close(log);
        return 1;
    }

    if (load_state_path && load_save_state(instance, log, load_state_path) != 0) {
        instance->api->destroy(instance->ctx);
        for (int i = 0; i < file_count; i++) {
            free(file_data[i]);
        }
        dlclose(handle);
        host_log_close(log);
        return 1;
    }

    const backend *be = select_backend(backend_name);
    if (!be) {
        host_log(log, VCOS_LOG_ERROR, "unknown backend %s", backend_name);
        instance->api->destroy(instance->ctx);
        for (int i = 0; i < file_count; i++) {
            free(file_data[i]);
        }
        dlclose(handle);
        host_log_close(log);
        return 2;
    }
    if (strcmp(be->name, "ppm") == 0) {
        backend_ppm_set_output(screenshot ? screenshot : "module-host.ppm");
    }
    if (be->init(scale, info->name) != 0) {
        host_log(log, VCOS_LOG_WARN, "%s backend unavailable, using null",
                 be->name);
        be = &backend_null;
        if (be->init(scale, info->name) != 0) {
            host_log(log, VCOS_LOG_ERROR, "no usable backend");
            instance->api->destroy(instance->ctx);
            for (int i = 0; i < file_count; i++) {
                free(file_data[i]);
            }
            dlclose(handle);
            host_log_close(log);
            return 1;
        }
    }
    host_log(log, VCOS_LOG_INFO, "backend=%s", be->name);

    if (audio_open_ex(audio_sink ? audio_sink : "auto", (int)info->sample_rate,
                      (int)info->channels) != 0) {
        host_log(log, VCOS_LOG_WARN, "audio disabled");
    }

    vcos_video_frame video = {
        .abi = {sizeof(vcos_video_frame), VCOS_MODULE_ABI_MAJOR,
                VCOS_MODULE_ABI_MINOR},
        .format = VCOS_PIXEL_XRGB8888,
        .capacity = video_bytes,
    };
    video.data = malloc(video_bytes);
    size_t audio_capacity = (size_t)info->sample_rate * info->fps_den /
                            info->fps_num + 1024u;
    if (audio_capacity < 1024u) {
        audio_capacity = 1024u;
    }
    vcos_audio_buffer audio = {
        .abi = {sizeof(vcos_audio_buffer), VCOS_MODULE_ABI_MAJOR,
                VCOS_MODULE_ABI_MINOR},
        .capacity_frames = audio_capacity,
        .sample_rate = info->sample_rate,
        .channels = info->channels,
        .format = VCOS_AUDIO_S16,
    };
    audio.samples = malloc(audio_capacity * info->channels * sizeof(int16_t));
    if (!video.data || !audio.samples) {
        host_log(log, VCOS_LOG_ERROR, "buffer allocation failed");
        free(video.data);
        free(audio.samples);
        audio_close();
        be->shutdown();
        instance->api->destroy(instance->ctx);
        for (int i = 0; i < file_count; i++) {
            free(file_data[i]);
        }
        dlclose(handle);
        host_log_close(log);
        return 1;
    }

    input in;
    input_init(&in);
    input_evdev_init();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    vcos_input_state input_state = {
        .abi = {sizeof(vcos_input_state), VCOS_MODULE_ABI_MAJOR,
                VCOS_MODULE_ABI_MINOR},
        .port_count = system->max_players ? system->max_players : 1,
        .port_stride = sizeof(vcos_input_port),
    };
    if (input_state.port_count > VCOS_MAX_PORTS) {
        input_state.port_count = VCOS_MAX_PORTS;
    }
    if (input_state.port_count > 2) {
        input_state.port_count = 2;
    }

    int quit = 0;
    int rc = 0;
    uint64_t frame = 0;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    host_log(log, VCOS_LOG_INFO, "running content=%s", content_path);

    while (!quit && !signal_quit && !ctx.quit) {
        if (frames > 0 && frame >= (uint64_t)frames) {
            break;
        }
        if (be->poll) {
            be->poll(&in, &quit);
        }
        input_evdev_poll(&in);
        for (uint32_t i = 0; i < input_state.port_count; i++) {
            input_state.ports[i].buttons = in.buttons[i];
            memset(input_state.ports[i].axis, 0,
                   sizeof(input_state.ports[i].axis));
        }

        result = instance->api->run_frame(instance->ctx, &input_state, &video,
                                          &audio);
        if (result != VCOS_OK) {
            host_log(log, VCOS_LOG_ERROR, "run_frame failed: %d",
                     (int)result);
            rc = 1;
            break;
        }
        if (video.width == 0 || video.height == 0 ||
            video.pitch != video.width * 4u ||
            video.used > video.capacity ||
            (size_t)video.pitch * video.height > video.capacity) {
            host_log(log, VCOS_LOG_ERROR, "invalid video frame");
            rc = 1;
            break;
        }
        be->present((const uint32_t *)video.data, (int)video.width,
                    (int)video.height);
        if (audio.frames > 0) {
            audio_write(audio.samples, (int)audio.frames);
        }
        frame++;
        if (!no_pace) {
            frame_wait(&next, info->fps_num, info->fps_den);
        }
    }

    if (screenshot && strcmp(be->name, "ppm") != 0 && video.width > 0) {
        write_ppm(screenshot, (const uint32_t *)video.data, (int)video.width,
                  (int)video.height);
    }
    if (state_path) {
        save_save_state(instance, log, state_path);
    }
    host_log(log, VCOS_LOG_INFO, "stopped after %llu frames",
             (unsigned long long)frame);

    audio_close();
    be->shutdown();
    instance->api->destroy(instance->ctx);
    for (int i = 0; i < file_count; i++) {
        free(file_data[i]);
    }
    free(video.data);
    free(audio.samples);
    dlclose(handle);
    host_log_close(log);
    return rc;
}
