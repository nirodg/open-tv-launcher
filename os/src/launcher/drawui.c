/* launcher/drawui.c — shared widget drawing.
 * Every measurement mirrors the prototype CSS (AGENTS §5 tokens). */
#define _GNU_SOURCE
#include "drawui.h"
#include <stdio.h>

/* ── ASCII transliteration (font atlas is ASCII-only) ─────────────────── */
const char *tx(const char *s)
{
	static char bufs[4][512];
	static int bi;
	char *o = bufs[bi++ & 3];
	const char *p = s;
	char *w = o, *end = o + 511;
	while (*p && w < end - 4) {
		unsigned char c = (unsigned char)*p;
		if (c < 0x80) { *w++ = (char)c; p++; continue; }
		/* UTF-8 two/three-byte sequences we care about */
		if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
			unsigned cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
			p += 2;
			switch (cp) {
			case 0xE9: case 0xE8: case 0xEA: *w++ = 'e'; break; /* é è ê */
			case 0xF1: *w++ = 'n'; break;                       /* ñ */
			case 0xE2: *w++ = 'a'; break;                       /* â */
			case 0x103: *w++ = 'a'; break;                      /* ă */
			case 0xE7: *w++ = 'c'; break;                       /* ç */
			case 0xFC: *w++ = 'u'; break;                       /* ü */
			case 0xF6: *w++ = 'o'; break;                       /* ö */
			case 0xE4: *w++ = 'a'; break;                       /* ä */
			case 0xDF: *w++ = 's'; *w++ = 's'; break;           /* ß */
			case 0xD7: *w++ = 'x'; break;                       /* × */
			case 0xB7: *w++ = '-'; break;                       /* · */
			case 0xB0: *w++ = 'o'; break;                       /* ° */
			case 0xAB: case 0xBB: *w++ = '"'; break;            /* « » */
			default: break;
			}
			continue;
		}
		if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
			unsigned cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			p += 3;
			switch (cp) {
			case 0x2026: *w++ = '.'; *w++ = '.'; *w++ = '.'; break; /* … */
			case 0x2019: case 0x2018: case 0x201C: case 0x201D:
				*w++ = '\''; break;
			case 0x2013: case 0x2014: *w++ = '-'; break;        /* – — */
			case 0x2212: *w++ = '-'; break;                     /* − */
			case 0x203A: *w++ = '>'; break;                     /* › */
			case 0x2605: *w++ = '*'; break;                     /* ★ */
			default: break;
			}
			continue;
		}
		p++; /* skip anything else */
	}
	*w = 0;
	return o;
}

/* ── fonts ────────────────────────────────────────────────────────────── */
void *f18(void) { return font_get(500, 18); }
void *f20(void) { return font_get(600, 20); }
void *f21(void) { return font_get(500, 21); }
void *f22(void) { return font_get(500, 22); }
void *f23(void) { return font_get(500, 23); }
void *f24(void) { return font_get(500, 24); }
void *f26(void) { return font_get(500, 26); }
void *f28(void) { return font_get(600, 28); }
void *f30(void) { return font_get(700, 30); }
void *f36(void) { return font_get(700, 36); }
void *f44(void) { return font_get(700, 44); }
void *f52(void) { return font_get(700, 52); }
void *f56(void) { return font_get(700, 56); }
void *f64(void) { return font_get(700, 64); }

Col grad_at(Grad g, float t)
{
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	Col c;
	c.r = (uint8_t)(g.r1 + (g.r2 - g.r1) * t);
	c.g = (uint8_t)(g.g1 + (g.g2 - g.g1) * t);
	c.b = (uint8_t)(g.b1 + (g.b2 - g.b1) * t);
	c.a = 255;
	return c;
}

Grad app_grad(const char *g)
{
	if (!strcmp(g, "red")) return G_RED;
	if (!strcmp(g, "blue")) return G_BLUE;
	if (!strcmp(g, "violet")) return G_VIOLET;
	if (!strcmp(g, "mint")) return G_MINT;
	if (!strcmp(g, "sun")) return G_SUN;
	if (!strcmp(g, "rose")) return G_ROSE;
	if (!strcmp(g, "sky")) return G_SKY;
	if (!strcmp(g, "store")) return G_STORE;
	return G_SLATE;
}

/* App-aware gradient: not-yet-implemented apps render gray-ish. */
Grad app_grad2(const App *a)
{
	return a->live ? app_grad(a->g) : G_GRAY;
}

