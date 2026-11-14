/* ui/scene.c — focus engine + navigation + layers.
 * Faithful port of prototype/index.html core: score()/move()/setFocus/
 * restoreFocus/bump, openLayer/closeLayer, go/back/refresh, toast/hud timers.
 * Layout space is 1920×1080; nodes carry rects identical to the drawn visuals. */
#include "ui.h"
#include <stdio.h>

#define MAX_LAYERS 8
#define HIST_MAX 16

static Layer *layers[MAX_LAYERS];
static int nlayers;
static char cur_id[16] = "home";
static char cur_p[48] = "";
static struct { char id[16]; char p[48]; } hist[HIST_MAX];
static int nhist;
static float enter_t0 = -1;

/* mem per screenKey (prototype `mem`) */
static struct { char key[64]; char k[48]; } mems[64];
static int nmems;

static void mem_set(const char *skey, const char *k)
{
	for (int i = 0; i < nmems; i++)
		if (!strcmp(mems[i].key, skey)) {
			snprintf(mems[i].k, sizeof(mems[i].k), "%s", k);
			return;
		}
	if (nmems >= 64)
		return;
	snprintf(mems[nmems].key, sizeof(mems[nmems].key), "%s", skey);
	snprintf(mems[nmems].k, sizeof(mems[nmems].k), "%s", k);
	nmems++;
}

static const char *mem_get(const char *skey)
{
	for (int i = 0; i < nmems; i++)
		if (!strcmp(mems[i].key, skey))
			return mems[i].k;
	return NULL;
}

/* ── toast / hud ───────────────────────────────────────────────────────── */
static struct { char msg[128]; char icon[24]; float until; bool on; } gtoast;
static float hud_until = 0;
static bool hud_on;

void toast(const char *msg, const char *icon)
{
	snprintf(gtoast.msg, sizeof(gtoast.msg), "%s", msg);
	snprintf(gtoast.icon, sizeof(gtoast.icon), "%s", icon ? icon : "check");
	gtoast.until = scene_now() + 2.2f;
	gtoast.on = true;
	g_dirty = true;
}

bool toast_on(void) { return gtoast.on && scene_now() < gtoast.until; }
const char *toast_msg(void) { return gtoast.msg; }
const char *toast_icon(void) { return gtoast.icon; }

void hud_show(void)
{
	hud_until = scene_now() + 1.5f;
	hud_on = true;
	g_dirty = true;
}
bool hud_on_now(void) { return hud_on && scene_now() < hud_until; }

/* ── time base ─────────────────────────────────────────────────────────── */
static float g_now;
float scene_now(void) { return g_now; }

/* ── layer helpers ─────────────────────────────────────────────────────── */
static Layer *scr; /* the base screen layer */

Layer *scene_top(void) { return nlayers ? layers[nlayers - 1] : scr; }
bool scene_in_layers(void) { return nlayers > 0; }
Layer *scene_screen_layer(void) { return scr; }
const char *scene_cur_id(void) { return cur_id; }
const char *scene_cur_p(void) { return cur_p; }
float scene_enter_t0(void) { return enter_t0; }

FNode *lnode(Layer *L, R r, const char *k, const char *act, bool def)
{
	if (L->ncnt >= L->ncap) {
		L->ncap = L->ncap ? L->ncap * 2 : 32;
		L->n = realloc(L->n, L->ncap * sizeof(FNode));
	}
	FNode *f = &L->n[L->ncnt++];
	memset(f, 0, sizeof(*f));
	f->r = r;
	snprintf(f->k, sizeof(f->k), "%s", k);
	snprintf(f->act, sizeof(f->act), "%s", act ? act : "");
	f->def = def;
	f->bump_t0 = -1;
	return f;
}

FNode *lnode_slider(Layer *L, R r, const char *k, const char *skey)
{
	FNode *f = lnode(L, r, k, "", false);
	f->skey = skey;
	return f;
}

void lnode_dis(FNode *f, const char *why)
{
	f->dis = true;
	if (why)
		snprintf(f->why, sizeof(f->why), "%s", why);
}

void lnode_menu(FNode *f, const char *m)
{
	if (m)
		snprintf(f->menu, sizeof(f->menu), "%s", m);
}

