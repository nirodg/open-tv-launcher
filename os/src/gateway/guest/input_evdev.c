/* gateway/guest/input_evdev.c — evdev → remote-key + pointer model.
 * Host keyboard over QEMU emulates the remote: arrows = D-pad, Enter = OK,
 * Esc/Backspace = Back, H = Home, M = Menu (quick panel), +/- = volume,
 * 0 = mute. A virtio-tablet (mouse) drives the pointer: hover moves focus,
 * click activates, right-click opens the item's options. */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include "gateway/gateway.h"

#define MAX_FDS 16
#define OTV_W 1920
#define OTV_H 1080

typedef struct {
	int fd;
	struct pollfd *pfd;
	bool pointer;          /* absolute-position device */
	int ax_min, ax_max;    /* ABS_X range for scaling */
	int x, y;              /* last reported position (screen px) */
	bool moved;            /* a move is pending */
} Dev;

static Dev devs[MAX_FDS];
static int ndevs;

static int map_keycode(int c)
{
	switch (c) {
	case KEY_UP:     return GWK_UP;
	case KEY_DOWN:   return GWK_DOWN;
	case KEY_LEFT:   return GWK_LEFT;
	case KEY_RIGHT:  return GWK_RIGHT;
	case KEY_ENTER:
	case KEY_KPENTER: return GWK_OK;
	case KEY_ESC:
	case KEY_BACKSPACE: return GWK_BACK;
	case KEY_H:      return GWK_HOME;
	case KEY_M:      return GWK_MENU;
	case KEY_VOLUMEUP:   return GWK_VOLUP;
	case KEY_VOLUMEDOWN: return GWK_VOLDOWN;
	case KEY_MUTE:   return GWK_MUTE;
	case KEY_CHANNELUP: return GWK_CHUP;
	case KEY_CHANNELDOWN: return GWK_CHDOWN;
	default:         return GWK_NONE;
	}
}

static bool has_bit(const unsigned char *bits, int bit)
{
	return bits[bit >> 3] & (1 << (bit & 7));
}

int gw_input_open(void)
{
	DIR *d = opendir("/dev/input");
	if (!d)
		return -1;
	struct dirent *de;
	while ((de = readdir(d)) && ndevs < MAX_FDS) {
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;
		char path[256];
		snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;

		unsigned char bits[(KEY_MAX + 7) / 8 + 1];
		unsigned char abs[(ABS_MAX + 7) / 8 + 1];
		memset(bits, 0, sizeof bits);
		memset(abs, 0, sizeof abs);
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) <= 0) {
			close(fd);
			continue;
		}

		bool pointer = false;
		if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) > 0 &&
		    has_bit(abs, ABS_X) && has_bit(abs, ABS_Y)) {
			/* absolute pointer (tablet) */
			struct input_absinfo ai;
			pointer = ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0;
			Dev *dv = &devs[ndevs];
			dv->ax_min = ai.minimum;
			dv->ax_max = ai.maximum ? ai.maximum : 32767;
		} else {
			/* keyboard: only devices that report the D-pad keys */
			bool has_keys = false;
			for (int k = KEY_UP; k <= KEY_DOWN; k++)
				if (has_bit(bits, k)) { has_keys = true; break; }
			if (!has_keys) {
				close(fd);
				continue;
			}
		}


		Dev *dv = &devs[ndevs];
		dv->fd = fd;
		dv->pointer = pointer;
		dv->x = OTV_W / 2;
		dv->y = OTV_H / 2;
		dv->moved = false;
		dv->pfd = NULL; /* filled below */
		ndevs++;
	}
	closedir(d);
	if (!ndevs) {
		fprintf(stderr, "otv: no input devices\n");
		return -1;
	}
	return 0;
}

static struct pollfd pfds[MAX_FDS];


void gw_input_close(void)
{
	for (int i = 0; i < ndevs; i++)
		close(devs[i].fd);
	ndevs = 0;
}

void gw_input_screen_size(int *w, int *h)
{
	*w = OTV_W;
	*h = OTV_H;
}

static int scale_x(const Dev *dv, int v)
{
	int t = (v - dv->ax_min) * OTV_W / (dv->ax_max - dv->ax_min);
	if (t < 0) t = 0;
	if (t >= OTV_W) t = OTV_W - 1;
	return t;
}

static int scale_y(const Dev *dv, int v)
{
	int t = (v - dv->ax_min) * OTV_H / (dv->ax_max - dv->ax_min);
	if (t < 0) t = 0;
	if (t >= OTV_H) t = OTV_H - 1;
	return t;
}

int gw_input_poll(gw_input_event *ev, int max, int timeout_ms)
{
	int out = 0;
	for (int i = 0; i < ndevs; i++) {
		pfds[i].fd = devs[i].fd;
		pfds[i].events = POLLIN;
		pfds[i].revents = 0;
	}
	if (poll(pfds, ndevs, timeout_ms) <= 0)
		return 0;

	/* drain every readable device — a half-read queue wedges later keys */
	for (int i = 0; i < ndevs; i++) {
		if (!(pfds[i].revents & POLLIN))
			continue;
		for (;;) {
			struct input_event e[32];
			ssize_t n = read(devs[i].fd, e, sizeof(e));
			if (n <= 0)
				break; /* EAGAIN / error: drained */
			int cnt = (int)(n / sizeof(e[0]));
			for (int j = 0; j < cnt && out < max; j++) {
				Dev *dv = &devs[i];
				if (dv->pointer) {
					switch (e[j].type) {
					case EV_ABS:
						if (e[j].code == ABS_X)
							dv->x = scale_x(dv, e[j].value);
						else if (e[j].code == ABS_Y)
							dv->y = scale_y(dv, e[j].value);
						dv->moved = true;
						break;
					case EV_KEY:
						if (e[j].code == BTN_LEFT ||
						    e[j].code == BTN_RIGHT) {
							/* click: position first, then the press */
							if (out + 1 < max && dv->moved) {
								ev[out++] = (gw_input_event){
									.pointer = true, .x = dv->x, .y = dv->y };
								dv->moved = false;
							}
							ev[out++] = (gw_input_event){
								.pointer = true, .down = e[j].value == 1,
								.right = e[j].code == BTN_RIGHT,
								.x = dv->x, .y = dv->y };
						}
						break;
					}
					continue;
				}
				if (e[j].type != EV_KEY)
					continue;
				if (e[j].value > 1) /* autorepeat */
					continue;
				int k = map_keycode(e[j].code);
				if (k == GWK_NONE)
					continue;
				ev[out++] = (gw_input_event){ .key = k, .down = e[j].value == 1 };
			}
			if (cnt < (int)(sizeof(e) / sizeof(e[0])))
				break; /* short read = queue empty */
		}
		/* flush a pending hover move even without a click */
		if (devs[i].pointer && devs[i].moved && out < max) {
			ev[out++] = (gw_input_event){
				.pointer = true, .x = devs[i].x, .y = devs[i].y };
			devs[i].moved = false;
		}
	}
	return out;
}
