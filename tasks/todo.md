# Tasks: os-build (bootable QEMU TV OS, first slice)

- [x] Task 1: Scaffold os/ tree + pinned Buildroot fetch (Makefile, .gitignore, README, external tree skeleton)
- [x] Task 2: defconfigs (aarch64 virt primary, x86_64 dev) + kernel fragment + overlay + stub packages
- [x] Task 3: First full build (background) + boot smoke both targets
- [x] Task 4: Gateway: display DRM, input evdev, time, sys mock + smoke bars screenshot
- [x] Task 5: UI toolkit: tokens, rasterizer, font cache, anim, scene graph + focus engine port
- [x] Task 6: Assets: render-icons.cjs (sprite icons → PNG) + Inter TTF vendored (OFL)
- [x] Task 7: Home screen (clock, chips, featured tiles, pins, all-apps, key hints)
- [x] Task 8: All apps screen (8-col grid + filter chips)
- [x] Task 9: Settings (sidebar, tog/opt/sld/act/info rows, picker) + toast + quick panel + dialog
- [x] Task 10: Screenshot comparison vs renders/ + fix drift (host look pass + guest verify; screenshots/ per run)
- [ ] Task 11: Commits, push branch os-build, open PR (no merge), update memory.md
- [x] Task 12: x86_64 target boot smoke (KVM fast-dev path)
- [x] Task 13: Device description file (`/etc/otv/device.conf`) + render-backend switch (kms-dumb default, kms-gles2 declared) + sample promo.mp4 in both storages
- [x] Task 14: Video app, first slice: real screen (home + player), lists .mp4s from device.conf storage dirs, Continue watching positions, filmstrip playback (12 fps pre-rendered strip, no codec deps), OK play/pause, ◀ ▶ seek, Back. Real decode+audio = task 16.
- [x] Task 14b: app liveness — implemented apps keep their colour icon (video, settings); not-implemented apps render gray-ish everywhere (home, pins, apps grid, Settings › Apps) and defer politely; every app is a ".app" entry in Settings › Apps (built-in + store, icon/size, app-info sub-page — Android-style)
- [x] Task 14c: Video app 1:1 with the updated prototype (tabs, folders, network shares, prefs, resume dialog, hold-OK menu, info view, updated player: auto-hide, seek bar, subtitles/speed/zoom pills, up-next)
- [x] Task 14d: pointer input end-to-end — virtio-tablet on both run targets, evdev ABS decoding, hover-focus / click-activate / right-click-options in the engine, clickable seek bar; os/tools/mouse.py drives it headless via QMP input-send-event (HMP mouse_move does NOT deliver under -display none)
- [ ] Task 15: GPU render backend `kms-gles2` (GBM/EGL behind gw_display; QEMU: virtio-gpu venus/virgl + guest Mesa) — optional per device.conf
- [x] Task 16 (video half): real decode in the player — FFmpeg libs (LGPL) in the image, ui/media.c decodes to BGRA, keyframe seeks, container-probed durations, 60 s sample
- [ ] Task 16 (audio half): audio output backend (ALSA/gateway; QEMU `-audiodev`) — the player decodes video only for now
- [ ] Task 17: on-screen keyboard (InputMethodService-equivalent) — unblocks play-a-link, add-network-share, rename, custom device name

- [x] Task 18: persistent QEMU test volume (/tmp/opentv) — host media/usb folders 9p-mounted as the guest storages; drop-in → appears on app entry; app-deletes hit the host; survives reboots/rebuilds. Fixed the missing /proc mount (S00mount) that had broken mount/network since the first boot.

## Checkpoints
- [x] Phase 1: ARM target boots to shell
- [x] Phase 2: our pixels on screen (QEMU screendump), D-pad input works (sendkey tour)
- [x] Phase 3 (host + ARM): desktop grid + settings navigable on screen
- [x] Phase 3: PR open — https://github.com/nirodg/open-tv-launcher/pull/1

## Notes
- Host dev loop: `make -C os/src run` + OTV_KEYSCRIPT tours; guest loop: `make -C os app-arm` → `run-arm-headless` → shot.py.
- Key fixes that got the guest visible: connector→CRTC match by BIT (possible_crtcs is a mask over the crtc INDEX),
  `-display none` → headless-panel fallback in find_output, PCI transport forms (virtio-gpu-pci et al) in the run targets,
  and `drmModeDirtyFB` on every damage (virtio-gpu only pushes buffer contents to the host on a flush).
- inittab: respawn entry must have an EMPTY id field (a name there is a tty device → busybox opens /dev/<name>).
- Keyscript clock: host_tick must get the measured frame delta, not a fixed 16 ms.
- Known pace issue: under TCG (no KVM) every full-frame redraw takes seconds — fine for verification, use KVM x86 or real
  hardware for interactive feel. QEMU sendkey tours need generous delays (~1 key/s or more).
- x86 specifics: q35 adds a default bochs VGA — pass `-vga none` or the screendump shows the wrong (blank) scanout;
  x86 images put the rootfs at `out-x86/images/rootfs.ext2` (no .ext4 symlink). Packages must not glob `*.o` from the
  shell cwd: both otv packages compile into `obj/` and archive/link from there.

Status notes live in /memory.md (session-independent).