void lclear(Layer *L)
{
	L->ncnt = 0;
	L->focus = -1;
	L->mem[0] = 0;
	L->scroll_y = 0;
	L->content_h = 0;
}

void lpush(Layer *L)
{
	if (nlayers >= MAX_LAYERS)
		return;
	L->focus = -1;
	L->enter_t0 = g_now;
	layers[nlayers++] = L;
	/* clear screen focus visuals */
	if (scr)
		for (int i = 0; i < scr->ncnt; i++)
			scr->n[i].fp = 0;
	lrestore(L, NULL);
}

void lpop(void)
{
	if (!nlayers)
		return;
	nlayers--;
	lrestore(nlayers ? layers[nlayers - 1] : scr,
		 nlayers ? layers[nlayers - 1]->mem : mem_get(screen_key_of(cur_id, cur_p)));
}

const char *screen_key_of(const char *id, const char *p)
{
	static char buf[64];
	snprintf(buf, sizeof(buf), "%s|%s", id, p);
	return buf;
}

void lpop_all(void)
{
	while (nlayers)
		lpop();
}

void lscroll_keep(Layer *L, R region, int content_h)
{
	L->sreg = region;
	L->content_h = content_h;
}

static void focus_apply(Layer *L, int idx)
{
	if (L->focus >= 0 && L->focus != idx)
		; /* fp animates back in tick */
	L->focus = idx;
	FNode *f = &L->n[idx];

	/* the screen's onFocus hook runs first (may switch the page and rebuild
	 * the node array, so re-fetch afterwards) */
	if (!nlayers && scr && scr->onfocus && L == scr)
		scr->onfocus(f);
	f = &L->n[L->focus];
	if (nlayers)
		snprintf(layers[nlayers - 1]->mem, sizeof(layers[nlayers - 1]->mem), "%s", f->k);
	else {
		char sk[64];
		snprintf(sk, sizeof(sk), "%s", screen_key_of(cur_id, cur_p));
		mem_set(sk, f->k);
	}

	/* scrollIntoViewSmart: keep the node inside the scroll region (+margin) */
	if (L->content_h > 0 && L->sreg.w > 0) {
		float m = 60;
		float t = f->r.y - L->scroll_y;
		float b = f->r.y + f->r.h - L->scroll_y;
		if (t < L->sreg.y + m)
			L->scroll_y -= (int)(L->sreg.y + m - t);
		else if (b > L->sreg.y + L->sreg.h - m)
			L->scroll_y += (int)(b - (L->sreg.y + L->sreg.h - m));
		int max_scroll = L->content_h - (int)L->sreg.h;
		if (max_scroll < 0) max_scroll = 0;
		if (L->scroll_y < 0) L->scroll_y = 0;
		if (L->scroll_y > max_scroll) L->scroll_y = max_scroll;
	}
}

void lrestore(Layer *L, const char *key)
{
	int idx = -1;
	if (key)
		for (int i = 0; i < L->ncnt && idx < 0; i++)
			if (!strcmp(L->n[i].k, key))
				idx = i;
	if (idx < 0)
		for (int i = 0; i < L->ncnt && idx < 0; i++)
			if (L->n[i].def)
				idx = i;
	if (idx < 0 && L->ncnt)
		idx = 0;
	L->focus = -1;
	if (idx >= 0)
		focus_apply(L, idx);
}

FNode *scene_focused(void)
{
	Layer *L = scene_top();
	return L && L->focus >= 0 ? &L->n[L->focus] : NULL;
}

static void bump(void)
{
	FNode *f = scene_focused();
	if (f)
		f->bump_t0 = g_now;
}

