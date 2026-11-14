/* gateway/device.c — the device description file (see device.h).
 * Deliberately dependency-free: the file IS the per-device contract, so the
 * parser must never grow a library dependency. */
#include "gateway/device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 64
#define KEY_LEN 48
#define VAL_LEN 96

typedef struct {
	char sec[KEY_LEN];
	char key[KEY_LEN];
	char val[VAL_LEN];
} Entry;

static Entry ents[MAX_ENTRIES];
static int nents;
static char summary[MAX_ENTRIES * (KEY_LEN + VAL_LEN + 4)];
static int summary_len;

static void put(const char *sec, const char *key, const char *val)
{
	if (nents >= MAX_ENTRIES)
		return;
	Entry *e = &ents[nents++];
	snprintf(e->sec, sizeof e->sec, "%s", sec);
	snprintf(e->key, sizeof e->key, "%s", key);
	snprintf(e->val, sizeof e->val, "%s", val);

	/* build the printable summary while parsing */
	char one[KEY_LEN + VAL_LEN + 4];
	snprintf(one, sizeof one, "%s.%s=%s", sec, key, val);
	int need = summary_len + (int)strlen(one) + 1;
	if (need < (int)sizeof summary) {
		memcpy(summary + summary_len, one, strlen(one));
		summary_len += (int)strlen(one);
		summary[summary_len++] = '\n';
		summary[summary_len] = 0;
	}
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
		    s[n - 1] == '\n'))
		s[--n] = 0;
	return s;
}

int otv_device_init(const char *path)
{
	FILE *f = path && path[0] ? fopen(path, "r") : NULL;
	if (!f)
		return -1;

	char line[256];
	char sec[KEY_LEN] = "";
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (!*s || *s == '#' || *s == ';')
			continue;
		if (*s == '[') {
			char *end = strchr(s, ']');
			if (end) {
				*end = 0;
				snprintf(sec, sizeof sec, "%s", trim(s + 1));
			}
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq || !sec[0])
			continue;
		*eq = 0;
		char *key = trim(s);
		char *val = trim(eq + 1);
		if (*key && *val)
			put(sec, key, val);
	}
	fclose(f);
	return 0;
}

const char *otv_device_str(const char *section, const char *key, const char *def)
{
	for (int i = 0; i < nents; i++)
		if (!strcmp(ents[i].sec, section) && !strcmp(ents[i].key, key))
			return ents[i].val;
	return def;
}

int otv_device_int(const char *section, const char *key, int def)
{
	const char *v = otv_device_str(section, key, NULL);
	return v ? atoi(v) : def;
}

const char *otv_device_summary(void)
{
	return summary;
}
