# memory.md — working memory for the OS build (os-build branch)

Keep short. Update status after every working increment.

## What this is
Buildroot-based ARM Linux TV OS ("Open TV" placeholder name) + our own UI runtime.
Branch `os-build` → PR into `main` (never merge without maintainer; never touch Relyr repo — AGENTS.md §2).
Behavior reference = `prototype/index.html` (focus engine, screens). Look reference = AGENTS.md §5 + `renders/`.

## Decisions (made, don't relitigate)
- Base: Buildroot pinned tarball, out-of-tree in `os/build/`. QEMU `-M virt` **aarch64** (`opentv_qemu_aarch64_virt_defconfig`,
  `-cpu cortex-a53`, kernel `Image`) = primary target (virtio-gpu/keyboard/9p); `opentv_qemu_x86_64_defconfig` (+KVM) =
  fast dev/CI target, same external tree.
- Public API is KMS/DRM (dumb buffers for milestone 1); GLES2/EGL backend (softpipe→llvmpipe→vendor blob) comes later
  behind the same `gw_display` API. No Mesa/LLVM in guest yet (CPU rasterizer instead).
- Gateway = frozen port layer: display(DRM), input(evdev→remote keys), time, sys(device mock). V4L2/ALSA/CEC declared, later.
- UI: own tiny C toolkit (tokens per AGENTS §5, CPU rasterizer, stb_truetype + Inter OFL, focus engine ported from prototype).
- Init: busybox init, `otv-launcher` respawned via inittab; 9p host share (`mount_tag=hostshare` → `/mnt/host`) for shots/iteration.
- Milestone 1 scope: home + all apps + settings + toast/picker (+quick panel if fits). No store/TV/keyboard yet.
- Icons: 68 SVG symbols from `prototype/index.html` sprite → white PNGs via `os/tools/render-icons.cjs` (Playwright).

## Build / run / verify
- `make -C os fetch` → pinned buildroot + sha256 into `os/build/`
- `make -C os config` → defconfig into out-of-tree output
- `make -C os build` → image (incremental; re-run to rebuild only changed packages)
- `make -C os run-arm` / `run-x86` → QEMU (x86 uses KVM). Headless: `make -C os run-x86-headless`
- Screenshot: `python3 os/tools/shot.py <monitor-socket> out.png` (QEMU screendump → PIL). D-pad test: monitor `sendkey down|up|kp_enter|esc`.
- Host share: `os/build/share/` ↔ guest `/mnt/host` (push rebuilt binary, `killall otv-launcher` → init respawns it).

## Layout
- `os/Makefile`, `os/README.md` — entry points
- `os/external/` — buildroot external tree (packages `otv-gateway`, `otv-launcher`; `board/opentv/` overlay+kernel fragment; `configs/`)
- `os/src/gateway/` — port layer (C, links libdrm)
- `os/src/ui/` — toolkit (tokens, gfx, font, scene+focus)
- `os/src/launcher/` — app (main loop, screens: home, apps, settings, overlays)
- `os/tools/` — icon render script, shot.py
- `tasks/plan.md`, `tasks/todo.md` — full plan + task list (check there first)

