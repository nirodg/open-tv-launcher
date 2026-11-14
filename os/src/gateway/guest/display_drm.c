/* gateway/guest/display_drm.c — DRM/KMS dumb-buffer scanout.
 * Public API decision: KMS/DRM, not fbdev. On real TV hardware the vendor
 * fbdev/blob sits behind this same file (port layer).
 * Two dumb buffers are page-flipped: frames are drawn into the off-screen
 * buffer and swapped atomically, so the scanout never sees a half-drawn
 * frame (single-buffer scanout flashed/teared on every redraw). If page
 * flips are unavailable the code falls back to drawing into the scanned
 * buffer and pushing it with DIRTYFB. virtio-gpu only pushes buffer
 * contents to the host on a flush (SetCrtc / flip / DIRTYFB). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "gateway/gateway.h"
#include "gateway/device.h"

#define OTV_W 1920
#define OTV_H 1080

static int drm_fd = -1;
static drmModeConnector *conn;
static drmModeCrtcPtr saved_crtc;
static uint32_t crtc_id;
static gw_surface surf;
static bool crtc_set;

typedef struct {
	uint32_t handle; /* dumb buffer */
	uint32_t fb;     /* framebuffer wrapping it */
	uint8_t *map;
	size_t size;
} DumbBuf;
static DumbBuf buf[2];
static int front;   /* index of the buffer the CRTC is scanning out */
static bool flip_ok = true;

static int find_output(drmModeRes *res)
{
	drmModeConnector *fallback = NULL;
	for (int i = 0; i < res->count_connectors && !conn; i++) {
		drmModeConnector *c = drmModeGetConnector(drm_fd, res->connectors[i]);
		if (!c)
			continue;
		if (!c->count_modes) {
			/* no EDID: synthesize the one mode we want */
			drmModeModeInfo *m = realloc(c->modes,
						     sizeof(*m) * (c->count_modes + 1));
			if (!m) {
				drmModeFreeConnector(c);
				continue;
			}
			memset(&m[c->count_modes], 0, sizeof(*m));
			m[c->count_modes] = (drmModeModeInfo){
				.clock = 148500,
				.hdisplay = 1920, .hsync_start = 2008,
				.hsync_end = 2052, .htotal = 2200,
				.vdisplay = 1080, .vsync_start = 1084,
				.vsync_end = 1089, .vtotal = 1125,
				.vrefresh = 60,
				.type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_USERDEF,
			};
			snprintf(m[c->count_modes].name, sizeof(m->name), "1920x1080");
			c->modes = m;
			c->count_modes++;
		}
		if (c->connection != DRM_MODE_CONNECTED) {
			/* Headless hosts (QEMU -display none) may report the
			 * panel as disconnected — a TV always drives its panel,
			 * so remember the first usable connector and only
			 * insist on "connected" when something is plugged in. */
			if (!fallback) fallback = c;
			else drmModeFreeConnector(c);
			continue;
		}
		/* prefer 1920×1080, else first mode */
		int best = 0;
		for (int m = 0; m < c->count_modes; m++)
			if (c->modes[m].hdisplay == OTV_W && c->modes[m].vdisplay == OTV_H)
				{ best = m; break; }
		c->modes[0] = c->modes[best];

		for (int e = 0; e < c->count_encoders && !conn; e++) {
			drmModeEncoder *enc = drmModeGetEncoder(drm_fd, c->encoders[e]);
			if (!enc)
				continue;
			/* possible_crtcs is a BITMASK over the crtc INDEX —
			   res->crtcs[cc] is an object ID, never a mask bit */
			for (int cc = 0; cc < res->count_crtcs; cc++) {
				if (enc->possible_crtcs & (1u << cc)) {
					crtc_id = res->crtcs[cc];
					conn = c;
					break;
				}
			}
			drmModeFreeEncoder(enc);
		}
		if (!conn)
			drmModeFreeConnector(c);
	}
	if (!conn && fallback) {
		fprintf(stderr, "otv: no connected output; using the headless panel\n");
		/* same mode pick + encoder/crtc walk on the fallback connector */
		int best = 0;
		for (int m = 0; m < fallback->count_modes; m++)
			if (fallback->modes[m].hdisplay == OTV_W &&
			    fallback->modes[m].vdisplay == OTV_H)
				{ best = m; break; }
		if (fallback->count_modes)
			fallback->modes[0] = fallback->modes[best];
		for (int e = 0; e < fallback->count_encoders && !conn; e++) {
			drmModeEncoder *enc = drmModeGetEncoder(drm_fd, fallback->encoders[e]);
			if (!enc)
				continue;
			for (int cc = 0; cc < res->count_crtcs; cc++) {
				if (enc->possible_crtcs & (1u << cc)) {
					crtc_id = res->crtcs[cc];
					conn = fallback;
					break;
				}
			}
			drmModeFreeEncoder(enc);
		}
		if (!conn)
			drmModeFreeConnector(fallback);
	}
	return conn ? 0 : -1;
}

