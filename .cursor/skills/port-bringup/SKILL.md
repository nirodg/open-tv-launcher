---
name: port-bringup
description: Turns a board into a working Open TV port — interactive bring-up that produces a defconfig, kernel config, device.conf and a board-specific test plan. Interprets portdoctor output (the deterministic on-device checker), modetest/dmesg/serial logs, and maps symptoms to fixes. Use when someone wants to port Open TV / OpenTV to a board (Raspberry Pi, ARM TV box, dev board, any SoC), mentions porting, bring-up, defconfig, DRM/KMS, evdev, device.conf, or reports a boot problem on new hardware.
---

# Port bring-up: board in → working port out

One agent, one job. The port surface is deliberately tiny (~500 lines of
gateway + a device.conf + a defconfig) — keep it that way; do not fragment
into multiple agents until 2–3 real hardware ports exist and distinct jobs
actually emerge.

**Ground truth, in order of trust:**
1. `os/tools/portdoctor` output (deterministic, runs on the device) — never
   guess what a script can check
2. `docs/PORTING_GUIDELINE.md` — the playbook; every fix hint references it
3. The guest source: `os/src/gateway/guest/` (display_drm.c, input_evdev.c)

**Hard rules for a port:**
- Never modify launcher or toolkit code to support a board — if a port seems
  to need it, the port is wrong; file an issue instead
- No proprietary GPU blobs: KMS dumb buffers + the CPU rasterizer are the
  supported display path (that's the project's design rule)
- FFmpeg stays LGPL (no GPL/nonfree options); `BR2_PACKAGE_FFMPEG_SWSCALE=y`
  is a SEPARATE buildroot option that defaults off — forgetting it is the #1
  build failure
- "Open TV" is a placeholder name (trademark); no licence chosen yet — don't
  add LICENSE files or ship public artefacts under the name
- Be honest in the deliverables about what is real vs mock (audio backend is
  null, network backend is mock until the real ones land)

## The flow

### Step 1 — Intake: what board/SoC?

Ask for: board name / SoC / RAM, and whether mainline Linux boots on it.
If the user already ran portdoctor on it, jump to Step 2. Otherwise give
them the script first:

```
scp os/tools/portdoctor root@<board>:/tmp   # any Linux that boots there
ssh root@<board> sh /tmp/portdoctor
```

If nothing boots on the board yet, ask for `modetest -c` (from libdrm-tests)
and `dmesg | grep -i drm` instead.

SoC → DRM driver (for the kernel fragment):

| SoC family | Kernel |
|---|---|
| Broadcom BCM283x (Raspberry Pi 0–3) | `CONFIG_DRM_VC4` |
| Amlogic Meson | `CONFIG_DRM_MESON` |
| Rockchip | `CONFIG_DRM_ROCKCHIP` |
| Allwinner (sun4i/sun8i, incl. F1C100s) | `CONFIG_DRM_SUN4I` |
| NXP i.MX6 / i.MX8 | `CONFIG_DRM_IMX` / `CONFIG_DRM_IMX_DCSS` |
| MediaTek | `CONFIG_DRM_MEDIATEK` |
| Qualcomm | `CONFIG_DRM_MSM` |
| fallback / bring-up | `CONFIG_DRM_SIMPLEDRM` |

Always also: `CONFIG_DRM_KMS_HELPER`, `CONFIG_INPUT_EVDEV`, `CONFIG_EXT4_FS`
+ the IR receiver driver and keymap for the remote (guideline §2.2).

### Step 2 — Read the portdoctor report

Interpret every FAIL/WARN against this table (deterministic — do not
improvise; each hint cites the guideline):

| portdoctor line | Meaning / action |
|---|---|
| `FAIL /proc not mounted` | ship S00mount (`mount -a` from fstab) — guideline §4 |
| `FAIL no /dev/dri/card*` | kernel lacks the SoC DRM driver; enable it, check dmesg for probe errors — §0/§2.2 |
| `FAIL cardN exists but no driver is bound` | deferred probe (regulator/bridge)? read dmesg — §4 |
| `FAIL connected connector reports no modes` | broken/missing EDID over LVDS/DSI; the gateway synthesises 1080p60 as fallback; fix the panel bridge if possible — §3 |
| `FAIL no key-capable evdev device` | IR receiver driver + keymap emitting KEY_UP/DOWN/LEFT/RIGHT/ENTER — §2.2 |
| `FAIL RAM < 64 MB` | 1080p cannot run; 720p at < 128 MB is the hard floor |
| `WARN storage.* does not exist yet` | mount the real volume at the device.conf path — §1 |
| `WARN otv-launcher installed but not running` | look for `otv: no display` on serial: late DRM probe; init retries — §4 |

If the user brings a serial boot log instead: check for the ladder
`otv: device <name>` → `display backend kms-dumb` → `page flip OK`, then
apply the guideline §5 symptom→cause table.

### Step 3 — Produce the four deliverables

1. **Defconfig** — start from Buildroot's board defconfig and append the
   Open TV block exactly as in guideline §2.1 (LIBDRM, OTV_LAUNCHER, FFMPEG
   + SWSCALE, hostname/issue, overlay path). Warn: changing a defconfig
   does not refresh an existing output dir — re-run `make config-<t>` and,
   when package options changed, `ffmpeg-dirclean`.
2. **Kernel config fragment** — the board's basics + the DRM/input lines
   from Step 1.
3. **device.conf** — filled in from the user's real storage paths and the
   board name; storage paths must exist at boot.
4. **Test plan** — the guideline §5 ladder tailored to the board: serial
   checks, UI pass (D-pad + hold-OK + video play), the 1-hour soak with the
   flip-fallback watch, plus the printable checklist from §8.

### Step 4 — Hand-off honesty

State clearly in the deliverables: audio muted (backend null), network
simulated (mock), GPU optional (kms-gles2 declared, not built). A port that
lists its mocks is a good port.

## Reference

- `docs/PORTING_GUIDELINE.md` — the full playbook (read it once per session;
  its § references appear in portdoctor hints)
- `os/tools/portdoctor` — the checker (also shipped in the image at
  /usr/bin/portdoctor; verified against the QEMU reference target:
  13 pass / 0 fail)
- `os/src/gateway/guest/` — the ~500 lines that constitute the port layer
