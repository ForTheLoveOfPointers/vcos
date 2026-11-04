################################################################################
#
# vcos-module-host
#
################################################################################

VCOS_MODULE_HOST_VERSION = 1.0.0
VCOS_MODULE_HOST_SITE = $(BR2_EXTERNAL_VCOS_PATH)/../../module-host
VCOS_MODULE_HOST_SITE_METHOD = local
VCOS_MODULE_HOST_LICENSE = MIT
VCOS_MODULE_HOST_DEPENDENCIES = libdrm

define VCOS_MODULE_HOST_PRE_CONFIGURE
	mkdir -p $(@D)/runtime
	rsync -a --delete $(BR2_EXTERNAL_VCOS_PATH)/../../runtime/src/ \
		$(@D)/runtime/src/
	rsync -a --delete $(BR2_EXTERNAL_VCOS_PATH)/../../include/ \
		$(@D)/include/
endef
VCOS_MODULE_HOST_PRE_CONFIGURE_HOOKS += VCOS_MODULE_HOST_PRE_CONFIGURE

define VCOS_MODULE_HOST_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		RUNTIME="$(@D)/runtime/src" \
		INCLUDE="$(@D)/include" \
		BUILD="$(@D)/build" \
		OUT="$(@D)/vcos-module-host" \
		X11=0 DRM=1 \
		DRM_CFLAGS="-I$(STAGING_DIR)/usr/include/libdrm" \
		DRM_LIBS="-ldrm"
endef

define VCOS_MODULE_HOST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/vcos-module-host \
		$(TARGET_DIR)/usr/bin/vcos-module-host
endef

$(eval $(generic-package))
