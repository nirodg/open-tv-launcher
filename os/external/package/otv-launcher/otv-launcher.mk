# Launcher app: UI toolkit + screens, linked against the gateway.
# All os/src/launcher/*.c compile automatically — add a screen file and rebuild.
OTV_LAUNCHER_VERSION = 0.1
OTV_LAUNCHER_SITE = $(BR2_EXTERNAL_OTV_PATH)/../src/launcher
OTV_LAUNCHER_SITE_METHOD = local
OTV_LAUNCHER_DEPENDENCIES = otv-gateway ffmpeg

OTV_UI_DIR := $(BR2_EXTERNAL_OTV_PATH)/../src/ui
OTV_ASSETS_DIR := $(BR2_EXTERNAL_OTV_PATH)/../assets

define OTV_LAUNCHER_BUILD_CMDS
	mkdir -p $(@D)/obj
	set -e; for f in $(OTV_UI_DIR)/*.c $(@D)/*.c; do \
		$(TARGET_CC) $(TARGET_CFLAGS) -c $$f -o $(@D)/obj/$$(basename $$f .c).o \
			-I$(OTV_UI_DIR) -I$(@D) -I$(STAGING_DIR)/usr/include; \
	done
	$(TARGET_CC) $(TARGET_LDFLAGS) $(@D)/obj/*.o -o $(@D)/otv-launcher \
		-L$(STAGING_DIR)/usr/lib -lgateway -ldrm \
		-lavformat -lavcodec -lswscale -lavutil -lm
endef

define OTV_LAUNCHER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/otv-launcher $(TARGET_DIR)/usr/bin/otv-launcher
	mkdir -p $(TARGET_DIR)/usr/share/otv/fonts $(TARGET_DIR)/usr/share/otv/icons
	for f in $(OTV_ASSETS_DIR)/fonts/*.ttf; do cp -r $$f $(TARGET_DIR)/usr/share/otv/fonts/; done
	for f in $(OTV_ASSETS_DIR)/icons/*.png; do cp -r $$f $(TARGET_DIR)/usr/share/otv/icons/; done
	cp -r $(OTV_ASSETS_DIR)/OFL.txt $(TARGET_DIR)/usr/share/otv/
endef

$(eval $(generic-package))
