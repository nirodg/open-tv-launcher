/* ui/gfx.c — CPU rasterizer: rounded rects with analytic AA, gradients,
 * blits, icons. Same primitives the future GLES2 backend will expose. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "ui.h"
#include "stb/stb_image.h"

static float g_alpha = 1.0f;
void gfx_set_alpha(float m) { g_alpha = m; }
float gfx_alpha(void) { return g_alpha; }

/* draw clip (scroll regions); everything honours it — starts full-screen */
static R g_clip = { 0, 0, OTV_W, OTV_H };
void gfx_clip(R r) { g_clip = r; }
void gfx_clip_off(void) { g_clip = rrect(0, 0, 1e6f, 1e6f); }
void gfx_clip_get(R *r) { *r = g_clip; }
static inline void clip_x0(int *v) { if (*v < (int)g_clip.x) *v = (int)g_clip.x; }
static inline void clip_y0(int *v) { if (*v < (int)g_clip.y) *v = (int)g_clip.y; }
static inline void clip_x1(int *v, gw_surface *s) { if (*v > (int)(g_clip.x + g_clip.w)) *v = (int)(g_clip.x + g_clip.w); if (*v > s->w) *v = s->w; }
static inline void clip_y1(int *v, gw_surface *s) { if (*v > (int)(g_clip.y + g_clip.h)) *v = (int)(g_clip.y + g_clip.h); if (*v > s->h) *v = s->h; }

static inline uint8_t mul_a(uint8_t a) { return (uint8_t)(a * g_alpha + 0.5f); }

/* src-over blend of an opaque-ish destination — shared with font.c.
 * Pixel word = 0xFF000000 | r<<16 | g<<8 | b (LE bytes B,G,R,X = XRGB8888,
 * matching the DRM dumb buffer and the .argb dump format). */
uint32_t gfx_blend_px(uint32_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
	if (sa == 255)
		return 0xFF000000u | ((uint32_t)sr << 16) | ((uint32_t)sg << 8) | sb;
	if (!sa)
		return dst;
	uint32_t db = dst & 0xFF, dg = (dst >> 8) & 0xFF, dr = (dst >> 16) & 0xFF;
	uint32_t a = sa + 1;
	uint32_t r = (sr * a + dr * (256 - sa)) >> 8;
	uint32_t g = (sg * a + dg * (256 - sa)) >> 8;
	uint32_t b = (sb * a + db * (256 - sa)) >> 8;
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline uint32_t blend(uint32_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
	return gfx_blend_px(dst, sr, sg, sb, sa);
}

void gfx_put(gw_surface *s, int x, int y, uint32_t px)
{
	s->pix[(size_t)y * (s->stride / 4) + x] = px;
}

static inline void put(gw_surface *s, int x, int y, uint32_t px)
{
	if (x < (int)g_clip.x || y < (int)g_clip.y ||
	    x >= (int)(g_clip.x + g_clip.w) || y >= (int)(g_clip.y + g_clip.h))
		return;
	s->pix[(size_t)y * (s->stride / 4) + x] = px;
}

void gfx_clear(gw_surface *s, Col c)
{
	uint32_t px = 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
	for (int y = 0; y < s->h; y++)
		for (int x = 0; x < s->w; x++)
			put(s, x, y, px);
}

void gfx_fill(gw_surface *s, R rr, Col c)
{
	c.a = mul_a(c.a);
	if (c.a == 0)
		return;
	int x0 = (int)rr.x, y0 = (int)rr.y;
	int x1 = (int)ceilf(rr.x + rr.w), y1 = (int)ceilf(rr.y + rr.h);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	clip_x0(&x0); clip_y0(&y0);
	clip_x1(&x1, s); clip_y1(&y1, s);
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			put(s, x, y, blend(s->pix[(size_t)y * (s->stride / 4) + x], c.r, c.g, c.b, c.a));
}

/* rounded-rect SDF coverage */
static inline float sd_rrect(float px, float py, R r, float rad)
{
	float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
	float hw = r.w / 2, hh = r.h / 2;
	if (rad > hw) rad = hw;
	if (rad > hh) rad = hh;
	float qx = fabsf(px - cx) - (hw - rad);
	float qy = fabsf(py - cy) - (hh - rad);
	float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
	return fminf(qx > qy ? qx : qy, 0) + sqrtf(ax * ax + ay * ay) - rad;
}

static inline float cov(float sd, float w)
{
	float c = 0.5f - sd / (w > 0.5f ? w : 0.5f);
	if (c < 0) c = 0;
	if (c > 1) c = 1;
	return c;
}

void gfx_rrect(gw_surface *s, R r, float rad, Col c)
{
	c.a = mul_a(c.a);
	if (c.a == 0)
		return;
	int x0 = (int)floorf(r.x) - 1, y0 = (int)floorf(r.y) - 1;
	int x1 = (int)ceilf(r.x + r.w) + 1, y1 = (int)ceilf(r.y + r.h) + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	clip_x0(&x0); clip_y0(&y0);
	clip_x1(&x1, s); clip_y1(&y1, s);
	uint32_t stride = s->stride / 4;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			float sd = sd_rrect(x + 0.5f, y + 0.5f, r, rad);
			float a = cov(sd, 1.0f);
			if (a <= 0)
				continue;
			uint8_t sa = (uint8_t)(c.a * a + 0.5f);
			if (sa)
				put(s, x, y, blend(s->pix[(size_t)y * stride + x], c.r, c.g, c.b, sa));
		}
	}
}

