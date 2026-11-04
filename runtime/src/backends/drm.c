#define _GNU_SOURCE
#include "../backend.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../log.h"

#ifdef VCOS_WITH_DRM

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static int drm_fd = -1;
static uint32_t drm_fb;
static uint32_t drm_handle;
static uint32_t drm_pitch;
static uint64_t drm_size;
static void *drm_map;
static uint32_t drm_width;
static uint32_t drm_height;
static int drm_scale = 1;
static int drm_xoff;
static int drm_yoff;
static uint32_t drm_connector;
static drmModeCrtc *drm_saved_crtc;

static void drm_cleanup(void)
{
    if (drm_fd < 0) {
        return;
    }
    if (drm_saved_crtc) {
        drmModeSetCrtc(drm_fd, drm_saved_crtc->crtc_id,
                       drm_saved_crtc->buffer_id, drm_saved_crtc->x,
                       drm_saved_crtc->y, &drm_connector, 1,
                       &drm_saved_crtc->mode);
        drmModeFreeCrtc(drm_saved_crtc);
        drm_saved_crtc = NULL;
    }
    if (drm_fb) {
        drmModeRmFB(drm_fd, drm_fb);
        drm_fb = 0;
    }
    if (drm_map && drm_size) {
        munmap(drm_map, (size_t)drm_size);
        drm_map = NULL;
    }
    if (drm_handle) {
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = drm_handle;
        drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        drm_handle = 0;
    }
    drmDropMaster(drm_fd);
    close(drm_fd);
    drm_fd = -1;
}

static drmModeModeInfo pick_mode(drmModeConnector *conn)
{
    drmModeModeInfo best = conn->modes[0];
    for (int i = 0; i < conn->count_modes; i++) {
        drmModeModeInfo *mode = &conn->modes[i];
        if (mode->type & DRM_MODE_TYPE_PREFERRED) {
            return *mode;
        }
        if ((uint64_t)mode->hdisplay * mode->vdisplay >
            (uint64_t)best.hdisplay * best.vdisplay) {
            best = *mode;
        }
    }
    return best;
}

static int drm_init(int scale, const char *title)
{
    (void)title;
    drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        vcos_error("drm: cannot open /dev/dri/card0");
        return -1;
    }
    if (drmSetMaster(drm_fd) != 0) {
        vcos_error("drm: cannot become DRM master");
        drm_cleanup();
        return -1;
    }
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) {
        vcos_error("drm: cannot query resources");
        drm_cleanup();
        return -1;
    }

    drmModeConnector *chosen = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeModeInfo mode;
    uint32_t crtc_id = 0;
    for (int i = 0; i < res->count_connectors && !chosen; i++) {
        drmModeConnector *conn =
            drmModeGetConnector(drm_fd, res->connectors[i]);
        if (!conn) {
            continue;
        }
        if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
            drmModeFreeConnector(conn);
            continue;
        }
        if (conn->encoder_id) {
            encoder = drmModeGetEncoder(drm_fd, conn->encoder_id);
        }
        if (!encoder) {
            for (int e = 0; e < conn->count_encoders; e++) {
                encoder = drmModeGetEncoder(drm_fd, conn->encoders[e]);
                if (encoder) {
                    break;
                }
            }
        }
        if (!encoder) {
            drmModeFreeConnector(conn);
            continue;
        }
        crtc_id = encoder->crtc_id;
        if (!crtc_id && res->count_crtcs > 0) {
            crtc_id = res->crtcs[0];
        }
        if (!crtc_id) {
            drmModeFreeEncoder(encoder);
            encoder = NULL;
            drmModeFreeConnector(conn);
            continue;
        }
        chosen = conn;
        mode = pick_mode(conn);
    }
    if (!chosen) {
        vcos_error("drm: no connected display");
        drmModeFreeResources(res);
        drm_cleanup();
        return -1;
    }

    drm_width = mode.hdisplay;
    drm_height = mode.vdisplay;
    drm_connector = chosen->connector_id;

    drm_saved_crtc = drmModeGetCrtc(drm_fd, crtc_id);

    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));
    creq.width = drm_width;
    creq.height = drm_height;
    creq.bpp = 32;
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
        vcos_error("drm: cannot create dumb buffer");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(chosen);
        drmModeFreeResources(res);
        drm_cleanup();
        return -1;
    }
    drm_handle = creq.handle;
    drm_pitch = creq.pitch;
    drm_size = creq.size;

    if (drmModeAddFB(drm_fd, drm_width, drm_height, 24, 32, drm_pitch,
                     drm_handle, &drm_fb) != 0) {
        vcos_error("drm: cannot add framebuffer");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(chosen);
        drmModeFreeResources(res);
        drm_cleanup();
        return -1;
    }

    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = drm_handle;
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
        vcos_error("drm: cannot map dumb buffer");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(chosen);
        drmModeFreeResources(res);
        drm_cleanup();
        return -1;
    }
    drm_map = mmap(NULL, (size_t)drm_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, drm_fd, (off_t)mreq.offset);
    if (drm_map == MAP_FAILED) {
        drm_map = NULL;
        vcos_error("drm: mmap failed");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(chosen);
        drmModeFreeResources(res);
        drm_cleanup();
        return -1;
    }

    int max_scale = (int)(drm_width / 256);
    int max_scale_y = (int)(drm_height / 240);
    if (max_scale_y < max_scale) {
        max_scale = max_scale_y;
    }
    if (scale <= 0 || scale > max_scale) {
        scale = max_scale > 0 ? max_scale : 1;
    }
    drm_scale = scale;
    drm_xoff = ((int)drm_width - 256 * scale) / 2;
    drm_yoff = ((int)drm_height - 240 * scale) / 2;

    if (drmModeSetCrtc(drm_fd, crtc_id, drm_fb, 0, 0, &drm_connector, 1,
                       &mode) != 0) {
        vcos_error("drm: cannot set CRTC");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(chosen);
        drmModeFreeResources(res);
        drm_cleanup();
        return -1;
    }

    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(chosen);
    drmModeFreeResources(res);
    vcos_log("drm: %ux%u scale %d", drm_width, drm_height, scale);
    return 0;
}

static void drm_present(const uint32_t *fb, int width, int height)
{
    if (!drm_map) {
        return;
    }
    for (int y = 0; y < height * drm_scale; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)drm_map +
                                     (size_t)(drm_yoff + y) * drm_pitch);
        int sy = y / drm_scale;
        for (int x = 0; x < width * drm_scale; x++) {
            row[drm_xoff + x] = 0xFF000000u | fb[sy * width + x / drm_scale];
        }
    }
    (void)drmModeDirtyFB(drm_fd, drm_fb, NULL, 0);
}

static int drm_poll(input *in, int *quit)
{
    (void)in;
    (void)quit;
    return 0;
}

static void drm_shutdown(void)
{
    drm_cleanup();
}

const backend backend_drm = {
    "drm",
    drm_init,
    drm_present,
    drm_poll,
    drm_shutdown,
};

#else

static int drm_unavailable(int scale, const char *title)
{
    (void)scale;
    (void)title;
    vcos_error("drm backend was not compiled in (build with libdrm)");
    return -1;
}

static void no_present(const uint32_t *fb, int width, int height)
{
    (void)fb;
    (void)width;
    (void)height;
}

static int no_poll(input *in, int *quit)
{
    (void)in;
    (void)quit;
    return 0;
}

static void no_shutdown(void) {}

const backend backend_drm = {
    "drm",
    drm_unavailable,
    no_present,
    no_poll,
    no_shutdown,
};

#endif
