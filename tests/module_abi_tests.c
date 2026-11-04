#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/vcos_module.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

_Static_assert(sizeof(vcos_abi_header) == 8, "abi header layout");
_Static_assert(offsetof(vcos_abi_header, size) == 0, "abi size offset");
_Static_assert(offsetof(vcos_abi_header, major) == 4, "abi major offset");
_Static_assert(offsetof(vcos_abi_header, minor) == 6, "abi minor offset");
_Static_assert(sizeof(vcos_result) == 4, "result width");
_Static_assert(offsetof(vcos_module_info, systems) < sizeof(vcos_module_info),
               "systems present");
_Static_assert(offsetof(vcos_module_api, run_frame) < sizeof(vcos_module_api),
               "run_frame present");

static void test_version(void)
{
    CHECK(VCOS_MODULE_ABI_MAJOR == 1u);
    CHECK(VCOS_MODULE_ABI_MINOR == 0u);
}

static void test_headers(void)
{
    vcos_abi_header header = VCOS_ABI_INIT(vcos_abi_header);
    CHECK(header.size == sizeof(vcos_abi_header));
    CHECK(header.major == VCOS_MODULE_ABI_MAJOR);
    CHECK(header.minor == VCOS_MODULE_ABI_MINOR);

    vcos_module_info info = {0};
    info.abi = (vcos_abi_header)VCOS_ABI_INIT(vcos_module_info);
    CHECK(info.abi.size == sizeof(info));
}

static void test_capabilities(void)
{
    vcos_module_info info = {0};
    info.capabilities = VCOS_MODULE_CAP_SAVE_STATES;
    CHECK(info.capabilities & VCOS_MODULE_CAP_SAVE_STATES);
    CHECK(!(info.capabilities & VCOS_MODULE_CAP_MULTIPLAYER));
    CHECK(VCOS_PIXEL_XRGB8888 == 1u);
    CHECK(VCOS_AUDIO_S16 == 1u);
}

static void test_systems(void)
{
    vcos_system_info systems[2] = {
        {.abi = VCOS_ABI_INIT(vcos_system_info), .id = "nes", .name = "NES"},
        {.abi = VCOS_ABI_INIT(vcos_system_info), .id = "famicom",
         .name = "Famicom"},
    };
    vcos_module_info info = {0};
    info.systems = systems;
    info.system_count = 2;
    info.system_stride = sizeof(vcos_system_info);
    CHECK(info.system_count == 2);
    CHECK(info.systems[1].id[0] == 'f');
}

int main(void)
{
    test_version();
    test_headers();
    test_capabilities();
    test_systems();
    puts("module ABI tests ok");
    return 0;
}
