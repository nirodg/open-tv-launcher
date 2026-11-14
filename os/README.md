# Open TV OS (working name — placeholder, see AGENTS.md §1)

A minimal, bootable Linux TV OS base: Buildroot + a frozen "gateway" port layer
(KMS/DRM display, evdev input, time, device-service mock) + our own UI runtime that
ports the `prototype/index.html` behavior to real pixels. Milestone 1 runs under
QEMU; real TV hardware is port #1+ later, implementing only the gateway interfaces.

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
make -C os run-arm-headless   # no window: boot.log + monitor socket
make -C os shot-arm       # screenshot via QEMU screendump (needs headless run active)
```

Keys (host keyboard emulates the remote): ↑↓←→ navigate · Enter = OK (hold 550 ms = options) · Esc/Backspace = Back · H = Home · M = Menu (quick panel) · +/− = volume · 0 = mute.

## Verify

Screenshots land in `screenshots/` (repo root) as `<timestamp>_<scope>.png`, one
batch per verification run — the user reviews them there. Host batches come from
keyscript tours; guest batches from QEMU screendumps (`os/tools/shot.py`, also
drives `--keys` D-pad tours through the monitor socket).

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
