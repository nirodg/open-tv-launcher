/* launcher/screen_video.c — the Video app (port of APPT.video).
 * Home: "Continue watching" shelf + "All videos" grid, cards 400×225.
 * Player: fullscreen frame from a pre-rendered filmstrip, progress bar,
 * back-10 / play-pause / fwd-30 controls. Positions carry over to the
 * library (Continue watching) while the app runs, like the prototype. */
#define _GNU_SOURCE
#include "screen_video.h"
#include "drawui.h"
#include "sys.h"
#include "img.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

enum { V_HOME, V_PLAY };

static struct {
	int view;
	int cur;        /* clip index in the play view */
	bool playing;
	int base_pos;   /* seconds when (re)started */
	float t0;       /* gw_now when (re)started */
	Img *strip;     /* loaded filmstrip (frame grid) */
	char strip_path[292];
	int strip_cols; /* frames per row */
	int strip_rows;
} vs;

static const int FW = 480, FH = 270, FPS = 12; /* filmstrip geometry */

/* ── small string helpers ─────────────────────────────────────────────── */
static char vb_buf[4][64];
static int vb_i;
static const char *vb(const char *fmt, ...)
{
	char *b = vb_buf[vb_i = (vb_i + 1) % 4];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, 64, fmt, ap);
	va_end(ap);
	return b;
}

static void mmss(char *b, size_t bn, int s)
{
	s = s < 0 ? 0 : s;
	snprintf(b, bn, "%d:%02d", s / 60, s % 60);
}

static unsigned vhash(const char *s)
{
	unsigned h = 5381;
	while (*s)
		h = h * 31 + (unsigned char)*s++;
	return h;
}

/* ── card art (prototype art(): palette gradient + soft light + glyph) ── */
static const char *const PAL[] = { "red", "blue", "violet", "mint",
				   "sun", "slate", "rose", "sky" };

static void art_draw(gw_surface *s, R r, const char *seed, const char *glyph)
{
	gfx_rrect_g2(s, r, 24, app_grad(PAL[vhash(seed) % 8]));
	/* soft radial light (CSS radial-gradient, .32 → transparent at 60%),
	 * clipped inside the art like the CSS overflow:hidden */
	R outer;
	gfx_clip_get(&outer);
	gfx_clip(r);
	float cx = r.x + r.w * (0.20f + (vhash(seed) % 60) / 100.0f);
	float cy = r.y + r.h * (0.20f + (vhash(seed + 1) % 60) / 100.0f);
	float rad = (r.w > r.h ? r.w : r.h) * 0.6f;
	for (int i = 0; i < 24; i++) {
		float t = 1 - i / 24.0f;
		Col c = { 255, 255, 255, (uint8_t)(82 * t * t + 0.5f) };
		gfx_ring(s, rrect(cx - rad * t, cy - rad * t, rad * t * 2, rad * t * 2),
			 rad * t, 8, c);
	}
	icon_draw(s, glyph, r.x + r.w * 0.92f, r.y + r.h * 0.88f, 195,
		  (Col){ 255, 255, 255, 64 });
	gfx_clip_off();
	gfx_clip(outer);
}

/* ── playback state ───────────────────────────────────────────────────── */
static int cur_pos(float now)
{
	int p = vs.base_pos;
	if (vs.playing)
		p = vs.base_pos + (int)(now - vs.t0);
	return p < 0 ? 0 : p;
}

static void load_strip(void)
{
	int n;
	VidClip *vc = vid_clips(&n);
	if (vs.strip && vs.strip_path[0] &&
	    !strncmp(vs.strip_path, vc[vs.cur].path, strlen(vc[vs.cur].path)))
		return;
	img_free(vs.strip);
	vs.strip = NULL;
	vs.strip_path[0] = 0;
	char jpg[288];
	snprintf(jpg, sizeof jpg, "%.*s.jpg",
		 (int)(strlen(vc[vs.cur].path) - 4), vc[vs.cur].path);
	vs.strip = img_load(jpg);
	if (vs.strip) {
		snprintf(vs.strip_path, sizeof vs.strip_path, "%s", jpg);
		vs.strip_cols = vs.strip->w / FW;
		vs.strip_rows = vs.strip->h / FH;
		if (vs.strip_cols < 1 || vs.strip_rows < 1) {
			img_free(vs.strip);
			vs.strip = NULL;
			vs.strip_path[0] = 0;
		}
	}
}

