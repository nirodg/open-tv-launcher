/* ui/font.c — Inter glyph cache on stb_truetype (weights 400/500/600/700).
 * One atlas per (weight,px) pair; ASCII 32..126 covers the UI copy.
 * Kerning pairs are not applied in milestone 1 (note for later polish). */
#define STBTT_STATIC
#define STBRP_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#define STBTT_RASTERIZER_VERSION 2
#include <stdio.h>
#include "stb/stb_rect_pack.h"
#include "stb/stb_truetype.h"
#include "ui.h"

#define ATLAS_W 1024
#define ATLAS_H 1024
#define FIRST_CH 32
#define NUM_CH 95

typedef struct Font {
	int weight, px;
	uint8_t *atlas;
	stbtt_packedchar ch[NUM_CH];
	float ascent, descent, linegap;
	struct Font *next;
} Font;

static Font *fonts;
static unsigned char *ttf_data[4];
static char fonts_dir[256];

static const char *ttf_name(int weight)
{
	switch (weight) {
	case 400: return "Inter-Regular.ttf";
	case 500: return "Inter-Medium.ttf";
	case 600: return "Inter-SemiBold.ttf";
	default:  return "Inter-Bold.ttf";
	}
}

void font_init(const char *dir)
{
	snprintf(fonts_dir, sizeof(fonts_dir), "%s", dir);
}

static unsigned char *load_ttf(int weight)
{
	int slot = weight / 100 - 4;
	if (ttf_data[slot])
		return ttf_data[slot];
	char path[320];
	snprintf(path, sizeof(path), "%s/%s", fonts_dir[0] ? fonts_dir : "/usr/share/otv/fonts",
		 ttf_name(weight));
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "otv: missing font %s\n", path);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *d = malloc(sz);
	if (!d || fread(d, 1, sz, f) != (size_t)sz) {
		fclose(f);
		free(d);
		return NULL;
	}
	fclose(f);
	ttf_data[slot] = d;
	return d;
}

void *font_get(int weight, int px)
{
	if (weight != 400 && weight != 500 && weight != 600 && weight != 700)
		weight = 400;
	for (Font *f = fonts; f; f = f->next)
		if (f->weight == weight && f->px == px)
			return f;

	unsigned char *data = load_ttf(weight);
	if (!data)
		return NULL;

	Font *f = calloc(1, sizeof(Font));
	f->weight = weight;
	f->px = px;
	f->atlas = calloc(ATLAS_W * ATLAS_H, 1);
	if (!f->atlas) {
		free(f);
		return NULL;
	}

	stbtt_fontinfo info;
	stbtt_InitFont(&info, data, 0);
	stbtt_pack_context pc;
	if (!stbtt_PackBegin(&pc, f->atlas, ATLAS_W, ATLAS_H, 0, 1, NULL)) {
		free(f->atlas);
		free(f);
		return NULL;
	}
	stbtt_PackSetOversampling(&pc, px <= 32 ? 2 : 1, px <= 32 ? 2 : 1);
	stbtt_PackFontRange(&pc, data, 0, (float)px, FIRST_CH, NUM_CH, f->ch);
	stbtt_PackEnd(&pc);

	float scale = stbtt_ScaleForPixelHeight(&info, (float)px);
	int a = 0, d = 0, lg = 0;
	stbtt_GetFontVMetrics(&info, &a, &d, &lg);
	f->ascent = a * scale;
	f->descent = d * scale;
	f->linegap = lg * scale;

	f->next = fonts;
	fonts = f;
	return f;
}

int text_w(void *fp, const char *s)
{
	Font *f = fp;
	if (!f)
		return 0;
	int w = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (*p < FIRST_CH || *p >= FIRST_CH + NUM_CH)
			continue;
		w += f->ch[*p - FIRST_CH].xadvance;
	}
	return (int)w;
}

static void draw_atlas_quad(gw_surface *s, Font *f, int idx, float x, float y, Col c)
{
	stbtt_aligned_quad q;
	stbtt_GetPackedQuad(f->ch, ATLAS_W, ATLAS_H, idx, &x, &y, &q, 0);
	int x0 = (int)q.x0, y0 = (int)q.y0;
	int x1 = (int)ceilf(q.x1), y1 = (int)ceilf(q.y1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	R cl;
	gfx_clip_get(&cl);
	if (x0 < (int)cl.x) x0 = (int)cl.x;
	if (y0 < (int)cl.y) y0 = (int)cl.y;
	if (x1 > (int)(cl.x + cl.w)) x1 = (int)(cl.x + cl.w);
	if (y1 > (int)(cl.y + cl.h)) y1 = (int)(cl.y + cl.h);
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	if (x0 >= x1 || y0 >= y1)
		return;
	uint8_t cr = c.r, cg = c.g, cb = c.b;
	uint8_t ca = (uint8_t)(c.a * gfx_alpha() + 0.5f);
	if (!ca)
		return;
	uint32_t stride = s->stride / 4;
	for (int py = y0; py < y1; py++) {
		for (int px = x0; px < x1; px++) {
			/* nearest sample of the alpha atlas */
			float u = (px + 0.5f - q.x0) / (q.x1 - q.x0) * (q.s1 - q.s0) + q.s0;
			float v = (py + 0.5f - q.y0) / (q.y1 - q.y0) * (q.t1 - q.t0) + q.t0;
			int au = (int)(u * ATLAS_W), av = (int)(v * ATLAS_H);
			if (au < 0) au = 0;
			if (au >= ATLAS_W) au = ATLAS_W - 1;
			if (av < 0) av = 0;
			if (av >= ATLAS_H) av = ATLAS_H - 1;
			uint8_t a = f->atlas[av * ATLAS_W + au];
			if (!a)
				continue;
			uint8_t sa = (uint8_t)(a * ca / 255);
			if (sa)
				gfx_put(s, px, py,
					gfx_blend_px(s->pix[(size_t)py * stride + px], cr, cg, cb, sa));
		}
	}
}

void text(void *fp, gw_surface *s, float x, float y_base, const char *str, Col c)
{
	Font *f = fp;
	if (!f || !s || !str)
		return;
	float pen_x = x, pen_y = y_base; /* pen_y is the BASELINE */
	for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
		if (*p < FIRST_CH || *p >= FIRST_CH + NUM_CH)
			continue;
		draw_atlas_quad(s, f, *p - FIRST_CH, pen_x, pen_y, c);
		pen_x += f->ch[*p - FIRST_CH].xadvance;
	}
}

void text_tl(void *fp, gw_surface *s, float x, float y_top, const char *str, Col c)
{
	Font *f = fp;
	if (!f)
		return;
	text(f, s, x, y_top + f->ascent, str, c);
}
