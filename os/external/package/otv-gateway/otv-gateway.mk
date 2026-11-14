# Gateway port layer — the only device-specific code in the OS.
# Layout: include/ (public API), time.c (generic), guest/ (guest-only backends),
# hostdev/ (host dev-iteration backends, never built into the OS image).
# License of OS code: TBD (AGENTS.md §8); upstream deps keep their own licenses.
OTV_GATEWAY_VERSION = 0.1
OTV_GATEWAY_SITE = $(BR2_EXTERNAL_OTV_PATH)/../src/gateway
OTV_GATEWAY_SITE_METHOD = local
OTV_GATEWAY_INSTALL_STAGING = YES
OTV_GATEWAY_DEPENDENCIES = libdrm

define OTV_GATEWAY_BUILD_CMDS
	mkdir -p $(@D)/obj
	set -e; for f in $(@D)/time.c $(@D)/guest/*.c; do \
		$(TARGET_CC) $(TARGET_CFLAGS) -c $$f -o $(@D)/obj/$$(basename $$f .c).o \
			-I$(@D)/include -I$(STAGING_DIR)/usr/include/drm; \
	done
	$(TARGET_AR) rcs $(@D)/libgateway.a $(@D)/obj/*.o
endef

define OTV_GATEWAY_INSTALL_STAGING_CMDS
	mkdir -p $(STAGING_DIR)/usr/include
	cp -r $(@D)/include/gateway $(STAGING_DIR)/usr/include/
	$(INSTALL) -D -m 0644 $(@D)/libgateway.a $(STAGING_DIR)/usr/lib/libgateway.a
endef

$(eval $(generic-package))
