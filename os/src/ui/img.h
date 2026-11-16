/* ui/img.h — image loading (stb_image) + scaled blit for the rasterizer.
 * Used by the Video app's pre-rendered filmstrips (QEMU has no video decode
 * yet; real decode arrives with the media backend). */
#ifndef OTV_IMG_H
#define OTV_IMG_H
#include "ui.h"

typedef struct { int w, h; uint32_t *pix; } Img;

/* Loads jpg/png/bmp (stb_image). Returns NULL when missing/broken. The
 * pixels are XRGB8888 like gw_surface. */
Img *img_load(const char *path);
void img_free(Img *im);

/* Draw src rect (sx,sy,sw,sh) scaled to fill dst (nearest neighbour). */
void img_draw(gw_surface *s, const Img *im, R dst,
	      int sx, int sy, int sw, int sh);

#endif /* OTV_IMG_H */
