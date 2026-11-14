/* Open TV UI toolkit — tokens, rasterizer, fonts, scene/focus engine.
 * Look reference: AGENTS.md §5 (design tokens) + renders/. Behavior
 * reference: prototype/index.html (the focus engine is a faithful port). */
#ifndef OTV_UI_H
#define OTV_UI_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gateway/gateway.h"

/* ── Design space: 1920×1080, safe area 96 L/R, 48 T/B (AGENTS §5) ─────── */
#define OTV_W 1920
#define OTV_H 1080
#define SAFE_X 96
#define SAFE_Y 48

typedef struct { float x, y, w, h; } R;
typedef struct { uint8_t r, g, b, a; } Col;

static inline R rrect(float x, float y, float w, float h) { R r = { x, y, w, h }; return r; }
static inline float rcx(R r) { return r.x + r.w / 2; }
static inline float rcy(R r) { return r.y + r.h / 2; }
static inline R rchild(R p, float dx, float dy, float w, float h)
{
	R r = { p.x + dx, p.y + dy, w, h };
	return r;
}

/* ── Colour tokens ─────────────────────────────────────────────────────── */
#define C_INK      ((Col){ 11, 15, 26, 255 })
#define C_ACCENT   ((Col){ 91, 140, 255, 255 })
#define C_VIOLET   ((Col){ 124, 92, 255, 255 })
#define C_MINT     ((Col){ 61, 214, 176, 255 })
#define C_WARM     ((Col){ 255, 138, 91, 255 })
#define C_ROSE     ((Col){ 255, 92, 138, 255 })
#define C_LIVE     ((Col){ 255, 77, 94, 255 })
#define C_TEXT     ((Col){ 245, 247, 251, 255 })
#define C_T2       ((Col){ 245, 247, 251, 173 })  /* text at 68% */
#define C_T3       ((Col){ 245, 247, 251, 117 })  /* text at 46% */
#define C_GLASS    ((Col){ 255, 255, 255, 20 })   /* white 8% */
#define C_GLASS_LN ((Col){ 255, 255, 255, 36 })   /* white 14% border */
#define C_GLASS_SEL ((Col){ 255, 255, 255, 41 })  /* white 16% */
#define C_GLASS_10 ((Col){ 255, 255, 255, 26 })
#define C_GLASS_14 ((Col){ 255, 255, 255, 36 })
#define C_GLASS_18 ((Col){ 255, 255, 255, 46 })
#define C_GLASS_22 ((Col){ 255, 255, 255, 56 })
#define C_PANEL    ((Col){ 28, 34, 52, 190 })     /* dialogs / quick panel */
#define C_PANEL_HI ((Col){ 28, 34, 52, 235 })
#define C_SCRIM    ((Col){ 5, 7, 13, 140 })
#define C_DANGER   ((Col){ 255, 138, 138, 255 })  /* "danger" text/buttons */

/* app tile gradients (135°) — token table from AGENTS §5 */
typedef struct { uint8_t r1, g1, b1, r2, g2, b2; } Grad;
#define G_RED    ((Grad){ 255, 106, 106, 200, 50, 78 })
#define G_BLUE   ((Grad){ 79, 123, 255, 42, 63, 217 })
#define G_VIOLET ((Grad){ 155, 123, 255, 90, 59, 224 })
#define G_MINT   ((Grad){ 61, 214, 176, 20, 144, 122 })
#define G_SUN    ((Grad){ 255, 194, 75, 240, 122, 43 })
#define G_SLATE  ((Grad){ 74, 86, 116, 38, 46, 68 })
#define G_ROSE   ((Grad){ 255, 125, 168, 194, 58, 120 })
#define G_SKY    ((Grad){ 94, 208, 255, 35, 133, 214 })
#define G_STORE  ((Grad){ 91, 140, 255, 124, 92, 255 })
/* "not implemented yet" apps: desaturated gray — the icon still shows, the
 * app politely defers (Open: toast). Implemented apps keep their colour. */
#define G_GRAY   ((Grad){ 122, 128, 142, 62, 66, 78 })

