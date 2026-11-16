# Implementation Plan: `os-build` — Bootable Linux TV OS (OpenSourceTV base)

## Overview

First working slice of the real OS: a Buildroot-based ARM Linux image that boots in
QEMU and shows the launcher desktop (home grid + all apps + settings) rendered by
our own UI runtime on top of the "gateway" port layer (KMS/DRM public API). This is
the milestone agreed for branch `os-build`: **boots → desktop grid visible → settings
usable with D-pad**, everything else (store, TV, GLES2 backend, real hardware port)
comes as later increments on the same tree.

## Architecture decisions

- **Base:** Buildroot (pinned release tarball, out-of-tree build in `os/build/`), as
  decided. QEMU target: `-M virt` arm32 Cortex‑A15 (`opentv_qemu_arm_virt_defconfig`)
  — same core class as the target TV SoC, and virt gives us virtio-gpu/keyboard/9p.
  A second `opentv_qemu_x86_64_defconfig` (KVM on this host) exists purely as a fast
  dev/CI target; both share the external tree. vexpress stays a fallback defconfig
  later; nothing in userspace depends on the machine.
- **Public API = KMS/DRM, not fbdev.** Gateway renders through DRM dumb buffers now;
  the GLES2/EGL backend (softpipe → llvmpipe → vendor blob on real TV SoCs) slots in
  behind the same `gw_display` API without touching screen code.
- **Gateway (frozen interfaces, per decision):** display (DRM/KMS planes), input
  (evdev → remote-key model), time, sys (device-service mock layer that later maps to
  real settings services). V4L2/ALSA/CEC stubs declared in headers, unimplemented.
- **UI runtime:** own tiny C toolkit, CPU rasterizer (rounded rects w/ analytic AA,
  gradients, atlas blits, glyph cache) — chosen over Mesa-in-guest for milestone 1
  because it removes LLVM/Mesa cross-build risk and works identically on both QEMU
  targets. Design tokens copied verbatim from AGENTS.md §5. Focus engine ported
  faithfully from `prototype/index.html` (score/move/bump/restore/layer trap).
- **Apps:** `otv-launcher` (home, all-apps, settings, toast/picker overlays; quick
  panel if it fits). Mock data mirrors the prototype (`APPT`, `CYC`, `S`).
- **Assets:** Inter (OFL) vendored + `stb_truetype` runtime rasterization; the 68
  prototype sprite icons rendered to white PNGs by `os/tools/render-icons.cjs`
  (Playwright, already in repo) and committed.
- **Boot flow:** busybox init; `otv-launcher` respawned from inittab; 9p host share
  for screenshots/iteration; QEMU monitor `screendump` (PIL on host) for verification.
- **Naming:** "Open TV" placeholder only (trademark note in AGENTS.md §1 applies).

## Task list (tracked in `tasks/todo.md`)

### Phase 1: OS base boots
- Task 1: Scaffold `os/` (Makefile fetch/config/build/run targets, `.gitignore`,
  README), Buildroot external tree skeleton, pinned buildroot fetch.
- Task 2: `opentq_arm_virt` + x86_64 defconfigs, kernel fragment (virtio-gpu, evdev,
  9p, no fbcon), overlay (inittab with launcher respawn, TZ), packages as stubs.
- Task 3: Full first build (background, ~20–40 min), boot smoke on both targets.

### Phase 2: Gateway + visible desktop
- Task 4: `gw_display` (DRM dumb buffer + SetCrtc), `gw_input` (evdev → remote keys),
  `gw_time`, `gw_sys` mock; smoke app draws color bars.
- Task 5: UI toolkit: tokens, rasterizer (rect/gradient/ring/blit/text), font cache,
  anim curve (CSS cubic-bezier), scene graph + focus engine port.
- Task 6: Assets pipeline: icon render script + Inter fetch (OFL + license file).
- Task 7: Home screen (clock/date/greeting, status chips, featured tiles, pins,
  all-apps button, key hints) — incremental build + screenshot.
- Task 8: All apps (8-col grid + filter chips) — incremental build + screenshot.
- Task 9: Settings (sidebar instant-switch, tog/opt/sld/act/info rows, picker
  overlay) + toast + quick panel if time — incremental build + screenshot.

### Phase 3: Ship
- Task 10: Compare screenshots against `renders/launcher/*.png`, fix drift.
- Task 11: Focused commits, push `os-build`, open PR into `main` (no merge),
  update `memory.md`, report what is tested vs not.

## Checkpoints

- After Phase 1: both QEMU targets boot to shell; serial log clean of hangs.
- After Phase 2: screenshot shows our pixels (smoke bars), D-pad input provably works.
- After Phase 3: desktop grid + settings visible and navigable on screen; PR open.

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| virtio-gpu mode/fb issues | Desktop invisible | Smoke bars app first; fbdev-emulation kept in kernel as fallback diagnostic; x86_64 target isolates ARM quirks |
| arm32 TCG slow (animations) | Choppy demo | Damage-rect rendering; x86_64+KVM target for smooth runs; acceptable for milestone |
| Buildroot/kernel pin surprises | Build breaks | Pin exact release + sha256; kernel fragment minimal additive |
| Disk space (60 GB free) | Build fails late | Out-of-tree build, ccache on, clean `make os-clean` documented |
| Inter license / icon provenance | Legal | OFL license file vendored; icons are our own sprite; no third-party brand assets (AGENTS §2) |

## Open questions (none blocking)

- Final product name (placeholder per AGENTS §1) — needed only before release.
- Which real device is port #1 (NT72671 Vestel assumed) — affects only later phases.