void glass(gw_surface *s, R r, float rad)
{
	gfx_rrect(s, r, rad, C_GLASS);
	gfx_ring(s, r, rad, 1, C_GLASS_LN);
}

void panel_draw(gw_surface *s, R r, float rad, float alpha_mul)
{
	Col bg = C_PANEL_HI; /* prototype dialog/toast: rgba(28,34,52,.9) */
	Col ln = C_GLASS_LN;
	bg.a = (uint8_t)(bg.a * alpha_mul);
	ln.a = (uint8_t)(ln.a * alpha_mul);
	gfx_rrect(s, r, rad, bg);
	gfx_ring(s, r, rad, 1, ln);
}

/* focus helper: tiles/icons scale 1+scale*fp, white 4px ring + accent glow */
void draw_focus_ring(gw_surface *s, R r, float rad, float fp, Col ring, float w)
{
	if (fp <= 0) return;
	gfx_ring(s, r, rad, w, ring);
	Col glow = C_ACCENT;
	glow.a = (uint8_t)(128 * fp);
	gfx_glow(s, r, rad, glow, fp);
}

void row_focus(gw_surface *s, R r, float fp)
{
	if (fp <= 0) return;
	Col fill = C_GLASS_SEL;
	fill.a = (uint8_t)(41 * fp);
	gfx_rrect(s, r, 0, fill);
	Col ln = { 255, 255, 255, (uint8_t)(178 * fp) };
	gfx_ring(s, rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2), 0, 2, ln);
}

/* ── app tiles (home featured) ────────────────────────────────────────── */
void tile_draw(gw_surface *s, FNode *f, const App *a)
{
	float fp = f->fp;
	R r = f->r;
	float sc = 1 + 0.08f * fp;
	if (sc != 1) {
		float cx = rcx(r), cy = rcy(r);
		r = rrect(cx - r.w * sc / 2, cy - r.h * sc / 2, r.w * sc, r.h * sc);
	}
	gfx_rrect_g2(s, r, 28, app_grad2(a));
	Col ln = { 255, 255, 255, 30 };
	gfx_ring(s, r, 28, 1, ln);
	/* glyph box 64 r18 white22, icon 34 */
	R gb = rchild(r, 28, 28, 64, 64);
	Col gbox = { 255, 255, 255, 56 };
	gfx_rrect(s, gb, 18, gbox);
	icon_draw(s, a->glyph, rcx(gb), rcy(gb), 34, C_TEXT);
	/* name 28 sb, sub 18 white72 at bottom (CSS: padding 28, gap 4) */
	text_tl(F28, s, r.x + 28, r.y + r.h - 86, tx(a->name), C_TEXT);
	Col sub = { 255, 255, 255, 184 };
	const char *subtxt = a->sub ? a->sub : a->cat;
	text_tl(F18, s, r.x + 28, r.y + r.h - 49, tx(subtxt), sub);
	if (fp > 0)
		draw_focus_ring(s, r, 28, fp, C_TEXT, 4);
}

/* ── pins ─────────────────────────────────────────────────────────────── */
void pin_draw(gw_surface *s, FNode *f, const App *a, const char *label_override)
{
	float fp = f->fp;
	R r = f->r;
	/* prototype .pin.is-focus: white 20% fill, no scale, no ring */
	glass(s, r, 20);
	if (fp > 0) {
		Col sel = { 255, 255, 255, (uint8_t)(51 * fp) }; /* 20% over the 8% glass */
		gfx_rrect(s, r, 20, sel);
	}
	R ib = rchild(r, r.w / 2 - 22, 22, 44, 44);
	if (a && !a->input) {
		gfx_rrect_g2(s, ib, 14, app_grad2(a));
		icon_draw(s, a->glyph, rcx(ib), rcy(ib), 24, C_TEXT);
	} else {
		Col bx = { 255, 255, 255, 36 };
		gfx_rrect(s, ib, 14, bx);
		icon_draw(s, a ? a->glyph : "more", rcx(ib), rcy(ib), 24, C_TEXT);
	}
	/* label 20px, baseline y+94 (top 75 in a 120px pin) */
	if (label_override)
		text_c(F20, s, rcx(r), r.y + 94, tx(label_override), C_TEXT);
	else
		text_c(F20, s, rcx(r), r.y + 94, tx(a->name), C_TEXT);
}