/* ── Global draw state ─────────────────────────────────────────────────── */
void gfx_set_alpha(float m);   /* multiplies every alpha (screen-enter fade) */
float gfx_alpha(void);         /* current multiplier (font.c reads it too) */

/* shared low-level helpers (used by font.c as well) */
uint32_t gfx_blend_px(uint32_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa);
void gfx_put(gw_surface *s, int x, int y, uint32_t px);

/* primitives */
void gfx_clear(gw_surface *s, Col c);
void gfx_fill(gw_surface *s, R r, Col c);
void gfx_rrect(gw_surface *s, R r, float rad, Col c);
void gfx_rrect_g2(gw_surface *s, R r, float rad, Grad g);
void gfx_ring(gw_surface *s, R r, float rad, float w, Col c);
void gfx_glow(gw_surface *s, R r, float rad, Col c, float strength);
void gfx_blit(gw_surface *s, R dst, const uint8_t *rgba, int sw, int sh, Col tint);
void gfx_copy(gw_surface *s, R dst, const gw_surface *src); /* scaled copy */
void gfx_clip(R r);        /* constrain later draws to r (scroll regions) */
void gfx_clip_off(void);
void gfx_clip_get(R *r);

/* icons (PNG sprites rendered from the prototype sprite by os/tools/render-icons.cjs) */
void icons_init(const char *dir);
void font_init(const char *dir);   /* font dir; atlases build lazily */
bool icon_draw(gw_surface *s, const char *id, float cx, float cy, float size, Col tint);

/* ── Fonts (Inter, OFL; weights 400/500/600/700) ──────────────────────── */
void *font_get(int weight, int px);
int   text_w(void *f, const char *s);
void  text(void *f, gw_surface *s, float x, float y_base, const char *str, Col c);
void  text_tl(void *f, gw_surface *s, float x, float y_top, const char *str, Col c);

/* ── Scene: focus engine + layers (port of prototype core) ─────────────── */
typedef struct FNode {
	R r;
	char k[48];
	char act[96];
	char menu[64];           /* hold-OK options spec ("app:id" etc) */
	bool def;
	const char *skey;        /* slider: left/right adjusts this state key */
	bool dis;
	char why[120];
	/* runtime */
	float fp;                /* focus progress 0..1, animated */
	float bump_t0;           /* bump animation start (-1 = none) */
} FNode;

typedef struct Layer Layer;
struct Layer {
	char name[16];
	FNode *n;
	int ncnt, ncap;
	int focus;               /* index, -1 = none */
	char mem[48];
	void (*draw)(gw_surface *s, Layer *L, float now);
	bool (*onmove)(int dir); /* handled internally? */
	void (*onfocus)(FNode *f);
	bool (*onback)(void);
	void *ctx;
	bool scrim;
	float enter_t0;
	const char *ov;          /* overlay kind for enter animation: "qp" / "dlg" / NULL */
	/* scroll region (settings main pane, apps grid) */
	R sreg;
	int scroll_y, content_h;
};

/* screens (registered by the launcher) */
typedef struct Screen {
	const char *id;
	void (*build)(Layer *L, const char *param);
	void (*draw)(gw_surface *s, Layer *L, float now);
	/* optional hooks: consume Back inside the screen (internal views),
	 * per-tick work (self-driven animations), and key interception
	 * (return true when handled — e.g. the player's ◀ ▶ = seek) */
	bool (*back)(void);
	void (*tick)(float now);
	bool (*key)(gw_key k, bool down, float now);
} Screen;
void screens_register(const Screen *const *list, int n); /* in main.c */
const Screen *screen_by_id(const char *id);

