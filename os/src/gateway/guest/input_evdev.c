/* gateway/guest/input_evdev.c — evdev → remote-key model.
 * Host keyboard over QEMU emulates the remote: arrows = D-pad, Enter = OK,
 * Esc/Backspace = Back, H = Home, M = Menu, +/- = volume, 0 = mute. */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/input.h>
#include "gateway/gateway.h"

#define MAX_FDS 16

static int fds[MAX_FDS];
static int nfds;
static struct pollfd pfds[MAX_FDS];

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

int gw_input_open(void)
{
	DIR *d = opendir("/dev/input");
	if (!d)
		return -1;
	struct dirent *de;
	while ((de = readdir(d)) && nfds < MAX_FDS) {
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;
		char path[256];
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		/* skip devices with no key events (mice/tablets) */
		unsigned char bits[(KEY_MAX + 7) / 8 + 1];
		memset(bits, 0, sizeof(bits));
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) <= 0) {
			close(fd);
			continue;
		}
		bool has_keys = false;
		for (int k = KEY_UP; k <= KEY_DOWN; k++)
			if (bits[k >> 3] & (1 << (k & 7))) { has_keys = true; break; }
		if (!has_keys) { close(fd); continue; }
		fds[nfds] = fd;
		pfds[nfds].fd = fd;
		pfds[nfds].events = POLLIN;
		nfds++;
	}
	closedir(d);
	if (!nfds) {
		fprintf(stderr, "otv: no keyboard input devices\n");
		return -1;
	}
	return 0;
}

void gw_input_close(void)
{
	for (int i = 0; i < nfds; i++)
		close(fds[i]);
	nfds = 0;
}

int gw_input_poll(gw_input_event *ev, int max, int timeout_ms)
{
	int out = 0;
	if (poll(pfds, nfds, timeout_ms) <= 0)
		return 0;

	/* drain every readable device — a half-read queue wedges later keys */
	for (int i = 0; i < nfds; i++) {
		if (!(pfds[i].revents & POLLIN))
			continue;
		for (;;) {
			struct input_event e[32];
			ssize_t n = read(fds[i], e, sizeof(e));
			if (n <= 0)
				break; /* EAGAIN / error: drained */
			int cnt = (int)(n / sizeof(e[0]));
			for (int j = 0; j < cnt && out < max; j++) {
				if (e[j].type != EV_KEY)
					continue;
				if (e[j].value > 1) /* autorepeat: treat as press */
					continue;
				int k = map_keycode(e[j].code);
				if (k == GWK_NONE)
					continue;
				ev[out].key = k;
				ev[out].down = e[j].value == 1;
				out++;
			}
			if (cnt < (int)(sizeof(e) / sizeof(e[0])))
				break; /* short read = queue empty */
		}
	}
	return out;
}
