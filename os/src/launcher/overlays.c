/* launcher/overlays.c — picker, dialog, quick panel, toast, HUD.
 * Port of the prototype's openLayer overlays (qpHTML, picker, dialog).
 * One instance per overlay type; scene_top()->name tells which is open. */
#define _GNU_SOURCE
#include "drawui.h"
#include <stdio.h>

static Layer pick_l, dlg_l, qp_l;

static void pick_layer_draw(gw_surface *s, Layer *L, float now);
static void dlg_layer_draw(gw_surface *s, Layer *L, float now);
static void qp_layer_draw(gw_surface *s, Layer *L, float now);

/* ── toast ────────────────────────────────────────────────────────────── */
void draw_toast(gw_surface *s, float now)
{
	(void)now;
	const char *msg = toast_msg();
	int w = text_w(F21, tx(msg)) + 28 * 2 + 40;
	R r = rrect(OTV_W / 2 - w / 2, 40, w, 64);
	panel_draw(s, r, 32, 1);
	icon_draw(s, toast_icon(), r.x + 28 + 13, rcy(r), 26, C_TEXT);
	text_tl(F21, s, r.x + 28 + 40, rcy(r) - 10, tx(msg), C_TEXT);
}

void draw_hud(gw_surface *s, float now)
{
	(void)now;
	R r = rrect(OTV_W - 56 - 84, (OTV_H - 360) / 2, 84, 360);
	panel_draw(s, r, 42, 1);
	bool muted = sv_true("muted");
	int vol = muted ? 0 : svi("vol");
	icon_draw(s, muted ? "mute" : "speaker", rcx(r), r.y + 37, 30, C_TEXT);
	R bar = rrect(rcx(r) - 6, r.y + 74, 12, 360 - 74 - 74);
	gfx_rrect(s, bar, 6, (Col){ 255, 255, 255, 46 });
	float fh = bar.h * vol / 100;
	if (fh > 0)
		gfx_rrect(s, rrect(bar.x, bar.y + bar.h - fh, 12, fh), 6, C_TEXT);
	char num[8];
	if (muted) snprintf(num, sizeof num, "Mute");
	else snprintf(num, sizeof num, "%d", vol);
	void *f20b = font_get(700, 20);
	text_c(f20b, s, rcx(r), r.y + r.h - 28, num, C_TEXT);
}

static float layer_anim(Layer *L, float now)
{
	float t = (now - L->enter_t0) / 0.28f;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return t * t * (3 - 2 * t);
}

/* ── picker ──────────────────────────────────────────────────────────── */
typedef struct {
	char key[32];             /* state key, or "" for a generic picker */
	char title[128], note[256];
	const char *const *opts;
	int n;
	char cur[96];
	void (*onpick)(const char *v);
	/* geometry, fixed at open time and shared by build + draw */
	float px, py, ry, rows_h, note_h;
} PickCtx;
static PickCtx pick_ctx;

/* value changes for state-key pickers go through set_opt (act.c) */
static void pick_apply_key(const char *v) { set_opt(pick_ctx.key, v); }

static void picker_fill(const char *title, const char *note,
			const char *const *opts, int n, const char *cur,
			void (*onpick)(const char *v))
{
	if (scene_top() == &pick_l)
		lpop();
	snprintf(pick_ctx.title, sizeof pick_ctx.title, "%s", title);
	snprintf(pick_ctx.note, sizeof pick_ctx.note, "%s", note ? note : "");
	pick_ctx.opts = opts;
	pick_ctx.n = n;
	snprintf(pick_ctx.cur, sizeof pick_ctx.cur, "%s", cur ? cur : "");
	pick_ctx.onpick = onpick;

	/* panel geometry — the note wraps at 664 px like the prototype's <p> */
	pick_ctx.note_h = 8;
	if (pick_ctx.note[0]) {
		char wrapped[512];
		pick_ctx.note_h =
			(float)text_wrap(F21, tx(pick_ctx.note), 664, wrapped,
					 sizeof wrapped) * 32;
	}
	float rows_h = n * 88 < 600 ? n * 88 : 600;
	pick_ctx.rows_h = rows_h;
	float ph = 100 + pick_ctx.note_h + 24 + rows_h + 40;
	pick_ctx.px = (OTV_W - 760) / 2;
	pick_ctx.py = (OTV_H - ph) / 2;
	pick_ctx.ry = pick_ctx.py + 100 + pick_ctx.note_h + 24;

	lclear(&pick_l);
	snprintf(pick_l.name, sizeof(pick_l.name), "picker");
	pick_l.draw = pick_layer_draw;

	for (int i = 0; i < n; i++) {
		char act[24], k[16];
		snprintf(act, sizeof act, "pick:%d", i);
		snprintf(k, sizeof k, "pk-%d", i);
		FNode *f = lnode(&pick_l, rrect(pick_ctx.px + 48, pick_ctx.ry + i * 88, 664, 76),
				 k, act, false);
		const char *why = pick_ctx.key[0] ? opt_dis(pick_ctx.key, opts[i]) : NULL;
		if (why)
			lnode_dis(f, why);
		if (!strcmp(opts[i], pick_ctx.cur))
			f->def = true;
	}
	lpush(&pick_l);
	lscroll_keep(&pick_l, rrect(pick_ctx.px + 48, pick_ctx.ry, 664, rows_h), n * 88);
}

