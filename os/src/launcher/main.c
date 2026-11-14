/* launcher/main.c — the Open TV launcher process.
 * Registers the screens, owns the cached wallpaper, and runs the loop:
 * poll input → scene_key → scene_tick → scene_draw → present.
 * Real build: same loop on KMS/DRM with evdev; hostdev backends swap in
 * behind the same gateway calls for desktop iteration. */
#define _GNU_SOURCE
#include "drawui.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gateway/gateway.h"
#include "gateway/device.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── screens (boot / standby live here; the rest register below) ───────── */
static void empty_build(Layer *L, const char *param)
{
	(void)L; (void)param;
}

static void boot_draw(gw_surface *s, Layer *L, float now)
{
	(void)L; (void)now;
	gfx_fill(s, rrect(0, 0, OTV_W, OTV_H), (Col){ 0, 0, 0, 255 });
	/* g-store mark 120×120 r34 + "Open TV" (prototype SCREENS.boot) */
	R m = rrect(OTV_W / 2 - 60, OTV_H / 2 - 60 - 34, 120, 120);
	gradmark_draw(s, m, "tv", G_STORE);
	text_c(f30(), s, OTV_W / 2, m.y + m.h + 28 + 26, "Open TV", C_TEXT);
}

static void standby_draw(gw_surface *s, Layer *L, float now)
{
	(void)L; (void)now;
	gfx_fill(s, rrect(0, 0, OTV_W, OTV_H), (Col){ 0, 0, 0, 255 });
	text_c(f20(), s, OTV_W / 2, OTV_H - 60 - 8,
	       tx("Standby · press any key to turn on"), (Col){ 255, 255, 255, 64 });
}

static const Screen boot_screen = { "boot", empty_build, boot_draw, NULL, NULL };
static const Screen standby_screen = { "standby", empty_build, standby_draw, NULL, NULL };
extern const Screen home_screen;    /* screen_home.c */
extern const Screen apps_screen;    /* screen_apps.c */
extern const Screen settings_screen; /* screen_settings.c */
extern const Screen video_screen;   /* screen_video.c */

static const Screen *const SCREENS[] = {
	&boot_screen, &standby_screen, &home_screen, &apps_screen,
	&settings_screen, &video_screen,
};

/* ── screen registry (ui.h) ────────────────────────────────────────────── */
static const Screen *screens[16];
static int nscreens;

void screens_register(const Screen *const *list, int n)
{
	nscreens = n < 16 ? n : 16;
	for (int i = 0; i < nscreens; i++)
		screens[i] = list[i];
}

const Screen *screen_by_id(const char *id)
{
	for (int i = 0; i < nscreens; i++)
		if (!strcmp(screens[i]->id, id))
			return screens[i];
	return NULL;
}

/* ── wallpaper: cached full-res render of the AGENTS §5 background ─────── */
static uint32_t *wall_cache;
static bool wall_dirty = true;

void wall_invalidate(void)
{
	wall_dirty = true;
	g_dirty = true;
}

static inline uint32_t wall_px(uint8_t r, uint8_t g, uint8_t b)
{
	return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void wall_render(void)
{
	free(wall_cache);
	wall_cache = malloc((size_t)OTV_W * OTV_H * 4);
	if (!wall_cache) {
		fprintf(stderr, "otv: no memory for the wallpaper\n");
		return;
	}

	/* linear-gradient(160deg, #0E1630, #0A0F1F 55%, #070A14) */
	const float dx = 0.34202f, dy = 0.93969f; /* (sin160°, -cos160°) */
	float L = OTV_W * dx + OTV_H * dy;        /* gradient-line length   */
	float cx = OTV_W / 2.0f, cy = OTV_H / 2.0f;
	for (int y = 0; y < OTV_H; y++) {
		uint32_t *row = wall_cache + (size_t)y * OTV_W;
		for (int x = 0; x < OTV_W; x++) {
			float t = ((x + 0.5f - cx) * dx + (y + 0.5f - cy) * dy) / L + 0.5f;
			float f;
			uint8_t r, g, b;
			if (t < 0.55f) {
				f = t < 0 ? 0 : t / 0.55f;
				r = (uint8_t)(14 + (10 - 14) * f);
				g = (uint8_t)(22 + (15 - 22) * f);
				b = (uint8_t)(48 + (31 - 48) * f);
			} else {
				f = t > 1 ? 1 : (t - 0.55f) / 0.45f;
				r = (uint8_t)(10 + (7 - 10) * f);
				g = (uint8_t)(15 + (10 - 15) * f);
				b = (uint8_t)(31 + (20 - 31) * f);
			}
			row[x] = wall_px(r, g, b);
		}
	}

	/* blurred glows (prototype #wall): accent 45% top-left, violet 40%
	 * top-right, mint 28% bottom; each fades to 0 at 60% of its ellipse */
	const struct { float cx, cy, rx, ry, a; uint8_t r, g, b; } G[] = {
		{ 0.12f * OTV_W, 0.08f * OTV_H, 900, 600, 0.45f, 91, 140, 255 },
		{ 0.92f * OTV_W, 0.18f * OTV_H, 800, 700, 0.40f, 124, 92, 255 },
		{ 0.70f * OTV_W, 1.10f * OTV_H, 900, 700, 0.28f, 61, 214, 176 },
	};
	for (unsigned gi = 0; gi < sizeof(G) / sizeof(*G); gi++) {
		int x0 = (int)G[gi].cx - (int)G[gi].rx, x1 = (int)(G[gi].cx + G[gi].rx);
		int y0 = (int)G[gi].cy - (int)G[gi].ry, y1 = (int)(G[gi].cy + G[gi].ry);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > OTV_W) x1 = OTV_W;
		if (y1 > OTV_H) y1 = OTV_H;
		float irx = 1.0f / G[gi].rx, iry = 1.0f / G[gi].ry;
		for (int y = y0; y < y1; y++) {
			uint32_t *row = wall_cache + (size_t)y * OTV_W;
			float ey = (y + 0.5f - G[gi].cy) * iry;
			for (int x = x0; x < x1; x++) {
				float ex = (x + 0.5f - G[gi].cx) * irx;
				float d = sqrtf(ex * ex + ey * ey);
				if (d >= 0.6f)
					continue;
				uint8_t a = (uint8_t)(G[gi].a * (1 - d / 0.6f) * 255);
				row[x] = gfx_blend_px(row[x], G[gi].r, G[gi].g, G[gi].b, a);
			}
		}
	}
}