void scene_init(void);
void scene_key(gw_key k, bool down, float now);
void scene_focus_key(const char *key); /* focus a node by its stable key */
void scene_tick(float now);
void scene_draw(gw_surface *s, float now);
bool scene_dirty(float now, float *next_wake);
void scene_go(const char *id, const char *param);
void scene_back(void);void scene_home(void);
void scene_refresh(void);
void scene_mark_dirty(void); /* force the next frame to redraw */
/* pointer (mouse/touch) input — hover-focus, click-activate, right-click = options */
void scene_pointer(int x, int y, bool down, bool right, float now);
float scene_idle(void);                 /* seconds since the last input */
void scene_click_pos(float *x, float *y); /* position of the last click */
Layer *scene_top(void);
bool scene_in_layers(void);
void scene_effects(gw_surface *s); /* backlight dim / night light / colour temp */
float scene_now(void);
void scene_request_redraw(void);
extern bool g_dirty;
bool toast_on(void);
const char *toast_msg(void);
const char *toast_icon(void);
bool hud_on_now(void);
FNode *scene_focused(void);
const char *scene_cur_id(void);
const char *scene_cur_p(void);
float scene_enter_t0(void);
Layer *scene_screen_layer(void);
const char *screen_key_of(const char *id, const char *p);
/* overlays.c draws these (scene_draw calls them last) */
void draw_toast(gw_surface *s, float now);
void draw_hud(gw_surface *s, float now);
/* main.c draws the wallpaper (cached, re-rendered when the setting changes) */
void draw_wallpaper(gw_surface *s);
void wall_invalidate(void);
/* settings screen hooks (screen_settings.c) */
void settings_hooks(Layer *L);
bool settings_onback(void);
bool settings_refresh_main(void);
void sys_tick(float now);
void sys_note_input(void); /* reset the idle clock (screen saver / auto-off) */

/* layer helpers */
FNode *lnode(Layer *L, R r, const char *k, const char *act, bool def);
FNode *lnode_slider(Layer *L, R r, const char *k, const char *skey);
void lnode_dis(FNode *f, const char *why);
void lnode_menu(FNode *f, const char *m);
void lclear(Layer *L);
void lrestore(Layer *L, const char *key);
void lpush(Layer *L);
void lpop(void);
void lpop_all(void);
void lscroll_keep(Layer *L, R region, int content_h); /* enable auto-scroll */

/* overlays (overlays.c) */
void toast(const char *msg, const char *icon);
void hud_show(void);
void qp_open(void);
void picker_open_key(const char *key);  /* pick a CYC list */
void picker_open(const char *title, const char *note, const char *const *opts,
                 int n, const char *cur, void (*onpick)(const char *v));
typedef struct { const char *label; const char *icon; void (*fn)(void); bool danger; } DlgBtn;
void dialog_open(const char *title, const char *text, const DlgBtn *b, int n, int def,
                 void (*onclose)(int chosen));
void dialog_big(const char *line);   /* optional large line for the next dialog */

/* ACT dispatch (act.c): act string is "cmd:arg" */
void act_init(void);
void act_fire(const char *act);
void act_after_change(const char *key);

/* state (sys.c) — prototype S/CYC ported; values are string-encoded */
void sys_init(void);
const char *sv(const char *key);             /* string value */
int svi(const char *key);                    /* value as int */
void sv_set(const char *key, const char *val);
void sv_seti(const char *key, int val);
bool sv_true(const char *key);
const char *const *cyc_opts(const char *key, int *n);
const char *cyc_title(const char *key);
const char *cyc_note(const char *key);
const char *opt_dis(const char *key, const char *val);
void cycle_key(const char *key, int dir);
void slide_key(const char *key, int delta, int abs);
void toggle_key(const char *key);
void toast_change(const char *key);          /* CHANGE_TOAST subset */

/* app catalogue (sys.c) */
typedef struct { const char *id, *name, *glyph, *g, *cat, *sub; const char *go;
		 bool input, hidden, store, live; } App;
const App *app_info(const char *id);
const App *const *app_all(int *n);           /* LAUNCH + installed store apps */
const App *const *app_installed_store(int *n);
const App *const *app_builtin(int *n);
typedef struct { const char *id, *name, *kind, *glyph, *g, *ver, *lic, *next, *anti; int size; bool installed; } StoreApp;
const StoreApp *store_app(const char *id);
void appst_ensure(const char *id);
bool appst_get(const char *id, const char *field, int *val); /* notif/stopped/data/cache */
void appst_set(const char *id, const char *field, int val);
const char *appst_perm(const char *id, const char *perm, int *has);
void appst_toggle_perm(const char *id, const char *perm);

#endif /* OTV_UI_H */
