# Open TV OS (working name — placeholder, see AGENTS.md §1)

A minimal, bootable Linux TV OS base: Buildroot + a frozen "gateway" port layer
(KMS/DRM display, evdev input, time, device-service mock) + our own UI runtime that
ports the `prototype/index.html` behavior to real pixels. Milestone 1 runs under
QEMU. **Bringing it up on real ARM hardware? Start at
[../docs/PORTING_GUIDELINE.md](../docs/PORTING_GUIDELINE.md) — if the kernel
has DRM/KMS + evdev, the port is mostly a defconfig and a device.conf.**
The image ships `portdoctor` (`/usr/bin/portdoctor` on the guest) — run it
on the device first: all-PASS means the board is ready for the bring-up
ladder. The bring-up itself is an agent job:
[.cursor/agents/port-bringup.md](../.cursor/agents/port-bringup.md) defines the
role, `.cursor/skills/port-bringup/` its knowledge — both consume portdoctor's
output.

**This is the behavior/look reference chain:** `prototype/index.html` (behavior) →
AGENTS.md §5 design tokens → `renders/launcher/*.png` (look).

## Build

```sh
make -C os fetch        # pinned Buildroot 2025.11.3 into os/build/ (out of tree)
make -C os config       # apply our two defconfigs (arm32 virt + x86_64 dev)
make -C os build        # full build, incremental after that (~20–40 min first run)
```

Targets:
- `opentv_qemu_aarch64_virt_defconfig` — aarch64 Cortex-A53 on QEMU `-M virt` (PCI virtio-gpu/keyboard/9p)
- `opentv_qemu_x86_64_defconfig` — same userspace under KVM for fast dev/CI

## Run

```sh
make -C os run-arm        # interactive window (D-pad = arrows, OK = Enter, Back = Esc/Backspace)
make -C os run-arm-headless   # no window: boot.log + monitor + QMP sockets
make -C os shot-arm       # screenshot via QEMU screendump (needs headless run active)
```

Keys (host keyboard emulates the remote): ↑↓←→ navigate · Enter = OK (hold 550 ms = options) · Esc/Backspace = Back · H = Home · M = Menu (quick panel) · +/− = volume · 0 = mute.

**Mouse works too** (both run targets attach a virtio-tablet): in the interactive
window just move and click — hover moves focus, click activates, right-click
opens the item's options, and the UI draws a software cursor (the guest owns
the pointer with a tablet device, so nothing shows unless we draw it).
Headless, drive it with `os/tools/mouse.py` over the QMP socket
(`mouse.py os/build/qmp-x86.sock click 300 480`); HMP `mouse_move`
does not deliver to the tablet under `-display none`, QMP `input-send-event`
does.

## Verify

Screenshots land in `screenshots/` (repo root) as `<timestamp>_<scope>.png`, one
batch per verification run — the user reviews them there. Host batches come from
keyscript tours; guest batches from QEMU screendumps (`os/tools/shot.py`, also
drives `--keys` D-pad tours through the monitor socket).

## Device description — one file per device (Android-style)

The Open TV analogue of Android's board configs + HAL configuration files:
`/etc/otv/device.conf` names the gateway **backend** each service uses on the
device it ships with. The OS above the gateway never changes between devices —
only this file and the gateway backends it names.

```ini
[display]
backend = kms-dumb     ; CPU rasterizer: no GPU, no vendor blobs, everywhere
                       ; kms-gles2 = optional GBM/EGL GPU path (declared)

[audio]
backend = null         ; null (QEMU) | alsa | tinyalsa (real board later)

[network]
backend = mock         ; mock (QEMU simulates online state) | real

[storage]
internal = /media/internal
external = /media/external
```

**Rendering: software first, GPU optional.** The default is `kms-dumb` —
KMS/DRM dumb buffers + our CPU rasterizer — so nobody porting this to a TV
needs to reverse-engineer vendor GL libraries; software must always work.
The GPU path (`kms-gles2`, behind the same `gw_display` API) is the optional
upgrade for devices that already ship working GL libraries — open drivers
(panfrost, lima, v3d) or the vendor's own EGL. It is a display *backend*
swap, never a toolkit change. Until the shared backend lands (task 15), a
port may implement its own; any other backend value falls back to
`kms-dumb` with a clear log line. QEMU GPU passthrough (virtio-gpu `venus`/virgl + guest Mesa) will be
attached when that backend exists — the KVM x86 target is already seamless
with software rendering and page-flips.

The QEMU targets ship an **all-simulated** device (screen, audio, network,
storage); a real TV port writes its own `device.conf` and implements only the
gateway backends it names. The launcher logs the whole description at startup
(`otv: device …` lines). Host dev loops can point at a different file with
`OTV_DEVICE_CONF=…` (missing file = built-in defaults).

## Persistent test volume — /tmp/opentv