/* filled rounded rect with a 135° two-stop gradient (app tiles) */
void gfx_rrect_g2(gw_surface *s, R r, float rad, Grad g)
{
	if (g_alpha == 0)
		return;
	int x0 = (int)floorf(r.x) - 1, y0 = (int)floorf(r.y) - 1;
	int x1 = (int)ceilf(r.x + r.w) + 1, y1 = (int)ceilf(r.y + r.h) + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	clip_x0(&x0); clip_y0(&y0);
	clip_x1(&x1, s); clip_y1(&y1, s);
	uint32_t stride = s->stride / 4;
	float denom = (r.w + r.h > 1 ? r.w + r.h : 1);
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			float sd = sd_rrect(x + 0.5f, y + 0.5f, r, rad);
			float a = cov(sd, 1.0f);
			if (a <= 0)
				continue;
			float t = ((x - r.x) + (y - r.y)) / denom;
			float dr = g.r1 + (g.r2 - g.r1) * t;
			float dg = g.g1 + (g.g2 - g.g1) * t;
			float db = g.b1 + (g.b2 - g.b1) * t;
			uint8_t sa = (uint8_t)(255 * g_alpha * a + 0.5f);
			put(s, x, y, blend(s->pix[(size_t)y * stride + x], (uint8_t)dr, (uint8_t)dg, (uint8_t)db, sa));
		}
	}
}

void gfx_ring(gw_surface *s, R r, float rad, float w, Col c)
{
	c.a = mul_a(c.a);
	if (c.a == 0)
		return;
	int x0 = (int)floorf(r.x - w) - 1, y0 = (int)floorf(r.y - w) - 1;
	int x1 = (int)ceilf(r.x + r.w + w) + 1, y1 = (int)ceilf(r.y + r.h + w) + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	clip_x0(&x0); clip_y0(&y0);
	clip_x1(&x1, s); clip_y1(&y1, s);
	uint32_t stride = s->stride / 4;
	float half = w / 2;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			float sd = sd_rrect(x + 0.5f, y + 0.5f, r, rad);
			float d = fabsf(sd) - half;
			float a = cov(d, 1.0f);
			if (a <= 0)
				continue;
			uint8_t sa = (uint8_t)(c.a * a + 0.5f);
			put(s, x, y, blend(s->pix[(size_t)y * stride + x], c.r, c.g, c.b, sa));
		}
	}
}

/* cheap soft glow: a few concentric strokes fading out */
void gfx_glow(gw_surface *s, R r, float rad, Col c, float strength)
{
	for (int i = 0; i < 6; i++) {
		Col a = c;
		float f = (1.0f - i / 6.0f) * 0.10f * strength;
		a.a = (uint8_t)(255 * f);
		gfx_ring(s, rrect(r.x - i * 3, r.y - i * 3, r.w + i * 6, r.h + i * 6), rad + i * 3, 3.5f, a);
	}
}

