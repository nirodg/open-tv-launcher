/* gateway/time.c — local wall clock (works on host and guest) */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "gateway/gateway.h"

static const char *WD[] = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
static const char *MON[] = { "January","February","March","April","May","June","July",
                             "August","September","October","November","December" };

void gw_time_parts(int *h, int *m, int *s, int *wd, int *day, int *mon, int *year)
{
	time_t t = time(NULL);
	struct tm tmv;
	localtime_r(&t, &tmv);
	if (h) *h = tmv.tm_hour;
	if (m) *m = tmv.tm_min;
	if (s) *s = tmv.tm_sec;
	if (wd) *wd = tmv.tm_wday;
	if (day) *day = tmv.tm_mday;
	if (mon) *mon = tmv.tm_mon;
	if (year) *year = tmv.tm_year + 1900;
}

const char *gw_wd_name(int wd) { return WD[wd % 7]; }
const char *gw_mon_name(int mon) { return MON[mon % 12]; }

float gw_now(void)
{
	/* Process-relative on purpose: float32 of an absolute monotonic value
	 * loses all sub-second precision on long-uptime machines (ULP grows
	 * with the value) — same reason the engine clock is relative. */
	static struct timespec t0;
	static bool have_base;
	if (!have_base) {
		clock_gettime(CLOCK_MONOTONIC, &t0);
		have_base = true;
	}
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (float)(ts.tv_sec - t0.tv_sec) +
	       (float)(ts.tv_nsec - t0.tv_nsec) / 1e9f;
}