void picker_open_key(const char *key)
{
	int n;
	const char *const *o = cyc_opts(key, &n);
	snprintf(pick_ctx.key, sizeof pick_ctx.key, "%s", key);
	picker_fill(cyc_title(key), cyc_note(key), o, n, sv(key), pick_apply_key);
}

void picker_open(const char *title, const char *note, const char *const *opts,
		 int n, const char *cur, void (*onpick)(const char *v))
{
	pick_ctx.key[0] = 0;
	picker_fill(title, note, opts, n, cur, onpick);
}

const char *pick_opt(int i)
{
	if (i < 0 || i >= pick_ctx.n)
		return NULL;
	return pick_ctx.opts[i];
}

void picker_choose(int i)
{
	const char *v = pick_opt(i);
	lpop();
	if (v && pick_ctx.onpick)
		pick_ctx.onpick(v);
}

static void pick_layer_draw(gw_surface *s, Layer *L, float now)
{
	float a = layer_anim(L, now);
	gfx_fill(s, rrect(0, 0, OTV_W, OTV_H), (Col){ 5, 7, 13, (uint8_t)(140 * a) });
	float rows_h = pick_ctx.rows_h;
	float ph = 100 + pick_ctx.note_h + 24 + rows_h + 40;
	R p = rrect(pick_ctx.px, pick_ctx.py, 760, ph);
	panel_draw(s, p, 40, a);
	text_tl(F36, s, p.x + 48, p.y + 82, tx(pick_ctx.title), C_TEXT);
	float ry = p.y + 100;
	if (pick_ctx.note[0]) {
		char wrapped[512];
		text_wrap(F21, tx(pick_ctx.note), 664, wrapped, sizeof wrapped);
		draw_wrap(F21, s, p.x + 48, ry, wrapped, C_T2, 32);
		ry += pick_ctx.note_h;
	}
	R rows = rrect(p.x + 48, ry, 664, p.y + ph - 24 - ry);
	gfx_clip(rows);
	for (int i = 0; i < pick_ctx.n; i++) {
		R r = L->n[i].r;
		r.y -= L->scroll_y;
		if (r.y + r.h < rows.y || r.y > rows.y + rows.h) continue;
		bool cur = !strcmp(pick_ctx.opts[i], pick_ctx.cur);
		if (L->n[i].dis) {
			Col bg = { 255, 255, 255, 10 };
			gfx_rrect(s, r, 24, bg);
			Col c = { 245, 247, 251, 115 };
			text_tl(F23, s, r.x + 28, rcy(r) - 14, tx(pick_ctx.opts[i]), c);
			const char *why = opt_dis(pick_ctx.key, pick_ctx.opts[i]);
			text_tl(F18, s, r.x + 28, rcy(r) + 10, tx(why ? why : ""), C_T3);
		} else {
			FNode save = L->n[i];
			L->n[i].r = r;
			dbtn_draw(s, &L->n[i], pick_ctx.opts[i], NULL, false);
			if (cur) {
				/* prototype: check at the right edge (margin-left:auto) */
				Col cc = L->n[i].fp > 0 ? C_INK : C_TEXT;
				icon_draw(s, "check", r.x + r.w - 28 - 13, rcy(r), 26, cc);
			}
			L->n[i].r = save.r;
		}
	}
	gfx_clip_off();
}

/* ── dialog ──────────────────────────────────────────────────────────── */
typedef struct {
	char title[128];
	char text[512];
	char big[40];            /* optional large line (settime clock) */
	DlgBtn b[8];
	int n;
	void (*onclose)(int chosen);
	float py, text_h;
} DlgCtx;
static DlgCtx dlg_ctx;
static char dlg_big_next[40];

/* optional large line for the next dialog (settime: the clock) */
void dialog_big(const char *line)
{
	snprintf(dlg_big_next, sizeof dlg_big_next, "%s", line ? line : "");
}

