/* gateway/hostdev/display_null.c — host dev-iteration backend.
 * Same interface as the DRM backend, but renders into a malloc'd buffer.
 * When OTV_SHOT_OUT is set, every present() appends a frame to an .argb file
 * (16-byte header: "OTVS", u32 LE w, h, stride; then BGRX rows) for os/tools/argb2png.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gateway/gateway.h"

static gw_surface surf;
static FILE *shot;

int gw_display_open(void)
{
	surf.w = 1920;
	surf.h = 1080;
	surf.stride = 1920 * 4;
	surf.pix = calloc((size_t)surf.h * surf.stride, 1);
	if (!surf.pix)
		return -1;
	const char *out = getenv("OTV_SHOT_OUT");
	if (out) {
		shot = fopen(out, "wb");
		if (shot) {
			uint32_t hdr[4] = { 0x5356544F, 1920, 1080, 1920 * 4 }; /* "OTVS" LE */
			fwrite(hdr, 4, 4, shot);
		}
	}
	return 0;
}

gw_surface *gw_display_fb(void) { return &surf; }

void host_shot(const char *name)
{
	char path[160];
	snprintf(path, sizeof(path), "%s%s", name, strstr(name, ".argb") ? "" : ".argb");
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	uint32_t hdr[4] = { 0x5356544F, (uint32_t)surf.w, (uint32_t)surf.h, (uint32_t)surf.stride };
	fwrite(hdr, 4, 4, f);
	fwrite(surf.pix, surf.stride, surf.h, f);
	fclose(f);
	printf("otv-host: wrote %s\n", path);
}

void gw_display_present(void)
{
	if (shot)
		fwrite(surf.pix, surf.stride, surf.h, shot);
}

void gw_display_damage(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }

void gw_display_close(void)
{
	if (shot)
		fclose(shot);
	free(surf.pix);
	memset(&surf, 0, sizeof(surf));
}
