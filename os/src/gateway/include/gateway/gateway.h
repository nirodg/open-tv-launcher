/* Open TV gateway — the device port layer.
 *
 * This is the only device-specific API in the OS. Everything above it
 * (launcher UI, settings services, later store/tv apps) stays device-agnostic.
 * Public API today: KMS/DRM display + evdev input (the five-interface plan:
 * EGL/GLES2, DRM planes, V4L2, ALSA, CEC/IR will be added here over time,
 * each behind the same narrow interface style).
 */
#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Display (KMS/DRM today; EGL/GLES2 backend lands behind this API) ── */
typedef struct gw_surface {
	int w, h;           /* pixels */
	int stride;         /* bytes per row */
	uint32_t *pix;      /* BGRX8888 (XRGB little-endian) */
} gw_surface;

int  gw_display_open(void);              /* 0 = ok; output is 1920×1080 */
void gw_display_close(void);
gw_surface *gw_display_fb(void);         /* surface to draw into (back buffer) */
void gw_display_present(void);           /* scanout */
void gw_display_damage(int x, int y, int w, int h); /* hint, may be ignored */

/* ── Input (evdev → the remote-key model) ────────────────────────────── */
typedef enum {
	GWK_NONE = 0,
	GWK_UP, GWK_DOWN, GWK_LEFT, GWK_RIGHT,
	GWK_OK, GWK_BACK, GWK_HOME, GWK_MENU,
	GWK_VOLUP, GWK_VOLDOWN, GWK_MUTE,
	GWK_CHUP, GWK_CHDOWN,
} gw_key;

typedef struct { gw_key key; bool down; } gw_input_event;

int  gw_input_open(void);
void gw_input_close(void);
/* Waits up to timeout_ms; returns number of events stored (0 = timeout). */
int  gw_input_poll(gw_input_event *ev, int max, int timeout_ms);

/* ── Host dev-iteration hooks (host-only backends implement these; the OS
 *    build links stubs from main.c — see gateway/hostdev/) ────────────── */
void host_tick(int ms);              /* advance the scripted-input clock */
void host_shot(const char *name);    /* dump current frame to name.argb */

/* ── Time (local) ─────────────────────────────────────────────────────── */
void gw_time_parts(int *h, int *m, int *s, int *wd, int *day, int *mon, int *year);
const char *gw_wd_name(int wd);      /* "Sunday" … */
const char *gw_mon_name(int mon);    /* "January" … */

#endif /* GATEWAY_H */
