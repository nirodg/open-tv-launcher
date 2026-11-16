/* launcher/screen_apps.c — ALL APPS (port of SCREENS.apps).
 * Title + search pill, filter chips, 8-column scrolling grid of icons. */
#define _GNU_SOURCE
#include "drawui.h"
#include <stdio.h>

static const char *const FILTERS[] = { "All", "Watch", "Listen", "Play", "Tools", "System" };

static void apps_build(Layer *L, const char *param)
{
	(void)param;
	/* search pill (right of the title) */
	R sr = rrect(OTV_W - SAFE_X - 560, 160, 560, 72);
	lnode(L, sr, "ap-search", "go:search", false);

	/* filter chips */
	float fx = SAFE_X, fy = 276;
	for (unsigned i = 0; i < sizeof(FILTERS) / sizeof(*FILTERS); i++) {
		FNode *f = lnode(L, rrect(fx, fy, 0, 52), "", "filter", false);
		char act[32], k[32];
		snprintf(act, sizeof act, "filter:%s", FILTERS[i]);
		snprintf(f->act, sizeof(f->act), "%s", act);
		snprintf(k, sizeof k, "flt-%s", FILTERS[i]);
		snprintf(f->k, sizeof(f->k), "%s", k);
		f->r.w = 52 + text_w(F20, FILTERS[i]);
		fx += f->r.w + 14;
	}

	/* app grid */
	float gx = SAFE_X, gy = 350 + 30;
	int col = 0;
	int n;
	const App *const *all = app_all(&n);
	const char *filter = sv("appsFilter");
	bool first_app = true;
	for (int i = 0; i < n; i++) {
		const App *a = all[i];
		if (a->hidden) continue;
		if (strcmp(filter, "All") && strcmp(a->cat, filter)) continue;
		FNode *f = lnode(L, rrect(gx, gy, 180, 196), a->id, "open", first_app);
		first_app = false;
		char act[48], k[40];
		snprintf(act, sizeof act, "open:%s", a->id);
		snprintf(f->act, sizeof(f->act), "%s", act);
		snprintf(k, sizeof k, "app-%s", a->id);
		snprintf(f->k, sizeof(f->k), "%s", k);
		lnode_menu(f, "app");
		gx += 180 + 42;
		if (++col == 8) {
			col = 0;
			gx = SAFE_X;
			gy += 196 + 44;
		}
	}
	/* scroll region: the grid viewport */
	lscroll_keep(L, rrect(0, 350, OTV_W, OTV_H - 350 - 80), (int)(gy + 80 - 350));
}

static void apps_draw(gw_surface *s, Layer *L, float now)
{
	(void)now;
	/* clock + avatar */
	void *f44 = font_get(600, 44);
	text_tl(f44, s, SAFE_X, 48 + 50, tx(clock_str()), C_TEXT);
	avatar_draw(s, rrect(OTV_W - SAFE_X - 56, 48, 56, 56), sv("profile"));

	/* title + search pill */
	text_tl(F56, s, SAFE_X, 160 + 56, "All apps", C_TEXT);
	R sr = rrect(OTV_W - SAFE_X - 560, 160, 560, 72);
	glass(s, sr, 36);
	FNode *sn = &L->n[0];
	if (sn->fp > 0) {
		Col ring = C_TEXT;
		ring.a = (uint8_t)(255 * sn->fp);
		gfx_ring(s, sr, 36, 2, ring);
	}
	icon_draw(s, "search", sr.x + 28 + 14, rcy(sr), 28, C_T3);
	text_tl(F24, s, sr.x + 28 + 56, rcy(sr) - 12, "Search apps and the Store", C_T3);

	/* filter chips */
	int ni = 1;
	const char *cur = sv("appsFilter");
	for (unsigned i = 0; i < sizeof(FILTERS) / sizeof(*FILTERS); i++, ni++)
		fchip_draw(s, &L->n[ni], FILTERS[i], !strcmp(FILTERS[i], cur));

	/* grid */
	R region = L->sreg;
	gfx_clip(region);
	int n;
	const App *const *all = app_all(&n);
	const char *filter = sv("appsFilter");
	int count = 0;
	for (int i = 0; i < n; i++) {
		const App *a = all[i];
		if (a->hidden) continue;
		if (strcmp(filter, "All") && strcmp(a->cat, filter)) continue;
		FNode *f = &L->n[ni];
		R save = f->r;
		f->r = rrect(save.x, save.y - L->scroll_y, save.w, save.h);
		if (f->r.y + f->r.h > region.y && f->r.y < region.y + region.h)
			appicon_draw(s, f, a);
		f->r = save;
		ni++;
		count++;
	}
	if (!count)
		text_tl(F24, s, SAFE_X, 380 + 60, "No apps in this group yet.", C_T3);
	gfx_clip_off();
}

const Screen apps_screen = { "apps", apps_build, apps_draw };