int gw_display_open(void)
{
	drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		fprintf(stderr, "otv: cannot open /dev/dri/card0\n");
		return -1;
	}
	/* Render backend from the device description: "kms-dumb" is the
	 * CPU rasterizer (no GPU, no proprietary blobs — the default everywhere);
	 * "kms-gles2" is the optional GBM/EGL GPU path, landing behind this same
	 * API. Until that backend exists, any other value falls back with a
	 * clear note. */
	const char *rbackend = otv_device_str("display", "backend", "kms-dumb");
	if (strcmp(rbackend, "kms-dumb") != 0)
		fprintf(stderr,
			"otv: display backend '%s' is not in this build — using kms-dumb (CPU)\n",
			rbackend);
	else
		fprintf(stderr, "otv: display backend kms-dumb (CPU rasterizer)\n");

	if (drmSetMaster(drm_fd) != 0)
		fprintf(stderr, "otv: drmSetMaster failed (continuing)\n");

	drmModeRes *res = drmModeGetResources(drm_fd);
	if (!res || find_output(res) != 0) {
		fprintf(stderr, "otv: no connected display found\n");
		if (res) drmModeFreeResources(res);
		return -1;
	}
	drmModeFreeResources(res);

	saved_crtc = drmModeGetCrtc(drm_fd, crtc_id);

	/* two dumb buffers for double-buffered flips; libdrm has no CREATE_DUMB
	 * wrapper — use the ioctl directly (handle/pitch/size come back). */
	int pitch = 0;
	for (int i = 0; i < 2; i++) {
		struct drm_mode_create_dumb creq = {
			.width = OTV_W,
			.height = OTV_H,
			.bpp = 32,
		};
		if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
			fprintf(stderr, "otv: dumb buffer create failed\n");
			return -1;
		}
		buf[i].handle = creq.handle;
		buf[i].size = (size_t)creq.size;
		pitch = (int)creq.pitch;

		if (drmModeAddFB(drm_fd, OTV_W, OTV_H, 24, 32, creq.pitch,
				 creq.handle, &buf[i].fb) != 0) {
			fprintf(stderr, "otv: AddFB failed\n");
			return -1;
		}

		struct drm_mode_map_dumb mreq = { .handle = creq.handle };
		if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
			fprintf(stderr, "otv: dumb buffer map ioctl failed\n");
			return -1;
		}
		buf[i].map = mmap(NULL, buf[i].size, PROT_READ | PROT_WRITE,
				  MAP_SHARED, drm_fd, mreq.offset);
		if (buf[i].map == MAP_FAILED) {
			fprintf(stderr, "otv: mmap dumb buffer failed\n");
			buf[i].map = NULL;
			return -1;
		}
	}

	surf.w = OTV_W;
	surf.h = OTV_H;
	surf.stride = pitch;
	front = 0;
	surf.pix = (uint32_t *)buf[1].map; /* draw into the off-screen buffer */
	if (getenv("OTV_NOFLIP"))
		flip_ok = false;

	if (drmModeSetCrtc(drm_fd, crtc_id, buf[0].fb, 0, 0, &conn->connector_id, 1,
			   &conn->modes[0]) != 0) {
		fprintf(stderr, "otv: SetCrtc failed\n");
		return -1;
	}
	crtc_set = true;
	return 0;
}

gw_surface *gw_display_fb(void) { return crtc_set ? &surf : NULL; }

static void push_front(void)
{
	drmModeClip clip = { .x1 = 0, .y1 = 0, .x2 = OTV_W - 1, .y2 = OTV_H - 1 };
	drmModeDirtyFB(drm_fd, buf[front].fb, &clip, 1);
}

void gw_display_present(void)
{
	if (drm_fd < 0 || !crtc_set)
		return;
	if (flip_ok) {
		int back = !front;
		static bool said;
		int rc = drmModePageFlip(drm_fd, crtc_id, buf[back].fb, 0, NULL);
		if (rc == 0) {
			if (!said) {
				said = true;
				fprintf(stderr, "otv: page flip OK (double-buffered)\n");
			}
			front = back;
			surf.pix = (uint32_t *)buf[!front].map;
		} else {
			if (!said) {
				said = true;
				fprintf(stderr,
					"otv: page flip failed (%s) — DIRTYFB fallback\n",
					strerror(errno));
			}
			/* flips unsupported here: keep drawing into the shown
			 * buffer and rely on the DIRTYFB flush below */
			flip_ok = false;
			surf.pix = (uint32_t *)buf[front].map;
		}
	}
	/* the kernel only copies buffer contents to the host on a flush */
	push_front();
}

void gw_display_damage(int x, int y, int w, int h)
{
	/* the flip path pushes the whole frame in present(); in the no-flip
	 * fallback present() pushes it too — nothing per-rect here */
	if (drm_fd < 0 || !crtc_set || flip_ok)
		return;
	(void)x; (void)y; (void)w; (void)h;
	push_front();
}

void gw_display_close(void)
{
	if (drm_fd < 0)
		return;
	if (crtc_set && saved_crtc)
		drmModeSetCrtc(drm_fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
			       saved_crtc->x, saved_crtc->y, &conn->connector_id, 1,
			       &saved_crtc->mode);
	if (saved_crtc)
		drmModeFreeCrtc(saved_crtc);
	for (int i = 0; i < 2; i++) {
		if (buf[i].fb)
			drmModeRmFB(drm_fd, buf[i].fb);
		if (buf[i].map)
			munmap(buf[i].map, buf[i].size);
		if (buf[i].handle)
			drmModeDestroyDumbBuffer(drm_fd, buf[i].handle);
		buf[i] = (DumbBuf){ 0 };
	}
	if (conn)
		drmModeFreeConnector(conn);
	drmDropMaster(drm_fd);
	close(drm_fd);
	drm_fd = -1;
}