void draw_wallpaper(gw_surface *s)
{
	if (wall_dirty || !wall_cache) {
		wall_render();
		wall_dirty = false;
	}
	if (!wall_cache)
		gfx_clear(s, (Col){ 14, 22, 48, 255 });
	for (int y = 0; y < OTV_H; y++)
		memcpy(s->pix + (size_t)y * (s->stride / 4),
		       wall_cache + (size_t)y * OTV_W, (size_t)OTV_W * 4);
}

/* ── host-dev hooks (strong defs live in the hostdev backends) ─────────── */
__attribute__((weak)) void host_tick(int ms) { (void)ms; }
__attribute__((weak)) void host_shot(const char *name) { (void)name; }

/* ── engine clock: monotonic seconds, relative to process start.
 * Kept small on purpose: float32 of CLOCK_MONOTONIC-absolute loses all
 * sub-60ms precision on long-uptime machines (ULP grows with the value). */
static float now_sec(void)
{
	static struct timespec t0;
	static bool have_base;
	if (!have_base) {
		clock_gettime(CLOCK_MONOTONIC, &t0);
		have_base = true;
	}
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (float)(ts.tv_sec - t0.tv_sec) + (float)(ts.tv_nsec - t0.tv_nsec) / 1e9f;
}

/* ── the loop ──────────────────────────────────────────────────────────── */
int main(void)
{
	/* The device description file (Android-style board/HAL config, unified)
	 * decides which gateway backend each service uses. QEMU targets ship a
	 * simulated one; a real TV port ships its own. */
	const char *dev = getenv("OTV_DEVICE_CONF");
	if (otv_device_init(dev && dev[0] ? dev : "/etc/otv/device.conf") == 0) {
		fprintf(stderr, "otv: device %s\n", otv_device_str("device", "name", "?"));
		for (const char *p = otv_device_summary(), *nl;
		     (nl = strchr(p, '\n')) && *p; p = nl + 1)
			fprintf(stderr, "otv:   %.*s\n", (int)(nl - p), p);
	} else {
		fprintf(stderr,
			"otv: no device description file (built-in defaults)\n");
	}

	char dir[256];
	const char *env = getenv("OTV_FONTS");
	snprintf(dir, sizeof dir, "%s", env && env[0] ? env : "/usr/share/otv/fonts");
	font_init(dir);
	env = getenv("OTV_ICONS");
	snprintf(dir, sizeof dir, "%s", env && env[0] ? env : "/usr/share/otv/icons");
	icons_init(dir);

	if (gw_display_open() != 0) {
		fprintf(stderr, "otv: no display\n");
		return 1;
	}
	gw_input_open();

	sys_init();
	act_init();
	screens_register(SCREENS, (int)(sizeof(SCREENS) / sizeof(*SCREENS)));
	scene_init();
	gw_surface *fb = gw_display_fb();
	gw_input_event ev[8];
	float last = now_sec();
	for (;;) {
		float now = now_sec();
		/* feed the host layer the real frame delta (the keyscript clock
		 * tracks this — a fixed step drifts when draws are slow) */
		host_tick((int)((now - last) * 1000.0f + 0.5f));
		last = now;
		int n = gw_input_poll(ev, 8, 16);
		for (int i = 0; i < n; i++) {
			if (ev[i].pointer)
				scene_pointer(ev[i].x, ev[i].y, ev[i].down,
					      ev[i].right, now_sec());
			else
				scene_key(ev[i].key, ev[i].down, now_sec());
		}
		now = now_sec();
		scene_tick(now);
		float next_wake;
		if (!scene_dirty(now, &next_wake))
			continue;
		scene_draw(fb, now);
		gw_display_present();
		gw_display_damage(0, 0, fb->w, fb->h);
	}
	return 0;
}
