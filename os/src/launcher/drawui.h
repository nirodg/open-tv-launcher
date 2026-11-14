/* launcher/drawui.h — shared widget drawing (port of the prototype CSS). */
#ifndef OTV_DRAWUI_H
#define OTV_DRAWUI_H

#include "ui.h"
#include "sys.h"

/* ASCII-safe strings: the bundled Inter atlas covers ASCII only, so every
 * text draw goes through tx() (×→x, ·→-, ›→> …). */
const char *tx(const char *s);

/* fonts at the prototype sizes */
void *f18(void), *f20(void), *f21(void), *f22(void), *f23(void), *f24(void),
     *f26(void), *f28(void), *f30(void), *f36(void), *f44(void), *f52(void),
     *f56(void), *f64(void);
#define F18  f18()
#define F20  f20()
#define F21  f21()
#define F22  f22()
#define F23  f23()
#define F24  f24()
#define F26  f26()
#define F28  f28()
#define F30  f30()
#define F36  f36()
#define F44  f44()
#define F52  f52()
#define F56  f56()
#define F64  f64()

/* gradient at t (0..1), for dots and swatches */
Col grad_at(Grad g, float t);
Grad app_grad(const char *g);

/* glass panel: white 8% fill + white 14% 1px border, radius rad */
void glass(gw_surface *s, R r, float rad);

/* focus visuals. fp = focus progress 0..1 */
void fx_tile(R r, float fp, float scale, R *out, float *ring_w, Col *ring_c, bool *glow);
void draw_focus_ring(gw_surface *s, R r, float rad, float fp, Col ring, float w);

/* app tiles / pins / icons / chips */
void tile_draw(gw_surface *s, FNode *f, const App *a);
void pin_draw(gw_surface *s, FNode *f, const App *a, const char *label_override);
void appicon_draw(gw_surface *s, FNode *f, const App *a);
void chip_draw(gw_surface *s, FNode *f, const char *icon);
void avatar_draw(gw_surface *s, R r, const char *letter);
void fchip_draw(gw_surface *s, FNode *f, const char *label, bool on);
void gradmark_draw(gw_surface *s, R r, const char *icon, Grad g); /* rounded mark */

/* settings widgets */
void sw_draw(gw_surface *s, R r, bool on);
void track_draw(gw_surface *s, R r, int pct);
void spinner_draw(gw_surface *s, float cx, float cy, float now);
void sigbars_draw(gw_surface *s, R r, int sig);
void dot_draw(gw_surface *s, R r, Col c);
void row_focus(gw_surface *s, R r, float fp); /* fill 16% + inner ring 70% */
void nit_draw(gw_surface *s, FNode *f, const char *label, const char *icon,
	      Col badge, bool sel);
void row_draw(gw_surface *s, Layer *L, int idx, const SRow *row,
	      R r, float now, bool last_in_group);
float row_h(const SRow *row);

/* quick panel widgets */
void qt_draw(gw_surface *s, FNode *f, const char *title, const char *sub,
	     const char *icon, bool on);
void qslider_draw(gw_surface *s, FNode *f, const char *label, const char *icon,
		  int pct);
void pbtn_draw(gw_surface *s, FNode *f, const char *label, const char *icon,
	       bool danger);

/* dialog bits */
void dbtn_draw(gw_surface *s, FNode *f, const char *label, const char *icon,
	       bool danger);
void panel_draw(gw_surface *s, R r, float rad, float alpha_mul);

/* text helpers */
int  text_wrap(void *f, const char *s, float maxw, char *out, int cap);
int  draw_wrap(void *f, gw_surface *s, float x, float y_top, const char *src,
	       Col c, float pitch);
void text_c(void *f, gw_surface *s, float cx, float y_base, const char *str, Col c);

/* generic action button pill (btn p / btn s / btn a) */
void btn_draw(gw_surface *s, FNode *f, const char *label, const char *icon,
	      int kind /* 0=p(white) 1=a(accent) 2=s(glass) */);

/* bottom key hints (home etc): {"OK","Open"} pairs, NULL key ends */
typedef struct { const char *key, *label; } HintSpec;
void hints_draw(gw_surface *s, float now, const HintSpec *hs);

#endif
