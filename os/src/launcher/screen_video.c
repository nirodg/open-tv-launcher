/* launcher/screen_video.c — the Video app, a 1:1 port of APPT.video.
 * Tabs: Videos / Folders / Network / Settings (appBar tabs, VTABS).
 * Views: folder grid, network share (browse mock while network=mock), info
 * page, and the player (videoView): auto-hiding UI, seek bar with knob,
 * pause overlay, subtitles, speed/zoom/CC/audio pills, up-next card.
 * Content: real .mp4s from the device.conf storage dirs + the prototype's
 * mock shares. Playback uses pre-rendered filmstrips (no codecs yet).
 * Input: D-pad (◀ ▶ seek, OK pause, hold OK = options) AND the mouse
 * (hover focuses, click activates, right-click = options, seek-bar clicks). */
#define _GNU_SOURCE
#include "screen_video.h"
#include "drawui.h"
#include "sys.h"
#include "img.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

enum { V_HOME, V_FOLDER, V_SHARE, V_INFO, V_PLAY };
#define MAXCLIPS 24

static struct {
	int view;
	char tab[12];
	char folder[48];
	int share;
	bool sh_loading, sh_loaded, sh_err;
	float sh_t0;
	char info_id[40];
	int from_view;

	VidClip *m;
	VidClip *list[MAXCLIPS];
	int nlist, idx;
	char ctx[32];          /* play list context (what plays next) */
	bool playing, hid;
	int base_pos;
	float t0;
	float speed;
	char cc[16];
	int zoom;             /* 0 fit · 1 fill · 2 original */
	Img *strip;
	char strip_path[292];
	int strip_cols, strip_rows;
} vs = { .tab = "videos", .view = V_HOME, .speed = 1 };

static const int FW = 480, FH = 270, FPS = 12;

/* ── string arenas: row text must outlive the build call ───────────────── */
static char arena[4096];
static int ap;
static void arena_reset(void) { ap = 0; }
static const char *astr(const char *fmt, ...)
{
	va_list ap1, ap2;
	va_start(ap1, fmt);
	va_copy(ap2, ap1);
	int need = vsnprintf(NULL, 0, fmt, ap1) + 1;
	va_end(ap1);
	if (ap + need > (int)sizeof arena)
		return "";
	char *out = arena + ap;
	vsnprintf(out, need, fmt, ap2);
	va_end(ap2);
	ap += need;
	return out;
}

