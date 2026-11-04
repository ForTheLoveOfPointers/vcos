################################################################################
#
# vcos-modules
#
################################################################################

VCOS_MODULES_VERSION = 1.0.0
VCOS_MODULES_SITE = $(BR2_EXTERNAL_VCOS_PATH)/../../modules
VCOS_MODULES_SITE_METHOD = local
VCOS_MODULES_LICENSE = MIT

define VCOS_MODULES_PRE_CONFIGURE
	mkdir -p $(@D)/runtime
	cp -a $(BR2_EXTERNAL_VCOS_PATH)/../../runtime/src $(@D)/runtime/src
	cp -a $(BR2_EXTERNAL_VCOS_PATH)/../../include $(@D)/include
endef
VCOS_MODULES_PRE_CONFIGURE_HOOKS += VCOS_MODULES_PRE_CONFIGURE

define VCOS_MODULES_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		RUNTIME="$(@D)/runtime/src" \
		INCLUDE="$(@D)/include" \
		BUILD="$(@D)/build"
endef

define VCOS_MODULES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/null.so \
		$(TARGET_DIR)/usr/lib/vcos/modules/null.so
	$(INSTALL) -D -m 0755 $(@D)/build/nes.so \
		$(TARGET_DIR)/usr/lib/vcos/modules/nes.so
endef

$(eval $(generic-package))