/* ── app icons (all-apps grid) ────────────────────────────────────────── */
void appicon_draw(gw_surface *s, FNode *f, const App *a)
{
	R cell = f->r;
	R ir = rchild(cell, (cell.w - 156) / 2, 0, 156, 156);
	float fp = f->fp;
	float sc = 1 + 0.10f * fp;
	R r = ir;
	if (sc != 1) {
		float cx = rcx(ir), cy = rcy(ir);
		r = rrect(cx - ir.w * sc / 2, cy - ir.h * sc / 2, ir.w * sc, ir.h * sc);
	}
	Col shadow = { 0, 0, 0, 40 };
	gfx_rrect(s, rchild(r, 0, 10, r.w, r.h), 40, shadow);
	gfx_rrect_g2(s, r, 40, app_grad2(a));
	Col ln = { 255, 255, 255, 36 };
	gfx_ring(s, r, 40, 1, ln);
	icon_draw(s, a->glyph, rcx(r), rcy(r), 60, C_TEXT);
	text_c(F20, s, rcx(cell), cell.y + 156 + 26, tx(a->name), C_T2);
	if (fp > 0)
		draw_focus_ring(s, r, 40, fp, C_TEXT, 4);
}

/* ── status chips / avatar ────────────────────────────────────────────── */
void chip_draw(gw_surface *s, FNode *f, const char *icon)
{
	R r = f->r;
	glass(s, r, 28);
	icon_draw(s, icon, rcx(r), rcy(r), 26, C_TEXT);
	if (f->fp > 0) {
		Col ring = C_TEXT;
		ring.a = (uint8_t)(255 * f->fp);
		gfx_ring(s, r, 28, 2, ring);
	}
}

void avatar_draw(gw_surface *s, R r, const char *letter)
{
	gfx_rrect_g2(s, r, r.w / 2, (Grad){ 255, 138, 91, 255, 92, 138 });
	gfx_ring(s, r, r.w / 2, 2, (Col){ 255, 255, 255, 90 });
	void *f22b = font_get(700, 22);
	text_c(f22b, s, rcx(r), rcy(r) + 8, letter, C_TEXT);
}

void fchip_draw(gw_surface *s, FNode *f, const char *label, bool on)
{
	R r = f->r;
	float sc = 1 + 0.06f * f->fp;
	if (sc != 1) {
		float cx = rcx(r), cy = rcy(r);
		r = rrect(cx - r.w * sc / 2, cy - r.h * sc / 2, r.w * sc, r.h * sc);
	}
	if (on) {
		gfx_rrect(s, r, 26, C_TEXT);
		text_c(F20, s, rcx(r), rcy(r) + 7, tx(label), C_INK);
	} else {
		glass(s, r, 26);
		text_c(F20, s, rcx(r), rcy(r) + 7, tx(label), C_T2);
	}
	if (f->fp > 0) {
		Col ring = C_TEXT;
		ring.a = (uint8_t)(255 * f->fp);
		gfx_ring(s, r, 26, 2, ring);
	}
}

/* rounded gradient mark with an icon (brand rows) */
void gradmark_draw(gw_surface *s, R r, const char *icon, Grad g)
{
	gfx_rrect_g2(s, r, 15, g);
	Col ln = { 255, 255, 255, 36 };
	gfx_ring(s, r, 15, 1, ln);
	icon_draw(s, icon, rcx(r), rcy(r), r.w * 0.54f, C_TEXT);
}

/* ── switches / sliders / spinners / signal ───────────────────────────── */
void sw_draw(gw_surface *s, R r, bool on)
{
	gfx_rrect(s, r, r.h / 2, on ? C_MINT : (Col){ 255, 255, 255, 46 });
	float knob = 36;
	float x = on ? r.x + r.w - knob - 4 : r.x + 4;
	R k = rrect(x, r.y + 4, knob, knob);
	gfx_rrect(s, k, k.h / 2, C_TEXT); /* knob stays white in both states */
}

void track_draw(gw_surface *s, R r, int pct)
{
	gfx_rrect(s, r, r.h / 2, (Col){ 255, 255, 255, 46 });
	float w = r.w * pct / 100;
	if (w < 6) w = pct > 0 ? 6 : 0;
	R fill = rrect(r.x, r.y, w, r.h);
	if (pct > 0) {
		gfx_rrect(s, fill, r.h / 2, C_TEXT);
		R knob = rrect(fill.x + fill.w - 12, r.y - 7, 24, 24);
		gfx_rrect(s, knob, 12, C_TEXT);
	}
}