void dialog_open(const char *title, const char *text, const DlgBtn *b, int n,
		 int def, void (*onclose)(int chosen))
{
	if (scene_top() == &dlg_l)
		lpop();
	snprintf(dlg_ctx.title, sizeof dlg_ctx.title, "%s", title);
	snprintf(dlg_ctx.text, sizeof dlg_ctx.text, "%s", text ? text : "");
	snprintf(dlg_ctx.big, sizeof dlg_ctx.big, "%s", dlg_big_next);
	dlg_big_next[0] = 0;
	dlg_ctx.n = n < 8 ? n : 8;
	for (int i = 0; i < dlg_ctx.n; i++)
		dlg_ctx.b[i] = b[i];
	dlg_ctx.onclose = onclose;

	char wrapped[1024];
	int lines = 1;
	if (dlg_ctx.text[0])
		lines = text_wrap(F21, tx(dlg_ctx.text), 664, wrapped, sizeof wrapped);
	dlg_ctx.text_h = dlg_ctx.text[0] ? lines * 32 + 32 : 12;
	float big_h = dlg_ctx.big[0] ? 96 + 8 : 0;
	float ph = 48 + 48 + 12 + dlg_ctx.text_h + big_h + dlg_ctx.n * 88 + 24;
	dlg_ctx.py = (OTV_H - ph) / 2;

	lclear(&dlg_l);
	snprintf(dlg_l.name, sizeof(dlg_l.name), "dialog");
	dlg_l.draw = dlg_layer_draw;

	float y = dlg_ctx.py + 48 + 48 + 12 + dlg_ctx.text_h + big_h;
	for (int i = 0; i < dlg_ctx.n; i++) {
		char act[16], k[16];
		snprintf(act, sizeof act, "dlg:%d", i);
		snprintf(k, sizeof k, "dlg-%d", i);
		lnode(&dlg_l, rrect((OTV_W - 760) / 2 + 48, y, 664, 76), k, act, i == def);
		y += 88;
	}
	lpush(&dlg_l);
}

const DlgBtn *dlg_btn(int i) { return i >= 0 && i < dlg_ctx.n ? &dlg_ctx.b[i] : NULL; }

void dialog_choose(int i)
{
	DlgBtn b = { 0 };
	bool have = false;
	if (i >= 0 && i < dlg_ctx.n) {
		b = dlg_ctx.b[i];
		have = true;
	}
	void (*onclose)(int) = dlg_ctx.onclose;
	lpop();
	if (onclose)
		onclose(i);
	if (have && b.fn)
		b.fn();
}

static void dlg_layer_draw(gw_surface *s, Layer *L, float now)
{
	float a = layer_anim(L, now);
	gfx_fill(s, rrect(0, 0, OTV_W, OTV_H), (Col){ 5, 7, 13, (uint8_t)(140 * a) });
	float big_h = dlg_ctx.big[0] ? 96 + 8 : 0;
	R p = rrect((OTV_W - 760) / 2, dlg_ctx.py, 760,
		    48 + 48 + 12 + dlg_ctx.text_h + big_h + L->ncnt * 88 + 24);
	panel_draw(s, p, 40, a);
	text_tl(F36, s, p.x + 48, p.y + 40, tx(dlg_ctx.title), C_TEXT);
	if (dlg_ctx.text[0]) {
		char wrapped[1024];
		text_wrap(F21, tx(dlg_ctx.text), 664, wrapped, sizeof wrapped);
		draw_wrap(F21, s, p.x + 48, p.y + 96, wrapped, C_T2, 32);
	}
	if (dlg_ctx.big[0])
		text_c(F64, s, p.x + p.w / 2, p.y + 96 + dlg_ctx.text_h + 96,
		       tx(dlg_ctx.big), C_TEXT);
	for (int i = 0; i < L->ncnt; i++)
		dbtn_draw(s, &L->n[i], dlg_ctx.b[i].label, dlg_ctx.b[i].icon,
			  dlg_ctx.b[i].danger);
}

/* ── quick panel ─────────────────────────────────────────────────────── */
void qp_open(void)
{
	if (scene_top() == &qp_l) {
		lpop();
		return;
	}
	lclear(&qp_l);
	snprintf(qp_l.name, sizeof(qp_l.name), "qp");
	qp_l.draw = qp_layer_draw;

	float x = OTV_W - 48 - 640, y = 48, w = 640, h = OTV_H - 96;
	float cx = x + 44, cw = w - 88;
	float tile_w = (cw - 20) / 2;
	float gy = y + 44 + 97 + 22;
	struct { const char *k, *act; } qts[] = {
		{ "qt-wifi", "tog:wifi" }, { "qt-bt", "tog:bt" },
		{ "qt-sleep", "cyc:sleep" }, { "qt-pic", "cyc:picMode" },
		{ "qt-snd", "cyc:soundOut" }, { "qt-in", "cyc:input" },
	};
	for (int i = 0; i < 6; i++) {
		int col = i % 2, rowi = i / 2;
		lnode(&qp_l, rrect(cx + col * (tile_w + 20), gy + rowi * 152, tile_w, 132),
		      qts[i].k, qts[i].act, i == 0);
	}
	lnode_slider(&qp_l, rrect(cx, gy + 3 * 152 + 12, cw, 96), "qs-backlight", "backlight");
	lnode_slider(&qp_l, rrect(cx, gy + 3 * 152 + 12 + 118, cw, 96), "qs-vol", "vol");
	float by = y + h - 44 - 76;
	lnode(&qp_l, rrect(cx, by, (cw - 16) / 2, 76), "qp-set", "gos:picture", false);
	lnode(&qp_l, rrect(cx + (cw - 16) / 2 + 16, by, (cw - 16) / 2, 76), "qp-pow", "powermenu", false);
	lpush(&qp_l);
}

