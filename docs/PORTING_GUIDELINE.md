# Porting Open TV to your ARM device

This guide is for anyone bringing Open TV (working name — see AGENTS.md §1
about the trademark placeholder) to real ARM hardware: a TV box, a dev board,
an old Android TV stick. No vendor blobs are required — if your device has a
mainline (or even partially mainline) kernel with DRM/KMS and evdev, it can
run this.

The port is intentionally small. The OS is layered like this:

```
┌────────────────────────────────────────────────────────────┐
│ launcher app (home, apps, settings, video…)                │  device-agnostic
│ UI toolkit (CPU rasterizer, fonts, focus engine)           │  device-agnostic
├────────────────────────────────────────────────────────────┤
│ GATEWAY  os/src/gateway/                                   │  THE PORT LAYER
│   guest/display_drm.c   KMS/DRM scanout                    │  ← you touch this
│   guest/input_evdev.c   keyboard/remote/pointer in         │  ← rarely at all
│   time.c                clock                              │
│   device.c              /etc/otv/device.conf parser        │
├────────────────────────────────────────────────────────────┤
│ Linux kernel + DRM driver + evdev   (your board's kernel)  │
└────────────────────────────────────────────────────────────┘
```

Everything above the gateway line is untouched by a port. If you find
yourself changing launcher or toolkit code to support a board, something is
wrong — file an issue instead.

---

## 0. What your device needs (the whole list)

**Start with `portdoctor`.** The OS image ships it at `/usr/bin/portdoctor`;
on any other Linux on your board, copy `os/tools/portdoctor` over and run
`sh portdoctor`. It checks everything in the table below deterministically,
prints PASS/FAIL with a fix hint per failure, and exits non-zero until the
board is ready — agents misread hardware, a script doesn't. The bring-up is
an agent job: `.cursor/agents/port-bringup.md` (role + contract) drives it,
`.cursor/skills/port-bringup/` carries the knowledge, and both consume
portdoctor's output.

| Requirement | Why | How to check |
|---|---|---|
| ARMv7 or ARM64 | the toolchains we test | `uname -m` on the device |
| Kernel with DRM/KMS exposing `/dev/dri/card0` | display | `ls /dev/dri/` |
| A DRM driver that supports **dumb buffers** and at least one CRTC + connector | the rasterizer scans out a dumb buffer | `modetest -c` (from libdrm-tests) |
| evdev input device for the remote/keyboard | input | `ls /dev/input/`, `evtest` |
| ext4 root filesystem support | rootfs | — |
| ~256 MB RAM, ~700 MHz CPU | 1080p software UI + H.264 480p decode | — |

Notes on the display requirement: **no GPU is needed.** The UI is a CPU
rasterizer writing into KMS dumb buffers — this works on Mali/Adreno/video-
decoder SoCs without touching any vendor GL library, which is a design rule
of the project. Almost every mainline DRM driver supports dumb buffers; if
yours does not, that is the one place you may need a (small) kernel-side fix.

The panel mode does not have to be 1080p — the UI is designed at 1920×1080
and the gateway picks the connector's best mode; on other resolutions the
framebuffer stays 1920×1080 and the driver scales (or you letterbox), but
1080p-class panels are what we test.

---

## 1. The device description file — most ports are ONLY this

`/etc/otv/device.conf` (parsed by `os/src/gateway/device.c`) declares which
backend each service uses on your board. The QEMU targets ship a fully
simulated one; a real board's file looks like:

```ini
[device]
name = your-board-name
board = vendor/revision

[display]
backend = kms-dumb          ; the CPU rasterizer — works without a GPU

[audio]
backend = null              ; until the ALSA backend lands; audio is muted

[network]
backend = mock              ; mock = simulated online state (see below)

[storage]
internal = /storage         ; where the Video app looks for media
external = /media/usb       ; USB stick mount point (may be empty)
```

Put this file in the board overlay (section 3). If the file is missing the
OS runs with built-in defaults and logs it — useful for first boot.

