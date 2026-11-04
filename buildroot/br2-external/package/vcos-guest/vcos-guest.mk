################################################################################
#
# vcos-guest
#
################################################################################

VCOS_GUEST_VERSION = 0.2.0
VCOS_GUEST_SITE = $(BR2_EXTERNAL_VCOS_PATH)/../../guest
VCOS_GUEST_SITE_METHOD = local
VCOS_GUEST_LICENSE = MIT
VCOS_GUEST_DEPENDENCIES = host-python3

define VCOS_GUEST_BUILD_CMDS
	cd $(@D) && $(HOST_DIR)/bin/python3 tools/build_roms.py \
		--out $(@D)/out --src $(@D)/src
endef

define VCOS_GUEST_INSTALL_TARGET_CMDS
	$(INSTALL) -d $(TARGET_DIR)/usr/share/vcos
	$(INSTALL) -m 0644 $(@D)/out/shell.nes $(TARGET_DIR)/usr/share/vcos/shell.nes
	$(INSTALL) -m 0644 $(@D)/out/demo.nes $(TARGET_DIR)/usr/share/vcos/demo.nes
	$(INSTALL) -m 0644 $(@D)/out/music.nes $(TARGET_DIR)/usr/share/vcos/music.nes
endef

$(eval $(generic-package))