void spinner_draw(gw_surface *s, float cx, float cy, float now)
{
	Col base = { 255, 255, 255, 64 };
	gfx_ring(s, rrect(cx - 13, cy - 13, 26, 26), 13, 3, base);
	float a0 = now * 7.5f;
	for (int i = 0; i < 8; i++) {
		float a = a0 + i * 0.35f;
		int px = (int)(cx + cosf(a) * 11);
		int py = (int)(cy + sinf(a) * 11);
		uint8_t al = (uint8_t)(255 - i * 28);
		gfx_put(s, px, py, gfx_blend_px(
			s->pix[(size_t)py * (s->stride / 4) + px], 255, 255, 255, al));
	}
}

void sigbars_draw(gw_surface *s, R r, int sig)
{
	for (int i = 1; i <= 4; i++) {
		float h = 4 + i * 5;
		R b = rchild(r, (i - 1) * 9, r.h - h, 5, h);
		gfx_rrect(s, b, 2, i <= sig ? C_TEXT : (Col){ 255, 255, 255, 56 });
	}
}

void dot_draw(gw_surface *s, R r, Col c)
{
	gfx_rrect(s, r, 6, c);
}

/* ── settings sidebar item ────────────────────────────────────────────── */
void nit_draw(gw_surface *s, FNode *f, const char *label, const char *icon,
	      Col badge, bool sel)
{
	R r = f->r;
	if (sel) gfx_rrect(s, r, 20, (Col){ 255, 255, 255, 26 });
	if (f->fp > 0) {
		Col fill = { 255, 255, 255, (uint8_t)(46 * f->fp) };
		gfx_rrect(s, r, 20, fill);
		Col ln = { 255, 255, 255, (uint8_t)(178 * f->fp) };
		gfx_ring(s, rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2), 19, 2, ln);
	}
	R b = rchild(r, 16, r.h / 2 - 20, 40, 40);
	gfx_rrect(s, b, 12, badge);
	icon_draw(s, icon, rcx(b), rcy(b), 22, C_TEXT);
	text_tl(F23, s, r.x + 72, rcy(r) - 12, tx(label), f->fp > 0 || sel ? C_TEXT : C_T2);
}

/* ── settings rows ────────────────────────────────────────────────────── */
float row_h(const SRow *row)
{
	if (!strcmp(row->t, "bar")) return 88;
	return row->d && row->d[0] ? 112 : 88;
}