/* short-lived rotate buffers (node keys/acts are copied by lnode) */
static char vb_buf[4][96];
static int vb_i;
static const char *vb(const char *fmt, ...)
{
	char *b = vb_buf[vb_i = (vb_i + 1) % 4];
	va_list apv;
	va_start(apv, fmt);
	vsnprintf(b, 96, fmt, apv);
	va_end(apv);
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

static bool started(const VidClip *c)
{
	return c->pos > 5 && c->pos < c->dur - 5;
}

static const char *const SUBS[] = {
	"sports", "…and that's a brilliant save from the keeper!",
	"news", "The council voted to approve the plan this afternoon.",
	"nature", "Thousands of fish gather here every spring.",
	"cinema", "\"Keep the light burning, whatever happens.\"",
	"kids", "Now fold the corner down, just like this!",
	"music", "♪ Singing all night under city lights ♪",
};
static const char *subs_line(void)
{
	const char *scene = vs.m && vs.m->is_share ? "nature" : "cinema";
	for (int i = 0; i < 6; i += 2)
		if (!strcmp(SUBS[i], scene))
			return SUBS[i + 1];
	return "";
}

/* card art (prototype art(): palette gradient + soft light + glyph) */
static const char *const PAL[] = { "red", "blue", "violet", "mint",
				   "sun", "slate", "rose", "sky" };
static void art_draw(gw_surface *s, R r, float rad, const char *seed,
		     const char *glyph, float glyph_size)
{
	gfx_rrect_g2(s, r, rad, app_grad(PAL[vhash(seed) % 8]));
	R outer;
	gfx_clip_get(&outer);
	gfx_clip(r);
	float cx = r.x + r.w * (0.20f + (vhash(seed) % 60) / 100.0f);
	float cy = r.y + r.h * (0.20f + (vhash(seed + 1) % 60) / 100.0f);
	float rr = (r.w > r.h ? r.w : r.h) * 0.6f;
	for (int i = 0; i < 24; i++) {
		float t = 1 - i / 24.0f;
		Col c = { 255, 255, 255, (uint8_t)(82 * t * t + 0.5f) };
		gfx_ring(s, rrect(cx - rr * t, cy - rr * t, rr * t * 2, rr * t * 2),
			 rr * t, 8, c);
	}
	icon_draw(s, glyph, r.x + r.w * 0.92f, r.y + r.h * 0.88f,
		  glyph_size, (Col){ 255, 255, 255, 64 });
	gfx_clip_off();
	gfx_clip(outer);
}

/* ── playback ──────────────────────────────────────────────────────────── */
static int cur_pos(float now)
{
	int p = vs.base_pos;
	if (vs.playing)
		p = vs.base_pos + (int)((now - vs.t0) * vs.speed);
	return p < 0 ? 0 : p;
}

static void load_strip(void)
{
	img_free(vs.strip);
	vs.strip = NULL;
	vs.strip_path[0] = 0;
	if (!vs.m || vs.m->is_share || !vs.m->path[0])
		return;
	char jpg[288];
	snprintf(jpg, sizeof jpg, "%.*s.jpg",
		 (int)(strlen(vs.m->path) - 4), vs.m->path);
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

static void vid_list_of(const char *ctx)
{
	vs.nlist = 0;
	if (!ctx || !ctx[0] || !strcmp(ctx, "one")) {
		vs.list[vs.nlist++] = vs.m;
	} else if (!strncmp(ctx, "f:", 2)) {
		int n;
		VidClip *l = vid_clips(&n);
		for (int i = 0; i < n && vs.nlist < MAXCLIPS; i++)
			if (!strcmp(l[i].folder, ctx + 2))
				vs.list[vs.nlist++] = &l[i];
	} else if (!strncmp(ctx, "s:", 2)) {
		char sidc[8] = "";
		int di = 0, sid = -1;
		sscanf(ctx, "s:%7[^:]:%d", sidc, &di);
		for (int i = 0; i < vid_shares_count(); i++)
			if (!strcmp(vid_share(i)->id, sidc))
				sid = i;
		if (sid >= 0) {
			int n;
			VidClip *l = vid_share_clips(sid, di, &n);
			for (int i = 0; i < n && vs.nlist < MAXCLIPS; i++)
				vs.list[vs.nlist++] = &l[i];
		}
	} else {
		int n;
		VidClip *l = vid_clips(&n);
		for (int i = 0; i < n && vs.nlist < MAXCLIPS; i++)
			vs.list[vs.nlist++] = &l[i];
	}
	vs.idx = 0;
	for (int i = 0; i < vs.nlist; i++)
		if (vs.list[i] == vs.m)
			vs.idx = i;
}

static void play_run(int from)
{
	if (from >= 0)
		vs.m->pos = from;
	vs.base_pos = vs.m->pos >= vs.m->dur - 1 ? 0 : vs.m->pos;
	vs.playing = true;
	vs.t0 = gw_now();
	vs.hid = false;
	vs.view = V_PLAY;
	load_strip();
	scene_refresh();
}

/* resume flow (prototype act.play) — dialog callbacks via the stash */
static char res_id[40], res_ctx[32];
static void resume_yes(void)
{
	vs.m = vid_find(res_id);
	if (vs.m) {
		vid_list_of(res_ctx);
		play_run(-1);
	}
}
static void resume_no(void)
{
	vs.m = vid_find(res_id);
	if (vs.m) {
		vid_list_of(res_ctx);
		play_run(0);
	}
}

static void play_ctx(const char *id, const char *ctx)
{
	VidClip *c = vid_find(id);
	if (!c)
		return;
	vs.m = c;
	vid_list_of(ctx);
	snprintf(vs.ctx, sizeof vs.ctx, "%s", ctx);
	if (!started(c) || !strcmp(sv("vidResume"), "Always"))
		return play_run(-1);
	if (!strcmp(sv("vidResume"), "Never"))
		return play_run(0);
	char t1[12], t2[12];
	mmss(t1, sizeof t1, c->pos);
	mmss(t2, sizeof t2, c->dur);
	snprintf(res_id, sizeof res_id, "%s", id);
	snprintf(res_ctx, sizeof res_ctx, "%s", ctx);
	static DlgBtn b[2];
	static char lbl[48];
	snprintf(lbl, sizeof lbl, "Resume from %s", t1);
	b[0] = (DlgBtn){ lbl, "play", resume_yes, false };
	b[1] = (DlgBtn){ "Start from the beginning", "refresh", resume_no, false };
	dialog_open(astr("Resume \xe2\x80\x9c%s\xe2\x80\x9d?", c->title),
		    astr("You stopped at %s of %s.", t1, t2), b, 2, 0, NULL);
}

void video_seek(int secs)
{
	if (vs.view != V_PLAY || !vs.m)
		return;
	float now = gw_now();
	int p = cur_pos(now) + secs;
	if (p < 0) p = 0;
	if (p > vs.m->dur) p = vs.m->dur;
	vs.base_pos = p;
	vs.t0 = now;
	vs.hid = false;
	scene_mark_dirty();
}

static void toggle_play(void)
{
	float now = gw_now();
	if (vs.playing)
		vs.base_pos = cur_pos(now);
	else if (vs.base_pos >= vs.m->dur)
		vs.base_pos = 0;
	vs.playing = !vs.playing;
	vs.t0 = now;
	vs.hid = false;
	scene_mark_dirty();
}

static bool video_back(void)
{
	if (vs.view == V_HOME)
		return false;
	if (vs.view == V_PLAY) {
		vs.m->pos = cur_pos(gw_now());
		if (vs.m->pos >= vs.m->dur - 1)
			vs.m->watched = true;
		vs.view = vs.from_view;
		img_free(vs.strip);
		vs.strip = NULL;
		vs.strip_path[0] = 0;
		scene_mark_dirty();
		scene_refresh();
		return true;
	}
	vs.view = V_HOME;
	scene_mark_dirty();
	scene_refresh();
	return true;
}

static void video_tick(float now)
{
	if (vs.view == V_SHARE && vs.sh_loading && now - vs.sh_t0 > 0.9f) {
		vs.sh_loading = false;
		vs.sh_loaded = true; /* browse mock (network=mock) */
		scene_refresh();
		return;
	}
	if (vs.view != V_PLAY || !vs.m)
		return;
	if (!vs.playing) {
		vs.t0 = now;
		return;
	}
	bool want_hid = scene_idle() > 5.0f;
	if (want_hid != vs.hid) {
		vs.hid = want_hid;
		scene_mark_dirty();
	}
	int p = cur_pos(now);
	static long last_frame = -1;
	long frame = (long)p * FPS;
	if (frame != last_frame) {
		last_frame = frame;
		scene_mark_dirty(); /* 12 fps content + progress */
	}
	if (p >= vs.m->dur) {
		vs.base_pos = vs.m->dur;
		vs.playing = false;
		vs.hid = false;
		scene_mark_dirty();
	}
}

/* the player consumes ◀ ▶ (seek) and OK (pause); the rest passes */
static bool video_key(gw_key k, bool down, float now)
{
	(void)now;
	if (vs.view != V_PLAY || !down || !vs.m)
		return false;
	switch (k) {
	case GWK_OK:
		toggle_play();
		return true;
	case GWK_LEFT:
		video_seek(-10);
		return true;
	case GWK_RIGHT:
		video_seek(atoi(sv("vidSkip")));
		return true;
	default:
		return false;
	}
}

/* ── rows (settings-style, for the tab pages + share + info views) ─────── */
#define MAXR 14
static SRow vrows[MAXR];
static int nvrows;
static float rows_y0;

static SRow *radd(const char *t, const char *k, const char *act,
		  const char *title)
{
	if (nvrows >= MAXR)
		return &vrows[MAXR - 1];
	SRow *r = &vrows[nvrows++];
	memset(r, 0, sizeof *r);
	r->t = t;
	r->k = k;
	r->act = act;
	r->title = title;
	return r;
}

/* ── appBar: brand · centered tab pill · clock ─────────────────────────── */
static const struct { const char *id, *name, *ic; } VT[4] = {
	{ "videos", "Videos", "film" },
	{ "folders", "Folders", "folder" },
	{ "network", "Network", "globe" },
	{ "settings", "Settings", "settings" },
};

static void appbar_draw(gw_surface *s, Layer *L, const char *title_override)
{
	const App *a = app_info("video");
	gfx_rrect_g2(s, rrect(96, 58, 52, 52), 15, app_grad2(a));
	icon_draw(s, "play", 122, 84, 28, C_TEXT);
	void *f30 = font_get(700, 30);
	text_tl(f30, s, 162, 93, tx(title_override ? title_override : "Video"),
		C_TEXT);
	if (!title_override && L->ncnt >= 6) {
		void *f20 = font_get(600, 20);
		float w[4], total = 16;
		for (int i = 0; i < 4; i++) {
			w[i] = 52 + text_w(f20, VT[i].name);
			total += w[i] + 6;
		}
		float x = (OTV_W - total) / 2 + 8;
		gfx_rrect(s, rrect(x - 8, 48, total, 76), 38,
			  (Col){ 255, 255, 255, 20 });
		for (int i = 0; i < 4; i++) {
			R tr = rrect(x, 56, w[i], 60);
			float fp = L->n[i].fp;
			bool on = !strcmp(vs.tab, VT[i].id);
			if (fp > 0) {
				gfx_rrect(s, tr, 30, C_TEXT);
				Col glow = C_ACCENT;
				glow.a = (uint8_t)(128 * fp);
				gfx_glow(s, tr, 30, glow, fp);
			} else if (on) {
				gfx_rrect(s, tr, 30, (Col){ 255, 255, 255, 56 });
			}
			Col c = fp > 0 ? C_INK : (on ? C_TEXT : C_T2);
			icon_draw(s, VT[i].ic, x + 26, 86, 22, c);
			text_tl(f20, s, x + 58, 78, VT[i].name, c);
			x += w[i] + 6;
		}
	}
	text_tl(F36, s, OTV_W - 96 - text_w(F36, tx(clock_str())),
		91, tx(clock_str()), C_TEXT);
}

/* ── cards ─────────────────────────────────────────────────────────────── */
static void vcard_draw(gw_surface *s, FNode *f, VidClip *c, float sy)
{
	R r = f->r;
	R art = rrect(r.x, r.y - sy, r.w, 225);
	float fp = f->fp;
	if (fp > 0) {
		float cx = rcx(art), cy = rcy(art);
		art = rrect(cx - art.w * 0.53f, cy - art.h * 0.53f,
			    art.w * 1.06f, art.h * 1.06f);
	}
	art_draw(s, art, 24, c->title, "film", 195);
	if (fp > 0)
		draw_focus_ring(s, art, 24, fp, C_TEXT, 4);
	if (started(c)) {
		R pb = rrect(art.x + 14, art.y + art.h - 18, art.w - 28, 6);
		gfx_rrect(s, pb, 3, (Col){ 0, 0, 0, 102 });
		gfx_rrect(s, rrect(pb.x, pb.y, pb.w * c->pos / c->dur, 6), 3, C_TEXT);
	}
	void *f22b = font_get(600, 22);
	text_tl(f22b, s, r.x, r.y - sy + 252, tx(c->title), C_TEXT);
	text_tl(F18, s, r.x, r.y - sy + 275,
		vb("%d:%02d · %s", c->dur / 60, c->dur % 60, c->date), C_T3);
}

static void card_node(Layer *L, VidClip *c, const char *ctx, float x, float y,
		      bool def)
{
	FNode *f = lnode(L, rrect(x, y, 400, 280), vb("v%s", c->id),
			 vb("vplay:%s:%s", c->id, ctx), def);
	lnode_menu(f, vb("vmenu:%s", c->id));
}

static int grid_build(Layer *L, VidClip *const *cl, int n, const char *ctx,
		      float gx, float gy, bool def_first)
{
	for (int i = 0; i < n; i++)
		card_node(L, cl[i], ctx, gx + (i % 4) * (400 + 42),
			  gy + (i / 4) * (280 + 40), def_first && i == 0);
	return n;
}

static int grid_height(int n)
{
	return ((n + 3) / 4) * (280 + 40);
}

/* ── sorting (prototype VSORT) ─────────────────────────────────────────── */
static VidClip *sorted_cache[MAXCLIPS];
static int sorted_n;
static int sort_cmp(const void *pa, const void *pb)
{
	const VidClip *a = *(const VidClip *const *)pa;
	const VidClip *b = *(const VidClip *const *)pb;
	const char *s = sv("vidSort");
	int r = strcmp(a->folder, b->folder);
	if (!strcmp(s, "Oldest"))
		return r;
	if (!strcmp(s, "Name A–Z"))
		return strcasecmp(a->title, b->title);
	if (!strcmp(s, "Longest"))
		return b->dur - a->dur;
	if (!strcmp(s, "Largest"))
		return (int)((b->size - a->size) >> 20);
	return -r; /* Newest */
}
static void sorted_local(void)
{
	int n;
	VidClip *l = vid_clips(&n);
	if (n > MAXCLIPS)
		n = MAXCLIPS;
	for (int i = 0; i < n; i++)
		sorted_cache[i] = &l[i];
	qsort(sorted_cache, n, sizeof *sorted_cache, sort_cmp);
	sorted_n = n;
}

/* ── build ─────────────────────────────────────────────────────────────── */
static float scroll_content;

static void chip_node(Layer *L, const char *k, const char *act, float x,
		      float w, bool def)
{
	lnode(L, rrect(x, 176, w, 52), k, act, def);
}

static void video_build(Layer *L, const char *param)
{
	(void)param;
	arena_reset();
	nvrows = 0;
	rows_y0 = 200;
	scroll_content = 0;

	if (vs.view == V_PLAY) {
		lnode(L, rrect(96, 850, OTV_W - 192, 44), "vc-bar", "vseekbar", false);
		float row = vs.nlist > 1 ? 72 + 28 + 96 + 28 + 72 + 28 + 72
					 : 72 + 28 + 96 + 28 + 72;
		float cx = (OTV_W - row) / 2;
		lnode(L, rrect(cx, 960, 72, 72), "vc-b", "vseekm", false);
		lnode(L, rrect(cx + 100, 936, 96, 96), "vc-play", "vpause", true);
		lnode(L, rrect(cx + 224, 960, 72, 72), "vc-f", "vseekp", false);
		if (vs.nlist > 1)
			lnode(L, rrect(cx + 324, 960, 72, 72), "vc-next", "vnext", false);
		void *f19 = font_get(600, 19);
		float px = OTV_W - 96;
		const char *ccl = vs.cc[0] ? vs.cc : "Off";
		float wcc = 22 + 26 + 10 + text_w(f19, ccl) + 22;
		lnode(L, rrect(px - wcc, 964, wcc, 64), "vc-cc", "vcc", false);
		px -= wcc + 14;
		float waud = 22 + 26 + 10 + text_w(f19, "Audio") + 22;
		lnode(L, rrect(px - waud, 964, waud, 64), "vc-aud", "vaud", false);
		px -= waud + 14;
		char sp[8];
		snprintf(sp, sizeof sp, "%.2g×", vs.speed);
		float wsp = 22 + 26 + 10 + text_w(f19, sp) + 22;
		lnode(L, rrect(px - wsp, 964, wsp, 64), "vc-sp", "vspeed", false);
		px -= wsp + 14;
		lnode(L, rrect(px - 70, 964, 70, 64), "vc-zoom", "vzoom", false);
		px -= 70 + 14;
		lnode(L, rrect(px - 70, 964, 70, 64), "vc-inf", "vminfo", false);
		return;
	}

	if (vs.view == V_HOME) {
		/* tabs: real rects so the mouse can hover them (draw re-reads fp) */
		void *f20 = font_get(600, 20);
		float tw[4], total = 16;
		for (int i = 0; i < 4; i++) {
			tw[i] = 52 + text_w(f20, VT[i].name);
			total += tw[i] + 6;
		}
		float x = (OTV_W - total) / 2 + 8;
		for (int i = 0; i < 4; i++) {
			lnode(L, rrect(x, 56, tw[i], 60), vb("vt-%s", VT[i].id),
			      vb("vtab:%s", VT[i].id), !strcmp(vs.tab, VT[i].id));
			x += tw[i] + 6;
		}

		if (!strcmp(vs.tab, "videos")) {
			sorted_local();
			VidClip *cont[MAXCLIPS];
			int nc = 0;
			for (int i = 0; i < sorted_n; i++)
				if (started(sorted_cache[i]))
					cont[nc++] = sorted_cache[i];
			/* chips right of the header (Sort · Play a link) */
			void *f20 = font_get(600, 20);
			const char *srt = astr("Sort: %s", sv("vidSort"));
			float ws = 26 + 22 + 10 + text_w(f20, srt) + 26;
			float wl = 26 + 22 + 10 + text_w(f20, "Play a link") + 26;
			chip_node(L, "vh-sort", "vsort", 1824 - ws - 14 - wl, wl, false);
			chip_node(L, "vh-link", "vlink", 1824 - ws, ws, false);
			(void)srt;
			float y = 256;
			if (nc) {
				grid_build(L, cont, nc, "all", 96, y, true);
				y += nc * 280 + 30;
			}
			float gy = y + 20 + 34 + 22;
			grid_build(L, sorted_cache, sorted_n, "all", 96, gy, !nc);
			scroll_content = gy + grid_height(sorted_n) + 40;
			return;
		}

		if (!strcmp(vs.tab, "folders")) {
			int n;
			(void)vid_clips(&n);
			const char *folders[8];
			int nf = 0;
			int dummy;
			VidClip *l = vid_clips(&dummy);
			for (int i = 0; i < n && nf < 8; i++) {
				bool has = false;
				for (int k = 0; k < nf; k++)
					if (!strcmp(folders[k], l[i].folder))
						has = true;
				if (!has)
					folders[nf++] = l[i].folder;
			}
			for (int i = 0; i < nf; i++) {
				int m, cnt = 0;
				long bytes = 0;
				VidClip *lv = vid_clips(&m);
				for (int k = 0; k < m; k++)
					if (!strcmp(lv[k].folder, folders[i])) {
						cnt++;
						bytes += lv[k].size;
					}
				const char *root = otv_device_str(
					"storage",
					!strcmp(folders[i], "USB drive") ? "external" : "internal",
					"/media/internal");
				SRow *r = radd("nav", astr("vf-%d", i),
					       astr("vfolder:%s", folders[i]), folders[i]);
				r->icon = "folder";
				r->d = astr("%s", root);
				r->v = astr("%d video%s · %ld MB", cnt,
					    cnt == 1 ? "" : "s", bytes >> 20);
			}
		} else if (!strcmp(vs.tab, "network")) {
			for (int i = 0; i < vid_shares_count(); i++) {
				const VShare *sh = vid_share(i);
				SRow *r = radd("nav", astr("sh-%s", sh->id),
					       astr("vshare:%d", i), sh->name);
				r->icon = !strcmp(sh->proto, "DLNA") ? "monitor" : "folder";
				r->d = astr("%s · %s", sh->proto, sh->addr);
			}
			SRow *r = radd("act", "sh-add", "vaddshare",
				       "Add a network share");
			r->icon = "plus";
			r->d = "SMB or NFS, like smb://192.168.1.20/media";
			r = radd("act", "ln-play", "vlink", "Play a link");
			r->icon = "link";
			r->d = "A video address or a live stream (HTTP, HLS or RTSP)";
		} else { /* settings */
			SRow *r = radd("opt", "vp-resume", "vpref:vidResume",
				       "Resume where you stopped");
			r->key = "vidResume";
			r = radd("opt", "vp-subs", "vpref:vidSubs",
				 "Subtitles by default");
			r->key = "vidSubs";
			r = radd("opt", "vp-skip", "vpref:vidSkip", "Skip forward by");
			r->key = "vidSkip";
			r = radd("tog", "vp-hw", "vhw", "Hardware decoding");
			r->key = "vidHw";
			r->d = "Uses the TV's video chip. Turn it off if a file plays badly";
			r = radd("act", "vp-clr", "vclear", "Clear watch history");
			r->d = "Forgets where you stopped and what you've watched";
		}
		for (int i = 0; i < nvrows; i++)
			lnode(L, rrect(96, rows_y0 + i * 100, 1300, 88), vrows[i].k,
			      vrows[i].act, i == 0);
		scroll_content = rows_y0 + nvrows * 100 + 40;
		return;
	}

	if (vs.view == V_FOLDER) {
		VidClip *cl[MAXCLIPS];
		int n = 0, total;
		VidClip *l = vid_clips(&total);
		for (int i = 0; i < total && n < MAXCLIPS; i++)
			if (!strcmp(l[i].folder, vs.folder))
				cl[n++] = &l[i];
		grid_build(L, cl, n, astr("f:%s", vs.folder), 96, 200, true);
		scroll_content = 200 + grid_height(n) + 40;
		return;
	}

	if (vs.view == V_SHARE) {
		if (vs.sh_err) {
			SRow *r = radd("info", "sv-err", NULL,
				       astr("Couldn't reach %s", vid_share(vs.share)->name));
			r->icon = "wifioff";
			r->d = "Check that it's turned on and on the same network as the TV";
			r = radd("act", "sv-retry", "vretry", "Try again");
			r->icon = "refresh";
		} else if (vs.sh_loading || !vs.sh_loaded) {
			SRow *r = radd("info", "sv-load", NULL,
				       astr("Connecting to %s…", vid_share(vs.share)->name));
			r->d = vid_share(vs.share)->addr;
			r->busy = true;
		} else {
			const char *sid = vid_share(vs.share)->id;
			float y = 200;
			for (int d = 0; d < vid_share_dirs(vs.share); d++) {
				int n;
				VidClip *l = vid_share_clips(vs.share, d, &n);
				VidClip *ptrs[MAXCLIPS];
				for (int i = 0; i < n && i < MAXCLIPS; i++)
					ptrs[i] = &l[i];
				y += 34 + 22;
				grid_build(L, ptrs, n, astr("s:%s:%d", sid, d), 96, y, d == 0);
				y += grid_height(n);
			}
			scroll_content = y + 60;
			return;
		}
		for (int i = 0; i < nvrows; i++)
			lnode(L, rrect(96, rows_y0 + i * 100, 1300, 88), vrows[i].k,
			      vrows[i].act, i == 0);
		scroll_content = rows_y0 + nvrows * 100 + 40;
		return;
	}

	if (vs.view == V_INFO) {
		VidClip *c = vid_find(vs.info_id);
		if (!c)
			return;
		bool st = started(c);
		lnode(L, rrect(96, 726, 320, 72), "vi-play",
		      astr("vplay:%s:one", c->id), true);
		lnode(L, rrect(96 + 334, 726, 200, 72), "vi-ren", "vren", false);
		lnode(L, rrect(96 + 548, 726, 200, 72), "vi-del",
		      astr("vdel:%s", c->id), false);
		(void)st;
		const char *info[8][2] = {
			{ "Resolution", c->res[0] ? c->res : "—" },
			{ "Video", c->codec[0] ? astr("%s · %d fps", c->codec, c->fps) : "—" },
			{ "Audio", "Original" },
			{ "Subtitles", "English" },
			{ "Length", astr("%d:%02d", c->dur / 60, c->dur % 60) },
			{ "Size", astr("%ld MB", c->size >> 20) },
			{ "Share", c->net[0] ? c->net : "—" },
			{ "Location", c->path },
		};
		for (int i = 0; i < 8; i++) {
			SRow *r = radd("info", astr("vif-%d", i), NULL, info[i][0]);
			r->v = info[i][1];
		}
		for (int i = 0; i < nvrows; i++)
			lnode(L, rrect(880, 200 + i * 88, 944, 88), vrows[i].k,
			      vrows[i].act, false);
	}
}

/* ── draw helpers for rows ─────────────────────────────────────────────── */
static void rows_draw(gw_surface *s, Layer *L, int n0, float sy)
{
	glass(s, rrect(96, rows_y0 - sy, 1300, nvrows * 100 - 12), 28);
	for (int i = 0; i < nvrows; i++) {
		R rr = L->n[n0 + i].r;
		rr.y -= sy;
		row_draw(s, L, n0 + i, &vrows[i], rr, gw_now(), i == nvrows - 1);
	}
}

/* local chip draw (fchip with an icon, prototype .fchip glass) */
static void vchip_draw(gw_surface *s, FNode *f, const char *ic, const char *lb)
{
	R r = f->r;
	float sc = 1 + 0.06f * f->fp;
	if (sc != 1) {
		float cx = rcx(r), cy = rcy(r);
		r = rrect(cx - r.w * sc / 2, cy - r.h * sc / 2, r.w * sc, r.h * sc);
	}
	glass(s, r, 26);
	Col c = C_T2;
	if (f->fp > 0) {
		gfx_rrect(s, r, 26, C_TEXT);
		c = C_INK;
	}
	icon_draw(s, ic, r.x + 26 + 11, rcy(r), 22, c);
	void *f20 = font_get(600, 20);
	text_tl(f20, s, r.x + 26 + 22 + 10, rcy(r) - 10, lb, c);
}

/* ── draw ──────────────────────────────────────────────────────────────── */
static void video_draw(gw_surface *s, Layer *L, float now)
{
	if (vs.view == V_PLAY) {
		int p = cur_pos(now);
		VidClip *m = vs.m;
		float sc = vs.zoom == 1 ? 1.18f : vs.zoom == 2 ? 0.78f : 1.0f;
		R dst = rrect(OTV_W / 2 - OTV_W * sc / 2, OTV_H / 2 - OTV_H * sc / 2,
			      OTV_W * sc, OTV_H * sc);
		if (vs.strip) {
			int total = vs.strip_cols * vs.strip_rows;
			int fi = p * FPS % total;
			img_draw(s, vs.strip, dst, (fi % vs.strip_cols) * FW,
				 (fi / vs.strip_cols) * FH, FW, FH);
		} else {
			gfx_clear(s, (Col){ 0, 0, 0, 255 });
			art_draw(s, rrect(dst.x + 240 * sc, dst.y, 1440 * sc,
					  OTV_H * sc), 0, m->title, "film", 390);
		}
		if (!vs.playing) { /* pause overlay */
			gfx_fill(s, rrect(0, 0, OTV_W, OTV_H), (Col){ 0, 0, 0, 89 });
			gfx_rrect(s, rrect(OTV_W / 2 - 75, OTV_H / 2 - 75, 150, 150),
				  75, (Col){ 0, 0, 0, 128 });
			icon_draw(s, "pause", OTV_W / 2, OTV_H / 2, 64, C_TEXT);
		}
		if (vs.cc[0] && vs.playing) { /* subtitle (moves down when hidden) */
			const char *line = subs_line();
			if (line[0]) {
				void *f30 = font_get(500, 30);
				int w = text_w(f30, tx(line));
				R pill = rrect(OTV_W / 2 - w / 2 - 18,
						(vs.hid ? 990 : 710) - 50, w + 36, 58);
				gfx_rrect(s, pill, 8, (Col){ 0, 0, 0, 184 });
				text_c(f30, s, rcx(pill), rcy(pill) + 11, tx(line), C_TEXT);
			}
		}
		if (!vs.hid) {
			for (int y = 0; y < 220; y++) {
				uint8_t a = (uint8_t)(191 * (1 - y / 220.0f));
				gfx_fill(s, rrect(0, y, OTV_W, 1), (Col){ 5, 7, 13, a });
			}
			for (int y = 0; y < 500; y++) {
				float t = y / 500.0f;
				uint8_t a = t < 0.6f
					? (uint8_t)(235 - (235 - 140) * (t / 0.6f))
					: (uint8_t)(140 * (1 - (t - 0.6f) / 0.4f));
				gfx_fill(s, rrect(0, OTV_H - 500 + y, OTV_W, 1),
					 (Col){ 5, 7, 13, a });
			}
			gfx_rrect_g2(s, rrect(96, 56, 44, 44), 13,
				     app_grad2(app_info("video")));
			icon_draw(s, "play", 118, 78, 22, C_TEXT);
			text_tl(F22, s, 156, 86, "Video", C_T2);
			text_tl(F36, s, OTV_W - 96 - text_w(F36, tx(clock_str())),
				85, tx(clock_str()), C_TEXT);

			void *f48 = font_get(700, 48);
			text_tl(f48, s, 96, 788, tx(m->title), C_TEXT);
			const char *sub = m->net[0]
				? vb("%s · %s", m->net, m->folder)
				: vb("%s · %s", m->folder, m->date);
			if (vs.nlist > 1)
				sub = vb("%s · %d of %d", sub, vs.idx + 1, vs.nlist);
			text_tl(F22, s, 96, 823, sub, C_T2);

			VidClip *nx = vs.nlist > 1 && vs.idx < vs.nlist - 1
				? vs.list[vs.idx + 1] : NULL;
			if (nx && m->dur - p <= 15) { /* up next */
				R card = rrect(OTV_W - 96 - 440, 640, 440, 160);
				gfx_rrect(s, card, 24, (Col){ 20, 26, 44, 178 });
				void *f16 = font_get(700, 16);
				text_tl(f16, s, card.x + 24, card.y + 30,
					vb("UP NEXT · IN %d S", m->dur - p), C_T3);
				void *f24 = font_get(600, 24);
				text_tl(f24, s, card.x + 24, card.y + 78,
					tx(nx->title), C_TEXT);
			}

			/* seek bar (node 0) + times */
			float pc = m->dur ? (float)p / m->dur : 0;
			float barfp = L->n[0].fp;
			float th = barfp > 0 ? 12 : 8;
			R track = rrect(96, 876 - th / 2, OTV_W - 192, th);
			gfx_rrect(s, track, th / 2, (Col){ 255, 255, 255, 56 });
			if (m->dur)
				gfx_rrect(s, rrect(track.x, track.y, track.w * pc, th),
					  th / 2, C_TEXT);
			if (barfp > 0) {
				float kx = track.x + track.w * pc;
				gfx_ring(s, rrect(kx - 17, 876 - 17, 34, 34), 17, 6,
					 (Col){ 255, 255, 255, 64 });
				gfx_rrect(s, rrect(kx - 14, 876 - 14, 28, 28), 14, C_TEXT);
			}
			char t1[12], t2s[12];
			mmss(t1, sizeof t1, p);
			mmss(t2s, sizeof t2s, m->dur);
			text_tl(F18, s, 96, 915, t1, C_T2);
			if (vs.speed != 1) {
				char spd[12];
				snprintf(spd, sizeof spd, "Speed %.2g×", vs.speed);
				text_tl(F18, s, OTV_W / 2 - text_w(F18, spd) / 2, 915, spd,
					C_T2);
			}
			text_tl(F18, s, OTV_W - 96 - text_w(F18, t2s), 915, t2s, C_T2);
			text_tl(F18, s, 96, 1008, "◀ ▶ Seek", C_T3);
			text_tl(F18, s, 236, 1008, "OK Pause", C_T3);

			/* controls */
			float row = vs.nlist > 1 ? 72 + 28 + 96 + 28 + 72 + 28 + 72
						 : 72 + 28 + 96 + 28 + 72;
			float cx0 = (OTV_W - row) / 2;
			struct Ctl { const FNode *f; float c; bool big; const char *ic; };
			struct Ctl cts[4];
			int nc = 0;
			cts[nc] = (struct Ctl){ &L->n[1], cx0 + 36, false, "skipb" }; nc++;
			cts[nc] = (struct Ctl){ &L->n[2], cx0 + 148, true,
						  vs.playing ? "pause" : "play" }; nc++;
			cts[nc] = (struct Ctl){ &L->n[3], cx0 + 260, false, "skipf" }; nc++;
			if (vs.nlist > 1) {
				cts[nc] = (struct Ctl){ &L->n[4], cx0 + 360, false,
							  "arrowr" }; nc++;
			}
			for (int i = 0; i < nc; i++) {
				float rad = cts[i].big ? 48 : 36;
				R r = rrect(cts[i].c - rad, (cts[i].big ? 984 : 996) - rad,
					    rad * 2, rad * 2);
				gfx_rrect(s, r, (int)(r.h / 2), (Col){ 255, 255, 255, 36 });
				Col ic = C_TEXT;
				if (cts[i].f->fp > 0) {
					gfx_rrect(s, r, (int)(r.h / 2), C_TEXT);
					ic = C_INK;
					Col glow = C_ACCENT;
					glow.a = (uint8_t)(128 * cts[i].f->fp);
					gfx_glow(s, r, (int)(r.h / 2), glow, cts[i].f->fp);
				}
				icon_draw(s, cts[i].ic, cts[i].c,
					  cts[i].big ? 984 : 996,
					  cts[i].big ? 38 : 30, ic);
			}

			/* right pills */
			void *f19 = font_get(600, 19);
			int p0 = 5; /* first pill node index */
			const char *pl[5][2] = {
				{ "cc", vs.cc[0] ? vs.cc : "Off" },
				{ "speaker", "Audio" },
				{ NULL, vb("%.2g×", vs.speed) },
				{ "display", NULL },
				{ "info", NULL },
			};
			for (int i = 0; i < 5; i++) {
				const FNode *f = &L->n[p0 + i];
				R r = f->r;
				gfx_rrect(s, r, 32, (Col){ 255, 255, 255, 36 });
				Col ic = C_TEXT, tc = C_TEXT;
				if (f->fp > 0) {
					gfx_rrect(s, r, 32, C_TEXT);
					ic = C_INK;
					tc = C_INK;
				}
				float x = r.x + 22;
				if (pl[i][0]) {
					icon_draw(s, pl[i][0], x + 13, rcy(r), 26, ic);
					x += 36;
				}
				if (pl[i][1])
					text_tl(f19, s, x, rcy(r) - 9, pl[i][1], tc);
			}
		}
		return;
	}

	/* ── non-player views ── */
	if (vs.view == V_HOME)
		appbar_draw(s, L, NULL);
	else if (vs.view == V_FOLDER)
		appbar_draw(s, L, vb("Video › %s", vs.folder));
	else if (vs.view == V_SHARE)
		appbar_draw(s, L, vb("Video › %s", vid_share(vs.share)->name));
	else
		appbar_draw(s, L, NULL);

	float sy = L->scroll_y;
	gfx_clip(rrect(0, 160, OTV_W, OTV_H - 160 - 80));

	if (vs.view == V_HOME) {
		if (!strcmp(vs.tab, "videos")) {
			sorted_local();
			VidClip *cont[MAXCLIPS];
			int nc = 0;
			for (int i = 0; i < sorted_n; i++)
				if (started(sorted_cache[i]))
					cont[nc++] = sorted_cache[i];
			long bytes = 0;
			for (int i = 0; i < sorted_n; i++)
				bytes += sorted_cache[i]->size;
			void *f22 = font_get(500, 22);
			text_tl(f22, s, 96, 200,
				vb("%d video%s · %ld MB", sorted_n,
				     sorted_n == 1 ? "" : "s", bytes >> 20), C_T2);
			/* chips: nodes 4 (link) and 5 (sort) */
			void *f20 = font_get(600, 20);
			vchip_draw(s, &L->n[4], "link", "Play a link");
			vchip_draw(s, &L->n[5], "list", vb("Sort: %s", sv("vidSort")));
			float y = 256;
			if (nc) {
				text_tl(F28, s, 96, 234, "Continue watching", C_TEXT);
				for (int i = 0; i < nc; i++)
					vcard_draw(s, &L->n[6 + i], cont[i], sy);
				y = 256 + nc * 280 + 30;
			}
			float hy = y + 20;
			text_tl(F28, s, 96, hy + 24, "All videos", C_TEXT);
			const char *hint = "Hold OK for more options";
			text_tl(f20, s, 1824 - text_w(f20, hint), hy + 24, hint, C_T2);
			float gy = hy + 34 + 22;
			for (int i = 0; i < sorted_n; i++)
				vcard_draw(s, &L->n[6 + nc + i], sorted_cache[i], sy);
			if (!sorted_n) {
				glass(s, rrect(96, gy, 1728, 140), 28);
				text_tl(f22, s, 128, gy + 70,
					tx("No videos on this TV yet. Videos you record or download appear here."),
					C_T2);
			}
		} else {
			int n0 = 4;
			if (!strcmp(vs.tab, "folders")) {
				text_tl(F18, s, 108, 214, "On this TV", C_T3);
				rows_draw(s, L, n0, sy);
			} else if (!strcmp(vs.tab, "network")) {
				int ns = vid_shares_count();
				text_tl(F18, s, 108, 214,
					"Shares on your network · hold OK to forget one", C_T3);
				rows_draw(s, L, n0, sy); /* one group: shares+add+link */
			} else { /* settings */
				text_tl(F18, s, 108, 214, "Playback", C_T3);
				int n0s = n0;
				glass(s, rrect(96, rows_y0 - sy, 1300, 4 * 100 - 12), 28);
				for (int i = 0; i < 4; i++) {
					R rr = L->n[n0s + i].r;
					rr.y -= sy;
					row_draw(s, L, n0s + i, &vrows[i], rr, now, i == 3);
				}
				float y2 = rows_y0 + 4 * 100 + 30;
				text_tl(F18, s, 108, y2 - sy + 14, "History", C_T3);
				glass(s, rrect(96, y2 + 24 - sy, 1300, 100 - 12), 28);
				R rr = L->n[n0s + 4].r;
				rr.y -= sy;
				row_draw(s, L, n0s + 4, &vrows[4], rr, now, true);
			}
		}
	} else if (vs.view == V_FOLDER) {
		int total, n = 0;
		VidClip *l = vid_clips(&total);
		for (int i = 0; i < total; i++)
			if (!strcmp(l[i].folder, vs.folder))
				n++;
		for (int i = 0; i < n; i++)
			vcard_draw(s, &L->n[i], &l[i], sy);
	} else if (vs.view == V_SHARE) {
		if (vs.sh_err || vs.sh_loading || !vs.sh_loaded) {
			rows_draw(s, L, 0, sy);
		} else {
			int ni = 0;
			float y = 200;
			void *f20 = font_get(600, 20);
			for (int d = 0; d < vid_share_dirs(vs.share); d++) {
				int n;
				VidClip *l = vid_share_clips(vs.share, d, &n);
				y += 34 + 22;
				text_tl(F28, s, 96, y - 22 + 24,
					vid_share_dir_name(vs.share, d), C_TEXT);
				char cnt[24];
				snprintf(cnt, sizeof cnt, "%d videos", n);
				text_tl(f20, s, 1824 - text_w(f20, cnt), y - 22 + 24, cnt,
					C_T2);
				for (int i = 0; i < n; i++)
					vcard_draw(s, &L->n[ni + i], &l[i], sy);
				ni += n;
				y += grid_height(n);
			}
		}
	} else if (vs.view == V_INFO) {
		VidClip *c = vid_find(vs.info_id);
		if (c) {
			R art = rrect(96, 170, 720, 405);
			art_draw(s, art, 32, c->title, "film", 260);
			gfx_ring(s, art, 32, 1, (Col){ 255, 255, 255, 36 });
			if (started(c)) {
				R pb = rrect(art.x + 20, art.y + art.h - 26, art.w - 40, 8);
				gfx_rrect(s, pb, 4, (Col){ 0, 0, 0, 102 });
				gfx_rrect(s, rrect(pb.x, pb.y, pb.w * c->pos / c->dur, 8), 4,
					  C_TEXT);
			}
			void *f48 = font_get(700, 48);
			text_tl(f48, s, 96, 654, tx(c->title), C_TEXT);
			char left[12];
			mmss(left, sizeof left, c->dur - c->pos);
			const char *st = c->watched ? "Watched"
				: started(c) ? vb("%s left", left)
					     : "Not watched yet";
			text_tl(F22, s, 96, 696,
				vb("%d:%02d · %s", c->dur / 60, c->dur % 60, st), C_T2);
			btn_draw(s, &L->n[0], started(c) ? "Resume" : "Play", "play", 0);
			btn_draw(s, &L->n[1], "Rename", NULL, 2);
			btn_draw(s, &L->n[2], "Delete", "trash", 2);
			text_tl(F18, s, 892, 186, "Details", C_T3);
			glass(s, rrect(880, 200, 944, nvrows * 88), 28);
			for (int i = 0; i < nvrows; i++) {
				R rr = L->n[3 + i].r;
				row_draw(s, L, 3 + i, &vrows[i], rr, now, i == nvrows - 1);
			}
		}
	}
	gfx_clip_off();
}

/* ── acts ──────────────────────────────────────────────────────────────── */
static char ppkey[24]; /* the pref a picker is choosing */

static void onpick_sort(const char *v)
{
	sv_set("vidSort", v);
	scene_refresh();
	toast(astr("Sorted: %s", v), "list");
}
static void onpick_pref(const char *v)
{
	sv_set(ppkey, v);
	scene_refresh();
}
static void onpick_speed(const char *v)
{
	vs.speed = atof(v);
	scene_refresh();
}
static void onpick_zoom(const char *v)
{
	vs.zoom = !strcmp(v, "Fill") ? 1 : !strcmp(v, "Original") ? 2 : 0;
	scene_refresh();
}
static void onpick_cc(const char *v)
{
	snprintf(vs.cc, sizeof vs.cc, "%s", strcmp(v, "Off") ? v : "");
	scene_refresh();
}

static void vm_details(void)
{
	vs.view = V_INFO;
	snprintf(vs.info_id, sizeof vs.info_id, "%s", res_id);
	scene_refresh();
}static void vm_watch(void)
{
	VidClip *c = vid_find(res_id);
	if (!c)
		return;
	c->watched = !c->watched;
	if (c->watched)
		c->pos = 0;
	scene_refresh();
	toast(c->watched ? "Marked as watched" : "Marked as not watched", "check");
}
static void vm_rename(void)
{
	toast("The on-screen keyboard arrives in a later build", "notes");
}
static void del_yes(void);
static void vm_delete(void)
{
	VidClip *c = vid_find(res_id);
	if (!c)
		return;
	static DlgBtn b[2];
	b[0] = (DlgBtn){ "Delete", "trash", del_yes, true };
	b[1] = (DlgBtn){ "Cancel", "x", NULL, false };
	dialog_open(astr("Delete \xe2\x80\x9c%s\xe2\x80\x9d?", c->title),
		    astr("The file (%ld MB) is removed from this TV. This can't be undone.",
			 c->size >> 20), b, 2, 1, NULL);
}
static void del_yes(void)
{
	VidClip *c = vid_find(res_id);
	if (!c)
		return;
	if (!c->is_share) {
		char side[292];
		unlink(c->path);
		snprintf(side, sizeof side, "%.*s.jpg",
			 (int)(strlen(c->path) - 4), c->path);
		unlink(side);
		snprintf(side, sizeof side, "%.*s.dur",
			 (int)(strlen(c->path) - 4), c->path);
		unlink(side);
		toast(astr("%s deleted", c->title), "trash");
		/* rescan on the next visit (sys.c owns the table) */
		vid_rescan();
		vs.view = V_HOME;
		scene_refresh();
	}
}

static void clearhist_yes(void)
{
	int n;
	VidClip *l = vid_clips(&n);
	for (int i = 0; i < n; i++) {
		l[i].pos = 0;
		l[i].watched = false;
	}
	for (int s = 0; s < vid_shares_count(); s++)
		for (int d = 0; d < vid_share_dirs(s); d++) {
			int m;
			VidClip *sl = vid_share_clips(s, d, &m);
			for (int i = 0; i < m; i++) {
				sl[i].pos = 0;
				sl[i].watched = false;
			}
		}
	scene_refresh();
	toast("Watch history cleared", "check");
}
static void clearhist_dlg(void)
{
	static DlgBtn b[2];
	b[0] = (DlgBtn){ "Clear", "trash", clearhist_yes, true };
	b[1] = (DlgBtn){ "Cancel", "x", NULL, false };
	dialog_open("Clear watch history?",
		    "Where you stopped and what you've watched will be forgotten. Your videos stay on the TV.",
		    b, 2, 1, NULL);
}

static void forget_share(void)
{
	toast(astr("%s forgotten", vid_share(vs.share)->name), "trash");
	/* the mock list returns on restart; a real client removes the entry */
}
static void forget_dlg(void)
{
	static DlgBtn b[2];
	b[0] = (DlgBtn){ "Forget share", "trash", forget_share, true };
	b[1] = (DlgBtn){ "Cancel", "x", NULL, false };
	dialog_open(vid_share(vs.share)->name,
		    astr("The videos stay on %s. Only this TV forgets the share.",
			 vid_share(vs.share)->name), b, 2, 1, NULL);
}

static const char *const o_sort[] = { "Newest", "Oldest", "Name A–Z",
				      "Longest", "Largest", NULL };
static const char *const o_resume[] = { "Ask", "Always", "Never", NULL };
static const char *const o_subs[] = { "Off", "English", "Español", "Română", NULL };
static const char *const o_skip[] = { "10 seconds", "30 seconds", "60 seconds", NULL };
static const char *const o_speed[] = { "1×", "1.25×", "1.5×", "2×", NULL };
static const char *const o_zoom[] = { "Fit", "Fill", "Original", NULL };

bool video_act(const char *cmd, const char *arg)
{
	if (!cmd)
		return false;
	if (!strcmp(cmd, "vtab")) {
		snprintf(vs.tab, sizeof vs.tab, "%s", arg);
		scene_refresh();
		return true;
	}
	if (!strcmp(cmd, "vplay")) {
		char sp[96];
		snprintf(sp, sizeof sp, "%s", arg);
		char *c1 = strchr(sp, ':');
		const char *ctx = "one";
		if (c1) {
			*c1 = 0;
			ctx = c1 + 1;
		}
		if (vs.view != V_PLAY)
			vs.from_view = vs.view;
		play_ctx(sp, ctx);
		return true;
	}
	if (!strcmp(cmd, "vmenu")) {
		VidClip *c = vid_find(arg);
		if (!c)
			return true;
		snprintf(res_id, sizeof res_id, "%s", arg);
		snprintf(res_ctx, sizeof res_ctx, "%s", "one");
		static DlgBtn b[6];
		int nb = 0;
		b[nb++] = (DlgBtn){ started(c) ? "Resume" : "Play", "play",
				    resume_yes, false };
		if (started(c))
			b[nb++] = (DlgBtn){ "Play from the beginning", "refresh",
					    resume_no, false };
		b[nb++] = (DlgBtn){ "Details", "info", vm_details, false };
		b[nb++] = (DlgBtn){ c->watched ? "Mark as not watched"
					       : "Mark as watched",
				    "check", vm_watch, false };
		b[nb++] = (DlgBtn){ "Rename", "notes", vm_rename, false };
		b[nb++] = (DlgBtn){ "Delete", "trash", vm_delete, true };
		dialog_open(c->title, "", b, nb, 0, NULL);
		return true;
	}
	if (!strcmp(cmd, "vsort")) {
		picker_open("Sort videos", NULL, o_sort, 5, sv("vidSort"), onpick_sort);
		return true;
	}
	if (!strcmp(cmd, "vpref")) {
		snprintf(ppkey, sizeof ppkey, "%s", arg);
		const char *const *opts = o_resume;
		int n = 3;
		const char *title = "Resume where you stopped";
		if (!strcmp(arg, "vidSubs")) {
			opts = o_subs;
			title = "Subtitles by default";
		} else if (!strcmp(arg, "vidSkip")) {
			opts = o_skip;
			title = "Skip forward by";
		}
		picker_open(title, NULL, opts, n, sv(arg), onpick_pref);
		return true;
	}
	if (!strcmp(cmd, "vhw")) {
		sv_seti("vidHw", sv_true("vidHw") ? 0 : 1);
		scene_refresh();
		toast(sv_true("vidHw") ? "Hardware decoding on"
				       : "Hardware decoding off · 4K videos may stutter",
		      "info");
		return true;
	}
	if (!strcmp(cmd, "vclear")) {
		clearhist_dlg();
		return true;
	}
	if (!strcmp(cmd, "vfolder")) {
		snprintf(vs.folder, sizeof vs.folder, "%s", arg);
		vs.view = V_FOLDER;
		scene_refresh();
		return true;
	}
	if (!strcmp(cmd, "vshare")) {
		vs.share = atoi(arg);
		vs.sh_loading = true;
		vs.sh_loaded = vs.sh_err = false;
		vs.sh_t0 = gw_now();
		vs.view = V_SHARE;
		scene_refresh();
		return true;
	}
	if (!strcmp(cmd, "vretry")) {
		vs.sh_err = false;
		vs.sh_loading = true;
		vs.sh_t0 = gw_now();
		scene_refresh();
		return true;
	}
	if (!strcmp(cmd, "vsmenu")) {
		vs.share = atoi(arg);
		forget_dlg();
		return true;
	}
	if (!strcmp(cmd, "vinfo") || !strcmp(cmd, "vminfo")) {
		const char *id = arg;
		if (!strcmp(cmd, "vminfo")) {
			if (!vs.m)
				return true;
			id = vs.m->id;
		}
		snprintf(vs.info_id, sizeof vs.info_id, "%s", id);
		vs.from_view = vs.view == V_PLAY ? vs.from_view : vs.view;
		vs.view = V_INFO;
		scene_refresh();
		return true;
	}
	if (!strcmp(cmd, "vseekbar")) {
		float x, y;
		scene_click_pos(&x, &y);
		int t = vs.m ? (int)((x - 96) / (OTV_W - 192) * vs.m->dur) : 0;
		video_seek(t - cur_pos(gw_now()));
		return true;
	}
	if (!strcmp(cmd, "vseekm")) {
		video_seek(-10);
		return true;
	}
	if (!strcmp(cmd, "vseekp")) {
		video_seek(atoi(sv("vidSkip")));
		return true;
	}
	if (!strcmp(cmd, "vpause")) {
		toggle_play();
		return true;
	}
	if (!strcmp(cmd, "vnext")) {
		if (vs.nlist > 1 && vs.idx < vs.nlist - 1) {
			vs.m = vs.list[vs.idx + 1];
			vid_list_of(vs.ctx);
			play_run(-1);
		}
		return true;
	}
	if (!strcmp(cmd, "vcc")) {
		picker_open("Subtitles", NULL, o_subs, 4,
			    vs.cc[0] ? vs.cc : "Off", onpick_cc);
		return true;
	}
	if (!strcmp(cmd, "vaud")) {
		static const char *const o_aud[] = { "Original", NULL };
		picker_open("Audio track", NULL, o_aud, 1, "Original", NULL);
		return true;
	}
	if (!strcmp(cmd, "vspeed")) {
		char cur[8];
		snprintf(cur, sizeof cur, "%.2g×", vs.speed);
		picker_open("Speed", NULL, o_speed, 4, cur, onpick_speed);
		return true;
	}
	if (!strcmp(cmd, "vzoom")) {
		static const char *cur[] = { "Fit", "Fill", "Original" };
		picker_open("Picture size", NULL, o_zoom, 3, cur[vs.zoom],
			    onpick_zoom);
		return true;
	}
	if (!strcmp(cmd, "vdel")) {
		snprintf(res_id, sizeof res_id, "%s", arg);
		vm_delete();
		return true;
	}
	if (!strcmp(cmd, "vlink")) {
		toast("Playing links arrives with the on-screen keyboard (a later build)",
		      "link");
		return true;
	}
	if (!strcmp(cmd, "vaddshare")) {
		toast("Adding shares arrives with the on-screen keyboard (a later build)",
		      "globe");
		return true;
	}
	if (!strcmp(cmd, "vren")) {
		vm_rename();
		return true;
	}
	return false;
}

const Screen video_screen = { "video", video_build, video_draw, video_back,
			      video_tick, video_key };
