/* launcher/screen_video.h — the Video app (real screen, per task 14).
 * Content comes from the device's storage dirs (sys.c vid_clips); playback
 * uses pre-rendered filmstrips in QEMU (no codec deps yet). */
#ifndef OTV_SCREEN_VIDEO_H
#define OTV_SCREEN_VIDEO_H
#include "ui.h"

extern const Screen video_screen;
void video_open_clip(int idx);   /* open a library clip in the player */
void video_toggle_play(void);    /* player: play/pause */
void video_seek(int secs);       /* player: jump (negative = back) */

#endif /* OTV_SCREEN_VIDEO_H */
