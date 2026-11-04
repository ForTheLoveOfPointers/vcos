################################################################################
#
# vcos-modman
#
################################################################################

VCOS_MODMAN_VERSION = 1.0.0
VCOS_MODMAN_SITE = $(BR2_EXTERNAL_VCOS_PATH)/../../modman
VCOS_MODMAN_SITE_METHOD = local
VCOS_MODMAN_LICENSE = MIT
VCOS_MODMAN_DEPENDENCIES = openssl

define VCOS_MODMAN_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		LDLIBS="-lcrypto" \
		BUILD="$(@D)/build" \
		OUT="$(@D)/vcos-modman"
endef

define VCOS_MODMAN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/vcos-modman \
		$(TARGET_DIR)/usr/bin/vcos-modman
endef

$(eval $(generic-package))