/* prototype score() — exact port */
static float score(R a, R b, int dir, bool *ok)
{
	bool h = dir == GWK_LEFT || dir == GWK_RIGHT;
	bool fwd = dir == GWK_RIGHT || dir == GWK_DOWN;
	float a1 = h ? a.x : a.y, a2 = h ? a.x + a.w : a.y + a.h;
	float b1 = h ? b.x : b.y, b2 = h ? b.x + b.w : b.y + b.h;
	float c1 = h ? a.y : a.x, c2 = h ? a.y + a.h : a.x + a.w;
	float d1 = h ? b.y : b.x, d2 = h ? b.y + b.h : b.x + b.w;
	float ac = (a1 + a2) / 2, bc = (b1 + b2) / 2;
	if (fwd) {
		if (bc <= ac + 2 || b1 < a1 + 2) { *ok = false; return 0; }
	} else {
		if (bc >= ac - 2 || b2 > a2 - 2) { *ok = false; return 0; }
	}
	float primary = fwd ? (b1 - a2 > 0 ? b1 - a2 : 0) : (a1 - b2 > 0 ? a1 - b2 : 0);
	float ov = (c2 < d2 ? c2 : d2) - (c1 > d1 ? c1 : d1);
	float secondary = ov > 0 ? 0 : -ov;
	if (h && ov <= 0) { *ok = false; return 0; } /* same row always wins off-axis */
	float s = (ov > 0 ? 0 : 100000) + primary + secondary * 3 +
		  fabsf((c1 + c2) / 2 - (d1 + d2) / 2) * 0.12f;
	*ok = true;
	return s;
}

static void move_dir(int dir)
{
	Layer *L = scene_top();
	if (!L || L->focus < 0) {
		lrestore(L, NULL);
		return;
	}
	FNode *f = &L->n[L->focus];

	/* sliders: left/right adjust the value instead of moving */
	if ((dir == GWK_LEFT || dir == GWK_RIGHT) && f->skey) {
		slide_key(f->skey, dir == GWK_RIGHT ? 5 : -5, -1);
		if (!strcmp(f->skey, "vol")) {
			sv_seti("muted", 0);
			if (!nlayers || strcmp(layers[nlayers - 1]->name, "qp"))
				hud_show();
		}
		return;
	}

	/* screen-level move hook (settings sidebar/page handoff) */
	if (!nlayers && scr && scr->onmove && scr->onmove(dir))
		return;

	R a = f->r;
	float best_s = 1e30f; /* prototype starts at Infinity — scores are >= 0 */
	int best = -1;
	for (int i = 0; i < L->ncnt; i++) {
		if (i == L->focus)
			continue;
		bool ok;
		float s = score(a, L->n[i].r, dir, &ok);
		if (ok && s < best_s) {
			best_s = s;
			best = i;
		}
	}
	if (best >= 0) {
		focus_apply(L, best);
	} else {
		bump();
	}
}

/* ── navigation ────────────────────────────────────────────────────────── */
static void build_screen(bool anim)
{
	const Screen *S = screen_by_id(cur_id);
	if (!S)
		return;
	scr->draw = S->draw;
	scr->onmove = NULL;
	scr->onfocus = NULL;
	scr->onback = NULL;
	lclear(scr);
	S->build(scr, cur_p);
	/* settings screen hooks */
	if (!strcmp(cur_id, "settings"))
		settings_hooks(scr);
	if (anim)
		enter_t0 = g_now;
}

void scene_go(const char *id, const char *param)
{
	lpop_all();
	snprintf(hist[nhist].id, sizeof(hist[nhist].id), "%s", cur_id);
	snprintf(hist[nhist].p, sizeof(hist[nhist].p), "%s", cur_p);
	if (nhist < HIST_MAX)
		nhist++;
	snprintf(cur_id, sizeof(cur_id), "%s", id);
	snprintf(cur_p, sizeof(cur_p), "%s", param ? param : "");
	build_screen(true);
	lrestore(scr, mem_get(screen_key_of(cur_id, cur_p)));
}

void scene_back(void)
{
	if (nlayers) {
		lpop();
		return;
	}
	const Screen *S = screen_by_id(cur_id);
	if (S && S->back && S->back())
		return;
	if (!nhist) {
		if (strcmp(cur_id, "home"))
			scene_home();
		return;
	}
	nhist--;
	snprintf(cur_id, sizeof(cur_id), "%s", hist[nhist].id);
	snprintf(cur_p, sizeof(cur_p), "%s", hist[nhist].p);
	build_screen(true);
	lrestore(scr, mem_get(screen_key_of(cur_id, cur_p)));
}

