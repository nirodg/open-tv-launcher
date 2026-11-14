# Open TV OS — build, run, verify
# All build state lives in os/build/ (git-ignored). Sources: os/src + os/external.
SHELL := /bin/bash
OTV_ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD := $(OTV_ROOT)/build
DL := $(BUILD)/dl
SHARE := $(BUILD)/share

# Pinned Buildroot release (newest stable available at adoption; verify .sign for release builds)
BR_VER := 2025.11.3
BR_SHA := f31d6943d0210eea3e0ab03cf1df358fd7b5a12b320acf2063c533ef02510b83
BR_URL := https://buildroot.org/downloads/buildroot-$(BR_VER).tar.gz
BR_TAR := $(DL)/buildroot-$(BR_VER).tar.gz
BR_SRC := $(BUILD)/buildroot-$(BR_VER)
EXT := $(OTV_ROOT)/external

OUT_ARM := $(BUILD)/out-arm
OUT_X86 := $(BUILD)/out-x86

IMAGES_ARM := $(OUT_ARM)/images
IMAGES_X86 := $(OUT_X86)/images

# ── fetch ────────────────────────────────────────────────────────────────
$(BR_TAR):
	mkdir -p $(DL)
	curl -fL --retry 3 -o $@ $(BR_URL)
	echo "$(BR_SHA)  $@" | sha256sum -c -

$(BR_SRC)/Makefile: $(BR_TAR)
	sha256sum $<
	mkdir -p $(BUILD)
	tar -C $(BUILD) -xzf $<
	touch $(BR_SRC)/Makefile

fetch: $(BR_SRC)/Makefile

# ── configure (out of tree, external tree) ──────────────────────────────
config-arm: fetch
	$(MAKE) -C $(BR_SRC) O=$(OUT_ARM) BR2_EXTERNAL=$(EXT) \
		opentv_qemu_aarch64_virt_defconfig

config-x86: fetch
	$(MAKE) -C $(BR_SRC) O=$(OUT_X86) BR2_EXTERNAL=$(EXT) \
		opentv_qemu_x86_64_defconfig

config: config-arm config-x86

# ── build (incremental: re-run rebuilds only changed packages) ───────────
build-arm:
	$(MAKE) -C $(BR_SRC) O=$(OUT_ARM) -j$$(nproc) all

build-x86:
	$(MAKE) -C $(BR_SRC) O=$(OUT_X86) -j$$(nproc) all

build: build-arm build-x86

# Rebuild one package + reassemble image (fast app iteration in the guest).
# Local-site packages snapshot their sources at extract — dirclean both so
# gateway/launcher changes propagate (ui/*.c is compiled from the live tree).
app-arm:
	$(MAKE) -C $(BR_SRC) O=$(OUT_ARM) otv-gateway-dirclean otv-launcher-dirclean \
		otv-gateway otv-launcher all

app-x86:
	$(MAKE) -C $(BR_SRC) O=$(OUT_X86) otv-gateway-dirclean otv-launcher-dirclean \
		otv-gateway otv-launcher all

# ── run ──────────────────────────────────────────────────────────────────
# Interactive: needs a display (gtk). Headless variants write boot.log + monitor
# socket; use `make shot-<t>` for screenshots and os/tools/shot.py for scripted keys.
run-arm:
	qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 2 -m 1G \
		-kernel $(IMAGES_ARM)/Image \
		-append 'console=ttyAMA0 root=/dev/vda rw quiet' \
		-drive file=$(IMAGES_ARM)/rootfs.ext4,if=virtio,format=raw \
		-device virtio-gpu-pci,xres=1920,yres=1080 \
		-device virtio-keyboard-pci \
		-fsdev local,id=fs0,path=$(SHARE),security_model=none \
		-device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-display gtk,gl=off

run-arm-headless:
	qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 2 -m 1G \
		-kernel $(IMAGES_ARM)/Image \
		-append 'console=ttyAMA0 root=/dev/vda rw quiet' \
		-drive file=$(IMAGES_ARM)/rootfs.ext4,if=virtio,format=raw \
		-device virtio-gpu-pci,xres=1920,yres=1080 \
		-device virtio-keyboard-pci \
		-fsdev local,id=fs0,path=$(SHARE),security_model=none \
		-device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-display none \
		-serial file:$(BUILD)/boot-arm.log \
		-monitor unix:$(BUILD)/mon-arm.sock,server,nowait

run-x86:
	qemu-system-x86_64 -M q35 -enable-kvm -cpu host -smp 4 -m 2G -vga none \
		-kernel $(IMAGES_X86)/bzImage \
		-append 'console=ttyS0 root=/dev/vda rw quiet' \
		-drive file=$(IMAGES_X86)/rootfs.ext2,if=virtio,format=raw \
		-device virtio-gpu-pci,xres=1920,yres=1080 \
		-device virtio-keyboard-pci \
		-fsdev local,id=fs0,path=$(SHARE),security_model=none \
		-device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-display gtk,gl=off

run-x86-headless:
	qemu-system-x86_64 -M q35 -enable-kvm -cpu host -smp 4 -m 2G -vga none \
		-kernel $(IMAGES_X86)/bzImage \
		-append 'console=ttyS0 root=/dev/vda rw quiet' \
		-drive file=$(IMAGES_X86)/rootfs.ext2,if=virtio,format=raw \
		-device virtio-gpu-pci,xres=1920,yres=1080 \
		-device virtio-keyboard-pci \
		-fsdev local,id=fs0,path=$(SHARE),security_model=none \
		-device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-display none \
		-serial file:$(BUILD)/boot-x86.log \
		-monitor unix:$(BUILD)/mon-x86.sock,server,nowait

# ── screenshots (via QEMU monitor screendump; PPM→PNG with PIL) ──────────
shot-arm:
	python3 $(OTV_ROOT)/tools/shot.py $(BUILD)/mon-arm.sock $(BUILD)/shot-arm.png

shot-x86:
	python3 $(OTV_ROOT)/tools/shot.py $(BUILD)/mon-x86.sock $(BUILD)/shot-x86.png

# ── misc ─────────────────────────────────────────────────────────────────
clean:
	rm -rf $(OUT_ARM) $(OUT_X86)
distclean: clean
	rm -rf $(BUILD)

.PHONY: fetch config config-arm config-x86 build build-arm build-x86 app-arm \
	run-arm run-x86 run-arm-headless run-x86-headless shot-arm shot-x86 clean distclean