**Storage paths**: the Video app scans `internal` and `external` for `.mp4`
files at app entry (durations are probed from the container itself — no
sidecar files). Point them at real mounts. On QEMU they are 9p shares from
the host's `/tmp/opentv`; on hardware use your actual partitions/USB mount.
`external` being empty/unmounted is fine — the app simply shows the internal
storage.

**network = mock** means the launcher shows a simulated-but-plausible network
state (the same one the reference prototype uses). A real netlink/Wi-Fi
backend is planned behind the same `[network]` key; until then the mock keeps
the UI honest about what is and isn't real on your port.

---

## 2. Build the image with Buildroot

Open TV builds as a Buildroot [external tree](../os/external/), pinned to
2025.11.3 (see `os/Makefile` for the tarball + sha256). You do not re-parent
the project — you add a defconfig for your board.

```
os/external/
├── configs/
│   ├── opentv_qemu_aarch64_virt_defconfig   ← reference aarch64 port
│   ├── opentv_qemu_x86_64_defconfig
│   └── opentv_yourboard_defconfig           ← you add this
├── package/otv-gateway/                     ← the port layer package
├── package/otv-launcher/                    ← the app package (links ffmpeg)
└── board/opentv/
    ├── overlay/                             ← rootfs overlay (init, device.conf)
    ├── linux-aarch64.config                 ← reference kernel config
    └── linux-virt.fragment
```

### 2.1 Write your defconfig

Start from Buildroot's defconfig for your board (they ship hundreds under
`configs/*_defconfig`) and append the Open TV block:

```ini
# ── Open TV OS ──────────────────────────────────────────────────────────
BR2_TARGET_GENERIC_HOSTNAME="opentv"
BR2_TARGET_GENERIC_ISSUE="Open TV OS (placeholder name) — development image"
BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_OTV_PATH)/board/opentv/overlay"
BR2_PACKAGE_LIBDRM=y
BR2_PACKAGE_OTV_LAUNCHER=y
# media: the Video app decodes with the FFmpeg libraries (LGPL build; the
# ffmpeg CLI tool is not needed in the image)
BR2_PACKAGE_FFMPEG=y
# BR2_PACKAGE_FFMPEG_FFMPEG is not set
BR2_PACKAGE_FFMPEG_SWSCALE=y
```

`BR2_PACKAGE_OTV_LAUNCHER` pulls in `otv-gateway` and `ffmpeg` automatically.
Buildroot quirks we hit (they will bite you too):

- **`BR2_PACKAGE_FFMPEG_SWSCALE` is a separate option that defaults off** —
  without it the launcher fails to find `<libswscale/swscale.h>`.
- **Changing your defconfig does not update an existing output dir.** Re-run
  `make config-<target>` after editing; and when you change *package options*
  (like enabling SWSCALE), also `make -C os/build/buildroot-<ver> O=<out> ffmpeg-dirclean`
  or the already-built ffmpeg keeps its old configuration.
- Use an **out-of-tree build** (`O=...`) — the whole setup depends on it.

### 2.2 Kernel config