## Status log (newest last)
- 2026-11-14: branch `os-build` created; plan + memory written. Next: scaffold os/, fetch buildroot, kick off first build in background, develop UI while it builds.
- 2026-11-14 (host look pass complete): fixed the All-apps SIGSEGV (`appicon_draw` passed the `f20` FUNCTION POINTER as the font → `text()` read code bytes as `Font`; survived ~20 apps, died on "Shelf"). Other fixes: `G_STORE` gradient → #5B8CFF→#7C5CFF (was violet→mint); apps default focus = first grid app (prototype data-default); `text_wrap` duplication bug (appended instead of rewrote → picker note garbage); new `draw_wrap` (per-line draw); picker rebuilt to prototype geometry (76px rows, check at right, opaque panel C_PANEL_HI); toggle knob always white; opt rows show state values + always-chevron; qp header clock/date overlap fixed; qp slider fill = rounded-left cap + square right edge, label/icon split ink/white across the fill edge; `host_tick` now feeds the REAL frame delta (keyscript clock no longer drifts ~1.5×); settings intro sits below the title. Screenshots per run in `screenshots/<timestamp>_<scope>.png` (kept, never deleted). Tour keyscript: `/tmp/opencode/ks-tour.txt` (boot→qp→settings→picker→sub→toggle→dialog→apps).
- Next: rebuild `app-arm`, verify in QEMU headless (9p push + shot), then x86_64 target, then small commits → push `os-build` → `gh pr create` into main (os/ + memory.md + tasks/ only).
- 2026-11-14 (guest running!): ARM aarch64 guest boots to the launcher UI in QEMU headless and passes the D-pad tour
  (tile focus, quick panel, settings, all apps — screenshots/ 20261114-0532/0537 arm-*). Fixes that got it there:
  inittab respawn id must be EMPTY (`lch::respawn` → `::respawn`; a name = tty device, output discarded),
  PCI transport forms (`virtio-gpu-pci` etc.) in run targets (legacy virtio-mmio gpu never probed),
  find_output: connector→crtc match by BIT (`possible_crtcs` is a mask over the crtc INDEX, not the object ID) +
  headless-panel fallback (`-display none` → connector reported disconnected),
  and `drmModeDirtyFB` full-frame on every damage — virtio-gpu pushes buffer contents to the host ONLY on a flush
  (SetCrtc/flip/DIRTYFB); raw CPU writes to the dumb buffer are invisible to QEMU.
  evdev poll now drains every readable device (half-read queue wedged later keys).
  TCG pace: each full-frame redraw takes seconds under emulation — fine for verification; KVM/x86 for interactive feel.
  8 commits pushed; PR #1 open: https://github.com/nirodg/open-tv-launcher/pull/1 (review only, never merge without maintainer).
- Pending: x86_64 KVM boot smoke (task 12).
- 2026-11-14 (x86 done, PR updated): x86_64 KVM target boots to the launcher in ~8 s and passes the D-pad tour
  (screenshots/ 20261114-0626/0628 x86-*). Fixes: `-vga none` in the x86 run targets (q35's default bochs VGA stole the
  screendump), image paths rootfs.ext2/bzImage under out-x86/images/, `app-x86` incremental target, and both otv packages
  now build CWD-independently into `obj/` (a stale `*.o` cwd glob once put launcher objects in the gateway archive and
  broke the x86 link). Debug inittab (launcher.log via 9p) reverted to the clean respawn. 10 commits on os-build;
  PR #1: https://github.com/nirodg/open-tv-launcher/pull/1 — ready for maintainer review; never merge without a yes.
- 2026-11-14 (flashing fixed — double-buffered page flips): user reported flashing on every key/screen move on x86.
  Cause: single-buffer scanout — every redraw repainted wallpaper+UI directly into the buffer the CRTC was scanning,
  so the display caught half-drawn frames (tearing) constantly. Fix in display_drm.c: TWO dumb buffers, draw into the
  off-screen one, drmModePageFlip (atomic swap, works on virtio-gpu — "page flip OK" in the boot log), then DIRTYFB the
  shown buffer (kernel copies contents to the host on a flush). damage() is a no-op in flip mode (one 8 MB push per
  frame, not two); automatic DIRTYFB fallback if flips are ever unavailable (OTV_NOFLIP=1 forces it for testing).
  Verified on both targets: rapid key bursts + burst screendumps capture only complete, atomic frames
  (screenshots/ 20261114-064741 x86-flip-*, 20261114-064928 arm-flip).
- 2026-11-14 (device description + backend switch): new `/etc/otv/device.conf` — the Android-style per-device setup
  file naming the gateway backend per service (display/audio/network/storage/samples); parsed by gateway/device.c
  (tiny INI, no deps), logged at startup, `OTV_DEVICE_CONF` overrides the path on the host. Render backend is now a
  conf choice: `kms-dumb` (CPU, default — the no-proprietary-blobs rule) vs `kms-gles2` (GPU, declared; falls back
  with a clear log line until the GBM/EGL backend lands). QEMU images ship sample clips at /media/internal/promo.mp4
  and /media/external/promo.mp4 (os/tools/make-samples.sh, deterministic ffmpeg testsrc2, 134 KB each) for the later
  Video app (task 14). Verified on both guests (device lines in boot log; x86 UI up in ~8 s).
- Pending: Video app real impl (task 14), kms-gles2 GPU backend (task 15).
