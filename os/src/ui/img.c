/* ui/img.c — image loading + nearest-neighbour scaled blit.
 * The stb_image implementation itself is compiled in gfx.c (single copy). */
#include "stb/stb_image.h"
#include "img.h"
#include <stdio.h>
#include <stdlib.h>

Img *img_load(const char *path)
{
	int w, h, n;
	unsigned char *data = stbi_load(path, &w, &h, &n, 4);
	if (!data)
		return NULL;
	Img *im = malloc(sizeof(Img));
	if (!im) {
		stbi_image_free(data);
		return NULL;
	}
	im->w = w;
	im->h = h;
	im->pix = malloc((size_t)w * h * 4);
	if (!im->pix) {
		free(im);
		stbi_image_free(data);
		return NULL;
	}
	/* RGBA -> XRGB8888 (BGRX in memory, matching gw_surface) */
	for (long i = 0; i < (long)w * h; i++) {
		im->pix[i] = 0xFF000000u |
			     ((uint32_t)data[i * 4 + 0] << 16) | /* r */
			     ((uint32_t)data[i * 4 + 1] << 8) |  /* g */
			     ((uint32_t)data[i * 4 + 2]);        /* b */
	}
	stbi_image_free(data);
	return im;
}

void img_free(Img *im)
{
	if (!im)
		return;
	free(im->pix);
	free(im);
}

void img_draw(gw_surface *s, const Img *im, R dst,
	      int sx, int sy, int sw, int sh)
{
	if (!im || !im->pix || sw <= 0 || sh <= 0 || dst.w <= 0 || dst.h <= 0)
		return;
	R cl;
	gfx_clip_get(&cl);
	int dx0 = dst.x, dy0 = dst.y;
	if (dx0 < (int)cl.x) dx0 = (int)cl.x;
	if (dy0 < (int)cl.y) dy0 = (int)cl.y;
	int dx1 = dst.x + dst.w, dy1 = dst.y + dst.h;
	if (dx1 > (int)(cl.x + cl.w)) dx1 = (int)(cl.x + cl.w);
	if (dy1 > (int)(cl.y + cl.h)) dy1 = (int)(cl.y + cl.h);
	if (dx1 > s->w) dx1 = s->w;
	if (dy1 > s->h) dy1 = s->h;
	if (dx0 >= dx1 || dy0 >= dy1)
		return;

	uint32_t stride = s->stride / 4;
	for (int dy = dy0; dy < dy1; dy++) {
		int sy2 = sy + (dy - dst.y) * sh / dst.h;
		if (sy2 < 0) sy2 = 0;
		if (sy2 >= im->h) sy2 = im->h - 1;
		uint32_t *srow = im->pix + (size_t)sy2 * im->w;
		uint32_t *drow = s->pix + (size_t)dy * stride;
		for (int dx = dx0; dx < dx1; dx++) {
			int sx2 = sx + (dx - dst.x) * sw / dst.w;
			if (sx2 < 0) sx2 = 0;
			if (sx2 >= im->w) sx2 = im->w - 1;
			drow[dx] = srow[sx2];
		}
	}
}
