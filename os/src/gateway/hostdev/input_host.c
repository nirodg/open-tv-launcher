/* gateway/hostdev/input_host.c — scripted input for host iteration.
 * Script file (env OTV_KEYSCRIPT), one command per line:
 *   <ms> <key>     when the script clock reaches ms, press key (up/down/left
 *                  right ok back home menu volup voldown mute)
 *   <ms> none      when the script clock reaches ms, do nothing (settles
 *                  animations before a shot)
 *   shot <file>    dump the current frame as .argb
 * Step times are absolute against the script clock (which tracks real time
 * via the poll pacing below) — give each step a larger ms than the last. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gateway/gateway.h"

typedef struct { int ms; gw_key k; int shot; char shot_name[128]; } Cmd;

static Cmd *cmds;
static int ncmds, next_cmd;
static int waited; /* ms since boot */

static gw_key key_named(const char *k)
{
	if (!strcmp(k, "up")) return GWK_UP;
	if (!strcmp(k, "down")) return GWK_DOWN;
	if (!strcmp(k, "left")) return GWK_LEFT;
	if (!strcmp(k, "right")) return GWK_RIGHT;
	if (!strcmp(k, "ok")) return GWK_OK;
	if (!strcmp(k, "back")) return GWK_BACK;
	if (!strcmp(k, "home")) return GWK_HOME;
	if (!strcmp(k, "menu")) return GWK_MENU;
	if (!strcmp(k, "volup")) return GWK_VOLUP;
	if (!strcmp(k, "voldown")) return GWK_VOLDOWN;
	if (!strcmp(k, "mute")) return GWK_MUTE;
	return GWK_NONE;
}

int gw_input_open(void)
{
	const char *path = getenv("OTV_KEYSCRIPT");
	if (!path) {
		/* no script: block forever on poll (UI still draws once) */
		return 0;
	}
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "otv-host: cannot open keyscript %s\n", path);
		return -1;
	}
	int cap = 64;
	cmds = calloc(cap, sizeof(Cmd));
	char line[192];
	while (fgets(line, sizeof(line), f)) {
		if (ncmds == cap) { cap *= 2; cmds = realloc(cmds, cap * sizeof(Cmd)); }
		Cmd *c = &cmds[ncmds];
		memset(c, 0, sizeof(Cmd));
		char name[32] = {0};
		if (sscanf(line, "%d %31s", &c->ms, name) >= 2) {
			c->k = key_named(name);
			if (c->k != GWK_NONE || !strcmp(name, "none")) { ncmds++; continue; }
		}
		char tag[16] = {0};
		if (sscanf(line, "%15s %127s", tag, name) == 2 && !strcmp(tag, "shot")) {
			c->shot = 1;
			snprintf(c->shot_name, sizeof(c->shot_name), "%s", name);
			ncmds++;
		}
	}
	fclose(f);
	printf("otv-host: %d script steps\n", ncmds);
	return 0;
}

void gw_input_close(void) { free(cmds); }

int gw_input_poll(gw_input_event *ev, int max, int timeout_ms)
{
	/* pace the loop to real time: sleep until the next step is due (capped
	 * at timeout_ms, which the main loop assumes is ~one frame). Without
	 * this the poll returns instantly and the script clock runs ~1000×
	 * faster than the animations it is meant to settle. */
	int next_due = next_cmd < ncmds ? cmds[next_cmd].ms : waited + timeout_ms;
	int ahead = next_due - waited;
	if (ahead > 0) {
		int sl = ahead < timeout_ms ? ahead : timeout_ms;
		struct timespec ts = { sl / 1000, (long)(sl % 1000) * 1000000L };
		nanosleep(&ts, NULL);
	}
	int out = 0;
	while (next_cmd < ncmds) {
		Cmd *c = &cmds[next_cmd];
		if (waited < c->ms)
			break; /* script clock driven by main loop tick */
		if (c->shot) {
			extern void host_shot(const char *name);
			host_shot(c->shot_name);
			next_cmd++;
			continue;
		}
		if (c->k == GWK_NONE) { /* wait-only step */
			next_cmd++;
			continue;
		}
		/* press + release pair */
		if (out < max) {
			ev[out].key = c->k;
			ev[out].down = true;
			out++;
		}
		if (out < max) {
			ev[out].key = c->k;
			ev[out].down = false;
			out++;
		}
		next_cmd++;
	}
	return out;
}

void host_tick(int ms) { waited += ms; }
