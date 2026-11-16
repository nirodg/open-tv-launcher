/* ui/media.h — real video playback via the FFmpeg libraries (LGPL).
 * QEMU and real hardware decode the same way; the audio track is ignored
 * until the audio backend lands (device.conf [audio] backend). */
#ifndef OTV_MEDIA_H
#define OTV_MEDIA_H
#include "ui.h"

/* Container duration probe (seconds, 0 on failure) — cheap, no decoder. */
double media_probe_duration(const char *path);

/* A decoding session. media_frame() returns XRGB8888 pixels for a
 * timestamp, seeking internally when the jump is backward or large. */
typedef struct Media Media;
Media *media_open(const char *path);
void media_close(Media *m);
const uint32_t *media_frame(Media *m, double ts); /* NULL while buffering */
int media_w(const Media *m);
int media_h(const Media *m);

#endif /* OTV_MEDIA_H */
