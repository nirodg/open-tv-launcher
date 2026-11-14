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
- [ ] Task 12: x86_64 target boot smoke (KVM fast-dev path)

## Checkpoints
- [x] Phase 1: ARM target boots to shell
- [x] Phase 2: our pixels on screen (QEMU screendump), D-pad input works (sendkey tour)
- [x] Phase 3 (host + ARM): desktop grid + settings navigable on screen
- [ ] Phase 3: PR open

## Notes
- Host dev loop: `make -C os/src run` + OTV_KEYSCRIPT tours; guest loop: `make -C os app-arm` → `run-arm-headless` → shot.py.
- Key fixes that got the guest visible: connector→CRTC match by BIT (possible_crtcs is a mask over the crtc INDEX),
  `-display none` → headless-panel fallback in find_output, PCI transport forms (virtio-gpu-pci et al) in the run targets,
  and `drmModeDirtyFB` on every damage (virtio-gpu only pushes buffer contents to the host on a flush).
- inittab: respawn entry must have an EMPTY id field (a name there is a tty device → busybox opens /dev/<name>).
- Keyscript clock: host_tick must get the measured frame delta, not a fixed 16 ms.
- Known pace issue: under TCG (no KVM) every full-frame redraw takes seconds — fine for verification, use KVM x86 or real
  hardware for interactive feel. QEMU sendkey tours need generous delays (~1 key/s or more).

Status notes live in /memory.md (session-independent).
