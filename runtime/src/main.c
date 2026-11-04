#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apu.h"
#include "audio.h"
#include "backend.h"
#include "bus.h"
#include "cartridge.h"
#include "input.h"
#include "log.h"
#include "ppu.h"
#include "services.h"

#define MAX_PRESS_EVENTS 32

static volatile sig_atomic_t signal_quit;

static void on_signal(int sig)
{
    (void)sig;
    signal_quit = 1;
}

typedef struct {
    int frame;
    uint8_t mask;
} press_event;

static void usage(const char *argv0)
{
    printf("usage: %s [options]\n", argv0);
    printf("\n");
    printf("  --rom PATH          cartridge to boot (default: <lib>/shell.nes)\n");
    printf("  --shell PATH        cartridge used for return-to-shell\n");
    printf("  --lib DIR           cartridge library directory (default: .)\n");
    printf("  --data DIR          save-data directory (default: --lib)\n");
    printf("  --backend NAME      x11, drm, fb, ppm or null (default: auto)\n");
    printf("  --scale N           integer upscale factor (default: backend best)\n");
    printf("  --frames N          stop after N frames (0 = run forever)\n");
    printf("  --screenshot PATH   write final frame as PPM\n");
    printf("  --audio SINK        auto, none, alsa, or wav:PATH (default: auto)\n");
    printf("  --on-shutdown ACT   exit, poweroff or reboot (default: exit)\n");
    printf("  --press N:BUTTON    inject a controller press at frame N\n");
    printf("                      buttons: a b select start up down left right\n");
    printf("  --verbose           verbose logging\n");
    printf("  --help              this message\n");
}