static FNode *qp_node(Layer *L, const char *k)
{
	for (int i = 0; i < L->ncnt; i++)
		if (!strcmp(L->n[i].k, k))
			return &L->n[i];
	return NULL;
}

static void qp_layer_draw(gw_surface *s, Layer *L, float now)
{
	float a = layer_anim(L, now);
	gfx_fill(s, rrect(0, 0, OTV_W, OTV_H), (Col){ 5, 7, 13, (uint8_t)(140 * a) });
	float slide = 60 * (1 - a);
	float x = OTV_W - 48 - 640 + slide, y = 48, w = 640, h = OTV_H - 96;
	Col bg = { 28, 34, 52, (uint8_t)(184 * a) };
	Col ln = { 255, 255, 255, (uint8_t)(40 * a) };
	gfx_rrect(s, rrect(x, y, w, h), 48, bg);
	gfx_ring(s, rrect(x, y, w, h), 48, 1, ln);
	float cx = x + 44;
	void *f56 = font_get(700, 56);
	/* header per prototype .qp: padding 44, clock 56px (lh ~67), date below */
	text_tl(f56, s, cx, y + 44, tx(clock_str()), C_TEXT);
	text_tl(F22, s, cx, y + 44 + 67 + 4, tx(date_short()), C_T2);
	avatar_draw(s, rrect(x + w - 44 - 56, y + 44, 56, 56), sv("profile"));
	const char *btsub = sv_true("bt") ? "Remote, Headphones" : "Off";
	struct { const char *k, *tt, *ic; const char *ts; bool on; } qts[] = {
		{ "qt-wifi", "Wi-Fi", online() ? "wifi" : "wifioff",
		  sv_true("wifi") ? (online() ? sv("wifiNet") : "Not connected") : "Off", sv_true("wifi") },
		{ "qt-bt", "Bluetooth", "bluetooth", btsub, sv_true("bt") },
		{ "qt-sleep", "Sleep timer", "moon",
		  strcmp(sv("sleep"), "Off") ? sv("sleep") : "Off", strcmp(sv("sleep"), "Off") },
		{ "qt-pic", "Picture", "display", sv("picMode"), strcmp(sv("picMode"), "Natural") },
		{ "qt-snd", "Sound output", "speaker", sv("soundOut"), strcmp(sv("soundOut"), "TV speakers") },
		{ "qt-in", "Input", "input", sv("input"), strcmp(sv("input"), "Home screen") },
	};
	for (int i = 0; i < 6; i++) {
		FNode *f = qp_node(L, qts[i].k);
		if (!f) continue;
		R save = f->r;
		f->r = rrect(save.x + slide, save.y, save.w, save.h);
		qt_draw(s, f, qts[i].tt, qts[i].ts, qts[i].ic, qts[i].on);
		f->r = save;
	}
	struct { const char *k, *lb, *ic, *key; } sls[] = {
		{ "qs-backlight", "Backlight", "sun", "backlight" },
		{ "qs-vol", "Volume", "speaker", "vol" },
	};
	for (int i = 0; i < 2; i++) {
		FNode *f = qp_node(L, sls[i].k);
		if (!f) continue;
		R save = f->r;
		f->r = rrect(save.x + slide, save.y, save.w, save.h);
		int v = (!strcmp(sls[i].key, "vol") && sv_true("muted")) ? 0 : svi(sls[i].key);
		qslider_draw(s, f, sls[i].lb, sls[i].ic, v);
		f->r = save;
	}
	struct { const char *k, *lb, *ic; bool danger; } pbs[] = {
		{ "qp-set", "All settings", "settings", false },
		{ "qp-pow", "Power", "power", true },
	};
	for (int i = 0; i < 2; i++) {
		FNode *f = qp_node(L, pbs[i].k);
		if (f)
			pbtn_draw(s, f, pbs[i].lb, pbs[i].ic, pbs[i].danger);
	}
}

/* ── which overlay is on top (act.c asks by name) ────────────────────── */
bool overlay_open(const char *name)
{
	Layer *t = scene_top();
	return t && t != scene_screen_layer() && !strcmp(t->name, name);
}