void scene_home(void)
{
	lpop_all();
	nhist = 0;
	snprintf(cur_id, sizeof(cur_id), "home");
	cur_p[0] = 0;
	build_screen(true);
	lrestore(scr, mem_get("home|"));
}

void scene_refresh(void)
{
	if (!strcmp(cur_id, "settings") && settings_refresh_main())
		return;
	char k[48] = "";
	if (scr->focus >= 0)
		snprintf(k, sizeof(k), "%s", scr->n[scr->focus].k);
	build_screen(false);
	lrestore(scr, k[0] ? k : NULL);
}

bool g_dirty;
void scene_request_redraw(void) { g_dirty = true; }

/* ── keys ──────────────────────────────────────────────────────────────── */
static bool ok_down;
static float ok_t0;

static void activate(void)
{
	FNode *f = scene_focused();
	if (!f)
		return;
	if (f->dis) {
		if (f->why[0])
			toast(f->why, "info");
		return;
	}
	if (f->act[0])
		act_fire(f->act);
}

void scene_key(gw_key k, bool down, float now)
{
	g_now = now;
	sys_note_input();
	if (!strcmp(cur_id, "standby")) {
		if (down)
			scene_home(); /* any key wakes the TV */
		return;
	}
	if (!strcmp(cur_id, "boot"))
		return;
	if (k == GWK_OK) {
		if (down) {
			ok_down = true;
			ok_t0 = now;
		} else if (ok_down) {
			ok_down = false;
			if (now - ok_t0 < 0.55f)
				activate(); /* short press = OK; hold = options */
		}
		return;
	}
	if (!down)
		return;
	switch (k) {
	case GWK_UP: move_dir(GWK_UP); break;
	case GWK_DOWN: move_dir(GWK_DOWN); break;
	case GWK_LEFT: move_dir(GWK_LEFT); break;
	case GWK_RIGHT: move_dir(GWK_RIGHT); break;
	case GWK_BACK: scene_back(); break;
	case GWK_HOME: scene_home(); break;
	case GWK_MENU: act_fire("qp"); break;
	case GWK_VOLUP: slide_key("vol", 5, -1); sv_seti("muted", 0); hud_show(); break;
	case GWK_VOLDOWN: slide_key("vol", -5, -1); sv_seti("muted", 0); hud_show(); break;
	case GWK_MUTE: sv_seti("muted", svi("muted") ? 0 : 1); hud_show(); break;
	default: break;
	}
}

/* hold OK for ~550ms → the focused item's options (prototype okTimer) */
static void check_ok_hold(float now)
{
	if (ok_down && now - ok_t0 >= 0.55f) {
		ok_down = false;
		FNode *f = scene_focused();
		if (f && !f->dis && f->act[0])
			act_fire("options");
	}
}

/* ── tick: timers, fp animation, dirty tracking ────────────────────────── */
static void tick_fps(Layer *L, float dt)
{
	for (int i = 0; i < L->ncnt; i++) {
		FNode *f = &L->n[i];
		float target = L->focus == i ? 1 : 0;
		float rate = 1.0f / 0.18f;
		if (fabsf(f->fp - target) > 0.001f) {
			if (f->fp < target) f->fp += rate * dt;
			else f->fp -= rate * dt;
			if (f->fp > 1) f->fp = 1;
			if (f->fp < 0) f->fp = 0;
			g_dirty = true;
		}
	}
}

static float boot_t0 = -1;
static int last_min = -1;

void scene_tick(float now)
{
	float dt = now - g_now;
	if (dt < 0) dt = 0;
	if (dt > 0.1f) dt = 0.1f;
	g_now = now;
	check_ok_hold(now);
	/* the boot screen hands over to Home on its own */
	if (boot_t0 >= 0 && now - boot_t0 > 1.8f) {
		boot_t0 = -1;
		scene_home();
	}
	/* keep the clock fresh (minute rollover) */
	int h, m, s, wd, day, mon, year;
	gw_time_parts(&h, &m, &s, &wd, &day, &mon, &year);
	(void)h; (void)s; (void)wd; (void)day; (void)mon; (void)year;
	if (m != last_min) {
		last_min = m;
		g_dirty = true;
	}
	for (int i = 0; i <= nlayers; i++)
		if (i == 0) tick_fps(scr, dt);
		else tick_fps(layers[i - 1], dt);
	if (gtoast.on && now >= gtoast.until) {
		gtoast.on = false;
		g_dirty = true;
	}
	if (hud_on && now >= hud_until) {
		hud_on = false;
		g_dirty = true;
	}
	/* per-screen tick (self-driven animations, e.g. the player) */
	const Screen *S = screen_by_id(cur_id);
	if (S && S->tick)
		S->tick(now);
	if (enter_t0 >= 0 && now - enter_t0 < 0.3f)
		g_dirty = true;
	sys_tick(now); /* wifi/bt/update busy state machines (sys.c) */
}