static void lead_draw(gw_surface *s, R r, const SRow *row)
{
	R b = rchild(r, 0, r.h / 2 - 22, 44, 44);
	if (!row->icon || !row->icon[0]) return;
	if (!strncmp(row->icon, "sig:", 4)) {
		sigbars_draw(s, b, atoi(row->icon + 4));
		return;
	}
	if (!strncmp(row->icon, "app:", 4)) {
		const App *a = app_info(row->icon + 4);
		if (a) {
			R ab = rchild(r, 0, r.h / 2 - 22, 44, 44);
			gfx_rrect_g2(s, ab, 12, app_grad2(a));
			icon_draw(s, a->glyph, rcx(ab), rcy(ab), 22, C_TEXT);
		}
		return;
	}
	if (!strncmp(row->icon, "dot:", 4)) {
		unsigned v = (unsigned)strtoul(row->icon + 4, NULL, 16);
		dot_draw(s, rchild(r, 0, r.h / 2 - 9, 18, 18),
			 (Col){ (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v, 255 });
		return;
	}
	Col bx = { 255, 255, 255, 30 };
	gfx_rrect(s, b, 12, bx);
	icon_draw(s, row->icon, rcx(b), rcy(b), 24, C_TEXT);
}

void row_draw(gw_surface *s, Layer *L, int idx, const SRow *row,
	      R r, float now, bool last_in_group)
{
	FNode *f = idx >= 0 && idx < L->ncnt ? &L->n[idx] : NULL;
	float fp = f ? f->fp : 0;
	bool dis = row->dis;
	Col title_c = row->danger ? C_DANGER : (dis ? C_T3 : C_TEXT);
	row_focus(s, r, fp);
	if (!last_in_group)
		gfx_fill(s, rrect(r.x, r.y + r.h - 1, r.w, 1), (Col){ 255, 255, 255, 20 });

	float tx0 = r.x + 32;
	if (row->icon && row->icon[0]) tx0 += 64;
	lead_draw(s, r, row);

	/* title + optional description */
	float ty = row->d && row->d[0] ? r.y + 20 : r.y + r.h / 2 - 12;
	text_tl(F24, s, tx0, ty, tx(row->title), title_c);
	if (row->d && row->d[0])
		text_tl(F18, s, tx0, ty + 32, tx(row->d), C_T3);

	float rx = r.x + r.w - 32;
	if (row->busy)
		spinner_draw(s, rx - 46, rcy(r), now);

	if (!strcmp(row->t, "tog")) {
		bool on = row->on_set ? row->on : sv_true(row->key);
		sw_draw(s, rrect(rx - 76, rcy(r) - 22, 76, 44), on);
	} else if (!strcmp(row->t, "sld") && !dis) {
		int v = svi(row->key);
		R tr = rrect(rx - 420 - 78, rcy(r) - 5, 420, 10);
		track_draw(s, tr, v);
		char pc[8];
		snprintf(pc, sizeof pc, "%d", v);
		void *f22m = font_get(500, 22);
		int tw = text_w(f22m, pc);
		text_tl(f22m, s, rx - tw, rcy(r) - 11, pc, C_T2);
	} else if (!strcmp(row->t, "bar")) {
		/* drawn by the caller (storage page) */
	} else {
		/* value text + chevron (opt falls back to its state value;
		   the chevron always shows on opt/nav/act rows) */
		bool optnav = !strcmp(row->t, "opt") || !strcmp(row->t, "nav") ||
			      !strcmp(row->t, "act");
		const char *v = row->v;
		if ((!v || !v[0]) && row->key && row->key[0])
			v = sv(row->key);
		int chev_w = 18, gap = 10;
		int vw = v && v[0] ? text_w(F22, tx(v)) : 0;
		if (row->vh && row->vh[0]) {
			icon_draw(s, row->vh, rx - 22, rcy(r), 22, (Col){ 255, 255, 255, 178 });
			rx -= 34;
		}
		if (v && v[0])
			text_tl(F22, s, rx - vw - chev_w - gap, rcy(r) - 11, tx(v), C_T2);
		if (optnav)
			icon_draw(s, "chev", rx - chev_w / 2, rcy(r), 18,
				  (Col){ 255, 255, 255, 150 });
	}
}

/* ── quick panel ──────────────────────────────────────────────────────── */
void qt_draw(gw_surface *s, FNode *f, const char *title, const char *sub,
	     const char *icon, bool on)
{
	R r = f->r;
	float sc = 1 + 0.04f * f->fp;
	if (sc != 1) {
		float cx = rcx(r), cy = rcy(r);
		r = rrect(cx - r.w * sc / 2, cy - r.h * sc / 2, r.w * sc, r.h * sc);
	}
	if (on) {
		gfx_rrect(s, r, 28, C_TEXT);
	} else {
		Col bg = { 255, 255, 255, 26 };
		gfx_rrect(s, r, 28, bg);
	}
	R ci = rchild(r, 24, 22, 48, 48);
	gfx_rrect(s, ci, 24, on ? C_ACCENT : (Col){ 255, 255, 255, 40 });
	icon_draw(s, icon, rcx(ci), rcy(ci), 24, C_TEXT);
	Col tt = on ? C_INK : C_TEXT;
	Col ts = on ? (Col){ 11, 15, 26, 160 } : C_T2;
	text_tl(font_get(600, 22), s, r.x + 24, r.y + 78, tx(title), tt);
	text_tl(F18, s, r.x + 24, r.y + 102, tx(sub), ts);
	if (f->fp > 0) {
		Col ring = C_ACCENT;
		ring.a = (uint8_t)(255 * f->fp);
		gfx_ring(s, r, 28, 4, ring);
		Col glow = C_ACCENT;
		glow.a = (uint8_t)(115 * f->fp);
		gfx_glow(s, r, 28, glow, f->fp);
	}
}

void qslider_draw(gw_surface *s, FNode *f, const char *label, const char *icon, int pct)
{
	R r = f->r;
	float sc = 1 + 0.03f * f->fp;
	if (sc != 1) {
		float cx = rcx(r), cy = rcy(r);
		r = rrect(cx - r.w * sc / 2, cy - r.h * sc / 2, r.w * sc, r.h * sc);
	}
	Col bg = { 255, 255, 255, 26 };
	gfx_rrect(s, r, 28, bg);
	float fw = r.w * pct / 100;
	if (fw > 0) {
		/* prototype .slider .fill: rounded left cap, square right edge.
		   The cap needs the full 2×radius width, the square body covers
		   its right half so no seam shows. */
		Col fill_c = { 255, 255, 255, 230 };
		if (fw >= 56) {
			gfx_rrect(s, rrect(r.x, r.y, 56, r.h), 28, fill_c);
			gfx_fill(s, rrect(r.x + 28, r.y, fw - 28, r.h), fill_c);
		} else {
			gfx_rrect(s, rrect(r.x, r.y, fw, r.h), fw / 2, fill_c);
		}
	}
	/* label + icon: ink over the fill, white past it (two clipped passes) */
	{
		R ink_side = rrect(r.x, r.y, fw, r.h);
		R rest_side = rrect(r.x + fw, r.y, r.x + r.w - (r.x + fw), r.h);
		float ix = r.x + 28 + 14;
		const char *lb = tx(label);
		gfx_clip(ink_side);
		icon_draw(s, icon, ix, rcy(r), 28, C_INK);
		text_tl(F22, s, r.x + 28 + 42, rcy(r) - 11, lb, C_INK);
		gfx_clip(rest_side);
		icon_draw(s, icon, ix, rcy(r), 28, C_TEXT);
		text_tl(F22, s, r.x + 28 + 42, rcy(r) - 11, lb, C_TEXT);
		gfx_clip_off();
	}
	char pc[8];
	snprintf(pc, sizeof pc, "%d", pct);
	void *f20m = font_get(500, 20);
	int w = text_w(f20m, pc);
	text_tl(f20m, s, r.x + r.w - 28 - w, rcy(r) - 10, pc, C_T2);
}

void pbtn_draw(gw_surface *s, FNode *f, const char *label, const char *icon, bool danger)
{
	R r = f->r;
	float sc = 1 + 0.04f * f->fp;
	if (sc != 1) {
		float cx = rcx(r), cy = rcy(r);
		r = rrect(cx - r.w * sc / 2, cy - r.h * sc / 2, r.w * sc, r.h * sc);
	}
	if (f->fp > 0) {
		gfx_rrect(s, r, 38, C_TEXT);
	} else {
		Col bg = { 255, 255, 255, 26 };
		gfx_rrect(s, r, 38, bg);
	}
	Col c = f->fp > 0 ? C_INK : (danger ? C_DANGER : C_TEXT);
	float tw = text_w(F21, tx(label));
	float x = rcx(r) - (tw + (icon ? 34 : 0)) / 2;
	if (icon) {
		icon_draw(s, icon, x + 12, rcy(r), 24, c);
		x += 34;
	}
	text_tl(F21, s, x, rcy(r) - 10, tx(label), c);
}

/* ── dialog buttons ───────────────────────────────────────────────────── */
void dbtn_draw(gw_surface *s, FNode *f, const char *label, const char *icon, bool danger)
{
	R r = f->r;
	float sc = 1 + 0.02f * f->fp;
	if (sc != 1) {
		float cx = rcx(r), cy = rcy(r);
		r = rrect(cx - r.w * sc / 2, cy - r.h * sc / 2, r.w * sc, r.h * sc);
	}
	Col c;
	if (f->fp > 0) {
		gfx_rrect(s, r, 24, C_TEXT);
		c = C_INK;
	} else {
		Col bg = { 255, 255, 255, 20 };
		gfx_rrect(s, r, 24, bg);
		c = danger ? C_DANGER : C_TEXT;
	}
	if (f->dis)
		c = (Col){ 245, 247, 251, 90 };
	float x = r.x + 28; /* prototype .dbtn: left-aligned content, padding 28 */
	if (icon) {
		icon_draw(s, icon, x + 13, rcy(r), 26, c);
		x += 42;
	}
	text_tl(F23, s, x, rcy(r) - 11, tx(label), c);
}

/* ── generic pill buttons ─────────────────────────────────────────────── */
void btn_draw(gw_surface *s, FNode *f, const char *label, const char *icon, int kind)
{
	R r = f->r;
	Col c = C_INK;
	if (kind == 0) {
		gfx_rrect(s, r, r.h / 2, C_TEXT); /* primary: solid white */
	} else if (kind == 1) {
		gfx_rrect(s, r, r.h / 2, C_ACCENT);
		c = C_TEXT;
	} else {
		Col bg = { 255, 255, 255, 30 };
		if (f->fp > 0) {
			gfx_rrect(s, r, r.h / 2, C_TEXT);
			c = C_INK;
		} else {
			gfx_rrect(s, r, r.h / 2, bg);
			c = C_TEXT;
		}
	}
	if (f->fp > 0) {
		Col ring = C_TEXT;
		ring.a = (uint8_t)(115 * f->fp);
		gfx_ring(s, r, r.h / 2, 4, ring);
	}
	float tw = text_w(F20, tx(label));
	float x = rcx(r) - (tw + (icon ? 34 : 0)) / 2;
	if (icon) {
		icon_draw(s, icon, x + 12, rcy(r), 24, c);
		x += 34;
	}
	text_tl(F20, s, x, rcy(r) - 10, tx(label), c);
}

/* ── text helpers ─────────────────────────────────────────────────────── */
int text_wrap(void *f, const char *s, float maxw, char *out, int cap)
{
	out[0] = 0;
	int ol = 0;
	const char *p = s;
	while (*p && ol < cap - 1) {
		const char *sp = p;
		while (*sp && *sp != ' ') sp++;
		size_t wl = (size_t)(sp - p);
		char word[128];
		if (wl >= sizeof word) wl = sizeof word - 1;
		memcpy(word, p, wl);
		word[wl] = 0;
		size_t cur = strlen(out);
		char test[512];
		snprintf(test, sizeof test, "%s%s%s", out, cur ? " " : "", word);
		if (text_w(f, test) > maxw && cur > 0) {
			out[ol++] = '\n';
			for (size_t i = 0; i < wl && ol < cap - 1; i++)
				out[ol++] = word[i];
			out[ol] = 0;
		} else {
			ol = 0; /* rewrite out with test (test already contains out) */
			for (size_t i = 0; i < strlen(test) && ol < cap - 1; i++)
				out[ol++] = test[i];
			out[ol] = 0;
		}
		p = *sp ? sp + 1 : sp;
	}
	int lines = 1;
	for (char *q = out; *q; q++)
		if (*q == '\n') lines++;
	return lines;
}

void text_c(void *f, gw_surface *s, float cx, float y_base, const char *str, Col c)
{
	int w = text_w(f, tx(str));
	text(f, s, cx - w / 2, y_base, tx(str), c);
}

/* draw a pre-wrapped buffer (text_wrap output) line by line; y_top is the
 * first line's top. Returns the number of lines drawn. */
int draw_wrap(void *f, gw_surface *s, float x, float y_top, const char *src,
	      Col c, float pitch)
{
	int line = 0;
	const char *p = src;
	do {
		char buf[256];
		int n = 0;
		while (*p && *p != '\n' && n < (int)sizeof buf - 1)
			buf[n++] = *p++;
		buf[n] = 0;
		if (n)
			text_tl(f, s, x, y_top + line * pitch, buf, c);
		if (*p == '\n')
			p++;
		line++;
	} while (*p);
	return line;
}

/* ── bottom key hints ─────────────────────────────────────────────────── */
void hints_draw(gw_surface *s, float now, const HintSpec *hs)
{
	(void)now;
	void *f18m = font_get(500, 18);
	void *f15 = font_get(600, 15);
	/* total width for centering */
	float w = 0;
	int n = 0;
	for (; hs[n].key; n++) {
		w += 10 + text_w(f15, hs[n].key) + 20 + 10 + text_w(f18m, hs[n].label);
		if (n) w += 40;
	}
	float x = OTV_W / 2 - w / 2;
	float y = OTV_H - 44;
	for (int i = 0; i < n; i++) {
		float kw = 10 + text_w(f15, hs[i].key) + 10;
		R kr = rrect(x, y - 30, kw, 30);
		gfx_rrect(s, kr, 8, (Col){ 255, 255, 255, 0 });
		gfx_ring(s, kr, 8, 1, (Col){ 255, 255, 255, 56 });
		text_c(f15, s, rcx(kr), y - 8, hs[i].key, C_T2);
		x += kw + 10;
		text_tl(f18m, s, x, y - 24, tx(hs[i].label), C_T3);
		x += text_w(f18m, hs[i].label) + 40;
	}
}