Minimum (beyond your board's basics):

```
CONFIG_DRM=y                    # + your SoC's DRM driver (meson, rockchip, sun4i, imx-dcss, …)
CONFIG_DRM_KMS_HELPER=y
# dumb-buffer support is implied by DRM_KMS_HELPER
CONFIG_INPUT_EVDEV=y
CONFIG_EXT4_FS=y
```

If your remote goes through IR: `CONFIG_RC_CORE`, your SoC's receiver driver
(`CONFIG_IR_MESON`, `CONFIG_IR_ROCKCHIP`, …) and a keymap that emits standard
`KEY_UP/DOWN/LEFT/RIGHT/ENTER/BACK` codes (most vendor device trees already
do). Non-standard keys can be mapped in `os/src/gateway/guest/input_evdev.c`
→ `map_keycode()` — that is the one input-side change a port may need.

### 2.3 The overlay

The shared overlay (`os/external/board/opentv/overlay/`) carries the init
scripts and defaults. For a board-specific device.conf, either fork the
overlay in your defconfig (`BR2_ROOTFS_OVERLAY` can point at your own copy
of it) or add a second overlay dir — Buildroot applies several in order.

---

## 3. What the gateway expects from your kernel (and the traps)

The guest backends (`os/src/gateway/guest/`) are ~500 lines total and
deliberately conservative. They already handle real-hardware realities; read
this section before assuming you need to change them.

### Display (`display_drm.c`)

- Opens `/dev/dri/card0`, becomes DRM master, walks connectors/encoders/ CRTCs.
- **`encoder->possible_crtcs` is a BITMASK over the CRTC *index* in
  `res->crtcs[]` — NOT the CRTC object ID.** Matching by object ID is a
  classic porting bug (we shipped one); it happens to work on single-CRTC
  devices and explodes the moment a SoC has two. The code does it right —
  keep it that way if you refactor.
- Picks the connector's 1920×1080 mode if present, else mode 0. If your
  panel reports no modes (broken EDID over LVDS/DSI is common), it
  synthesises a 1080p60 mode — check `find_output()`.
- Headless boards with no connected connector still work: the gateway falls
  back to the first usable connector (a TV always drives its panel — see the
  comment in `find_output()`).
- Two dumb buffers are **page-flipped** — frames are drawn off-screen and
  swapped atomically, so the panel never sees a half-drawn frame. If flips
  are ever unsupported it falls back to draw-in-place + a full-frame
  `drmModeDirtyFB`.
- Pixel format is **XRGB8888** (word `0xFF000000 | r<<16 | g<<8 | b`).

### Input (`input_evdev.c`)

- Opens every `/dev/input/event*`; keeps devices that report arrow keys
  (keyboards, IR remotes) and absolute-position devices (touchpanels,
  tablets). Mice (relative) are ignored for now.
- **Drains the whole evdev queue on every poll.** If you "optimise" the
  read loop, keys wedge after the first burst — we shipped that bug once.
- Absolute pointers are scaled from the device's `EVIOCGABS(ABS_X)` range
  to the 1920×1080 UI coordinate space.
- Key map: arrows = D-pad, Enter = OK (hold 550 ms = options), Esc/Backspace
  = Back, H = Home, M = quick panel, volume keys, channel keys. Map your
  remote's codes in `map_keycode()`.

### Clock (`time.c` / `gw_now`)

Monotonic and **process-relative**. Do not "simplify" it to an absolute
monotonic value: the engine time is a float32, and at multi-day uptimes the
float ULP exceeds frame intervals — timers freeze. This is documented in the
code because it bit us.

---

## 4. Boot integration

The overlay's init is busybox `init`:

- `overlay/etc/inittab` respawns the launcher:
  `::respawn:/usr/bin/otv-launcher`. **The id field must stay EMPTY** —
  busybox treats a non-empty id as a tty device name and opens `/dev/<id>`,
  discarding the process output (that cost us an afternoon).
- `S00mount` runs `mount -a` (proc/sys/devpts/tmp/run from fstab). If you
  build your own rootfs from a vendor BSP that already mounts these, you can
  drop it — but keep /proc: mount checks, sysctl and the network scripts
  need it.- `S30hostshare`/`S35media` mount the QEMU 9p shares; both exit immediately
  on real hardware (no `/sys/bus/virtio/devices`). Replace `S35media` with
  real mounts matching your device.conf `[storage]` paths.

Boot the launcher after the display driver is up. If your DRM driver probes
late (deferred probe waiting on a regulator/bridge), the launcher exits with
"otv: no display" and busybox init keeps respawning it with a backoff —
usually it catches up. For boards where probe needs userspace (rare), gate
on a uevent before starting.

Serial console: keep one. Every diagnostic the system has prints to stderr
on the console — the `otv:` lines on boot tell you the whole device story:

```
otv: device your-board-name
otv:   device.name=your-board-name
otv:   display.backend=kms-dumb
otv:   storage.internal=/storage
otv: display backend kms-dumb (CPU rasterizer)
otv: page flip OK (double-buffered)
```

---

## 5. Testing ladder

Step zero on real hardware: `sh /usr/bin/portdoctor` (shipped in the image)
— it must report all-PASS before anything else matters. Its hints point at
the sections of this document.

1. **QEMU first.** Port your *kernel config* against
   `make -C os run-arm` before touching hardware; it catches display/input
   regressions in minutes. The QEMU target is the reference aarch64 port —
   if something breaks there, it breaks everywhere.
2. **Serial console on hardware.** Expect these in order: the `otv: device`
   summary, `display backend kms-dumb`, `page flip OK`. Anything else is the
   port bug you need to fix (see the table below).
3. **UI pass.** Home screen renders, D-pad moves focus, the Video app lists
   files from your `[storage]` paths and plays an mp4 end-to-end.
4. **Soak.** Leave it running a video for an hour; watch for flips failing
   (`page flip failed (…) — DIRTYFB fallback` appears once if so — the
   fallback keeps working, but flips should hold on a healthy driver).

### Symptom → cause table (all of these bit us)

| Symptom | Cause |
|---|---|
| `otv: no connected display found` | connector has no encoders, or `possible_crtcs` matched by object ID (real hardware: check `modetest -c`) |
| Black screen, no messages | connector mode list empty and the fallback disabled, or panel needs a bridge init your kernel isn't doing |
| UI visible but flashes on every key | page flips failing (check dmesg) — you're on the DIRTYFB fallback; fix the flip path, don't accept the flicker |
| Keys work, then die after a burst | evdev read loop not draining the queue |
| Launcher output invisible on serial | inittab respawn id is not empty |
| `sed: /proc/mounts: No such file` | nothing mounted /proc — ship S00mount or equivalent |
| Video plays but freezes after minutes | engine clock made absolute-monotonic (float precision) |
| Fonts garbled after ~20 items | font atlas overflow — the atlas is 1024²; if you add font sizes, check the packer result |

---

## 6. What is *not* ported yet (be honest in your port notes)

- **Audio**: `backend = null` — the player decodes video only. The ALSA
  backend lands behind the same `[audio]` key (task list: `tasks/todo.md`).
- **Network**: `mock` shows a simulated state; no real Wi-Fi/netlink yet.
- **GPU rendering**: `kms-gles2` is declared but not built; the CPU
  rasterizer is the supported path (and the point — no vendor blobs).
- **CEC, IR raw protocols**: planned gateway interfaces; today remotes must
  arrive as standard evdev keys.

A port with all of the above still honestly listed is a good port. Say what
works, what is mocked, and what is missing — that's the project's standard
(see how `os/README.md` describes the QEMU target).

---

## 7. Licence and naming notes for porters

- The OS code's licence is not chosen yet (AGENTS.md §8) — don't add
  `LICENSE` files to your port, and check that everything you enable in
  Buildroot is licence-compatible with a future choice. The FFmpeg build is
  configured LGPL (no GPL/nonfree options enabled); keep it that way unless
  you deliberately decide otherwise with the maintainers.
- "Open TV" is a placeholder — the name is a known trademark in the TV
  industry (AGENTS.md §1). Call your port anything you like in private, but
  don't publish store listings or domains under this name.

---

## 8. Checklist (print this)

```
[ ] portdoctor: all PASS (run /usr/bin/portdoctor on the device)
[ ] /dev/dri/card0 exists; modetest shows a connector with modes
[ ] /dev/input/event* exists; evtest shows your remote's keys
[ ] board defconfig with the Open TV block (FFMPEG + SWSCALE!)
[ ] kernel: DRM driver + EVDEV + EXT4
[ ] device.conf for your board (real storage paths)
[ ] boot log shows: device summary → kms-dumb → page flip OK
[ ] D-pad: focus moves; hold-OK opens options; Back behaves
[ ] Video app: lists your media, plays an mp4, seek/pause work
[ ] 1-hour video soak: no flip fallback, no memory growth
[ ] serial console kept enabled
```

Questions, ports and fixes are welcome — the QEMU aarch64 target
(`opentv_qemu_aarch64_virt_defconfig`) is the reference implementation for
everything described here.
