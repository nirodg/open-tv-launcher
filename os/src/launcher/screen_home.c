/* launcher/screen_home.c — HOME (port of SCREENS.home).
 * Clock + date + greeting, status chips, 4 featured tiles, pinned row. */
#define _GNU_SOURCE
#include "drawui.h"
#include <stdio.h>

static void home_build(Layer *L, const char *param)
{
	(void)param;
	int nf;
	const App *const *all = app_all(&nf);
	(void)all;

	/* status row: clock/date left, chips right (top 48, h 64) */
	R st = rrect(SAFE_X, 48, OTV_W - SAFE_X * 2, 64);
	const char *featured[] = { "video", "movies", "music", "livetv" };
	(void)featured;

	/* right side chips (drawn right→left: avatar, settings, qp, wifi, search) */
	float rx = st.x + st.w;
	R av_r = rrect(rx - 56, 48, 56, 56);
	rx -= 56 + 16;
	R ch_set = rrect(rx - 56, 48, 56, 56);
	lnode(L, ch_set, "st-set", "gos:picture", false);
	rx -= 56 + 16;
	R ch_qp = rrect(rx - 56, 48, 56, 56);
	lnode(L, ch_qp, "st-qp", "qp", false);
	rx -= 56 + 16;
	R ch_wifi = rrect(rx - 56, 48, 56, 56);
	lnode(L, ch_wifi, "st-wifi", "gos:network", false);
	rx -= 56 + 16;
	R ch_search = rrect(rx - 56, 48, 56, 56);
	lnode(L, ch_search, "st-search", "go:search", false);
	lnode(L, av_r, "st-av", "profile", false);

	/* featured tiles */
	float fx = SAFE_X, fy = 400;
	int npin;
	const char *const *pins = pin_list(&npin);
	(void)pins;
	for (int i = 0; i < 4; i++) {
		const App *a = app_info(featured[i]);
		if (!a) continue;
		FNode *f = lnode(L, rrect(fx, fy, 392, 240), featured[i], "open", i == 0);
		char act[48];
		snprintf(act, sizeof act, "open:%s", featured[i]);
		snprintf(f->act, sizeof(f->act), "%s", act);
		char k[32];
		snprintf(k, sizeof k, "feat-%s", featured[i]);
		snprintf(f->k, sizeof(f->k), "%s", k);
		fx += 392 + 32;
	}

	/* pinned row at y 741 (label) / 793 (pins) */
	int px = SAFE_X;
	float py = 793;
	for (int i = 0; i < npin; i++) {
		const App *a = app_info(pins[i]);
		if (!a) continue;
		FNode *f = lnode(L, rrect(px, py, 200, 120), pins[i], "open", false);
		char act[48], k[40];
		snprintf(act, sizeof act, "open:%s", pins[i]);
		snprintf(f->act, sizeof(f->act), "%s", act);
		snprintf(k, sizeof k, "pin-%s", pins[i]);
		snprintf(f->k, sizeof(f->k), "%s", k);
		lnode_menu(f, "pin");
		px += 200 + 24;
	}
	FNode *allp = lnode(L, rrect(px, py, 200, 120), "pin-all", "go:apps", false);
	(void)allp;
}

static void home_draw(gw_surface *s, Layer *L, float now)
{
	(void)now;
	/* status row: clock + date baseline-aligned at top 48 (CSS .status) */
	void *f44 = font_get(600, 44);
	text_tl(f44, s, SAFE_X, 52, tx(clock_str()), C_TEXT);
	void *f22 = font_get(500, 22);
	text_tl(f22, s, SAFE_X + text_w(f44, clock_str()) + 20, 73, tx(date_str(false)), C_T2);

	int ni = 0;
	/* nodes are in build order: st-set, st-qp, st-wifi, st-search, st-av */
	chip_draw(s, &L->n[ni++], "settings");
	chip_draw(s, &L->n[ni++], "menu");
	chip_draw(s, &L->n[ni++], online() ? "wifi" : "wifioff");
	chip_draw(s, &L->n[ni++], "search");
	avatar_draw(s, L->n[ni].r, sv("profile"));
	ni++;

	/* greeting block (CSS: abs top 176; greet 64, sub 26 + 12 margin) */
	text_tl(F64, s, SAFE_X, 183, greeting(), C_TEXT);
	text_tl(F26, s, SAFE_X, 265, tx("Pick up where you left off, or open something new."), C_T2);

	/* featured tiles */
	const char *featured[] = { "video", "movies", "music", "livetv" };
	for (int i = 0; i < 4; i++) {
		const App *a = app_info(featured[i]);
		if (a)
			tile_draw(s, &L->n[ni++], a);
	}

	/* pinned row (CSS: label at 740, scroller padding-top 20) */
	text_tl(F24, s, SAFE_X, 741, "Pinned", C_T2);
	int npin;
	const char *const *pins = pin_list(&npin);
	for (int i = 0; i < npin; i++) {
		const App *a = app_info(pins[i]);
		if (a)
			pin_draw(s, &L->n[ni++], a, NULL);
	}
	pin_draw(s, &L->n[ni++], app_info("more") ?: &(App){ .id = "more", .name = "", .glyph = "more", .g = "slate" }, "All apps");

	/* hints */
	hints_draw(s, now, (HintSpec[]){
		{ "OK", "Open" }, { "Hold OK", "Move or remove" },
		{ "M", "Quick panel" }, { "?", "Help" }, { NULL } });
}

const Screen home_screen = { "home", home_build, home_draw };