static void dirname_of(const char *path, char *out, size_t len)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, len, ".");
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n == 0) {
        n = 1;
    }
    if (n >= len) {
        n = len - 1;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

static uint8_t parse_button(const char *name)
{
    if (strcmp(name, "a") == 0) return VCOS_BTN_A;
    if (strcmp(name, "b") == 0) return VCOS_BTN_B;
    if (strcmp(name, "select") == 0) return VCOS_BTN_SELECT;
    if (strcmp(name, "start") == 0) return VCOS_BTN_START;
    if (strcmp(name, "up") == 0) return VCOS_BTN_UP;
    if (strcmp(name, "down") == 0) return VCOS_BTN_DOWN;
    if (strcmp(name, "left") == 0) return VCOS_BTN_LEFT;
    if (strcmp(name, "right") == 0) return VCOS_BTN_RIGHT;
    return 0;
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

static int switch_cart(const char *path, cartridge **cart, ppu *p, bus *b)
{
    cartridge *next = cart_load(path);
    if (!next) {
        return -1;
    }
    cart_free(*cart);
    *cart = next;
    ppu_init(p, next);
    b->cart = next;
    bus_reset(b);
    return 0;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL;
    const char *shell_path = NULL;
    const char *lib_dir = NULL;
    const char *data_dir = NULL;
    const char *backend_name = NULL;
    const char *screenshot = NULL;
    const char *audio_sink = NULL;
    const char *on_shutdown = "exit";
    int scale = 0;
    int frames = 0;
    int verbose = 0;
    press_event presses[MAX_PRESS_EVENTS];
    int press_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(arg, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(arg, "--rom") == 0 && i + 1 < argc) {
            rom_path = argv[++i];
        } else if (strcmp(arg, "--shell") == 0 && i + 1 < argc) {
            shell_path = argv[++i];
        } else if (strcmp(arg, "--lib") == 0 && i + 1 < argc) {
            lib_dir = argv[++i];
        } else if (strcmp(arg, "--data") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
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
        } else if (strcmp(arg, "--on-shutdown") == 0 && i + 1 < argc) {
            on_shutdown = argv[++i];
        } else if (strcmp(arg, "--press") == 0 && i + 1 < argc && press_count < MAX_PRESS_EVENTS) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s", argv[++i]);
            char *colon = strchr(buf, ':');
            if (colon) {
                *colon = '\0';
                presses[press_count].frame = atoi(buf);
                presses[press_count].mask = parse_button(colon + 1);
                if (presses[press_count].mask != 0) {
                    press_count++;
                }
            }
        } else {
            fprintf(stderr, "vcos: unknown option %s\n", arg);
            usage(argv[0]);
            return 2;
        }
    }

    vcos_verbose = verbose;

    char lib_buf[512];
    if (!lib_dir) {
        if (rom_path) {
            dirname_of(rom_path, lib_buf, sizeof(lib_buf));
        } else {
            snprintf(lib_buf, sizeof(lib_buf), ".");
        }
        lib_dir = lib_buf;
    }

    char rom_buf[600];
    if (!rom_path) {
        snprintf(rom_buf, sizeof(rom_buf), "%s/shell.nes", lib_dir);
        rom_path = rom_buf;
    }
    char shell_buf[600];
    if (!shell_path) {
        snprintf(shell_buf, sizeof(shell_buf), "%s/shell.nes", lib_dir);
        shell_path = shell_buf;
    }

    if (lib_dir[0] != '\0' && strcmp(lib_dir, ".") != 0) {
        mkdir(lib_dir, 0755);
    }
    if (!data_dir) {
        data_dir = lib_dir;
    }
    if (data_dir[0] != '\0' && strcmp(data_dir, ".") != 0) {
        mkdir(data_dir, 0755);
    }

    cartridge *cart = cart_load(rom_path);
    if (!cart) {
        return 1;
    }

    ppu p;
    apu a;
    input in;
    services svc;
    bus b;
    ppu_init(&p, cart);
    apu_init(&a);
    input_init(&in);
    services_init(&svc, shell_path, lib_dir, data_dir, verbose);
    bus_init(&b, cart, &p, &a, &in, &svc);
    bus_reset(&b);

    const backend *be = select_backend(backend_name);
    if (!be) {
        vcos_error("unknown backend %s", backend_name);
        cart_free(cart);
        return 2;
    }

    if (strcmp(be->name, "ppm") == 0) {
        backend_ppm_set_output(screenshot ? screenshot : "vcos.ppm");
    }
    if (be->init(scale, "VCOS") != 0) {
        const backend *fallbacks[3];
        int count = 0;
        if (be != &backend_drm) {
            fallbacks[count++] = &backend_drm;
        }
        if (be != &backend_fb) {
            fallbacks[count++] = &backend_fb;
        }
        fallbacks[count++] = &backend_null;
        const backend *failed = be;
        be = NULL;
        for (int i = 0; i < count; i++) {
            vcos_error("%s unavailable, trying %s", failed->name,
                       fallbacks[i]->name);
            if (fallbacks[i]->init(scale, "VCOS") == 0) {
                be = fallbacks[i];
                break;
            }
        }
        if (!be) {
            fprintf(stderr, "vcos: no usable backend\n");
            cart_free(cart);
            return 1;
        }
    }

    vcos_log("system: backend=%s rom=%s lib=%s", be->name, rom_path, lib_dir);

    input_evdev_init();

    if (audio_open(audio_sink ? audio_sink : "auto") != 0) {
        vcos_error("system: audio disabled");
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int quit = 0;
    int crash_count = 0;
    uint64_t frame = 0;
    int16_t audio_buf[VCOS_AUDIO_FRAME_SAMPLES];
    int audio_on = audio_active();

    while (!quit && !signal_quit) {
        if (frames > 0 && frame >= (uint64_t)frames) {
            break;
        }

        p.frame_ready = 0;
        while (!p.frame_ready) {
            if (b.nmi_pending) {
                b.nmi_pending = 0;
                cpu_nmi(&b.cpu, &b);
            }
            if (b.irq_pending && !(b.cpu.p & CPU_FLAG_I)) {
                b.irq_pending = 0;
                cpu_irq(&b.cpu, &b);
            }
            int cycles = cpu_step(&b.cpu, &b);
            bus_tick(&b, cycles);
            if (b.cpu.pc < 0x6000 && b.cart && b.cycles > 1000) {
                vcos_error("cpu: runaway pc=%04X, recovering to shell", b.cpu.pc);
                crash_count++;
                if (crash_count > 3 ||
                    switch_cart(shell_path, &cart, &p, &b) != 0) {
                    quit = 1;
                }
                b.cycles = 0;
            }
        }

        frame++;

        be->present(p.fb, 256, 240);
        if (audio_on) {
            apu_render(&a, audio_buf, VCOS_AUDIO_FRAME_SAMPLES, VCOS_AUDIO_RATE);
            audio_write(audio_buf, VCOS_AUDIO_FRAME_SAMPLES);
        }
        if (be->poll) {
            be->poll(&in, &quit);
        }
        input_evdev_poll(&in);

        for (int i = 0; i < press_count; i++) {
            if ((int)frame == presses[i].frame) {
                input_set(&in, 0, presses[i].mask);
            } else if ((int)frame == presses[i].frame + 4) {
                input_set(&in, 0, 0);
            }
        }

        char next_path[600];
        int request = services_take_request(&svc, next_path, sizeof(next_path));
        if (request == VCOS_SVC_REQUEST_SHUTDOWN) {
            vcos_log("system: shutdown requested");
            break;
        }
        if (request == VCOS_SVC_REQUEST_REBOOT) {
            vcos_log("system: reboot requested");
            if (strcmp(on_shutdown, "exit") != 0) {
                on_shutdown = "reboot";
            }
            break;
        }
        if (request == VCOS_SVC_REQUEST_LOAD) {
            vcos_log("system: loading %s", next_path);
            if (switch_cart(next_path, &cart, &p, &b) != 0) {
                vcos_error("system: cannot load %s, keeping current cartridge", next_path);
            }
        }
    }

    if (screenshot && strcmp(be->name, "ppm") != 0) {
        write_ppm(screenshot, p.fb, 256, 240);
    }

    services_flush(&svc);
    be->shutdown();
    audio_close();
    cart_free(cart);
    vcos_log("system: stopped after %llu frames", (unsigned long long)frame);

    if (signal_quit) {
        return 0;
    }
    if (strcmp(on_shutdown, "poweroff") == 0) {
        int rc = system("sync");
        (void)rc;
        return system("poweroff") == 0 ? 0 : 1;
    }
    if (strcmp(on_shutdown, "reboot") == 0) {
        int rc = system("sync");
        (void)rc;
        return system("reboot") == 0 ? 0 : 1;
    }
    return 0;
}