void scene_mark_dirty(void) { g_dirty = true; }

bool scene_dirty(float now, float *next_wake)
{
	(void)now;
	*next_wake = 0.25f;
	if (g_dirty) {
		g_dirty = false;
		*next_wake = 0.016f;
		return true;
	}
	if (toast_on() || hud_on_now())
		*next_wake = 0.25f;
	return false;
}

/* ── effects (applyEffects): backlight dim, night light, colour temp ───── */
void scene_effects(gw_surface *s)
{
	int bl = svi("backlight");
	if (bl < 100) {
		Col c = { 0, 0, 0, (uint8_t)((100 - bl) * 0.65f * 255 / 100) };
		gfx_fill(s, rrect(0, 0, s->w, s->h), c);
	}
	if (sv_true("nightLight")) {
		float a = 0.06f + svi("nightStr") * 0.0024f;
		gfx_fill(s, rrect(0, 0, s->w, s->h), (Col){ 255, 150, 60, (uint8_t)(a * 255) });
	}
	if (!strcmp(sv("colorTemp"), "Warm"))
		gfx_fill(s, rrect(0, 0, s->w, s->h), (Col){ 255, 176, 102, (uint8_t)(0.35f * 255) });
	else if (!strcmp(sv("colorTemp"), "Cool"))
		gfx_fill(s, rrect(0, 0, s->w, s->h), (Col){ 127, 178, 255, (uint8_t)(0.35f * 255) });
}

/* ── init / draw ──────────────────────────────────────────────────────── */
void scene_init(void)
{
	scr = calloc(1, sizeof(Layer));
	if (!scr)
		return;
	snprintf(scr->name, sizeof(scr->name), "screen");
	/* boot screen first; scene_tick hands over to Home after ~1.8 s */
	snprintf(cur_id, sizeof(cur_id), "boot");
	cur_p[0] = 0;
	build_screen(false);
	enter_t0 = g_now;
	boot_t0 = g_now;
	lrestore(scr, NULL);
}

void scene_focus_key(const char *key)
{
	if (!scr || !key || nlayers)
		return;
	for (int i = 0; i < scr->ncnt; i++)
		if (!strcmp(scr->n[i].k, key)) {
			focus_apply(scr, i);
			return;
		}
}

void scene_draw(gw_surface *s, float now)
{
	draw_wallpaper(s);

	/* screen enter: 260 ms fade (the slight scale of the prototype is
	 * skipped — a full-screen re-render per frame is too costly here) */
	float a = 1;
	if (enter_t0 >= 0) {
		float t = (now - enter_t0) / 0.26f;
		if (t <= 0)
			a = 0;
		else if (t < 1)
			a = t * t * (3 - 2 * t);
	}
	gfx_set_alpha(a);
	if (getenv("OTV_DBG"))
		fprintf(stderr, "scene_draw: cur=%s ncnt=%d nlayers=%d a=%.3f now=%.3f enter_t0=%.3f\n",
			cur_id, scr->ncnt, nlayers, a, now, enter_t0);
	if (scr->draw)
		scr->draw(s, scr, now);
	gfx_set_alpha(1);

	/* overlays draw their own scrims */
	for (int i = 0; i < nlayers; i++)
		if (layers[i]->draw)
			layers[i]->draw(s, layers[i], now);

	if (toast_on())
		draw_toast(s, now);
	if (hud_on_now())
		draw_hud(s, now);
	scene_effects(s);
}