void video_open_clip(int idx)
{
	int n;
	VidClip *vc = vid_clips(&n);
	if (idx < 0 || idx >= n)
		return;
	vs.cur = idx;
	vs.base_pos = vc[idx].pos >= vc[idx].dur - 1 ? 0 : vc[idx].pos;
	vs.playing = true;
	vs.t0 = gw_now();
	vs.view = V_PLAY;
	scene_refresh();
}

void video_toggle_play(void)
{
	if (vs.view != V_PLAY)
		return;
	float now = gw_now();
	if (vs.playing)
		vs.base_pos = cur_pos(now);
	vs.playing = !vs.playing;
	vs.t0 = now;
	scene_mark_dirty();
}

void video_seek(int secs)
{
	if (vs.view != V_PLAY)
		return;
	int n;
	VidClip *vc = vid_clips(&n);
	float now = gw_now();
	int p = cur_pos(now) + secs;
	if (p < 0) p = 0;
	if (p > vc[vs.cur].dur) p = vc[vs.cur].dur;
	vs.base_pos = p;
	vs.t0 = now;
	scene_mark_dirty();
}

static bool video_back(void)
{
	if (vs.view != V_PLAY)
		return false;
	/* save the position — Continue watching */
	int n;
	VidClip *vc = vid_clips(&n);
	vc[vs.cur].pos = cur_pos(gw_now());
	vs.view = V_HOME;
	scene_mark_dirty();
	scene_refresh();
	return true;
}

static void video_tick(float now)
{
	if (vs.view != V_PLAY)
		return;
	if (!vs.playing) {
		vs.t0 = now; /* keep the pause point fresh */
		return;
	}
	int n;
	VidClip *vc = vid_clips(&n);
	int p = cur_pos(now);
	static long last_frame = -1;
	long frame = (long)p * FPS;
	if (frame != last_frame) {
		last_frame = frame;
		scene_mark_dirty(); /* 12 fps content + progress */
	}
	if (p >= vc[vs.cur].dur) {
		vs.base_pos = vc[vs.cur].dur;
		vs.playing = false;
		scene_mark_dirty();
	}
}

/* ── HOME build/draw ──────────────────────────────────────────────────── */
static void video_build(Layer *L, const char *param)
{
	(void)param;
	int n;
	VidClip *vc = vid_clips(&n);

	if (vs.view == V_PLAY) {
		load_strip();
		/* controls (prototype ctlRow): back-10 / play-pause / fwd-30 */
		lnode(L, rrect(812, 960, 72, 72), "vc-b", "vseek:-10", false);
		lnode(L, rrect(912, 936, 96, 96), "vc-play", "vplay", true);
		lnode(L, rrect(1036, 960, 72, 72), "vc-f", "vseek:30", false);
		return;
	}

	/* continue watching = clips started but not finished */
	int cont[24], nc = 0;
	for (int i = 0; i < n; i++)
		if (vc[i].pos > 0 && vc[i].pos < vc[i].dur)
			cont[nc++] = i;

	float y = 222;
	int first_def = 1;
	for (int k = 0; k < nc; k++, y += 280) {
		lnode(L, rrect(96 + k * (400 + 32), y, 400, 280),
		      vb("vc%d", cont[k]), vb("vopen:%d", cont[k]), first_def);
		first_def = false;
	}
	if (nc)
		y += 30;
	float gy = y + 20 + 34 + 22;
	for (int i = 0; i < n; i++) {
		int col = i % 4, row = i / 4;
		lnode(L, rrect(96 + col * (400 + 42), gy + row * (280 + 40),
			       400, 280),
		      vb("va%d", i), vb("vopen:%d", i), first_def);
		first_def = false;
	}
	if (!n)
		lnode(L, rrect(96, gy, 1728, 88), "v-empty", "", true);
	int rows = (n + 3) / 4;
	float content = gy + rows * (280 + 40) + 40;
	lscroll_keep(L, rrect(0, 150, OTV_W, OTV_H - 150 - 80), (int)content);
}

