################################################################################
#
# vcos-runtime
#
################################################################################

VCOS_RUNTIME_VERSION = 0.2.0
VCOS_RUNTIME_SITE = $(BR2_EXTERNAL_VCOS_PATH)/../../runtime
VCOS_RUNTIME_SITE_METHOD = local
VCOS_RUNTIME_LICENSE = MIT
VCOS_RUNTIME_DEPENDENCIES = alsa-lib libdrm

define VCOS_RUNTIME_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		X11=0 DRM=1 \
		DRM_CFLAGS="-I$(STAGING_DIR)/usr/include/libdrm" \
		DRM_LIBS="-ldrm" \
		BUILD="$(@D)/build" OUT="$(@D)/vcos"
endef

define VCOS_RUNTIME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/vcos $(TARGET_DIR)/usr/bin/vcos
endef

$(eval $(generic-package))
