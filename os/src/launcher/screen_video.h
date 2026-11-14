/* launcher/screen_video.h — the Video app (1:1 port of APPT.video). */
#ifndef OTV_SCREEN_VIDEO_H
#define OTV_SCREEN_VIDEO_H
#include "ui.h"

extern const Screen video_screen;

/* returns true when the act was a video-app act (act.c dispatch hook) */
bool video_act(const char *cmd, const char *arg);

#endif /* OTV_SCREEN_VIDEO_H */