static void card_draw(gw_surface *s, FNode *f, VidClip *c, float sy, bool prog)
{
	R r = f->r;
	R art = rrect(r.x, r.y - sy, r.w, 225);
	float fp = f->fp;
	if (fp > 0) {
		float cx = rcx(art), cy = rcy(art);
		art = rrect(cx - art.w * 0.53f, cy - art.h * 0.53f,
			    art.w * 1.06f, art.h * 1.06f);
	}
	gfx_clip(rrect(0, 150, OTV_W, OTV_H - 150 - 80));
	art_draw(s, art, c->title, "film");
	if (fp > 0)
		draw_focus_ring(s, art, 24, fp, C_TEXT, 4);
	if (prog && c->dur) {
		R pb = rrect(art.x + 14, art.y + art.h - 18, art.w - 28, 6);
		Col bg = { 0, 0, 0, 102 };
		gfx_rrect(s, pb, 3, bg);
		gfx_rrect(s, rrect(pb.x, pb.y, pb.w * c->pos / c->dur, 6), 3, C_TEXT);
	}
	/* title 22 semibold + sub 18 (CSS .mc .n 22 / .c 17→18 floor) */
	void *f22b = font_get(600, 22);
	text_tl(f22b, s, r.x, r.y - sy + 225 + 10 + 17, tx(c->title), C_TEXT);
	long mb = c->size / (1024 * 1024);
	text_tl(F18, s, r.x, r.y - sy + 225 + 10 + 22 + 4 + 14,
		vb("%d:%02d · %s · %s", c->dur / 60, c->dur % 60, c->date,
		   mb ? vb("%ld MB", mb) : vb("%ld KB", c->size / 1024)),
		C_T3);
	gfx_clip_off();
}