/* bilinear RGBA blit with tint (icons: white glyph → tinted) */
void gfx_blit(gw_surface *s, R dst, const uint8_t *rgba, int sw, int sh, Col tint)
{
	if (!rgba || g_alpha == 0)
		return;
	int x0 = (int)floorf(dst.x), y0 = (int)floorf(dst.y);
	int x1 = (int)ceilf(dst.x + dst.w), y1 = (int)ceilf(dst.y + dst.h);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	clip_x0(&x0); clip_y0(&y0);
	clip_x1(&x1, s); clip_y1(&y1, s);
	uint32_t stride = s->stride / 4;
	float sx = (float)sw / dst.w, sy = (float)sh / dst.h;
	uint8_t tr = tint.r, tg = tint.g, tb = tint.b;
	uint8_t ta = (uint8_t)(tint.a * g_alpha + 0.5f);
	for (int y = y0; y < y1; y++) {
		float fy = (y + 0.5f - dst.y) * sy - 0.5f;
		if (fy < 0) fy = 0;
		if (fy > sh - 1) fy = sh - 1;
		int iy = (int)fy;
		float fyf = fy - iy;
		int iy1 = iy + 1 < sh ? iy + 1 : iy;
		const uint8_t *r0 = rgba + (size_t)iy * sw * 4;
		const uint8_t *r1 = rgba + (size_t)iy1 * sw * 4;
		for (int x = x0; x < x1; x++) {
			float fx = (x + 0.5f - dst.x) * sx - 0.5f;
			if (fx < 0) fx = 0;
			if (fx > sw - 1) fx = sw - 1;
			int ix = (int)fx;
			float fxf = fx - ix;
			int ix1 = ix + 1 < sw ? ix + 1 : ix;
			float a00 = r0[ix * 4 + 3], a10 = r0[ix1 * 4 + 3];
			float a01 = r1[ix * 4 + 3], a11 = r1[ix1 * 4 + 3];
			float a = a00 * (1 - fxf) * (1 - fyf) + a10 * fxf * (1 - fyf) +
				  a01 * (1 - fxf) * fyf + a11 * fxf * fyf;
			if (a < 1)
				continue;
			uint8_t sa = (uint8_t)(a / 255.0f * ta + 0.5f);
			if (sa)
				put(s, x, y, blend(s->pix[(size_t)y * stride + x], tr, tg, tb, sa));
		}
	}
}

/* scaled copy of another surface (wallpaper) — nearest */
void gfx_copy(gw_surface *s, R dst, const gw_surface *src)
{
	if (g_alpha == 0)
		return;
	int x0 = (int)dst.x, y0 = (int)dst.y;
	int x1 = x0 + (int)dst.w, y1 = y0 + (int)dst.h;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	uint32_t dstride = s->stride / 4, sstride = src->stride / 4;
	for (int y = y0; y < y1; y++) {
		int syy = (int)((y - dst.y) * src->h / dst.h);
		if (syy >= src->h) syy = src->h - 1;
		const uint32_t *sp = src->pix + (size_t)syy * sstride;
		uint32_t *dp = s->pix + (size_t)y * dstride;
		for (int x = x0; x < x1; x++) {
			int sxx = (int)((x - dst.x) * src->w / dst.w);
			if (sxx >= src->w) sxx = src->w - 1;
			dp[x] = sp[sxx];
		}
	}
}

/* ── icons ─────────────────────────────────────────────────────────────── */
typedef struct Icon { char id[32]; int w, h; uint8_t *rgba; struct Icon *next; } Icon;
static Icon *icons;
static char icons_dir[256];

void icons_init(const char *dir)
{
	snprintf(icons_dir, sizeof(icons_dir), "%s", dir);
}

static Icon *icon_load(const char *id)
{
	char path[320];
	snprintf(path, sizeof(path), "%s/%s.png", icons_dir, id);
	int w, h, n;
	unsigned char *data = stbi_load(path, &w, &h, &n, 4);
	if (!data)
		return NULL;
	Icon *ic = calloc(1, sizeof(Icon));
	snprintf(ic->id, sizeof(ic->id), "%s", id);
	ic->w = w;
	ic->h = h;
	ic->rgba = data;
	ic->next = icons;
	icons = ic;
	return ic;
}

bool icon_draw(gw_surface *s, const char *id, float cx, float cy, float size, Col tint)
{
	if (!icons_dir[0])
		return false;
	Icon *ic = icons;
	while (ic && strcmp(ic->id, id))
		ic = ic->next;
	if (!ic) {
		ic = icon_load(id);
		if (!ic)
			return false;
	}
	gfx_blit(s, rrect(cx - size / 2, cy - size / 2, size, size), ic->rgba, ic->w, ic->h, tint);
	return true;
}