All run targets mount the host folder `/tmp/opentv` into the guest, so you can
test WITHOUT rebuilding (or asking an agent for anything):

```
/tmp/opentv/media   ↔  guest /media/internal   the TV's storage (Video app)
/tmp/opentv/usb     ↔  guest /media/external   the "USB drive"
/tmp/opentv/host    ↔  guest /mnt/host         binary-swap share (dev loop)
```

- The media folder is seeded with the sample clip on first boot; drop any
  `.mp4` into `/tmp/opentv/media` and it appears in the Video app the next
  time you open it (rescans on entry) — no rebuild, no reboot.
- Deleting a video in the app removes the file from the host folder.
- Everything survives reboots and image rebuilds (the folder is outside
  `os/build/`). Override the location with `make -C os VOL=/somewhere run-x86`.

The guest mounts the shares at boot (`S35media`); the run targets create and
seed the folders (`vol-prep`). A `/proc` mount was missing from the image
since the first boot (`S00mount` now runs `mount -a` from /etc/fstab) — that
is also why the network script used to print a sed warning.

The sample clip lives in the image as a SEED
(`usr/share/otv/samples/promo.mp4`): the run targets copy it into the
persistent volume on first boot (see above). Regenerate it with
`os/tools/make-samples.sh` (60 s, 480×270 H.264 + a quiet AAC track,
deterministic ffmpeg testsrc2). The player decodes it for real (FFmpeg libs);
durations come from the container.

## The Video app

A 1:1 port of the prototype's APPT.video (keep it that way for every future
app — the prototype is the behaviour source of truth):

- **Tabs**: Videos / Folders / Network / Settings (appBar tabs, VTABS).
- **Videos**: Continue watching + All videos grid, sort chip, real files from
  the device.conf storage dirs; hold OK (or right-click) a card for options
  (resume, watched, rename, delete — delete removes the real file).
- **Folders**: real folders from the scan; **Network**: the prototype's mock
  shares (SMB/DLNA) while device.conf says network=mock — clicking one shows
  the connecting → browse flow; **Settings**: resume/subtitles/skip prefs,
  hardware-decoding toggle, clear watch history.
- **Player** (videoView 1:1): auto-hiding UI (5 s), pause overlay, seek bar
  with knob (click anywhere on it to seek), subtitles pill, speed ×, picture
  size (Fit/Fill/Original), up-next card, back-10 / play / fwd-N / next.
  D-pad: ◀ ▶ seek, OK pause, Back returns (position saved → Continue watching).
- **Real playback**: the player decodes with the FFmpeg libraries
  (`ui/media.c`: libavformat/avcodec/swscale → BGRA frames, backward seeks
  jump to the keyframe). Durations come from the container; the sample
  promo.mp4 is 60 s. Audio is deferred to the audio backend (device.conf
  `[audio] backend = null`) — the track exists in the file but is not played.
- **Input**: D-pad AND mouse — hover moves focus, click activates, right-click
  opens options, the seek bar is clickable. The default focus on entering the
  app is the first content item (tabs are reachable with ▲), matching the
  prototype. Apps that are NOT implemented yet render gray-ish icons
  everywhere and still politely defer when opened.

## Fast UI iteration (host, no QEMU)

The launcher has a host mode using the same renderer code — the gateway swaps its
DRM/evdev backends for `display_null` + `input_host` (scripted key tours):

```sh
OTV_FONTS=os/assets/fonts OTV_ICONS=os/assets/icons \
OTV_KEYSCRIPT=<script> make -C os/src run        # keyscript: "<ms> <key>" steps + "shot <file>"
python3 os/tools/argb2png.py shot.argb shot.png  # .argb dumps (OTVS header) → PNG
```

The guest share `os/build/share/` ↔ `/mnt/host` allows pushing a rebuilt binary:
`cp out-arm/target/usr/bin/otv-launcher os/build/share/` then in the guest console
`cp /mnt/host/otv-launcher /usr/bin/ && killall otv-launcher` (init respawns it).

## Status / what works

See `/memory.md` (agent working memory) and `tasks/todo.md`. Screens done per
milestone are listed there with screenshots under `os/build/` (not committed).

## Layout

```
os/Makefile            build/run entry points
os/external/           Buildroot external tree (packages, board overlay, defconfigs)
os/src/gateway/        port layer (display/input/time/sys) — the only device-specific C
os/src/ui/             UI toolkit (tokens, rasterizer, font, scene+focus)
os/src/launcher/       launcher app (screens: home, all apps, settings, overlays)
os/tools/              icon render script, screenshot helpers
os/.gitignore          keeps the 10+ GB buildroot output (os/build/) out of git
```

License of the OS code: not yet chosen (AGENTS.md §8) — Apache-2.0 vs GPL-3.0
decision pending; third-party vendored bits keep their own licenses (stb = public
domain/MIT, Inter = SIL OFL 1.1, Buildroot/kernel/busybox = upstream licenses).