static void video_draw(gw_surface *s, Layer *L, float now)
{
	int n;
	VidClip *vc = vid_clips(&n);

	if (vs.view == V_PLAY) {
		int p = cur_pos(now);
		/* content: filmstrip frame (or art fallback) */
		if (vs.strip) {
			int total = vs.strip_cols * vs.strip_rows;
			int fi = p * FPS % total;
			img_draw(s, vs.strip, rrect(0, 0, OTV_W, OTV_H),
				 (fi % vs.strip_cols) * FW,
				 (fi / vs.strip_cols) * FH, FW, FH);
		} else {
			gfx_clear(s, (Col){ 0, 0, 0, 255 });
			art_draw(s, rrect(240, 0, 1440, 1080),
				 vc[vs.cur].title, "film");
		}
		if (!vs.playing) {
			Col dim = { 0, 0, 0, 89 };
			gfx_fill(s, rrect(0, 0, OTV_W, OTV_H), dim);
		}
		/* top + bottom gradient bands (CSS linear-gradients) */
		for (int y = 0; y < 220; y++) {
			uint8_t a = (uint8_t)(191 * (1 - y / 220.0f));
			gfx_fill(s, rrect(0, y, OTV_W, 1), (Col){ 5, 7, 13, a });
		}
		for (int y = 0; y < 480; y++) {
			float t = y / 480.0f;
			uint8_t a = t < 0.6f
					    ? (uint8_t)(235 - (235 - 140) * (t / 0.6f))
					    : (uint8_t)(140 * (1 - (t - 0.6f) / 0.4f));
			gfx_fill(s, rrect(0, OTV_H - 480 + y, OTV_W, 1),
				 (Col){ 5, 7, 13, a });
		}
		/* brand + clock */
		gfx_rrect_g2(s, rrect(96, 56, 44, 44), 13, app_grad2(app_info("video")));
		icon_draw(s, "play", 118, 78, 22, C_TEXT);
		text_tl(F22, s, 156, 86, "Video", C_T2);
		text_tl(F36, s, OTV_W - 96 - text_w(F36, tx(clock_str())),
			85, tx(clock_str()), C_TEXT);
		/* title + sub (prototype col bottom: 1080-250) */
		void *f48 = font_get(700, 48);
		text_tl(f48, s, 96, 788, tx(vc[vs.cur].title), C_TEXT);
		char t1[12], t2s[12];
		mmss(t1, sizeof t1, p);
		mmss(t2s, sizeof t2s, vc[vs.cur].dur);
		text_tl(F22, s, 96, 823, vb("%s · %s", t1, t2s), C_T2);
		/* progress bar (mpBar) */
		R tr = rrect(96, 876, OTV_W - 192, 8);
		Col track = { 255, 255, 255, 51 };
		gfx_rrect(s, tr, 4, track);
		if (vc[vs.cur].dur)
			gfx_rrect(s, rrect(tr.x, tr.y, tr.w * p / vc[vs.cur].dur, 8),
				  4, C_TEXT);
		/* controls (ctlRow): 72px circles + 96px big center */
		struct { const FNode *f; int cx, cy; bool big; const char *ic; } cts[] = {
			{ &L->n[0], 848, 996, false, "skipb" },
			{ &L->n[1], 960, 984, true, vs.playing ? "pause" : "play" },
			{ &L->n[2], 1072, 996, false, "skipf" },
		};
		for (int i = 0; i < 3; i++) {
			float rad = cts[i].big ? 48 : 36;
			R r = rrect(cts[i].cx - rad, cts[i].cy - rad, rad * 2, rad * 2);
			Col bg = { 255, 255, 255, 36 };
			gfx_rrect(s, r, (int)(r.h / 2), bg);
			Col ic = C_TEXT;
			if (cts[i].f->fp > 0) {
				gfx_rrect(s, r, (int)(r.h / 2), C_TEXT);
				ic = C_INK;
				Col glow = C_ACCENT;
				glow.a = (uint8_t)(128 * cts[i].f->fp);
				gfx_glow(s, r, (int)(r.h / 2), glow, cts[i].f->fp);
			}
			icon_draw(s, cts[i].ic, cts[i].cx, cts[i].cy,
				  cts[i].big ? 38 : 30, ic);
		}
		return;
	}

	/* ── HOME ── */
	/* appBar: brand mark + name + clock */
	gfx_rrect_g2(s, rrect(96, 58, 52, 52), 15, app_grad2(app_info("video")));
	icon_draw(s, "play", 122, 84, 28, C_TEXT);
	void *f30 = font_get(700, 30);
	text_tl(f30, s, 162, 93, "Video", C_TEXT);
	text_tl(F36, s, OTV_W - 96 - text_w(F36, tx(clock_str())),
		91, tx(clock_str()), C_TEXT);

	float sy = L->scroll_y;
	int cont[24], nc = 0;
	for (int i = 0; i < n; i++)
		if (vc[i].pos > 0 && vc[i].pos < vc[i].dur)
			cont[nc++] = i;

	gfx_clip(rrect(0, 150, OTV_W, OTV_H - 150 - 80));
	int ni = 0;
	if (nc) {
		text_tl(F28, s, 96, 190, "Continue watching", C_TEXT);
		for (int k = 0; k < nc; k++, ni++)
			card_draw(s, &L->n[ni], &vc[cont[k]], sy, true);
	}
	float y = 222 + (nc ? nc * 280 + 30 : 0);
	/* "All videos" header: title left, count right (shelf-h) */
	float hy = y + 20;
	text_tl(F28, s, 96, hy + 24, "All videos", C_TEXT);
	void *f20 = font_get(600, 20);
	text_tl(f20, s, 1824 - text_w(f20, vb("%d videos", n)),
		hy + 24, vb("%d videos", n), C_T2);
	float gy = hy + 34 + 22;
	for (int i = 0; i < n; i++, ni++)
		card_draw(s, &L->n[ni], &vc[i], sy, false);
	if (!n) {
		glass(s, rrect(96, gy, 1728, 88), 28);
		void *f22 = font_get(500, 22);
		text_tl(f22, s, 128, gy + 56, tx("No videos on this TV yet."), C_T2);
	}
	gfx_clip_off();
}

const Screen video_screen = { "video", video_build, video_draw, video_back, video_tick };
